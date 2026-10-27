#include "router.h"
#include "database.h"
#include "auth.h"
#include "complaints.h"
#include "analytics.h"
#include "algorithms.h"
#include "validation.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *get_http_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

void send_json_response(struct mg_connection *c, int status_code, bool success, const char *message, const char *json_data_str) {
    char body[65536];
    snprintf(body, sizeof(body), "{\"success\":%s,\"message\":\"%s\",\"data\":%s}",
             success ? "true" : "false",
             message ? message : "",
             (json_data_str && strlen(json_data_str) > 0) ? json_data_str : "null");

    mg_printf(c,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
              "Content-Length: %lu\r\n"
              "\r\n%s",
              status_code, get_http_status_text(status_code),
              (unsigned long)strlen(body), body);
}

void send_json_error(struct mg_connection *c, int status_code, const char *message) {
    send_json_response(c, status_code, false, message, "null");
}

bool extract_auth_token(struct mg_http_message *hm, char *token_buf, size_t buf_size) {
    if (!hm || !token_buf || buf_size == 0) return false;
    token_buf[0] = '\0';

    // 1. Check Cookie header for session_token=...
    struct mg_str *cookie_hdr = mg_http_get_header(hm, "Cookie");
    if (cookie_hdr && cookie_hdr->ptr) {
        char cookie_str[512] = {0};
        size_t c_len = (cookie_hdr->len < sizeof(cookie_str) - 1) ? cookie_hdr->len : sizeof(cookie_str) - 1;
        memcpy(cookie_str, cookie_hdr->ptr, c_len);
        cookie_str[c_len] = '\0';

        char *token_ptr = strstr(cookie_str, "session_token=");
        if (token_ptr) {
            token_ptr += 14;
            char *end = strchr(token_ptr, ';');
            size_t t_len = end ? (size_t)(end - token_ptr) : strlen(token_ptr);
            if (t_len < buf_size) {
                memcpy(token_buf, token_ptr, t_len);
                token_buf[t_len] = '\0';
                sanitize_string(token_buf);
                if (strlen(token_buf) > 10) return true;
            }
        }
    }

    // 2. Check Authorization: Bearer <token>
    struct mg_str *auth_hdr = mg_http_get_header(hm, "Authorization");
    if (auth_hdr && auth_hdr->ptr) {
        char auth_str[512] = {0};
        size_t a_len = (auth_hdr->len < sizeof(auth_str) - 1) ? auth_hdr->len : sizeof(auth_str) - 1;
        memcpy(auth_str, auth_hdr->ptr, a_len);
        auth_str[a_len] = '\0';

        if (strncmp(auth_str, "Bearer ", 7) == 0) {
            const char *b_token = auth_str + 7;
            while (*b_token == ' ') b_token++;
            strncpy(token_buf, b_token, buf_size - 1);
            token_buf[buf_size - 1] = '\0';
            sanitize_string(token_buf);
            if (strlen(token_buf) > 10) return true;
        }
    }

    return false;
}

// Extract client IP address
static void get_client_ip(struct mg_connection *c, char *ip_buf, size_t buf_size) {
    mg_ntoa(&c->rem, ip_buf, buf_size);
}

// Convert User struct to cJSON object
static cJSON* user_to_cjson(const User *u) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "user_id", u->user_id);
    cJSON_AddStringToObject(obj, "full_name", u->full_name);
    cJSON_AddStringToObject(obj, "email", u->email);
    cJSON_AddStringToObject(obj, "student_id", u->student_id);
    cJSON_AddStringToObject(obj, "department", u->department);
    cJSON_AddStringToObject(obj, "role", u->role);
    cJSON_AddStringToObject(obj, "account_status", u->account_status);
    cJSON_AddStringToObject(obj, "created_at", u->created_at);
    return obj;
}

// Convert Complaint struct to cJSON object
static cJSON* complaint_to_cjson(const Complaint *c) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "complaint_id", c->complaint_id);
    cJSON_AddNumberToObject(obj, "user_id", c->user_id);
    cJSON_AddStringToObject(obj, "student_name", c->student_name);
    cJSON_AddStringToObject(obj, "student_email", c->student_email);
    cJSON_AddStringToObject(obj, "title", c->title);
    cJSON_AddStringToObject(obj, "category", c->category);
    cJSON_AddStringToObject(obj, "description", c->description);
    cJSON_AddStringToObject(obj, "location", c->location);
    cJSON_AddStringToObject(obj, "room_number", c->room_number);
    cJSON_AddNumberToObject(obj, "priority", c->priority);
    cJSON_AddStringToObject(obj, "status", c->status);
    cJSON_AddStringToObject(obj, "assigned_department", c->assigned_department);
    cJSON_AddStringToObject(obj, "created_at", c->created_at);
    cJSON_AddStringToObject(obj, "updated_at", c->updated_at);
    cJSON_AddStringToObject(obj, "resolved_at", c->resolved_at);
    return obj;
}

// ==========================================
// API Handlers
// ==========================================

static void handle_auth_register(struct mg_connection *c, struct mg_http_message *hm) {
    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(json, "full_name"));
    const char *email = cJSON_GetStringValue(cJSON_GetObjectItem(json, "email"));
    const char *student_id = cJSON_GetStringValue(cJSON_GetObjectItem(json, "student_id"));
    const char *dept = cJSON_GetStringValue(cJSON_GetObjectItem(json, "department"));
    const char *password = cJSON_GetStringValue(cJSON_GetObjectItem(json, "password"));
    const char *confirm = cJSON_GetStringValue(cJSON_GetObjectItem(json, "confirm_password"));

    char err_msg[256] = {0};
    if (!validate_registration(name, email, student_id, dept, password, confirm, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }

    User new_user;
    if (!auth_register(name, email, student_id, dept, password, "student", &new_user, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }

    cJSON_Delete(json);
    cJSON *user_json = user_to_cjson(&new_user);
    char *user_str = cJSON_PrintUnformatted(user_json);
    send_json_response(c, 201, true, "Registration successful! You may now log in.", user_str);
    free(user_str);
    cJSON_Delete(user_json);
}

static void handle_auth_login(struct mg_connection *c, struct mg_http_message *hm) {
    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *email = cJSON_GetStringValue(cJSON_GetObjectItem(json, "email"));
    const char *password = cJSON_GetStringValue(cJSON_GetObjectItem(json, "password"));

    char err_msg[256] = {0};
    if (!validate_login(email, password, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }

    char ip_address[64] = {0};
    get_client_ip(c, ip_address, sizeof(ip_address));

    char token[65] = {0};
    User user;
    if (!auth_login(email, password, ip_address, token, sizeof(token), &user, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 401, err_msg);
        return;
    }
    cJSON_Delete(json);

    // Build response data with token and user profile
    cJSON *res_data = cJSON_CreateObject();
    cJSON_AddStringToObject(res_data, "token", token);
    cJSON_AddItemToObject(res_data, "user", user_to_cjson(&user));
    char *data_str = cJSON_PrintUnformatted(res_data);

    char body[65536];
    snprintf(body, sizeof(body), "{\"success\":true,\"message\":\"Login successful!\",\"data\":%s}", data_str);
    free(data_str);
    cJSON_Delete(res_data);

    // Send Set-Cookie header with HttpOnly and SameSite=Lax
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Set-Cookie: session_token=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Content-Length: %lu\r\n"
              "\r\n%s",
              token, SESSION_DURATION_SEC, (unsigned long)strlen(body), body);
}

static void handle_auth_logout(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (extract_auth_token(hm, token, sizeof(token))) {
        auth_logout(token);
    }

    const char *body = "{\"success\":true,\"message\":\"Logged out successfully.\",\"data\":null}";
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Set-Cookie: session_token=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Content-Length: %lu\r\n"
              "\r\n%s",
              (unsigned long)strlen(body), body);
}

static void handle_auth_me(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    cJSON *u_json = user_to_cjson(&current_user);
    char *u_str = cJSON_PrintUnformatted(u_json);
    send_json_response(c, 200, true, "User authenticated.", u_str);
    free(u_str);
    cJSON_Delete(u_json);
}

static void handle_auth_change_password(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *old_pwd = cJSON_GetStringValue(cJSON_GetObjectItem(json, "old_password"));
    const char *new_pwd = cJSON_GetStringValue(cJSON_GetObjectItem(json, "new_password"));

    char err_msg[256] = {0};
    if (!auth_change_password(current_user.user_id, old_pwd, new_pwd, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }

    cJSON_Delete(json);
    send_json_response(c, 200, true, "Password changed successfully.", "null");
}

// Student Dashboard Handler
static void handle_student_dashboard(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    StudentDashboardData data;
    if (!analytics_get_student_dashboard(current_user.user_id, &data)) {
        send_json_error(c, 500, "Failed to retrieve student dashboard data.");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_complaints", data.total_complaints);
    cJSON_AddNumberToObject(root, "pending_complaints", data.pending_complaints);
    cJSON_AddNumberToObject(root, "in_progress_complaints", data.in_progress_complaints);
    cJSON_AddNumberToObject(root, "resolved_complaints", data.resolved_complaints);

    cJSON *recent_arr = cJSON_CreateArray();
    for (int i = 0; i < data.recent_count; i++) {
        cJSON_AddItemToArray(recent_arr, complaint_to_cjson(&data.recent_complaints[i]));
    }
    cJSON_AddItemToObject(root, "recent_complaints", recent_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Student dashboard retrieved.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

// Student Complaints List Handler
static void handle_student_complaints_list(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    char page_str[16] = {0}, page_size_str[16] = {0}, search[64] = {0}, cat[48] = {0}, status[32] = {0}, prio_str[16] = {0}, sort_by[32] = {0};
    mg_http_get_var(&hm->query, "page", page_str, sizeof(page_str));
    mg_http_get_var(&hm->query, "page_size", page_size_str, sizeof(page_size_str));
    mg_http_get_var(&hm->query, "search", search, sizeof(search));
    mg_http_get_var(&hm->query, "category", cat, sizeof(cat));
    mg_http_get_var(&hm->query, "status", status, sizeof(status));
    mg_http_get_var(&hm->query, "priority", prio_str, sizeof(prio_str));
    mg_http_get_var(&hm->query, "sort_by", sort_by, sizeof(sort_by));

    int page = atoi(page_str) > 0 ? atoi(page_str) : 1;
    int page_size = atoi(page_size_str) > 0 ? atoi(page_size_str) : 10;
    int prio = atoi(prio_str);

    Complaint list[50];
    int count = 0, total = 0;
    complaints_list_student(current_user.user_id, search, cat, status, prio, sort_by, page, page_size, list, 50, &count, &total);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "page", page);
    cJSON_AddNumberToObject(root, "page_size", page_size);
    cJSON_AddNumberToObject(root, "total", total);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, complaint_to_cjson(&list[i]));
    }
    cJSON_AddItemToObject(root, "complaints", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Complaints retrieved.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

// Student Submit Complaint Handler
static void handle_student_create_complaint(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *title = cJSON_GetStringValue(cJSON_GetObjectItem(json, "title"));
    const char *cat = cJSON_GetStringValue(cJSON_GetObjectItem(json, "category"));
    const char *desc = cJSON_GetStringValue(cJSON_GetObjectItem(json, "description"));
    const char *loc = cJSON_GetStringValue(cJSON_GetObjectItem(json, "location"));
    const char *room = cJSON_GetStringValue(cJSON_GetObjectItem(json, "room_number"));
    cJSON *prio_item = cJSON_GetObjectItem(json, "priority");
    int priority = (prio_item && prio_item->type == cJSON_Number) ? prio_item->valueint : 2; // Default Medium

    char cid[32] = {0};
    char err_msg[256] = {0};
    if (!complaints_create(current_user.user_id, title, cat, desc, loc, room, priority, cid, sizeof(cid), err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }

    cJSON_Delete(json);

    cJSON *res_data = cJSON_CreateObject();
    cJSON_AddStringToObject(res_data, "complaint_id", cid);
    char *res_str = cJSON_PrintUnformatted(res_data);
    send_json_response(c, 201, true, "Complaint submitted successfully! Your tracking ID has been generated.", res_str);
    free(res_str);
    cJSON_Delete(res_data);
}

// Complaint Detail Handler (with audit timeline)
static void handle_complaint_detail(struct mg_connection *c, struct mg_http_message *hm, const char *cid) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    Complaint comp;
    ComplaintHistory hist[32];
    int h_count = 0;
    if (!complaints_get_by_id(cid, &comp, hist, 32, &h_count)) {
        send_json_error(c, 404, "Complaint not found.");
        return;
    }

    // RBAC: If student, ensure ownership
    if (strcmp(current_user.role, "admin") != 0 && comp.user_id != current_user.user_id) {
        send_json_error(c, 403, "You do not have permission to view this complaint.");
        return;
    }

    cJSON *root = complaint_to_cjson(&comp);
    cJSON *h_arr = cJSON_CreateArray();
    for (int i = 0; i < h_count; i++) {
        cJSON *h_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(h_obj, "history_id", hist[i].history_id);
        cJSON_AddStringToObject(h_obj, "complaint_id", hist[i].complaint_id);
        cJSON_AddNumberToObject(h_obj, "changed_by", hist[i].changed_by);
        cJSON_AddStringToObject(h_obj, "changed_by_name", hist[i].changed_by_name);
        cJSON_AddStringToObject(h_obj, "changed_by_role", hist[i].changed_by_role);
        cJSON_AddStringToObject(h_obj, "old_status", hist[i].old_status);
        cJSON_AddStringToObject(h_obj, "new_status", hist[i].new_status);
        cJSON_AddStringToObject(h_obj, "remarks", hist[i].remarks);
        cJSON_AddStringToObject(h_obj, "created_at", hist[i].created_at);
        cJSON_AddItemToArray(h_arr, h_obj);
    }
    cJSON_AddItemToObject(root, "history", h_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Complaint details retrieved.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

// Student Profile Handler
static void handle_student_profile(struct mg_connection *c, struct mg_http_message *hm) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return;
    }

    User current_user;
    if (!auth_validate_session(token, &current_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return;
    }

    if (mg_vcasecmp(&hm->method, "PATCH") == 0) {
        cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
        if (!json) {
            send_json_error(c, 400, "Invalid JSON payload.");
            return;
        }

        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(json, "full_name"));
        const char *dept = cJSON_GetStringValue(cJSON_GetObjectItem(json, "department"));

        char err_msg[256] = {0};
        if (!auth_update_profile(current_user.user_id, name, dept, err_msg, sizeof(err_msg))) {
            cJSON_Delete(json);
            send_json_error(c, 400, err_msg);
            return;
        }
        cJSON_Delete(json);

        auth_get_user_by_id(current_user.user_id, &current_user);
    }

    cJSON *u_json = user_to_cjson(&current_user);
    char *u_str = cJSON_PrintUnformatted(u_json);
    send_json_response(c, 200, true, "Profile retrieved.", u_str);
    free(u_str);
    cJSON_Delete(u_json);
}

// ==========================================
// Admin Handlers
// ==========================================

static bool require_admin(struct mg_http_message *hm, User *out_user, struct mg_connection *c) {
    char token[128] = {0};
    if (!extract_auth_token(hm, token, sizeof(token))) {
        send_json_error(c, 401, "Authentication required.");
        return false;
    }

    if (!auth_validate_session(token, out_user)) {
        send_json_error(c, 401, "Session expired or invalid.");
        return false;
    }

    if (strcmp(out_user->role, "admin") != 0) {
        send_json_error(c, 403, "Access restricted to administrators.");
        return false;
    }

    return true;
}

static void handle_admin_dashboard(struct mg_connection *c, struct mg_http_message *hm) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    AdminDashboardData data;
    if (!analytics_get_admin_dashboard(&data)) {
        send_json_error(c, 500, "Failed to retrieve administrator analytics.");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_complaints", data.total_complaints);
    cJSON_AddNumberToObject(root, "pending_complaints", data.pending_complaints);
    cJSON_AddNumberToObject(root, "in_progress_complaints", data.in_progress_complaints);
    cJSON_AddNumberToObject(root, "resolved_complaints", data.resolved_complaints);
    cJSON_AddNumberToObject(root, "rejected_complaints", data.rejected_complaints);
    cJSON_AddNumberToObject(root, "critical_complaints", data.critical_complaints);
    cJSON_AddNumberToObject(root, "high_priority_complaints", data.high_priority_complaints);
    cJSON_AddNumberToObject(root, "avg_resolution_hours", data.avg_resolution_hours);

    // Priority breakdown
    cJSON *p_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(p_obj, "low", data.priority_counts[1]);
    cJSON_AddNumberToObject(p_obj, "medium", data.priority_counts[2]);
    cJSON_AddNumberToObject(p_obj, "high", data.priority_counts[3]);
    cJSON_AddNumberToObject(p_obj, "critical", data.priority_counts[4]);
    cJSON_AddItemToObject(root, "priority_distribution", p_obj);

    // Category breakdown
    cJSON *cat_arr = cJSON_CreateArray();
    for (int i = 0; i < data.category_count; i++) {
        cJSON *co = cJSON_CreateObject();
        cJSON_AddStringToObject(co, "category", data.category_stats[i].category);
        cJSON_AddNumberToObject(co, "count", data.category_stats[i].count);
        cJSON_AddItemToArray(cat_arr, co);
    }
    cJSON_AddItemToObject(root, "category_distribution", cat_arr);

    // Recent 7-day trend
    cJSON *trend_arr = cJSON_CreateArray();
    for (int i = 0; i < data.trend_count; i++) {
        cJSON *to = cJSON_CreateObject();
        cJSON_AddStringToObject(to, "date", data.recent_trends[i].date);
        cJSON_AddNumberToObject(to, "count", data.recent_trends[i].count);
        cJSON_AddItemToArray(trend_arr, to);
    }
    cJSON_AddItemToObject(root, "recent_trends", trend_arr);

    // Recent complaints
    cJSON *rec_arr = cJSON_CreateArray();
    for (int i = 0; i < data.recent_count; i++) {
        cJSON_AddItemToArray(rec_arr, complaint_to_cjson(&data.recent_complaints[i]));
    }
    cJSON_AddItemToObject(root, "recent_complaints", rec_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Admin dashboard retrieved.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

static void handle_admin_complaints_list(struct mg_connection *c, struct mg_http_message *hm) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    char page_str[16] = {0}, page_size_str[16] = {0}, search[64] = {0}, cat[48] = {0}, status[32] = {0}, prio_str[16] = {0}, sort_by[32] = {0};
    mg_http_get_var(&hm->query, "page", page_str, sizeof(page_str));
    mg_http_get_var(&hm->query, "page_size", page_size_str, sizeof(page_size_str));
    mg_http_get_var(&hm->query, "search", search, sizeof(search));
    mg_http_get_var(&hm->query, "category", cat, sizeof(cat));
    mg_http_get_var(&hm->query, "status", status, sizeof(status));
    mg_http_get_var(&hm->query, "priority", prio_str, sizeof(prio_str));
    mg_http_get_var(&hm->query, "sort_by", sort_by, sizeof(sort_by));

    int page = atoi(page_str) > 0 ? atoi(page_str) : 1;
    int page_size = atoi(page_size_str) > 0 ? atoi(page_size_str) : 10;
    int prio = atoi(prio_str);

    Complaint list[50];
    int count = 0, total = 0;
    complaints_list_admin(search, cat, status, prio, sort_by, page, page_size, list, 50, &count, &total);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "page", page);
    cJSON_AddNumberToObject(root, "page_size", page_size);
    cJSON_AddNumberToObject(root, "total", total);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, complaint_to_cjson(&list[i]));
    }
    cJSON_AddItemToObject(root, "complaints", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Admin complaints retrieved.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

static void handle_admin_update_status(struct mg_connection *c, struct mg_http_message *hm, const char *cid) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(json, "status"));
    const char *remarks = cJSON_GetStringValue(cJSON_GetObjectItem(json, "remarks"));

    char err_msg[256] = {0};
    if (!complaints_update_status(cid, admin.user_id, status, remarks, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }
    cJSON_Delete(json);

    send_json_response(c, 200, true, "Complaint status updated successfully.", "null");
}

static void handle_admin_update_priority(struct mg_connection *c, struct mg_http_message *hm, const char *cid) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    cJSON *prio_item = cJSON_GetObjectItem(json, "priority");
    int priority = (prio_item && prio_item->type == cJSON_Number) ? prio_item->valueint : 0;
    const char *remarks = cJSON_GetStringValue(cJSON_GetObjectItem(json, "remarks"));

    char err_msg[256] = {0};
    if (!complaints_update_priority(cid, admin.user_id, priority, remarks, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }
    cJSON_Delete(json);

    send_json_response(c, 200, true, "Complaint priority updated successfully.", "null");
}

static void handle_admin_add_remarks(struct mg_connection *c, struct mg_http_message *hm, const char *cid) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *remarks = cJSON_GetStringValue(cJSON_GetObjectItem(json, "remarks"));

    char err_msg[256] = {0};
    if (!complaints_add_remarks(cid, admin.user_id, remarks, err_msg, sizeof(err_msg))) {
        cJSON_Delete(json);
        send_json_error(c, 400, err_msg);
        return;
    }
    cJSON_Delete(json);

    send_json_response(c, 200, true, "Remarks added to complaint audit log.", "null");
}

static void handle_admin_users(struct mg_connection *c, struct mg_http_message *hm) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    sqlite3 *db = db_get_handle();
    if (!db) {
        send_json_error(c, 500, "Database unavailable.");
        return;
    }

    cJSON *arr = cJSON_CreateArray();
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT u.user_id, u.full_name, u.email, u.student_id, u.department, u.role, u.account_status, u.created_at, "
                      "(SELECT COUNT(*) FROM complaints c WHERE c.user_id = u.user_id) as complaint_count "
                      "FROM users u ORDER BY u.created_at DESC;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cJSON *u_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(u_obj, "user_id", sqlite3_column_int(stmt, 0));
            cJSON_AddStringToObject(u_obj, "full_name", (const char *)sqlite3_column_text(stmt, 1));
            cJSON_AddStringToObject(u_obj, "email", (const char *)sqlite3_column_text(stmt, 2));
            const char *sid = (const char *)sqlite3_column_text(stmt, 3);
            cJSON_AddStringToObject(u_obj, "student_id", sid ? sid : "");
            cJSON_AddStringToObject(u_obj, "department", (const char *)sqlite3_column_text(stmt, 4));
            cJSON_AddStringToObject(u_obj, "role", (const char *)sqlite3_column_text(stmt, 5));
            cJSON_AddStringToObject(u_obj, "account_status", (const char *)sqlite3_column_text(stmt, 6));
            cJSON_AddStringToObject(u_obj, "created_at", (const char *)sqlite3_column_text(stmt, 7));
            cJSON_AddNumberToObject(u_obj, "complaint_count", sqlite3_column_int(stmt, 8));
            cJSON_AddItemToArray(arr, u_obj);
        }
        sqlite3_finalize(stmt);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    send_json_response(c, 200, true, "Users retrieved.", json_str);
    free(json_str);
    cJSON_Delete(arr);
}

static void handle_admin_export_csv(struct mg_connection *c, struct mg_http_message *hm) {
    User admin;
    if (!require_admin(hm, &admin, c)) return;

    char *csv = analytics_export_complaints_csv();
    if (!csv) {
        send_json_error(c, 500, "Failed to generate CSV export.");
        return;
    }

    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/csv; charset=utf-8\r\n"
              "Content-Disposition: attachment; filename=\"campus_complaints_export.csv\"\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Content-Length: %lu\r\n"
              "\r\n%s",
              (unsigned long)strlen(csv), csv);
    free(csv);
}

// ==========================================
// DAA Algorithm Lab Handlers
// ==========================================

static void handle_daa_metadata(struct mg_connection *c) {
    cJSON *root = cJSON_CreateObject();
    cJSON *algos = cJSON_CreateArray();

    // Algo 1: Merge Sort
    cJSON *a1 = cJSON_CreateObject();
    cJSON_AddStringToObject(a1, "id", "merge_sort");
    cJSON_AddStringToObject(a1, "name", "Merge Sort");
    cJSON_AddStringToObject(a1, "type", "Sorting");
    cJSON_AddStringToObject(a1, "purpose", "Deterministic priority and chronological sorting of campus complaints");
    cJSON_AddStringToObject(a1, "best_time", "O(n log n)");
    cJSON_AddStringToObject(a1, "avg_time", "O(n log n)");
    cJSON_AddStringToObject(a1, "worst_time", "O(n log n)");
    cJSON_AddStringToObject(a1, "aux_space", "O(n)");
    cJSON_AddStringToObject(a1, "stability", "Stable with deterministic tie-breaking");
    cJSON_AddStringToObject(a1, "pseudocode",
                            "procedure merge_sort(A, left, right):\n"
                            "    if left < right:\n"
                            "        mid = left + (right - left) / 2\n"
                            "        merge_sort(A, left, mid)\n"
                            "        merge_sort(A, mid + 1, right)\n"
                            "        merge(A, left, mid, right)");
    cJSON_AddItemToArray(algos, a1);

    // Algo 2: Binary Search
    cJSON *a2 = cJSON_CreateObject();
    cJSON_AddStringToObject(a2, "id", "binary_search");
    cJSON_AddStringToObject(a2, "name", "Iterative Binary Search");
    cJSON_AddStringToObject(a2, "type", "Searching");
    cJSON_AddStringToObject(a2, "purpose", "Logarithmic ID lookup in pre-sorted complaint records");
    cJSON_AddStringToObject(a2, "best_time", "O(1)");
    cJSON_AddStringToObject(a2, "avg_time", "O(log n)");
    cJSON_AddStringToObject(a2, "worst_time", "O(log n)");
    cJSON_AddStringToObject(a2, "aux_space", "O(1)");
    cJSON_AddStringToObject(a2, "stability", "N/A");
    cJSON_AddStringToObject(a2, "pseudocode",
                            "procedure binary_search(A, n, target):\n"
                            "    low = 0, high = n - 1\n"
                            "    while low <= high:\n"
                            "        mid = low + (high - low) / 2\n"
                            "        if A[mid].id == target: return mid\n"
                            "        else if A[mid].id < target: low = mid + 1\n"
                            "        else: high = mid - 1\n"
                            "    return -1");
    cJSON_AddItemToArray(algos, a2);

    // Algo 3: Max-Heap Priority Queue
    cJSON *a3 = cJSON_CreateObject();
    cJSON_AddStringToObject(a3, "id", "max_heap");
    cJSON_AddStringToObject(a3, "name", "Max-Heap Priority Queue");
    cJSON_AddStringToObject(a3, "type", "Priority Queue / Heap");
    cJSON_AddStringToObject(a3, "purpose", "Constant time peak critical issue discovery & logarithmic triage dispatch");
    cJSON_AddStringToObject(a3, "best_time", "Peek O(1), Build O(n)");
    cJSON_AddStringToObject(a3, "avg_time", "Insert O(log n), Extract O(log n)");
    cJSON_AddStringToObject(a3, "worst_time", "O(log n)");
    cJSON_AddStringToObject(a3, "aux_space", "O(n)");
    cJSON_AddStringToObject(a3, "stability", "Deterministic FIFO for equal severity");
    cJSON_AddStringToObject(a3, "pseudocode",
                            "procedure heap_insert(H, item):\n"
                            "    H.data[size] = item\n"
                            "    sift_up(H, size)\n"
                            "    size = size + 1\n"
                            "\n"
                            "procedure heap_extract_max(H):\n"
                            "    max_item = H.data[0]\n"
                            "    H.data[0] = H.data[size - 1]\n"
                            "    size = size - 1\n"
                            "    sift_down(H, 0)\n"
                            "    return max_item");
    cJSON_AddItemToArray(algos, a3);

    // Algo 4: Linear Search
    cJSON *a4 = cJSON_CreateObject();
    cJSON_AddStringToObject(a4, "id", "linear_search");
    cJSON_AddStringToObject(a4, "name", "Linear Search");
    cJSON_AddStringToObject(a4, "type", "Searching (Baseline)");
    cJSON_AddStringToObject(a4, "purpose", "Baseline exhaustive scan across unsorted collections & category filters");
    cJSON_AddStringToObject(a4, "best_time", "O(1)");
    cJSON_AddStringToObject(a4, "avg_time", "O(n)");
    cJSON_AddStringToObject(a4, "worst_time", "O(n)");
    cJSON_AddStringToObject(a4, "aux_space", "O(1)");
    cJSON_AddStringToObject(a4, "stability", "Stable");
    cJSON_AddStringToObject(a4, "pseudocode",
                            "procedure linear_search(A, n, target):\n"
                            "    for i from 0 to n - 1:\n"
                            "        if A[i] matches target: return i\n"
                            "    return -1");
    cJSON_AddItemToArray(algos, a4);

    cJSON_AddItemToObject(root, "algorithms", algos);
    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "DAA algorithm metadata retrieved.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

// Generate sample complaints for DAA demos
static void generate_sample_dataset(ComplaintRecord *arr, int count) {
    const char *cats[] = {"Electricity", "Hostel", "Water Supply", "Internet and Wi-Fi", "Classroom", "Cleanliness", "Library", "Laboratory"};
    const char *locations[] = {"Block A", "Block B", "Hostel 3", "Library 2nd Fl", "Tech Park", "Mess Hall", "Admin Wing"};
    const char *sample_titles[] = {
        "Main corridor lights flickering", "Water leakage near washroom",
        "Wi-Fi signal drop in study room", "AC malfunction in Room 204",
        "Broken window pane", "Projector display blurred",
        "Drinking water dispenser empty", "Elevator button unresponsive"
    };

    for (int i = 0; i < count; i++) {
        snprintf(arr[i].complaint_id, sizeof(arr[i].complaint_id), "CMP-2026-%04d", (i * 37 + 7) % (count * 2 + 1) + 1);
        arr[i].user_id = (i % 20) + 1;
        snprintf(arr[i].student_name, sizeof(arr[i].student_name), "Student %d", arr[i].user_id);
        snprintf(arr[i].title, sizeof(arr[i].title), "%s (%d)", sample_titles[i % 8], i + 1);
        strncpy(arr[i].category, cats[i % 8], sizeof(arr[i].category) - 1);
        strncpy(arr[i].location, locations[i % 7], sizeof(arr[i].location) - 1);
        arr[i].priority = (i % 4) + 1; // 1 to 4
        strncpy(arr[i].status, (i % 3 == 0) ? "pending" : (i % 3 == 1 ? "in_progress" : "resolved"), sizeof(arr[i].status) - 1);
        snprintf(arr[i].created_at, sizeof(arr[i].created_at), "2026-10-%02dT%02d:%02d:00Z", (i % 25) + 1, (i % 24), (i * 7) % 60);
    }
}

// DAA Sort Demo (Merge Sort with C live execution)
static void handle_daa_sort_demo(struct mg_connection *c, struct mg_http_message *hm) {
    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    int count = 50;
    SortOrder order = SORT_BY_PRIORITY_DESC;

    if (json) {
        cJSON *cnt_item = cJSON_GetObjectItem(json, "count");
        if (cnt_item && cnt_item->type == cJSON_Number && cnt_item->valueint > 0) {
            count = cnt_item->valueint;
            if (count > 5000) count = 5000;
        }
        const char *order_str = cJSON_GetStringValue(cJSON_GetObjectItem(json, "order"));
        if (order_str) {
            if (strcmp(order_str, "priority") == 0) order = SORT_BY_PRIORITY_DESC;
            else if (strcmp(order_str, "date_desc") == 0) order = SORT_BY_DATE_DESC;
            else if (strcmp(order_str, "date_asc") == 0) order = SORT_BY_DATE_ASC;
            else if (strcmp(order_str, "id") == 0) order = SORT_BY_ID_ASC;
        }
        cJSON_Delete(json);
    }

    ComplaintRecord *dataset = (ComplaintRecord *)malloc(sizeof(ComplaintRecord) * count);
    if (!dataset) {
        send_json_error(c, 500, "Memory allocation error.");
        return;
    }
    generate_sample_dataset(dataset, count);

    // Capture before preview (first 10 items)
    cJSON *before_arr = cJSON_CreateArray();
    int preview_n = (count < 10) ? count : 10;
    for (int i = 0; i < preview_n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "complaint_id", dataset[i].complaint_id);
        cJSON_AddNumberToObject(item, "priority", dataset[i].priority);
        cJSON_AddStringToObject(item, "category", dataset[i].category);
        cJSON_AddStringToObject(item, "created_at", dataset[i].created_at);
        cJSON_AddItemToArray(before_arr, item);
    }

    // Run Handcrafted C Merge Sort
    AlgorithmMetrics metrics;
    merge_sort(dataset, count, order, &metrics);

    // Capture after preview (first 10 items)
    cJSON *after_arr = cJSON_CreateArray();
    for (int i = 0; i < preview_n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "complaint_id", dataset[i].complaint_id);
        cJSON_AddNumberToObject(item, "priority", dataset[i].priority);
        cJSON_AddStringToObject(item, "category", dataset[i].category);
        cJSON_AddStringToObject(item, "created_at", dataset[i].created_at);
        cJSON_AddItemToArray(after_arr, item);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "algorithm", "Merge Sort");
    cJSON_AddNumberToObject(root, "dataset_size", count);
    cJSON_AddNumberToObject(root, "comparisons", (double)metrics.comparisons);
    cJSON_AddNumberToObject(root, "movements", (double)metrics.movements);
    cJSON_AddNumberToObject(root, "duration_us", metrics.duration_us);
    cJSON_AddStringToObject(root, "theoretical_complexity", "O(n log n)");
    cJSON_AddStringToObject(root, "auxiliary_space", "O(n)");
    cJSON_AddItemToObject(root, "before_preview", before_arr);
    cJSON_AddItemToObject(root, "after_preview", after_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Merge Sort executed in C engine.", json_str);
    free(json_str);
    cJSON_Delete(root);
    free(dataset);
}

// DAA Search Demo (Binary Search vs Linear Search C benchmark)
static void handle_daa_search_demo(struct mg_connection *c, struct mg_http_message *hm) {
    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    int count = 500;
    char target_id[32] = {0};

    if (json) {
        cJSON *cnt_item = cJSON_GetObjectItem(json, "count");
        if (cnt_item && cnt_item->type == cJSON_Number && cnt_item->valueint > 0) {
            count = cnt_item->valueint;
            if (count > 10000) count = 10000;
        }
        const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(json, "target_id"));
        if (t) strncpy(target_id, t, sizeof(target_id) - 1);
        cJSON_Delete(json);
    }

    ComplaintRecord *dataset = (ComplaintRecord *)malloc(sizeof(ComplaintRecord) * count);
    if (!dataset) {
        send_json_error(c, 500, "Memory allocation error.");
        return;
    }
    generate_sample_dataset(dataset, count);

    // If no target ID provided, select an existing element in the latter half
    if (strlen(target_id) == 0) {
        strncpy(target_id, dataset[(count * 3) / 4].complaint_id, sizeof(target_id) - 1);
    }

    // 1. Run Linear Search on unsorted array
    AlgorithmMetrics lin_metrics;
    int lin_idx = linear_search_by_id(dataset, count, target_id, &lin_metrics);

    // 2. Sort array by ID for Binary Search
    AlgorithmMetrics sort_metrics;
    merge_sort(dataset, count, SORT_BY_ID_ASC, &sort_metrics);

    // 3. Run Binary Search on sorted array
    AlgorithmMetrics bin_metrics;
    int bin_idx = binary_search_by_id(dataset, count, target_id, &bin_metrics);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "dataset_size", count);
    cJSON_AddStringToObject(root, "target_id", target_id);

    // Binary search results
    cJSON *bin_res = cJSON_CreateObject();
    cJSON_AddBoolToObject(bin_res, "found", bin_idx >= 0);
    cJSON_AddNumberToObject(bin_res, "index", bin_idx);
    cJSON_AddNumberToObject(bin_res, "comparisons", (double)bin_metrics.comparisons);
    cJSON_AddNumberToObject(bin_res, "duration_us", bin_metrics.duration_us);
    cJSON_AddStringToObject(bin_res, "theoretical_complexity", "O(log n)");
    cJSON_AddStringToObject(bin_res, "auxiliary_space", "O(1)");
    cJSON_AddItemToObject(root, "binary_search", bin_res);

    // Linear search results
    cJSON *lin_res = cJSON_CreateObject();
    cJSON_AddBoolToObject(lin_res, "found", lin_idx >= 0);
    cJSON_AddNumberToObject(lin_res, "index", lin_idx);
    cJSON_AddNumberToObject(lin_res, "comparisons", (double)lin_metrics.comparisons);
    cJSON_AddNumberToObject(lin_res, "duration_us", lin_metrics.duration_us);
    cJSON_AddStringToObject(lin_res, "theoretical_complexity", "O(n)");
    cJSON_AddStringToObject(lin_res, "auxiliary_space", "O(1)");
    cJSON_AddItemToObject(root, "linear_search", lin_res);

    // Efficiency comparison ratio
    double ratio = (bin_metrics.comparisons > 0) ? (double)lin_metrics.comparisons / (double)bin_metrics.comparisons : 0.0;
    cJSON_AddNumberToObject(root, "comparison_reduction_factor", ratio);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Search comparison benchmark completed in C.", json_str);
    free(json_str);
    cJSON_Delete(root);
    free(dataset);
}

// DAA Max-Heap Priority Queue Demo
static void handle_daa_priority_demo(struct mg_connection *c, struct mg_http_message *hm) {
    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    int count = 20;

    if (json) {
        cJSON *cnt_item = cJSON_GetObjectItem(json, "count");
        if (cnt_item && cnt_item->type == cJSON_Number && cnt_item->valueint > 0) {
            count = cnt_item->valueint;
            if (count > 200) count = 200;
        }
        cJSON_Delete(json);
    }

    ComplaintRecord *dataset = (ComplaintRecord *)malloc(sizeof(ComplaintRecord) * count);
    if (!dataset) {
        send_json_error(c, 500, "Memory allocation error.");
        return;
    }
    generate_sample_dataset(dataset, count);

    // Create Max-Heap and build bottom-up
    ComplaintHeap *heap = heap_create(count);
    heap_build(heap, dataset, count);

    // Peek root (highest priority item)
    ComplaintRecord root_item;
    bool has_peek = heap_peek(heap, &root_item);

    // Extract elements in prioritized sequence
    cJSON *extracted_arr = cJSON_CreateArray();
    ComplaintRecord out;
    while (heap_extract_max(heap, &out)) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "complaint_id", out.complaint_id);
        cJSON_AddNumberToObject(item, "priority", out.priority);
        cJSON_AddStringToObject(item, "category", out.category);
        cJSON_AddStringToObject(item, "created_at", out.created_at);
        cJSON_AddItemToArray(extracted_arr, item);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "algorithm", "Max-Heap Priority Queue");
    cJSON_AddNumberToObject(root, "dataset_size", count);
    cJSON_AddNumberToObject(root, "comparisons", (double)heap->metrics.comparisons);
    cJSON_AddNumberToObject(root, "movements", (double)heap->metrics.movements);
    cJSON_AddNumberToObject(root, "duration_us", heap->metrics.duration_us);
    cJSON_AddStringToObject(root, "peek_complexity", "O(1)");
    cJSON_AddStringToObject(root, "insert_extract_complexity", "O(log n)");
    cJSON_AddStringToObject(root, "build_complexity", "O(n)");

    if (has_peek) {
        cJSON *pk = cJSON_CreateObject();
        cJSON_AddStringToObject(pk, "complaint_id", root_item.complaint_id);
        cJSON_AddNumberToObject(pk, "priority", root_item.priority);
        cJSON_AddStringToObject(pk, "category", root_item.category);
        cJSON_AddStringToObject(pk, "created_at", root_item.created_at);
        cJSON_AddItemToObject(root, "highest_priority_item", pk);
    }

    cJSON_AddItemToObject(root, "prioritized_dispatch_order", extracted_arr);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Max-Heap priority queue operations demonstrated in C.", json_str);
    free(json_str);
    cJSON_Delete(root);
    heap_free(heap);
    free(dataset);
}

// Development password reset endpoint (as required by Section 5)
static void handle_auth_reset_dev(struct mg_connection *c, struct mg_http_message *hm) {
    cJSON *json = cJSON_ParseWithLength(hm->body.ptr, hm->body.len);
    if (!json) {
        send_json_error(c, 400, "Invalid JSON payload.");
        return;
    }

    const char *email = cJSON_GetStringValue(cJSON_GetObjectItem(json, "email"));
    const char *new_pwd = cJSON_GetStringValue(cJSON_GetObjectItem(json, "new_password"));

    if (!email || !new_pwd || strlen(new_pwd) < 6) {
        cJSON_Delete(json);
        send_json_error(c, 400, "Email and new password (min 6 chars) are required.");
        return;
    }

    sqlite3 *db = db_get_handle();
    if (!db) {
        cJSON_Delete(json);
        send_json_error(c, 500, "Database unavailable.");
        return;
    }

    // Check user exists
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT user_id FROM users WHERE email = ? COLLATE NOCASE;", -1, &stmt, NULL) != SQLITE_OK) {
        cJSON_Delete(json);
        send_json_error(c, 500, "Database query error.");
        return;
    }
    sqlite3_bind_text(stmt, 1, email, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        cJSON_Delete(json);
        send_json_error(c, 404, "No account found with this email address.");
        return;
    }
    int user_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    char salt[65] = {0}, hash[65] = {0};
    crypto_hash_password(new_pwd, salt, sizeof(salt), hash, sizeof(hash));

    char now[32];
    get_current_utc_timestamp(now, sizeof(now));

    sqlite3_stmt *upd = NULL;
    if (sqlite3_prepare_v2(db, "UPDATE users SET password_hash = ?, salt = ?, updated_at = ? WHERE user_id = ?;", -1, &upd, NULL) == SQLITE_OK) {
        sqlite3_bind_text(upd, 1, hash, -1, SQLITE_STATIC);
        sqlite3_bind_text(upd, 2, salt, -1, SQLITE_STATIC);
        sqlite3_bind_text(upd, 3, now, -1, SQLITE_STATIC);
        sqlite3_bind_int(upd, 4, user_id);
        sqlite3_step(upd);
        sqlite3_finalize(upd);
    }
    cJSON_Delete(json);

    send_json_response(c, 200, true, "Password reset successfully (Development Workflow). You can now log in.", "null");
}

// Health Check
static void handle_health_check(struct mg_connection *c) {
    sqlite3 *db = db_get_handle();
    bool db_ok = (db != NULL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "healthy");
    cJSON_AddStringToObject(root, "service", "Campus Complaint Tracker API Engine");
    cJSON_AddStringToObject(root, "backend", "C (Mongoose HTTP + SQLite3 + cJSON)");
    cJSON_AddBoolToObject(root, "database_connected", db_ok);
    cJSON_AddStringToObject(root, "sqlite_version", sqlite3_libversion());

    char *json_str = cJSON_PrintUnformatted(root);
    send_json_response(c, 200, true, "Service running normally.", json_str);
    free(json_str);
    cJSON_Delete(root);
}

// ==========================================
// Main Request Dispatcher & Static File Server
// ==========================================

void handle_http_request(struct mg_connection *c, struct mg_http_message *hm) {
    // 1. Handle CORS Preflight (OPTIONS)
    if (mg_vcasecmp(&hm->method, "OPTIONS") == 0) {
        mg_printf(c,
                  "HTTP/1.1 204 No Content\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n"
                  "Access-Control-Allow-Headers: Content-Type, Authorization, Cookie\r\n"
                  "Access-Control-Allow-Credentials: true\r\n"
                  "Content-Length: 0\r\n\r\n");
        return;
    }

    char uri[512] = {0};
    size_t u_len = (hm->uri.len < sizeof(uri) - 1) ? hm->uri.len : sizeof(uri) - 1;
    memcpy(uri, hm->uri.ptr, u_len);
    uri[u_len] = '\0';

    // 2. Health Check
    if (mg_match(hm->uri, mg_str("/api/health"), NULL)) {
        handle_health_check(c);
        return;
    }

    // 3. Authentication Routes
    if (mg_match(hm->uri, mg_str("/api/auth/register"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_auth_register(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/auth/login"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_auth_login(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/auth/logout"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_auth_logout(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/auth/me"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_auth_me(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/auth/change-password"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_auth_change_password(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/auth/reset-dev"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_auth_reset_dev(c, hm);
        return;
    }

    // 4. Student Routes
    if (mg_match(hm->uri, mg_str("/api/student/dashboard"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_student_dashboard(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/student/complaints"), NULL)) {
        if (mg_vcasecmp(&hm->method, "GET") == 0) {
            handle_student_complaints_list(c, hm);
            return;
        } else if (mg_vcasecmp(&hm->method, "POST") == 0) {
            handle_student_create_complaint(c, hm);
            return;
        }
    }
    if (mg_match(hm->uri, mg_str("/api/student/profile"), NULL)) {
        handle_student_profile(c, hm);
        return;
    }

    // 5. Admin Routes
    if (mg_match(hm->uri, mg_str("/api/admin/dashboard"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_admin_dashboard(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/admin/complaints"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_admin_complaints_list(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/admin/users"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_admin_users(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/admin/analytics"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_admin_dashboard(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/admin/export.csv"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_admin_export_csv(c, hm);
        return;
    }

    // 6. Parametric Routes (/api/student/complaints/:id and /api/admin/complaints/:id/...)
    struct mg_str id_match;
    if (mg_match(hm->uri, mg_str("/api/student/complaints/*"), &id_match) ||
        mg_match(hm->uri, mg_str("/api/admin/complaints/*"), &id_match)) {
        char full_subpath[256] = {0};
        size_t id_len = (id_match.len < sizeof(full_subpath) - 1) ? id_match.len : sizeof(full_subpath) - 1;
        memcpy(full_subpath, id_match.ptr, id_len);
        full_subpath[id_len] = '\0';

        char cid[64] = {0};
        char action[64] = {0};
        char *slash = strchr(full_subpath, '/');
        if (slash) {
            size_t c_len = (size_t)(slash - full_subpath);
            if (c_len < sizeof(cid)) {
                memcpy(cid, full_subpath, c_len);
                cid[c_len] = '\0';
            }
            strncpy(action, slash + 1, sizeof(action) - 1);
        } else {
            strncpy(cid, full_subpath, sizeof(cid) - 1);
        }

        if (strlen(action) == 0 || strcmp(action, "history") == 0) {
            if (mg_vcasecmp(&hm->method, "GET") == 0) {
                handle_complaint_detail(c, hm, cid);
                return;
            }
        } else if (strcmp(action, "status") == 0) {
            if (mg_vcasecmp(&hm->method, "PATCH") == 0) {
                handle_admin_update_status(c, hm, cid);
                return;
            }
        } else if (strcmp(action, "priority") == 0) {
            if (mg_vcasecmp(&hm->method, "PATCH") == 0) {
                handle_admin_update_priority(c, hm, cid);
                return;
            }
        } else if (strcmp(action, "remarks") == 0) {
            if (mg_vcasecmp(&hm->method, "POST") == 0) {
                handle_admin_add_remarks(c, hm, cid);
                return;
            }
        }
    }

    // 7. DAA Lab Routes
    if (mg_match(hm->uri, mg_str("/api/algorithms"), NULL) && mg_vcasecmp(&hm->method, "GET") == 0) {
        handle_daa_metadata(c);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/algorithms/sort-demo"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_daa_sort_demo(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/algorithms/search-demo"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_daa_search_demo(c, hm);
        return;
    }
    if (mg_match(hm->uri, mg_str("/api/algorithms/priority-demo"), NULL) && mg_vcasecmp(&hm->method, "POST") == 0) {
        handle_daa_priority_demo(c, hm);
        return;
    }

    // 8. API 404 Fallback for any unknown /api/ route
    if (strncmp(uri, "/api/", 5) == 0) {
        send_json_error(c, 404, "Endpoint not found.");
        return;
    }

    // 9. Static File Serving from ./public
    struct mg_http_serve_opts opts = {.root_dir = "public"};
    if (strcmp(uri, "/") == 0) {
        mg_http_serve_file(c, hm, "public/index.html", &opts);
    } else {
        mg_http_serve_dir(c, hm, &opts);
    }
}
