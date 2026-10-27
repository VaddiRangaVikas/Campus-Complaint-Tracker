#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../lib/mongoose.h"
#include "cJSON.h"
#include "server.h"
#include "database.h"
#include "auth.h"
#include "algorithms.h"

static bool s_running = true;
static ServerConfig s_config;

static void send_json_response(struct mg_connection *c, int status_code, bool success, const char *msg, cJSON *data, const char *extra_headers)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", success);
    cJSON_AddStringToObject(root, "message", msg ? msg : "");
    if (data) {
        cJSON_AddItemToObject(root, "data", data);
    } else {
        cJSON_AddNullToObject(root, "data");
    }

    char *json_str = cJSON_PrintUnformatted(root);

    char headers[512];
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Methods: GET, POST, PATCH, OPTIONS\r\n"
             "Access-Control-Allow-Headers: Content-Type, Authorization, Cookie\r\n"
             "%s",
             extra_headers ? extra_headers : "");

    mg_http_reply(c, status_code, headers, "%s\n", json_str ? json_str : "{}");

    if (json_str) free(json_str);
    cJSON_Delete(root);
}

static void send_error(struct mg_connection *c, int status_code, const char *msg)
{
    send_json_response(c, status_code, false, msg, NULL, NULL);
}

// Extract token from Cookie or Authorization header
static bool extract_token(struct mg_http_message *hm, char *out_token, size_t max_len)
{
    if (!hm || !out_token || max_len < 16) return false;

    // Check Cookie header: session_id=...
    struct mg_str *cookie_hdr = mg_http_get_header(hm, "Cookie");
    if (cookie_hdr && cookie_hdr->len > 0) {
        char cookie_str[512];
        size_t clen = cookie_hdr->len < sizeof(cookie_str) - 1 ? cookie_hdr->len : sizeof(cookie_str) - 1;
        memcpy(cookie_str, cookie_hdr->buf, clen);
        cookie_str[clen] = '\0';

        char *ptr = strstr(cookie_str, "session_id=");
        if (ptr) {
            ptr += strlen("session_id=");
            char *end = strchr(ptr, ';');
            if (end) *end = '\0';
            while (*ptr == ' ') ptr++;
            strncpy(out_token, ptr, max_len - 1);
            out_token[max_len - 1] = '\0';
            if (strlen(out_token) > 0) return true;
        }
    }

    // Check Authorization header: Bearer ...
    struct mg_str *auth_hdr = mg_http_get_header(hm, "Authorization");
    if (auth_hdr && auth_hdr->len > 0) {
        char auth_str[512];
        size_t alen = auth_hdr->len < sizeof(auth_str) - 1 ? auth_hdr->len : sizeof(auth_str) - 1;
        memcpy(auth_str, auth_hdr->buf, alen);
        auth_str[alen] = '\0';

        if (strncmp(auth_str, "Bearer ", 7) == 0) {
            char *tok = auth_str + 7;
            while (*tok == ' ') tok++;
            strncpy(out_token, tok, max_len - 1);
            out_token[max_len - 1] = '\0';
            if (strlen(out_token) > 0) return true;
        }
    }

    return false;
}

static bool get_authenticated_user(struct mg_http_message *hm, User *out_user)
{
    char token[128];
    if (!extract_token(hm, token, sizeof(token))) return false;
    return db_get_session(token, out_user);
}

// Convert Complaint struct to cJSON
static cJSON *complaint_to_json(const Complaint *c)
{
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

// Convert User struct to cJSON
static cJSON *user_to_json(const User *u)
{
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

// ========================================================
// API Route Handlers
// ========================================================

// POST /api/auth/register
static void handle_register(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON payload in request");
        return;
    }

    cJSON *name_item = cJSON_GetObjectItem(body, "full_name");
    cJSON *email_item = cJSON_GetObjectItem(body, "email");
    cJSON *pass_item = cJSON_GetObjectItem(body, "password");
    cJSON *dept_item = cJSON_GetObjectItem(body, "department");
    cJSON *sid_item = cJSON_GetObjectItem(body, "student_id");
    cJSON *role_item = cJSON_GetObjectItem(body, "role");

    if (!name_item || !email_item || !pass_item || !dept_item) {
        cJSON_Delete(body);
        send_error(c, 400, "Missing required registration fields (full_name, email, password, department)");
        return;
    }

    const char *full_name = name_item->valuestring;
    const char *email = email_item->valuestring;
    const char *password = pass_item->valuestring;
    const char *department = dept_item->valuestring;
    const char *student_id = sid_item && sid_item->valuestring ? sid_item->valuestring : "";
    const char *role = role_item && role_item->valuestring ? role_item->valuestring : "student";

    if (strlen(full_name) < 2 || strlen(email) < 5 || strlen(password) < 6) {
        cJSON_Delete(body);
        send_error(c, 400, "Validation error: Name must be >= 2 chars, password >= 6 chars");
        return;
    }

    // Role safety: public register can only register "student"
    if (strcmp(role, "admin") == 0) {
        role = "student";
    }

    // Check if user already exists
    User existing;
    if (db_get_user_by_email(email, &existing, NULL, NULL)) {
        cJSON_Delete(body);
        send_error(c, 409, "An account with this email address already exists");
        return;
    }

    char salt[65], hash[65];
    auth_generate_salt(salt, sizeof(salt));
    auth_hash_password(password, salt, hash, sizeof(hash));

    User user;
    memset(&user, 0, sizeof(User));
    strncpy(user.full_name, full_name, sizeof(user.full_name) - 1);
    strncpy(user.email, email, sizeof(user.email) - 1);
    strncpy(user.student_id, student_id, sizeof(user.student_id) - 1);
    strncpy(user.department, department, sizeof(user.department) - 1);
    strncpy(user.role, role, sizeof(user.role) - 1);
    strncpy(user.account_status, "active", sizeof(user.account_status) - 1);

    int new_user_id = 0;
    if (!db_create_user(&user, hash, salt, &new_user_id)) {
        cJSON_Delete(body);
        send_error(c, 500, "Failed to create user record in database");
        return;
    }

    user.user_id = new_user_id;

    // Create session immediately
    char token[128];
    auth_generate_token(token, sizeof(token));
    db_create_session(token, new_user_id, SESSION_DURATION_SEC);

    char cookie_header[256];
    snprintf(cookie_header, sizeof(cookie_header),
             "Set-Cookie: session_id=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d\r\n",
             token, SESSION_DURATION_SEC);

    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "token", token);
    cJSON_AddItemToObject(resp_data, "user", user_to_json(&user));

    cJSON_Delete(body);
    send_json_response(c, 201, true, "Registration successful", resp_data, cookie_header);
}

// POST /api/auth/login
static void handle_login(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON payload in login request");
        return;
    }

    cJSON *email_item = cJSON_GetObjectItem(body, "email");
    cJSON *pass_item = cJSON_GetObjectItem(body, "password");

    if (!email_item || !pass_item || !email_item->valuestring || !pass_item->valuestring) {
        cJSON_Delete(body);
        send_error(c, 400, "Email and password are required");
        return;
    }

    const char *email = email_item->valuestring;
    const char *password = pass_item->valuestring;

    User user;
    char stored_hash[65] = {0};
    char stored_salt[65] = {0};

    bool user_found = db_get_user_by_email(email, &user, stored_hash, stored_salt);

    if (!user_found || !auth_verify_password(password, stored_salt, stored_hash)) {
        cJSON_Delete(body);
        send_error(c, 401, "Invalid email or password credentials");
        return;
    }

    if (strcmp(user.account_status, "suspended") == 0) {
        cJSON_Delete(body);
        send_error(c, 403, "Account has been suspended. Please contact campus admin.");
        return;
    }

    // Create session token
    char token[128];
    auth_generate_token(token, sizeof(token));
    db_create_session(token, user.user_id, SESSION_DURATION_SEC);

    char cookie_header[256];
    snprintf(cookie_header, sizeof(cookie_header),
             "Set-Cookie: session_id=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d\r\n",
             token, SESSION_DURATION_SEC);

    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "token", token);
    cJSON_AddItemToObject(resp_data, "user", user_to_json(&user));

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Login successful", resp_data, cookie_header);
}

// POST /api/auth/logout
static void handle_logout(struct mg_connection *c, struct mg_http_message *hm)
{
    char token[128];
    if (extract_token(hm, token, sizeof(token))) {
        db_delete_session(token);
    }

    const char *cookie_header = "Set-Cookie: session_id=; Path=/; HttpOnly; Max-Age=0\r\n";
    send_json_response(c, 200, true, "Logged out successfully", NULL, cookie_header);
}

// GET /api/auth/me
static void handle_me(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    send_json_response(c, 200, true, "User authenticated", user_to_json(&user), NULL);
}

// POST /api/auth/change-password
static void handle_change_password(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON payload");
        return;
    }

    cJSON *old_item = cJSON_GetObjectItem(body, "old_password");
    cJSON *new_item = cJSON_GetObjectItem(body, "new_password");

    if (!old_item || !new_item || !old_item->valuestring || !new_item->valuestring) {
        cJSON_Delete(body);
        send_error(c, 400, "old_password and new_password are required");
        return;
    }

    // Verify current password
    char cur_hash[65] = {0};
    char cur_salt[65] = {0};
    User check_u;
    db_get_user_by_email(user.email, &check_u, cur_hash, cur_salt);

    if (!auth_verify_password(old_item->valuestring, cur_salt, cur_hash)) {
        cJSON_Delete(body);
        send_error(c, 400, "Current password does not match");
        return;
    }

    if (strlen(new_item->valuestring) < 6) {
        cJSON_Delete(body);
        send_error(c, 400, "New password must be at least 6 characters");
        return;
    }

    char new_salt[65], new_hash[65];
    auth_generate_salt(new_salt, sizeof(new_salt));
    auth_hash_password(new_item->valuestring, new_salt, new_hash, sizeof(new_hash));

    db_update_user_password(user.user_id, new_hash, new_salt);

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Password updated successfully", NULL, NULL);
}

// POST /api/auth/reset-dev
static void handle_reset_dev(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON payload");
        return;
    }

    cJSON *email_item = cJSON_GetObjectItem(body, "email");
    cJSON *new_item = cJSON_GetObjectItem(body, "new_password");

    if (!email_item || !new_item || !email_item->valuestring || !new_item->valuestring) {
        cJSON_Delete(body);
        send_error(c, 400, "Email and new_password are required");
        return;
    }

    User user;
    if (!db_get_user_by_email(email_item->valuestring, &user, NULL, NULL)) {
        cJSON_Delete(body);
        send_error(c, 404, "User account not found");
        return;
    }

    char new_salt[65], new_hash[65];
    auth_generate_salt(new_salt, sizeof(new_salt));
    auth_hash_password(new_item->valuestring, new_salt, new_hash, sizeof(new_hash));

    db_update_user_password(user.user_id, new_hash, new_salt);

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Password reset successfully via local development workflow", NULL, NULL);
}

// ========================================================
// Student Route Handlers
// ========================================================

// GET /api/student/dashboard
static void handle_student_dashboard(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    cJSON *stats = NULL;
    db_get_dashboard_stats(user.user_id, user.role, &stats);

    // Get recent complaints (up to 5)
    Complaint *complaints = NULL;
    int count = 0;
    db_get_complaints_by_user(user.user_id, &complaints, &count);

    cJSON *recent_arr = cJSON_CreateArray();
    int limit = count > 5 ? 5 : count;
    for (int i = 0; i < limit; i++) {
        cJSON_AddItemToArray(recent_arr, complaint_to_json(&complaints[i]));
    }
    if (complaints) free(complaints);

    cJSON_AddItemToObject(stats, "recent_complaints", recent_arr);
    send_json_response(c, 200, true, "Student dashboard data", stats, NULL);
}

// GET /api/student/complaints
static void handle_student_complaints(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    Complaint *complaints = NULL;
    int count = 0;
    db_get_complaints_by_user(user.user_id, &complaints, &count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, complaint_to_json(&complaints[i]));
    }
    if (complaints) free(complaints);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "total", count);
    cJSON_AddItemToObject(resp, "complaints", arr);

    send_json_response(c, 200, true, "Student complaints", resp, NULL);
}

// POST /api/student/complaints
static void handle_submit_complaint(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON body");
        return;
    }

    cJSON *title_item = cJSON_GetObjectItem(body, "title");
    cJSON *cat_item = cJSON_GetObjectItem(body, "category");
    cJSON *desc_item = cJSON_GetObjectItem(body, "description");
    cJSON *loc_item = cJSON_GetObjectItem(body, "location");
    cJSON *room_item = cJSON_GetObjectItem(body, "room_number");
    cJSON *pri_item = cJSON_GetObjectItem(body, "priority");

    if (!title_item || !cat_item || !desc_item || !loc_item) {
        cJSON_Delete(body);
        send_error(c, 400, "title, category, description, and location are required");
        return;
    }

    Complaint comp;
    memset(&comp, 0, sizeof(Complaint));
    comp.user_id = user.user_id;
    strncpy(comp.title, title_item->valuestring, sizeof(comp.title) - 1);
    strncpy(comp.category, cat_item->valuestring, sizeof(comp.category) - 1);
    strncpy(comp.description, desc_item->valuestring, sizeof(comp.description) - 1);
    strncpy(comp.location, loc_item->valuestring, sizeof(comp.location) - 1);
    if (room_item && room_item->valuestring) {
        strncpy(comp.room_number, room_item->valuestring, sizeof(comp.room_number) - 1);
    }
    comp.priority = (pri_item && pri_item->valueint >= 1 && pri_item->valueint <= 4) ? pri_item->valueint : 2;
    strncpy(comp.status, "pending", sizeof(comp.status) - 1);

    if (!db_create_complaint(&comp)) {
        cJSON_Delete(body);
        send_error(c, 500, "Failed to persist complaint in database");
        return;
    }

    cJSON_Delete(body);
    send_json_response(c, 201, true, "Complaint submitted successfully", complaint_to_json(&comp), NULL);
}

// GET /api/student/complaints/:id
static void handle_complaint_detail(struct mg_connection *c, struct mg_http_message *hm, const char *cid)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    Complaint comp;
    if (!db_get_complaint(cid, &comp)) {
        send_error(c, 404, "Complaint not found");
        return;
    }

    // Role check: student can only view their own complaints, admin can view all
    if (strcmp(user.role, "student") == 0 && comp.user_id != user.user_id) {
        send_error(c, 403, "Access denied to this complaint record");
        return;
    }

    // Also fetch history
    ComplaintHistory *hist = NULL;
    int hcount = 0;
    db_get_complaint_history(cid, &hist, &hcount);

    cJSON *hist_arr = cJSON_CreateArray();
    for (int i = 0; i < hcount; i++) {
        cJSON *h = cJSON_CreateObject();
        cJSON_AddNumberToObject(h, "history_id", hist[i].history_id);
        cJSON_AddStringToObject(h, "changed_by_name", hist[i].changed_by_name);
        cJSON_AddStringToObject(h, "changed_by_role", hist[i].changed_by_role);
        cJSON_AddStringToObject(h, "old_status", hist[i].old_status);
        cJSON_AddStringToObject(h, "new_status", hist[i].new_status);
        cJSON_AddStringToObject(h, "remarks", hist[i].remarks);
        cJSON_AddStringToObject(h, "created_at", hist[i].created_at);
        cJSON_AddItemToArray(hist_arr, h);
    }
    if (hist) free(hist);

    cJSON *res = complaint_to_json(&comp);
    cJSON_AddItemToObject(res, "history", hist_arr);

    send_json_response(c, 200, true, "Complaint details", res, NULL);
}

// PATCH /api/student/profile
static void handle_update_profile(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }

    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON body");
        return;
    }

    cJSON *name_item = cJSON_GetObjectItem(body, "full_name");
    cJSON *dept_item = cJSON_GetObjectItem(body, "department");

    const char *name = name_item && name_item->valuestring ? name_item->valuestring : user.full_name;
    const char *dept = dept_item && dept_item->valuestring ? dept_item->valuestring : user.department;

    db_update_user_profile(user.user_id, name, dept);

    User updated;
    db_get_user_by_id(user.user_id, &updated);

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Profile updated successfully", user_to_json(&updated), NULL);
}

// ========================================================
// Administrator Route Handlers
// ========================================================

// GET /api/admin/dashboard
static void handle_admin_dashboard(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    cJSON *stats = NULL;
    db_get_dashboard_stats(user.user_id, "admin", &stats);

    Complaint *complaints = NULL;
    int count = 0;
    db_get_all_complaints(&complaints, &count);

    cJSON *recent_arr = cJSON_CreateArray();
    int limit = count > 8 ? 8 : count;
    for (int i = 0; i < limit; i++) {
        cJSON_AddItemToArray(recent_arr, complaint_to_json(&complaints[i]));
    }
    if (complaints) free(complaints);

    cJSON_AddItemToObject(stats, "recent_complaints", recent_arr);
    send_json_response(c, 200, true, "Admin dashboard metrics", stats, NULL);
}

// GET /api/admin/complaints
static void handle_admin_complaints(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    Complaint *complaints = NULL;
    int count = 0;
    db_get_all_complaints(&complaints, &count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, complaint_to_json(&complaints[i]));
    }
    if (complaints) free(complaints);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "total", count);
    cJSON_AddItemToObject(res, "complaints", arr);

    send_json_response(c, 200, true, "All campus complaints", res, NULL);
}

// PATCH /api/admin/complaints/:id/status
static void handle_admin_update_status(struct mg_connection *c, struct mg_http_message *hm, const char *cid)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON body");
        return;
    }

    cJSON *status_item = cJSON_GetObjectItem(body, "status");
    cJSON *remarks_item = cJSON_GetObjectItem(body, "remarks");

    if (!status_item || !status_item->valuestring) {
        cJSON_Delete(body);
        send_error(c, 400, "Status is required");
        return;
    }

    const char *new_status = status_item->valuestring;
    const char *remarks = remarks_item && remarks_item->valuestring ? remarks_item->valuestring : "";

    if (!db_update_complaint_status(cid, new_status, user.user_id, remarks)) {
        cJSON_Delete(body);
        send_error(c, 500, "Failed to update complaint status");
        return;
    }

    Complaint updated;
    db_get_complaint(cid, &updated);

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Complaint status updated", complaint_to_json(&updated), NULL);
}

// PATCH /api/admin/complaints/:id/priority
static void handle_admin_update_priority(struct mg_connection *c, struct mg_http_message *hm, const char *cid)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON body");
        return;
    }

    cJSON *pri_item = cJSON_GetObjectItem(body, "priority");
    cJSON *remarks_item = cJSON_GetObjectItem(body, "remarks");

    if (!pri_item || pri_item->valueint < 1 || pri_item->valueint > 4) {
        cJSON_Delete(body);
        send_error(c, 400, "Priority must be between 1 and 4");
        return;
    }

    int priority = pri_item->valueint;
    const char *remarks = remarks_item && remarks_item->valuestring ? remarks_item->valuestring : "";

    if (!db_update_complaint_priority(cid, priority, user.user_id, remarks)) {
        cJSON_Delete(body);
        send_error(c, 500, "Failed to update complaint priority");
        return;
    }

    Complaint updated;
    db_get_complaint(cid, &updated);

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Complaint priority updated", complaint_to_json(&updated), NULL);
}

// POST /api/admin/complaints/:id/remarks
static void handle_admin_add_remarks(struct mg_connection *c, struct mg_http_message *hm, const char *cid)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    if (!body) {
        send_error(c, 400, "Invalid JSON body");
        return;
    }

    cJSON *rem_item = cJSON_GetObjectItem(body, "remarks");
    if (!rem_item || !rem_item->valuestring || strlen(rem_item->valuestring) == 0) {
        cJSON_Delete(body);
        send_error(c, 400, "Remarks text is required");
        return;
    }

    db_add_complaint_remarks(cid, user.user_id, rem_item->valuestring);

    cJSON_Delete(body);
    send_json_response(c, 200, true, "Remark recorded in complaint timeline", NULL, NULL);
}

// GET /api/admin/users
static void handle_admin_users(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    User *users = NULL;
    int count = 0;
    db_get_all_users(&users, &count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, user_to_json(&users[i]));
    }
    if (users) free(users);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "total", count);
    cJSON_AddItemToObject(res, "users", arr);

    send_json_response(c, 200, true, "Registered users list", res, NULL);
}

// GET /api/admin/analytics
static void handle_admin_analytics(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    cJSON *analytics = NULL;
    db_get_admin_analytics(&analytics);

    send_json_response(c, 200, true, "Aggregated campus analytics", analytics, NULL);
}

// GET /api/admin/export.csv
static void handle_admin_export_csv(struct mg_connection *c, struct mg_http_message *hm)
{
    User user;
    if (!get_authenticated_user(hm, &user)) {
        send_error(c, 401, "Not authenticated");
        return;
    }
    if (strcmp(user.role, "admin") != 0) {
        send_error(c, 403, "Admin authorization required");
        return;
    }

    char *csv_content = NULL;
    if (!db_export_complaints_csv(&csv_content)) {
        send_error(c, 500, "Failed to generate CSV export");
        return;
    }

    mg_http_reply(c, 200,
                  "Content-Type: text/csv\r\n"
                  "Content-Disposition: attachment; filename=\"campus_complaints.csv\"\r\n"
                  "Access-Control-Allow-Origin: *\r\n",
                  "%s", csv_content);

    free(csv_content);
}

// ========================================================
// DAA Algorithm Endpoints
// ========================================================

// GET /api/algorithms
static void handle_algorithms_meta(struct mg_connection *c, struct mg_http_message *hm)
{
    (void)hm;
    cJSON *list = cJSON_CreateArray();

    // Algo A: Merge Sort
    cJSON *a1 = cJSON_CreateObject();
    cJSON_AddStringToObject(a1, "name", "Merge Sort");
    cJSON_AddStringToObject(a1, "purpose", "Deterministic multi-key sorting (Priority, Date, ID)");
    cJSON_AddStringToObject(a1, "best_case", "O(n log n)");
    cJSON_AddStringToObject(a1, "avg_case", "O(n log n)");
    cJSON_AddStringToObject(a1, "worst_case", "O(n log n)");
    cJSON_AddStringToObject(a1, "aux_space", "O(n)");
    cJSON_AddItemToArray(list, a1);

    // Algo B: Binary Search
    cJSON *a2 = cJSON_CreateObject();
    cJSON_AddStringToObject(a2, "name", "Binary Search");
    cJSON_AddStringToObject(a2, "purpose", "Logarithmic ID lookup in sorted collection");
    cJSON_AddStringToObject(a2, "best_case", "O(1)");
    cJSON_AddStringToObject(a2, "avg_case", "O(log n)");
    cJSON_AddStringToObject(a2, "worst_case", "O(log n)");
    cJSON_AddStringToObject(a2, "aux_space", "O(1)");
    cJSON_AddItemToArray(list, a2);

    // Algo C: Max-Heap Priority Queue
    cJSON *a3 = cJSON_CreateObject();
    cJSON_AddStringToObject(a3, "name", "Max-Heap Priority Queue");
    cJSON_AddStringToObject(a3, "purpose", "Urgent complaint dispatching (Critical -> Low with FIFO tie-break)");
    cJSON_AddStringToObject(a3, "best_case", "Peek: O(1), Insert: O(log n), Extract: O(log n)");
    cJSON_AddStringToObject(a3, "avg_case", "Build: O(n) Floyd's bottom-up algorithm");
    cJSON_AddStringToObject(a3, "worst_case", "Extract: O(log n)");
    cJSON_AddStringToObject(a3, "aux_space", "O(n)");
    cJSON_AddItemToArray(list, a3);

    // Algo D: Linear Search
    cJSON *a4 = cJSON_CreateObject();
    cJSON_AddStringToObject(a4, "name", "Linear Search");
    cJSON_AddStringToObject(a4, "purpose", "Baseline scan for category, location or unsorted search");
    cJSON_AddStringToObject(a4, "best_case", "O(1)");
    cJSON_AddStringToObject(a4, "avg_case", "O(n)");
    cJSON_AddStringToObject(a4, "worst_case", "O(n)");
    cJSON_AddStringToObject(a4, "aux_space", "O(1)");
    cJSON_AddItemToArray(list, a4);

    // Algo E: Indexed SQLite B-Tree Search
    cJSON *a5 = cJSON_CreateObject();
    cJSON_AddStringToObject(a5, "name", "SQLite B-Tree Indexed Search");
    cJSON_AddStringToObject(a5, "purpose", "Persistent on-disk lookup via SQLite query planner indexes");
    cJSON_AddStringToObject(a5, "best_case", "O(1) index cache hit");
    cJSON_AddStringToObject(a5, "avg_case", "O(log n) B-Tree node traversals");
    cJSON_AddStringToObject(a5, "worst_case", "O(log n) disk reads");
    cJSON_AddStringToObject(a5, "aux_space", "O(1) in-memory page");
    cJSON_AddItemToArray(list, a5);

    send_json_response(c, 200, true, "DAA algorithm specifications", list, NULL);
}

// POST /api/algorithms/sort-demo
static void handle_sort_demo(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    int size = 100;
    SortOrder order = SORT_BY_PRIORITY_DESC;

    if (body) {
        cJSON *sz = cJSON_GetObjectItem(body, "size");
        if (sz && sz->valueint >= 1 && sz->valueint <= 10000) size = sz->valueint;

        cJSON *sort_by = cJSON_GetObjectItem(body, "sort_by");
        if (sort_by && sort_by->valuestring) {
            if (strcmp(sort_by->valuestring, "date_desc") == 0) order = SORT_BY_DATE_DESC;
            else if (strcmp(sort_by->valuestring, "date_asc") == 0) order = SORT_BY_DATE_ASC;
            else if (strcmp(sort_by->valuestring, "id") == 0) order = SORT_BY_ID_ASC;
            else order = SORT_BY_PRIORITY_DESC;
        }
    }

    ComplaintRecord *arr = (ComplaintRecord *)malloc(size * sizeof(ComplaintRecord));
    for (int i = 0; i < size; i++) {
        snprintf(arr[i].complaint_id, sizeof(arr[i].complaint_id), "CMP-2026-%04d", (rand() % (size * 2)) + 1);
        arr[i].user_id = (i % 20) + 1;
        arr[i].priority = (rand() % 4) + 1;
        snprintf(arr[i].title, sizeof(arr[i].title), "Sample Issue #%d", i + 1);
        snprintf(arr[i].category, sizeof(arr[i].category), (i % 2 == 0) ? "Hostel" : "Electricity");
        snprintf(arr[i].created_at, sizeof(arr[i].created_at), "2026-10-%02dT%02d:%02d:00Z",
                 (rand() % 28) + 1, rand() % 24, rand() % 60);
    }

    // Capture first 5 before sort
    cJSON *before_sample = cJSON_CreateArray();
    int sample_len = size > 6 ? 6 : size;
    for (int i = 0; i < sample_len; i++) {
        cJSON *itm = cJSON_CreateObject();
        cJSON_AddStringToObject(itm, "id", arr[i].complaint_id);
        cJSON_AddNumberToObject(itm, "priority", arr[i].priority);
        cJSON_AddStringToObject(itm, "created_at", arr[i].created_at);
        cJSON_AddItemToArray(before_sample, itm);
    }

    AlgorithmMetrics metrics;
    merge_sort(arr, size, order, &metrics);

    // Capture first 6 after sort
    cJSON *after_sample = cJSON_CreateArray();
    for (int i = 0; i < sample_len; i++) {
        cJSON *itm = cJSON_CreateObject();
        cJSON_AddStringToObject(itm, "id", arr[i].complaint_id);
        cJSON_AddNumberToObject(itm, "priority", arr[i].priority);
        cJSON_AddStringToObject(itm, "created_at", arr[i].created_at);
        cJSON_AddItemToArray(after_sample, itm);
    }

    free(arr);
    if (body) cJSON_Delete(body);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "algorithm", "Merge Sort");
    cJSON_AddNumberToObject(res, "input_size", size);
    cJSON_AddNumberToObject(res, "comparisons", (double)metrics.comparisons);
    cJSON_AddNumberToObject(res, "movements", (double)metrics.movements);
    cJSON_AddNumberToObject(res, "duration_microseconds", metrics.duration_us);
    cJSON_AddStringToObject(res, "time_complexity", "O(n log n)");
    cJSON_AddStringToObject(res, "space_complexity", "O(n)");
    cJSON_AddItemToObject(res, "before_sample", before_sample);
    cJSON_AddItemToObject(res, "after_sample", after_sample);

    send_json_response(c, 200, true, "Merge sort executed in C backend", res, NULL);
}

// POST /api/algorithms/search-demo
static void handle_search_demo(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    int size = 1000;
    char target[32] = "CMP-2026-0500";

    if (body) {
        cJSON *sz = cJSON_GetObjectItem(body, "size");
        if (sz && sz->valueint >= 1 && sz->valueint <= 10000) size = sz->valueint;

        cJSON *tgt = cJSON_GetObjectItem(body, "target_id");
        if (tgt && tgt->valuestring && strlen(tgt->valuestring) > 0) {
            strncpy(target, tgt->valuestring, sizeof(target) - 1);
        }
    }

    ComplaintRecord *arr = (ComplaintRecord *)malloc(size * sizeof(ComplaintRecord));
    for (int i = 0; i < size; i++) {
        snprintf(arr[i].complaint_id, sizeof(arr[i].complaint_id), "CMP-2026-%04d", (i + 1));
        arr[i].priority = (i % 4) + 1;
    }

    // Binary search (array is already sorted by ID)
    AlgorithmMetrics b_metrics;
    int b_idx = binary_search_by_id(arr, size, target, &b_metrics);

    // Linear search
    AlgorithmMetrics l_metrics;
    int l_idx = linear_search_by_id(arr, size, target, &l_metrics);

    free(arr);
    if (body) cJSON_Delete(body);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "target_id", target);
    cJSON_AddNumberToObject(res, "dataset_size", size);

    cJSON *bs = cJSON_CreateObject();
    cJSON_AddStringToObject(bs, "name", "Binary Search");
    cJSON_AddNumberToObject(bs, "found_index", b_idx);
    cJSON_AddNumberToObject(bs, "comparisons", (double)b_metrics.comparisons);
    cJSON_AddNumberToObject(bs, "duration_microseconds", b_metrics.duration_us);
    cJSON_AddStringToObject(bs, "complexity", "O(log n)");
    cJSON_AddItemToObject(res, "binary_search", bs);

    cJSON *ls = cJSON_CreateObject();
    cJSON_AddStringToObject(ls, "name", "Linear Search");
    cJSON_AddNumberToObject(ls, "found_index", l_idx);
    cJSON_AddNumberToObject(ls, "comparisons", (double)l_metrics.comparisons);
    cJSON_AddNumberToObject(ls, "duration_microseconds", l_metrics.duration_us);
    cJSON_AddStringToObject(ls, "complexity", "O(n)");
    cJSON_AddItemToObject(res, "linear_search", ls);

    send_json_response(c, 200, true, "Search benchmark executed in C backend", res, NULL);
}

// POST /api/algorithms/priority-demo
static void handle_priority_demo(struct mg_connection *c, struct mg_http_message *hm)
{
    cJSON *body = cJSON_ParseWithOpts(hm->body.buf, NULL, 0);
    int size = 50;
    if (body) {
        cJSON *sz = cJSON_GetObjectItem(body, "size");
        if (sz && sz->valueint >= 1 && sz->valueint <= 1000) size = sz->valueint;
    }

    ComplaintRecord *items = (ComplaintRecord *)malloc(size * sizeof(ComplaintRecord));
    for (int i = 0; i < size; i++) {
        snprintf(items[i].complaint_id, sizeof(items[i].complaint_id), "CMP-2026-%04d", i + 1);
        items[i].priority = (rand() % 4) + 1;
        snprintf(items[i].created_at, sizeof(items[i].created_at), "2026-10-%02dT%02d:%02d:00Z",
                 (rand() % 28) + 1, rand() % 24, rand() % 60);
    }

    ComplaintHeap *heap = heap_create(size);
    heap_build(heap, items, size);

    cJSON *extracted = cJSON_CreateArray();
    int extract_limit = size > 8 ? 8 : size;

    for (int i = 0; i < extract_limit; i++) {
        ComplaintRecord out;
        if (heap_extract_max(heap, &out)) {
            cJSON *itm = cJSON_CreateObject();
            cJSON_AddStringToObject(itm, "id", out.complaint_id);
            cJSON_AddNumberToObject(itm, "priority", out.priority);
            cJSON_AddStringToObject(itm, "created_at", out.created_at);
            cJSON_AddItemToArray(extracted, itm);
        }
    }

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "algorithm", "Max-Heap Priority Queue");
    cJSON_AddNumberToObject(res, "total_items", size);
    cJSON_AddNumberToObject(res, "build_comparisons", (double)heap->metrics.comparisons);
    cJSON_AddNumberToObject(res, "build_movements", (double)heap->metrics.movements);
    cJSON_AddNumberToObject(res, "duration_microseconds", heap->metrics.duration_us);
    cJSON_AddStringToObject(res, "build_complexity", "O(n) Floyd's Heapify");
    cJSON_AddStringToObject(res, "extract_complexity", "O(log n)");
    cJSON_AddItemToObject(res, "extracted_highest_priority", extracted);

    heap_free(heap);
    free(items);
    if (body) cJSON_Delete(body);

    send_json_response(c, 200, true, "Max-Heap priority queue executed in C backend", res, NULL);
}

// GET /api/health
static void handle_health(struct mg_connection *c, struct mg_http_message *hm)
{
    (void)hm;
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "UP");
    cJSON_AddStringToObject(data, "database", "SQLite 3.53.3");
    cJSON_AddStringToObject(data, "architecture", "C Embedded Server");
    cJSON_AddStringToObject(data, "version", "1.0.0");
    send_json_response(c, 200, true, "Campus Complaint Tracker API is online", data, NULL);
}

// ========================================================
// Main Event Handler & Router
// ========================================================

static void fn(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_MSG) return;

    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    // Handle CORS Preflight
    if (mg_strcmp(hm->method, mg_str("OPTIONS")) == 0) {
        mg_http_reply(c, 204,
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, PATCH, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type, Authorization, Cookie\r\n"
                      "Access-Control-Max-Age: 86400\r\n",
                      "");
        return;
    }

    char uri[256];
    size_t ulen = hm->uri.len < sizeof(uri) - 1 ? hm->uri.len : sizeof(uri) - 1;
    memcpy(uri, hm->uri.buf, ulen);
    uri[ulen] = '\0';

    char method[16];
    size_t mlen = hm->method.len < sizeof(method) - 1 ? hm->method.len : sizeof(method) - 1;
    memcpy(method, hm->method.buf, mlen);
    method[mlen] = '\0';

    // Route: /api/health
    if (strcmp(uri, "/api/health") == 0) {
        handle_health(c, hm);
        return;
    }

    // Auth Routes
    if (strcmp(uri, "/api/auth/register") == 0 && strcmp(method, "POST") == 0) {
        handle_register(c, hm);
        return;
    }
    if (strcmp(uri, "/api/auth/login") == 0 && strcmp(method, "POST") == 0) {
        handle_login(c, hm);
        return;
    }
    if (strcmp(uri, "/api/auth/logout") == 0 && strcmp(method, "POST") == 0) {
        handle_logout(c, hm);
        return;
    }
    if (strcmp(uri, "/api/auth/me") == 0 && strcmp(method, "GET") == 0) {
        handle_me(c, hm);
        return;
    }
    if (strcmp(uri, "/api/auth/change-password") == 0 && strcmp(method, "POST") == 0) {
        handle_change_password(c, hm);
        return;
    }
    if (strcmp(uri, "/api/auth/reset-dev") == 0 && strcmp(method, "POST") == 0) {
        handle_reset_dev(c, hm);
        return;
    }

    // Student Routes
    if (strcmp(uri, "/api/student/dashboard") == 0 && strcmp(method, "GET") == 0) {
        handle_student_dashboard(c, hm);
        return;
    }
    if (strcmp(uri, "/api/student/complaints") == 0 && strcmp(method, "GET") == 0) {
        handle_student_complaints(c, hm);
        return;
    }
    if (strcmp(uri, "/api/student/complaints") == 0 && strcmp(method, "POST") == 0) {
        handle_submit_complaint(c, hm);
        return;
    }
    if (strncmp(uri, "/api/student/complaints/", 24) == 0 && strcmp(method, "GET") == 0) {
        const char *cid = uri + 24;
        handle_complaint_detail(c, hm, cid);
        return;
    }
    if (strcmp(uri, "/api/student/profile") == 0 && strcmp(method, "PATCH") == 0) {
        handle_update_profile(c, hm);
        return;
    }

    // Administrator Routes
    if (strcmp(uri, "/api/admin/dashboard") == 0 && strcmp(method, "GET") == 0) {
        handle_admin_dashboard(c, hm);
        return;
    }
    if (strcmp(uri, "/api/admin/complaints") == 0 && strcmp(method, "GET") == 0) {
        handle_admin_complaints(c, hm);
        return;
    }
    if (strcmp(uri, "/api/admin/users") == 0 && strcmp(method, "GET") == 0) {
        handle_admin_users(c, hm);
        return;
    }
    if (strcmp(uri, "/api/admin/analytics") == 0 && strcmp(method, "GET") == 0) {
        handle_admin_analytics(c, hm);
        return;
    }
    if (strcmp(uri, "/api/admin/export.csv") == 0 && strcmp(method, "GET") == 0) {
        handle_admin_export_csv(c, hm);
        return;
    }
    if (strncmp(uri, "/api/admin/complaints/", 22) == 0) {
        const char *rest = uri + 22;
        char cid[32];
        const char *slash = strchr(rest, '/');
        if (slash) {
            size_t cid_len = slash - rest;
            if (cid_len < sizeof(cid)) {
                strncpy(cid, rest, cid_len);
                cid[cid_len] = '\0';
                if (strcmp(slash, "/status") == 0 && strcmp(method, "PATCH") == 0) {
                    handle_admin_update_status(c, hm, cid);
                    return;
                }
                if (strcmp(slash, "/priority") == 0 && strcmp(method, "PATCH") == 0) {
                    handle_admin_update_priority(c, hm, cid);
                    return;
                }
                if (strcmp(slash, "/remarks") == 0 && strcmp(method, "POST") == 0) {
                    handle_admin_add_remarks(c, hm, cid);
                    return;
                }
            }
        } else if (strcmp(method, "GET") == 0) {
            handle_complaint_detail(c, hm, rest);
            return;
        }
    }

    // DAA Algorithm Routes
    if (strcmp(uri, "/api/algorithms") == 0 && strcmp(method, "GET") == 0) {
        handle_algorithms_meta(c, hm);
        return;
    }
    if (strcmp(uri, "/api/algorithms/sort-demo") == 0 && strcmp(method, "POST") == 0) {
        handle_sort_demo(c, hm);
        return;
    }
    if (strcmp(uri, "/api/algorithms/search-demo") == 0 && strcmp(method, "POST") == 0) {
        handle_search_demo(c, hm);
        return;
    }
    if (strcmp(uri, "/api/algorithms/priority-demo") == 0 && strcmp(method, "POST") == 0) {
        handle_priority_demo(c, hm);
        return;
    }

    // If API route not found
    if (strncmp(uri, "/api/", 5) == 0) {
        send_error(c, 404, "API endpoint not found");
        return;
    }

    // Static Web File Serving
    struct mg_http_serve_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.root_dir = s_config.web_root;

    if (strcmp(uri, "/") == 0) {
        mg_http_serve_file(c, hm, "public/index.html", &opts);
    } else {
        mg_http_serve_dir(c, hm, &opts);
    }
}

bool server_start(const ServerConfig *config)
{
    if (config) {
        s_config = *config;
    } else {
        s_config.port = DEFAULT_PORT;
        strncpy(s_config.web_root, DEFAULT_WEB_ROOT, sizeof(s_config.web_root) - 1);
        strncpy(s_config.db_path, DEFAULT_DB_PATH, sizeof(s_config.db_path) - 1);
    }

    if (!db_init(s_config.db_path)) {
        fprintf(stderr, "[Server Error] Failed to initialize database: %s\n", s_config.db_path);
        return false;
    }

    db_seed_demo_data();

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    char url[64];
    snprintf(url, sizeof(url), "http://0.0.0.0:%d", s_config.port);

    struct mg_connection *c = mg_http_listen(&mgr, url, fn, &mgr);
    if (!c) {
        fprintf(stderr, "[Server Error] Cannot listen on %s\n", url);
        db_close();
        return false;
    }

    printf("\n=======================================================\n");
    printf("   CAMPUS COMPLAINT TRACKER - FULL-STACK DAA SERVER    \n");
    printf("=======================================================\n");
    printf("   * Server running on: http://localhost:%d\n", s_config.port);
    printf("   * Web Root:          %s\n", s_config.web_root);
    printf("   * SQLite DB:         %s\n", s_config.db_path);
    printf("   * Admin Account:     admin@campus.edu (Admin@123)\n");
    printf("   * Demo Students:     student1@campus.edu (Student@123)\n");
    printf("                        student2@campus.edu (Student@123)\n");
    printf("=======================================================\n");
    fflush(stdout);

    while (s_running) {
        mg_mgr_poll(&mgr, 100);
    }

    mg_mgr_free(&mgr);
    db_close();
    return true;
}

void server_stop(void)
{
    s_running = false;
}
