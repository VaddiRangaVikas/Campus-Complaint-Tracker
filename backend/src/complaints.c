#include "complaints.h"
#include "database.h"
#include "validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool complaints_generate_id(char *out_id, size_t id_size) {
    if (!out_id || id_size < 20) return false;

    sqlite3 *db = db_get_handle();
    if (!db) return false;

    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    int year = tm_info ? (tm_info->tm_year + 1900) : 2026;

    // Get current maximum sequence for the year
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT COUNT(*) FROM complaints;";
    int count = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    snprintf(out_id, id_size, "CMP-%d-%04d", year, count + 1);
    return true;
}

bool complaints_create(int user_id, const char *title, const char *category,
                       const char *description, const char *location,
                       const char *room_number, int priority,
                       char *out_complaint_id, size_t id_size,
                       char *err_msg, size_t err_size) {
    sqlite3 *db = db_get_handle();
    if (!db) {
        snprintf(err_msg, err_size, "Database not available.");
        return false;
    }

    if (!validate_complaint(title, category, description, location, room_number, priority, err_msg, err_size)) {
        return false;
    }

    char cid[32];
    if (!complaints_generate_id(cid, sizeof(cid))) {
        snprintf(err_msg, err_size, "Failed to generate unique complaint ID.");
        return false;
    }

    char now[32];
    get_current_utc_timestamp(now, sizeof(now));

    // Begin ACID transaction
    if (!db_begin()) {
        snprintf(err_msg, err_size, "Failed to begin transaction.");
        return false;
    }

    // 1. Insert into complaints table
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO complaints (complaint_id, user_id, title, category, description, location, room_number, priority, status, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        db_rollback();
        snprintf(err_msg, err_size, "Database statement preparation error: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, cid, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, category, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, location, -1, SQLITE_STATIC);
    if (room_number && strlen(room_number) > 0) {
        sqlite3_bind_text(stmt, 7, room_number, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 7);
    }
    sqlite3_bind_int(stmt, 8, priority);
    sqlite3_bind_text(stmt, 9, now, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, now, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        db_rollback();
        snprintf(err_msg, err_size, "Failed to insert complaint: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_finalize(stmt);

    // 2. Insert initial audit timeline entry into complaint_history
    sqlite3_stmt *hist_stmt = NULL;
    const char *hist_sql = "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
                           "VALUES (?, ?, NULL, 'pending', 'Complaint submitted by student.', ?);";

    if (sqlite3_prepare_v2(db, hist_sql, -1, &hist_stmt, NULL) != SQLITE_OK) {
        db_rollback();
        snprintf(err_msg, err_size, "Failed to record audit history: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(hist_stmt, 1, cid, -1, SQLITE_STATIC);
    sqlite3_bind_int(hist_stmt, 2, user_id);
    sqlite3_bind_text(hist_stmt, 3, now, -1, SQLITE_STATIC);

    if (sqlite3_step(hist_stmt) != SQLITE_DONE) {
        sqlite3_finalize(hist_stmt);
        db_rollback();
        snprintf(err_msg, err_size, "Failed to insert audit history: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_finalize(hist_stmt);

    // Commit ACID transaction
    if (!db_commit()) {
        db_rollback();
        snprintf(err_msg, err_size, "Failed to commit complaint creation.");
        return false;
    }

    if (out_complaint_id) {
        strncpy(out_complaint_id, cid, id_size - 1);
        out_complaint_id[id_size - 1] = '\0';
    }

    return true;
}

bool complaints_get_by_id(const char *complaint_id, Complaint *out_complaint,
                          ComplaintHistory *out_history, int max_history, int *out_history_count) {
    if (!complaint_id || !out_complaint) return false;

    sqlite3 *db = db_get_handle();
    if (!db) return false;

    // Fetch complaint with student info
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT c.complaint_id, c.user_id, u.full_name, u.email, c.title, c.category, c.description, "
                      "c.location, c.room_number, c.priority, c.status, c.assigned_department, c.created_at, c.updated_at, c.resolved_at "
                      "FROM complaints c JOIN users u ON c.user_id = u.user_id "
                      "WHERE c.complaint_id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, complaint_id, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    memset(out_complaint, 0, sizeof(Complaint));
    strncpy(out_complaint->complaint_id, (const char *)sqlite3_column_text(stmt, 0), sizeof(out_complaint->complaint_id) - 1);
    out_complaint->user_id = sqlite3_column_int(stmt, 1);
    const char *name = (const char *)sqlite3_column_text(stmt, 2);
    if (name) strncpy(out_complaint->student_name, name, sizeof(out_complaint->student_name) - 1);
    const char *email = (const char *)sqlite3_column_text(stmt, 3);
    if (email) strncpy(out_complaint->student_email, email, sizeof(out_complaint->student_email) - 1);
    const char *title = (const char *)sqlite3_column_text(stmt, 4);
    if (title) strncpy(out_complaint->title, title, sizeof(out_complaint->title) - 1);
    const char *cat = (const char *)sqlite3_column_text(stmt, 5);
    if (cat) strncpy(out_complaint->category, cat, sizeof(out_complaint->category) - 1);
    const char *desc = (const char *)sqlite3_column_text(stmt, 6);
    if (desc) strncpy(out_complaint->description, desc, sizeof(out_complaint->description) - 1);
    const char *loc = (const char *)sqlite3_column_text(stmt, 7);
    if (loc) strncpy(out_complaint->location, loc, sizeof(out_complaint->location) - 1);
    const char *room = (const char *)sqlite3_column_text(stmt, 8);
    if (room) strncpy(out_complaint->room_number, room, sizeof(out_complaint->room_number) - 1);
    out_complaint->priority = sqlite3_column_int(stmt, 9);
    const char *st = (const char *)sqlite3_column_text(stmt, 10);
    if (st) strncpy(out_complaint->status, st, sizeof(out_complaint->status) - 1);
    const char *adept = (const char *)sqlite3_column_text(stmt, 11);
    if (adept) strncpy(out_complaint->assigned_department, adept, sizeof(out_complaint->assigned_department) - 1);
    const char *c_at = (const char *)sqlite3_column_text(stmt, 12);
    if (c_at) strncpy(out_complaint->created_at, c_at, sizeof(out_complaint->created_at) - 1);
    const char *u_at = (const char *)sqlite3_column_text(stmt, 13);
    if (u_at) strncpy(out_complaint->updated_at, u_at, sizeof(out_complaint->updated_at) - 1);
    const char *r_at = (const char *)sqlite3_column_text(stmt, 14);
    if (r_at) strncpy(out_complaint->resolved_at, r_at, sizeof(out_complaint->resolved_at) - 1);

    sqlite3_finalize(stmt);

    // Fetch history audit trail
    if (out_history && max_history > 0) {
        sqlite3_stmt *h_stmt = NULL;
        const char *h_sql = "SELECT h.history_id, h.complaint_id, h.changed_by, u.full_name, u.role, h.old_status, h.new_status, h.remarks, h.created_at "
                            "FROM complaint_history h JOIN users u ON h.changed_by = u.user_id "
                            "WHERE h.complaint_id = ? "
                            "ORDER BY h.history_id ASC;";

        int h_count = 0;
        if (sqlite3_prepare_v2(db, h_sql, -1, &h_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(h_stmt, 1, complaint_id, -1, SQLITE_STATIC);
            while (sqlite3_step(h_stmt) == SQLITE_ROW && h_count < max_history) {
                memset(&out_history[h_count], 0, sizeof(ComplaintHistory));
                out_history[h_count].history_id = sqlite3_column_int(h_stmt, 0);
                strncpy(out_history[h_count].complaint_id, (const char *)sqlite3_column_text(h_stmt, 1), sizeof(out_history[h_count].complaint_id) - 1);
                out_history[h_count].changed_by = sqlite3_column_int(h_stmt, 2);
                const char *h_name = (const char *)sqlite3_column_text(h_stmt, 3);
                if (h_name) strncpy(out_history[h_count].changed_by_name, h_name, sizeof(out_history[h_count].changed_by_name) - 1);
                const char *h_role = (const char *)sqlite3_column_text(h_stmt, 4);
                if (h_role) strncpy(out_history[h_count].changed_by_role, h_role, sizeof(out_history[h_count].changed_by_role) - 1);
                const char *old_st = (const char *)sqlite3_column_text(h_stmt, 5);
                if (old_st) strncpy(out_history[h_count].old_status, old_st, sizeof(out_history[h_count].old_status) - 1);
                const char *new_st = (const char *)sqlite3_column_text(h_stmt, 6);
                if (new_st) strncpy(out_history[h_count].new_status, new_st, sizeof(out_history[h_count].new_status) - 1);
                const char *rem = (const char *)sqlite3_column_text(h_stmt, 7);
                if (rem) strncpy(out_history[h_count].remarks, rem, sizeof(out_history[h_count].remarks) - 1);
                const char *hc_at = (const char *)sqlite3_column_text(h_stmt, 8);
                if (hc_at) strncpy(out_history[h_count].created_at, hc_at, sizeof(out_history[h_count].created_at) - 1);

                h_count++;
            }
            sqlite3_finalize(h_stmt);
        }
        if (out_history_count) *out_history_count = h_count;
    }

    return true;
}

// Helper to populate complaint item from query row
static void populate_complaint_from_row(sqlite3_stmt *stmt, Complaint *c) {
    memset(c, 0, sizeof(Complaint));
    strncpy(c->complaint_id, (const char *)sqlite3_column_text(stmt, 0), sizeof(c->complaint_id) - 1);
    c->user_id = sqlite3_column_int(stmt, 1);
    const char *name = (const char *)sqlite3_column_text(stmt, 2);
    if (name) strncpy(c->student_name, name, sizeof(c->student_name) - 1);
    const char *email = (const char *)sqlite3_column_text(stmt, 3);
    if (email) strncpy(c->student_email, email, sizeof(c->student_email) - 1);
    const char *title = (const char *)sqlite3_column_text(stmt, 4);
    if (title) strncpy(c->title, title, sizeof(c->title) - 1);
    const char *cat = (const char *)sqlite3_column_text(stmt, 5);
    if (cat) strncpy(c->category, cat, sizeof(c->category) - 1);
    const char *desc = (const char *)sqlite3_column_text(stmt, 6);
    if (desc) strncpy(c->description, desc, sizeof(c->description) - 1);
    const char *loc = (const char *)sqlite3_column_text(stmt, 7);
    if (loc) strncpy(c->location, loc, sizeof(c->location) - 1);
    const char *room = (const char *)sqlite3_column_text(stmt, 8);
    if (room) strncpy(c->room_number, room, sizeof(c->room_number) - 1);
    c->priority = sqlite3_column_int(stmt, 9);
    const char *st = (const char *)sqlite3_column_text(stmt, 10);
    if (st) strncpy(c->status, st, sizeof(c->status) - 1);
    const char *dept = (const char *)sqlite3_column_text(stmt, 11);
    if (dept) strncpy(c->assigned_department, dept, sizeof(c->assigned_department) - 1);
    const char *c_at = (const char *)sqlite3_column_text(stmt, 12);
    if (c_at) strncpy(c->created_at, c_at, sizeof(c->created_at) - 1);
    const char *u_at = (const char *)sqlite3_column_text(stmt, 13);
    if (u_at) strncpy(c->updated_at, u_at, sizeof(c->updated_at) - 1);
    const char *r_at = (const char *)sqlite3_column_text(stmt, 14);
    if (r_at) strncpy(c->resolved_at, r_at, sizeof(c->resolved_at) - 1);
}

// Builds filtered queries dynamically with prepared parameter bindings
static bool list_complaints_internal(int student_id_filter, const char *search,
                                     const char *category, const char *status,
                                     int priority, const char *sort_by,
                                     int page, int page_size,
                                     Complaint *out_list, int max_list,
                                     int *out_count, int *out_total) {
    sqlite3 *db = db_get_handle();
    if (!db) return false;

    if (page < 1) page = 1;
    if (page_size < 1) page_size = 10;
    if (page_size > 100) page_size = 100;
    int offset = (page - 1) * page_size;

    char where_clause[1024] = " WHERE 1=1";
    char search_pattern[256] = {0};

    if (student_id_filter > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), " AND c.user_id = %d", student_id_filter);
        strcat(where_clause, buf);
    }
    if (category && strlen(category) > 0 && strcmp(category, "all") != 0) {
        strcat(where_clause, " AND c.category = ?");
    }
    if (status && strlen(status) > 0 && strcmp(status, "all") != 0) {
        strcat(where_clause, " AND c.status = ?");
    }
    if (priority >= 1 && priority <= 4) {
        char buf[32];
        snprintf(buf, sizeof(buf), " AND c.priority = %d", priority);
        strcat(where_clause, buf);
    }
    if (search && strlen(search) > 0) {
        strcat(where_clause, " AND (c.complaint_id LIKE ? OR c.title LIKE ? OR c.location LIKE ?)");
        snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", search);
    }

    // Determine ORDER BY
    const char *order_sql = " ORDER BY c.created_at DESC";
    if (sort_by) {
        if (strcmp(sort_by, "priority_desc") == 0) {
            order_sql = " ORDER BY c.priority DESC, c.created_at ASC";
        } else if (strcmp(sort_by, "priority_asc") == 0) {
            order_sql = " ORDER BY c.priority ASC, c.created_at DESC";
        } else if (strcmp(sort_by, "date_asc") == 0) {
            order_sql = " ORDER BY c.created_at ASC";
        } else if (strcmp(sort_by, "id_asc") == 0) {
            order_sql = " ORDER BY c.complaint_id ASC";
        } else if (strcmp(sort_by, "id_desc") == 0) {
            order_sql = " ORDER BY c.complaint_id DESC";
        }
    }

    // 1. Get Total Count
    char count_sql[1500];
    snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) FROM complaints c %s;", where_clause);

    sqlite3_stmt *c_stmt = NULL;
    if (sqlite3_prepare_v2(db, count_sql, -1, &c_stmt, NULL) != SQLITE_OK) return false;

    int bind_idx = 1;
    if (category && strlen(category) > 0 && strcmp(category, "all") != 0) {
        sqlite3_bind_text(c_stmt, bind_idx++, category, -1, SQLITE_STATIC);
    }
    if (status && strlen(status) > 0 && strcmp(status, "all") != 0) {
        sqlite3_bind_text(c_stmt, bind_idx++, status, -1, SQLITE_STATIC);
    }
    if (search && strlen(search) > 0) {
        sqlite3_bind_text(c_stmt, bind_idx++, search_pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(c_stmt, bind_idx++, search_pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(c_stmt, bind_idx++, search_pattern, -1, SQLITE_STATIC);
    }

    int total_records = 0;
    if (sqlite3_step(c_stmt) == SQLITE_ROW) {
        total_records = sqlite3_column_int(c_stmt, 0);
    }
    sqlite3_finalize(c_stmt);
    if (out_total) *out_total = total_records;

    // 2. Fetch Paginated Records
    char fetch_sql[2048];
    snprintf(fetch_sql, sizeof(fetch_sql),
             "SELECT c.complaint_id, c.user_id, u.full_name, u.email, c.title, c.category, c.description, "
             "c.location, c.room_number, c.priority, c.status, c.assigned_department, c.created_at, c.updated_at, c.resolved_at "
             "FROM complaints c JOIN users u ON c.user_id = u.user_id %s %s LIMIT %d OFFSET %d;",
             where_clause, order_sql, page_size, offset);

    sqlite3_stmt *f_stmt = NULL;
    if (sqlite3_prepare_v2(db, fetch_sql, -1, &f_stmt, NULL) != SQLITE_OK) return false;

    bind_idx = 1;
    if (category && strlen(category) > 0 && strcmp(category, "all") != 0) {
        sqlite3_bind_text(f_stmt, bind_idx++, category, -1, SQLITE_STATIC);
    }
    if (status && strlen(status) > 0 && strcmp(status, "all") != 0) {
        sqlite3_bind_text(f_stmt, bind_idx++, status, -1, SQLITE_STATIC);
    }
    if (search && strlen(search) > 0) {
        sqlite3_bind_text(f_stmt, bind_idx++, search_pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(f_stmt, bind_idx++, search_pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(f_stmt, bind_idx++, search_pattern, -1, SQLITE_STATIC);
    }

    int count = 0;
    while (sqlite3_step(f_stmt) == SQLITE_ROW && count < max_list) {
        populate_complaint_from_row(f_stmt, &out_list[count]);
        count++;
    }
    sqlite3_finalize(f_stmt);

    if (out_count) *out_count = count;
    return true;
}

bool complaints_list_student(int user_id, const char *search, const char *category,
                             const char *status, int priority, const char *sort_by,
                             int page, int page_size,
                             Complaint *out_list, int max_list, int *out_count, int *out_total) {
    return list_complaints_internal(user_id, search, category, status, priority, sort_by, page, page_size, out_list, max_list, out_count, out_total);
}

bool complaints_list_admin(const char *search, const char *category,
                           const char *status, int priority, const char *sort_by,
                           int page, int page_size,
                           Complaint *out_list, int max_list, int *out_count, int *out_total) {
    return list_complaints_internal(0, search, category, status, priority, sort_by, page, page_size, out_list, max_list, out_count, out_total);
}

bool complaints_update_status(const char *complaint_id, int admin_id,
                              const char *new_status, const char *remarks,
                              char *err_msg, size_t err_size) {
    sqlite3 *db = db_get_handle();
    if (!db) {
        snprintf(err_msg, err_size, "Database not available.");
        return false;
    }

    // Get current status
    sqlite3_stmt *chk_stmt = NULL;
    const char *chk_sql = "SELECT status FROM complaints WHERE complaint_id = ?;";
    if (sqlite3_prepare_v2(db, chk_sql, -1, &chk_stmt, NULL) != SQLITE_OK) {
        snprintf(err_msg, err_size, "Database error.");
        return false;
    }

    sqlite3_bind_text(chk_stmt, 1, complaint_id, -1, SQLITE_STATIC);
    if (sqlite3_step(chk_stmt) != SQLITE_ROW) {
        sqlite3_finalize(chk_stmt);
        snprintf(err_msg, err_size, "Complaint '%s' not found.", complaint_id);
        return false;
    }

    char current_status[32] = {0};
    strncpy(current_status, (const char *)sqlite3_column_text(chk_stmt, 0), sizeof(current_status) - 1);
    sqlite3_finalize(chk_stmt);

    // Validate state transition
    if (!validate_status_transition(current_status, new_status, err_msg, err_size)) {
        return false;
    }

    char now[32];
    get_current_utc_timestamp(now, sizeof(now));

    // Begin ACID transaction
    if (!db_begin()) {
        snprintf(err_msg, err_size, "Failed to begin transaction.");
        return false;
    }

    // Update complaint status
    sqlite3_stmt *upd_stmt = NULL;
    const char *upd_sql = (strcmp(new_status, "resolved") == 0)
        ? "UPDATE complaints SET status = ?, updated_at = ?, resolved_at = ? WHERE complaint_id = ?;"
        : "UPDATE complaints SET status = ?, updated_at = ?, resolved_at = NULL WHERE complaint_id = ?;";

    if (sqlite3_prepare_v2(db, upd_sql, -1, &upd_stmt, NULL) != SQLITE_OK) {
        db_rollback();
        snprintf(err_msg, err_size, "Database update error: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(upd_stmt, 1, new_status, -1, SQLITE_STATIC);
    sqlite3_bind_text(upd_stmt, 2, now, -1, SQLITE_STATIC);
    if (strcmp(new_status, "resolved") == 0) {
        sqlite3_bind_text(upd_stmt, 3, now, -1, SQLITE_STATIC);
        sqlite3_bind_text(upd_stmt, 4, complaint_id, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(upd_stmt, 3, complaint_id, -1, SQLITE_STATIC);
    }

    if (sqlite3_step(upd_stmt) != SQLITE_DONE) {
        sqlite3_finalize(upd_stmt);
        db_rollback();
        snprintf(err_msg, err_size, "Failed to update complaint status: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_finalize(upd_stmt);

    // Insert history record
    sqlite3_stmt *hist_stmt = NULL;
    const char *hist_sql = "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
                           "VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, hist_sql, -1, &hist_stmt, NULL) != SQLITE_OK) {
        db_rollback();
        snprintf(err_msg, err_size, "Database history error: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(hist_stmt, 1, complaint_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(hist_stmt, 2, admin_id);
    sqlite3_bind_text(hist_stmt, 3, current_status, -1, SQLITE_STATIC);
    sqlite3_bind_text(hist_stmt, 4, new_status, -1, SQLITE_STATIC);
    sqlite3_bind_text(hist_stmt, 5, (remarks && strlen(remarks) > 0) ? remarks : "Status updated by admin.", -1, SQLITE_STATIC);
    sqlite3_bind_text(hist_stmt, 6, now, -1, SQLITE_STATIC);

    if (sqlite3_step(hist_stmt) != SQLITE_DONE) {
        sqlite3_finalize(hist_stmt);
        db_rollback();
        snprintf(err_msg, err_size, "Failed to append history record: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_finalize(hist_stmt);

    // Commit transaction
    if (!db_commit()) {
        db_rollback();
        snprintf(err_msg, err_size, "Failed to commit status change.");
        return false;
    }

    return true;
}

bool complaints_update_priority(const char *complaint_id, int admin_id,
                                int new_priority, const char *remarks,
                                char *err_msg, size_t err_size) {
    if (!validate_priority(new_priority, err_msg, err_size)) {
        return false;
    }

    sqlite3 *db = db_get_handle();
    if (!db) {
        snprintf(err_msg, err_size, "Database not available.");
        return false;
    }

    char now[32];
    get_current_utc_timestamp(now, sizeof(now));

    if (!db_begin()) return false;

    // Update priority
    sqlite3_stmt *stmt = NULL;
    const char *sql = "UPDATE complaints SET priority = ?, updated_at = ? WHERE complaint_id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        db_rollback();
        snprintf(err_msg, err_size, "Database error: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, new_priority);
    sqlite3_bind_text(stmt, 2, now, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, complaint_id, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        db_rollback();
        snprintf(err_msg, err_size, "Failed to update priority: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_finalize(stmt);

    // Log priority change in history
    char auto_remark[256];
    const char *prio_names[] = {"", "Low", "Medium", "High", "Critical"};
    snprintf(auto_remark, sizeof(auto_remark), "Priority modified to %s (%d). %s",
             prio_names[new_priority], new_priority, (remarks ? remarks : ""));

    sqlite3_stmt *h_stmt = NULL;
    const char *h_sql = "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
                        "SELECT complaint_id, ?, status, status, ?, ? FROM complaints WHERE complaint_id = ?;";
    if (sqlite3_prepare_v2(db, h_sql, -1, &h_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(h_stmt, 1, admin_id);
        sqlite3_bind_text(h_stmt, 2, auto_remark, -1, SQLITE_STATIC);
        sqlite3_bind_text(h_stmt, 3, now, -1, SQLITE_STATIC);
        sqlite3_bind_text(h_stmt, 4, complaint_id, -1, SQLITE_STATIC);
        sqlite3_step(h_stmt);
        sqlite3_finalize(h_stmt);
    }

    db_commit();
    return true;
}

bool complaints_add_remarks(const char *complaint_id, int admin_id,
                            const char *remarks,
                            char *err_msg, size_t err_size) {
    if (!remarks || strlen(remarks) < 2) {
        snprintf(err_msg, err_size, "Remark text cannot be empty.");
        return false;
    }

    sqlite3 *db = db_get_handle();
    if (!db) return false;

    char now[32];
    get_current_utc_timestamp(now, sizeof(now));

    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
                      "SELECT complaint_id, ?, status, status, ?, ? FROM complaints WHERE complaint_id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(err_msg, err_size, "Database error: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, admin_id);
    sqlite3_bind_text(stmt, 2, remarks, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, complaint_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(err_msg, err_size, "Failed to add remark: %s", sqlite3_errmsg(db));
        return false;
    }

    return true;
}
