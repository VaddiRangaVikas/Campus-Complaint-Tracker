#include "analytics.h"
#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool analytics_get_admin_dashboard(AdminDashboardData *data) {
    if (!data) return false;
    memset(data, 0, sizeof(AdminDashboardData));

    sqlite3 *db = db_get_handle();
    if (!db) return false;

    // 1. Status Counts
    sqlite3_stmt *stmt = NULL;
    const char *status_sql = "SELECT status, COUNT(*) FROM complaints GROUP BY status;";
    if (sqlite3_prepare_v2(db, status_sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *st = (const char *)sqlite3_column_text(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            data->total_complaints += count;

            if (st) {
                if (strcmp(st, "pending") == 0) data->pending_complaints = count;
                else if (strcmp(st, "in_progress") == 0) data->in_progress_complaints = count;
                else if (strcmp(st, "resolved") == 0) data->resolved_complaints = count;
                else if (strcmp(st, "rejected") == 0) data->rejected_complaints = count;
            }
        }
        sqlite3_finalize(stmt);
    }

    // 2. Priority Counts
    const char *prio_sql = "SELECT priority, COUNT(*) FROM complaints GROUP BY priority;";
    if (sqlite3_prepare_v2(db, prio_sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int p = sqlite3_column_int(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            if (p >= 1 && p <= 4) {
                data->priority_counts[p] = count;
                if (p == 4) data->critical_complaints = count;
                if (p == 3) data->high_priority_complaints = count;
            }
        }
        sqlite3_finalize(stmt);
    }

    // 3. Average Resolution Time (Hours)
    const char *avg_sql = "SELECT AVG((strftime('%s', resolved_at) - strftime('%s', created_at)) / 3600.0) "
                          "FROM complaints WHERE resolved_at IS NOT NULL AND resolved_at != '';";
    if (sqlite3_prepare_v2(db, avg_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            data->avg_resolution_hours = sqlite3_column_double(stmt, 0);
            if (data->avg_resolution_hours < 0) data->avg_resolution_hours = 0.0;
        }
        sqlite3_finalize(stmt);
    }

    // 4. Category breakdown
    const char *cat_sql = "SELECT category, COUNT(*) FROM complaints GROUP BY category ORDER BY COUNT(*) DESC LIMIT 16;";
    if (sqlite3_prepare_v2(db, cat_sql, -1, &stmt, NULL) == SQLITE_OK) {
        int idx = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && idx < 16) {
            const char *c = (const char *)sqlite3_column_text(stmt, 0);
            int cnt = sqlite3_column_int(stmt, 1);
            if (c) strncpy(data->category_stats[idx].category, c, sizeof(data->category_stats[idx].category) - 1);
            data->category_stats[idx].count = cnt;
            idx++;
        }
        data->category_count = idx;
        sqlite3_finalize(stmt);
    }

    // 5. Recent 7-Day Trend
    const char *trend_sql = "SELECT substr(created_at, 1, 10) as day, COUNT(*) "
                            "FROM complaints GROUP BY day ORDER BY day DESC LIMIT 7;";
    if (sqlite3_prepare_v2(db, trend_sql, -1, &stmt, NULL) == SQLITE_OK) {
        int idx = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && idx < 7) {
            const char *d = (const char *)sqlite3_column_text(stmt, 0);
            int cnt = sqlite3_column_int(stmt, 1);
            if (d) strncpy(data->recent_trends[idx].date, d, sizeof(data->recent_trends[idx].date) - 1);
            data->recent_trends[idx].count = cnt;
            idx++;
        }
        data->trend_count = idx;
        sqlite3_finalize(stmt);
    }

    // 6. Recent 5 complaints
    const char *rec_sql = "SELECT c.complaint_id, c.user_id, u.full_name, u.email, c.title, c.category, c.description, "
                          "c.location, c.room_number, c.priority, c.status, c.assigned_department, c.created_at, c.updated_at, c.resolved_at "
                          "FROM complaints c JOIN users u ON c.user_id = u.user_id ORDER BY c.created_at DESC LIMIT 5;";
    if (sqlite3_prepare_v2(db, rec_sql, -1, &stmt, NULL) == SQLITE_OK) {
        int idx = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && idx < 5) {
            Complaint *c = &data->recent_complaints[idx];
            strncpy(c->complaint_id, (const char *)sqlite3_column_text(stmt, 0), sizeof(c->complaint_id) - 1);
            c->user_id = sqlite3_column_int(stmt, 1);
            const char *name = (const char *)sqlite3_column_text(stmt, 2);
            if (name) strncpy(c->student_name, name, sizeof(c->student_name) - 1);
            const char *title = (const char *)sqlite3_column_text(stmt, 4);
            if (title) strncpy(c->title, title, sizeof(c->title) - 1);
            const char *cat = (const char *)sqlite3_column_text(stmt, 5);
            if (cat) strncpy(c->category, cat, sizeof(c->category) - 1);
            c->priority = sqlite3_column_int(stmt, 9);
            const char *st = (const char *)sqlite3_column_text(stmt, 10);
            if (st) strncpy(c->status, st, sizeof(c->status) - 1);
            const char *c_at = (const char *)sqlite3_column_text(stmt, 12);
            if (c_at) strncpy(c->created_at, c_at, sizeof(c->created_at) - 1);
            idx++;
        }
        data->recent_count = idx;
        sqlite3_finalize(stmt);
    }

    return true;
}

bool analytics_get_student_dashboard(int user_id, StudentDashboardData *data) {
    if (!data) return false;
    memset(data, 0, sizeof(StudentDashboardData));

    sqlite3 *db = db_get_handle();
    if (!db) return false;

    sqlite3_stmt *stmt = NULL;
    const char *status_sql = "SELECT status, COUNT(*) FROM complaints WHERE user_id = ? GROUP BY status;";
    if (sqlite3_prepare_v2(db, status_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *st = (const char *)sqlite3_column_text(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            data->total_complaints += count;

            if (st) {
                if (strcmp(st, "pending") == 0) data->pending_complaints = count;
                else if (strcmp(st, "in_progress") == 0) data->in_progress_complaints = count;
                else if (strcmp(st, "resolved") == 0) data->resolved_complaints = count;
            }
        }
        sqlite3_finalize(stmt);
    }

    // Recent 5 complaints for this student
    const char *rec_sql = "SELECT complaint_id, user_id, '', '', title, category, description, "
                          "location, room_number, priority, status, assigned_department, created_at, updated_at, resolved_at "
                          "FROM complaints WHERE user_id = ? ORDER BY created_at DESC LIMIT 5;";
    if (sqlite3_prepare_v2(db, rec_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        int idx = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && idx < 5) {
            Complaint *c = &data->recent_complaints[idx];
            strncpy(c->complaint_id, (const char *)sqlite3_column_text(stmt, 0), sizeof(c->complaint_id) - 1);
            c->user_id = user_id;
            const char *title = (const char *)sqlite3_column_text(stmt, 4);
            if (title) strncpy(c->title, title, sizeof(c->title) - 1);
            const char *cat = (const char *)sqlite3_column_text(stmt, 5);
            if (cat) strncpy(c->category, cat, sizeof(c->category) - 1);
            c->priority = sqlite3_column_int(stmt, 9);
            const char *st = (const char *)sqlite3_column_text(stmt, 10);
            if (st) strncpy(c->status, st, sizeof(c->status) - 1);
            const char *c_at = (const char *)sqlite3_column_text(stmt, 12);
            if (c_at) strncpy(c->created_at, c_at, sizeof(c->created_at) - 1);
            idx++;
        }
        data->recent_count = idx;
        sqlite3_finalize(stmt);
    }

    return true;
}

// Utility to escape string for CSV
static void csv_escape_and_append(char **buf, size_t *buf_size, size_t *len, const char *str) {
    if (!str) str = "";
    // Double quote escaping
    char escaped[1024];
    size_t e_idx = 0;
    escaped[e_idx++] = '"';
    for (size_t i = 0; str[i] != '\0' && e_idx < sizeof(escaped) - 3; i++) {
        if (str[i] == '"') {
            escaped[e_idx++] = '"';
            escaped[e_idx++] = '"';
        } else if (str[i] == '\r' || str[i] == '\n') {
            escaped[e_idx++] = ' ';
        } else {
            escaped[e_idx++] = str[i];
        }
    }
    escaped[e_idx++] = '"';
    escaped[e_idx] = '\0';

    size_t add_len = strlen(escaped);
    while (*len + add_len + 2 >= *buf_size) {
        *buf_size *= 2;
        *buf = (char *)realloc(*buf, *buf_size);
    }
    memcpy(*buf + *len, escaped, add_len);
    *len += add_len;
    (*buf)[*len] = '\0';
}

char* analytics_export_complaints_csv(void) {
    sqlite3 *db = db_get_handle();
    if (!db) return NULL;

    size_t buf_size = 65536;
    size_t len = 0;
    char *csv = (char *)malloc(buf_size);
    if (!csv) return NULL;

    const char *header = "Complaint ID,Title,Category,Location,Room,Priority,Status,Created At,Resolved At,Student Name,Student Email\r\n";
    strcpy(csv, header);
    len = strlen(header);

    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT c.complaint_id, c.title, c.category, c.location, IFNULL(c.room_number, ''), "
                      "c.priority, c.status, c.created_at, IFNULL(c.resolved_at, ''), u.full_name, u.email "
                      "FROM complaints c JOIN users u ON c.user_id = u.user_id "
                      "ORDER BY c.created_at DESC;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            for (int col = 0; col < 11; col++) {
                if (col == 5) {
                    // Priority name
                    int p = sqlite3_column_int(stmt, col);
                    const char *pnames[] = {"", "Low", "Medium", "High", "Critical"};
                    const char *pname = (p >= 1 && p <= 4) ? pnames[p] : "Unknown";
                    csv_escape_and_append(&csv, &buf_size, &len, pname);
                } else {
                    const char *val = (const char *)sqlite3_column_text(stmt, col);
                    csv_escape_and_append(&csv, &buf_size, &len, val ? val : "");
                }

                if (col < 10) {
                    csv[len++] = ',';
                    csv[len] = '\0';
                }
            }
            csv[len++] = '\r';
            csv[len++] = '\n';
            csv[len] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    return csv;
}
