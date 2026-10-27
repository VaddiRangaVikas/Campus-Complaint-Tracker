#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "database.h"
#include "auth.h"

static sqlite3 *g_db = NULL;

void get_current_utc_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
}

sqlite3* db_get_handle(void)
{
    return g_db;
}

static const char *s_embedded_schema =
"PRAGMA foreign_keys = ON;"
"CREATE TABLE IF NOT EXISTS users ("
"    user_id INTEGER PRIMARY KEY AUTOINCREMENT,"
"    full_name TEXT NOT NULL,"
"    email TEXT NOT NULL UNIQUE COLLATE NOCASE,"
"    student_id TEXT UNIQUE,"
"    department TEXT NOT NULL,"
"    password_hash TEXT NOT NULL,"
"    salt TEXT NOT NULL,"
"    role TEXT NOT NULL CHECK(role IN ('student', 'admin')),"
"    account_status TEXT NOT NULL DEFAULT 'active' CHECK(account_status IN ('active', 'inactive', 'suspended')),"
"    created_at TEXT NOT NULL,"
"    updated_at TEXT NOT NULL"
");"
"CREATE TABLE IF NOT EXISTS complaints ("
"    complaint_id TEXT PRIMARY KEY,"
"    user_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,"
"    title TEXT NOT NULL,"
"    category TEXT NOT NULL,"
"    description TEXT NOT NULL,"
"    location TEXT NOT NULL,"
"    room_number TEXT,"
"    priority INTEGER NOT NULL CHECK(priority BETWEEN 1 AND 4),"
"    status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending', 'in_progress', 'resolved', 'rejected')),"
"    assigned_department TEXT,"
"    created_at TEXT NOT NULL,"
"    updated_at TEXT NOT NULL,"
"    resolved_at TEXT"
");"
"CREATE TABLE IF NOT EXISTS complaint_history ("
"    history_id INTEGER PRIMARY KEY AUTOINCREMENT,"
"    complaint_id TEXT NOT NULL REFERENCES complaints(complaint_id) ON DELETE CASCADE,"
"    changed_by INTEGER NOT NULL REFERENCES users(user_id),"
"    old_status TEXT,"
"    new_status TEXT NOT NULL,"
"    remarks TEXT,"
"    created_at TEXT NOT NULL"
");"
"CREATE TABLE IF NOT EXISTS sessions ("
"    token TEXT PRIMARY KEY,"
"    user_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,"
"    expires_at INTEGER NOT NULL,"
"    created_at INTEGER NOT NULL"
");"
"CREATE TABLE IF NOT EXISTS login_attempts ("
"    ip_address TEXT NOT NULL,"
"    attempt_time INTEGER NOT NULL"
");"
"CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);"
"CREATE INDEX IF NOT EXISTS idx_users_role ON users(role);"
"CREATE INDEX IF NOT EXISTS idx_complaints_user_id ON complaints(user_id);"
"CREATE INDEX IF NOT EXISTS idx_complaints_status ON complaints(status);"
"CREATE INDEX IF NOT EXISTS idx_complaints_priority ON complaints(priority);"
"CREATE INDEX IF NOT EXISTS idx_complaints_category ON complaints(category);"
"CREATE INDEX IF NOT EXISTS idx_complaints_created_at ON complaints(created_at);"
"CREATE INDEX IF NOT EXISTS idx_history_complaint_id ON complaint_history(complaint_id);"
"CREATE INDEX IF NOT EXISTS idx_sessions_token ON sessions(token);";

bool db_init(const char *db_path)
{
    if (g_db) return true;

    int rc = sqlite3_open(db_path, &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB Error] Cannot open database %s: %s\n", db_path, sqlite3_errmsg(g_db));
        return false;
    }

    sqlite3_exec(g_db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);

    char *errmsg = NULL;
    rc = sqlite3_exec(g_db, s_embedded_schema, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB Error] Schema initialization failed: %s\n", errmsg ? errmsg : "unknown");
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }

    // Ensure default administrator account exists
    sqlite3_stmt *stmt;
    const char *check_admin = "SELECT COUNT(*) FROM users WHERE role = 'admin';";
    if (sqlite3_prepare_v2(g_db, check_admin, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count == 0) {
                User admin;
                memset(&admin, 0, sizeof(User));
                strncpy(admin.full_name, "Campus Administrator", sizeof(admin.full_name) - 1);
                strncpy(admin.email, "admin@campus.edu", sizeof(admin.email) - 1);
                strncpy(admin.department, "Central Administration", sizeof(admin.department) - 1);
                strncpy(admin.role, "admin", sizeof(admin.role) - 1);
                strncpy(admin.account_status, "active", sizeof(admin.account_status) - 1);

                char salt[65];
                char hash[65];
                auth_generate_salt(salt, sizeof(salt));
                auth_hash_password("Admin@123", salt, hash, sizeof(hash));

                int admin_id = 0;
                db_create_user(&admin, hash, salt, &admin_id);
                printf("[DB] Initial Administrator created: admin@campus.edu (ID: %d)\n", admin_id);
            }
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

void db_close(void)
{
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

// ========================================================
// User Queries
// ========================================================

bool db_create_user(const User *user, const char *password_hash, const char *salt, int *out_user_id)
{
    if (!g_db || !user || !password_hash || !salt) return false;

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));

    const char *sql = 
        "INSERT INTO users (full_name, email, student_id, department, password_hash, salt, role, account_status, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, user->full_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user->email, -1, SQLITE_STATIC);
    if (strlen(user->student_id) > 0) {
        sqlite3_bind_text(stmt, 3, user->student_id, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, user->department, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, password_hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, salt, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, user->role, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, strlen(user->account_status) > 0 ? user->account_status : "active", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, now_str, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, now_str, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        if (out_user_id) *out_user_id = (int)sqlite3_last_insert_rowid(g_db);
        return true;
    }
    return false;
}

bool db_get_user_by_email(const char *email, User *out_user, char *out_hash, char *out_salt)
{
    if (!g_db || !email) return false;

    const char *sql = 
        "SELECT user_id, full_name, email, COALESCE(student_id, ''), department, password_hash, salt, role, account_status, created_at, updated_at "
        "FROM users WHERE email = ? COLLATE NOCASE;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, email, -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        if (out_user) {
            memset(out_user, 0, sizeof(User));
            out_user->user_id = sqlite3_column_int(stmt, 0);
            strncpy(out_user->full_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(out_user->full_name) - 1);
            strncpy(out_user->email, (const char *)sqlite3_column_text(stmt, 2), sizeof(out_user->email) - 1);
            strncpy(out_user->student_id, (const char *)sqlite3_column_text(stmt, 3), sizeof(out_user->student_id) - 1);
            strncpy(out_user->department, (const char *)sqlite3_column_text(stmt, 4), sizeof(out_user->department) - 1);
            strncpy(out_user->role, (const char *)sqlite3_column_text(stmt, 7), sizeof(out_user->role) - 1);
            strncpy(out_user->account_status, (const char *)sqlite3_column_text(stmt, 8), sizeof(out_user->account_status) - 1);
            strncpy(out_user->created_at, (const char *)sqlite3_column_text(stmt, 9), sizeof(out_user->created_at) - 1);
            strncpy(out_user->updated_at, (const char *)sqlite3_column_text(stmt, 10), sizeof(out_user->updated_at) - 1);
        }
        if (out_hash) strncpy(out_hash, (const char *)sqlite3_column_text(stmt, 5), 64);
        if (out_salt) strncpy(out_salt, (const char *)sqlite3_column_text(stmt, 6), 64);
    }

    sqlite3_finalize(stmt);
    return found;
}

bool db_get_user_by_id(int user_id, User *out_user)
{
    if (!g_db || !out_user) return false;

    const char *sql = 
        "SELECT user_id, full_name, email, COALESCE(student_id, ''), department, role, account_status, created_at, updated_at "
        "FROM users WHERE user_id = ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, user_id);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        memset(out_user, 0, sizeof(User));
        out_user->user_id = sqlite3_column_int(stmt, 0);
        strncpy(out_user->full_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(out_user->full_name) - 1);
        strncpy(out_user->email, (const char *)sqlite3_column_text(stmt, 2), sizeof(out_user->email) - 1);
        strncpy(out_user->student_id, (const char *)sqlite3_column_text(stmt, 3), sizeof(out_user->student_id) - 1);
        strncpy(out_user->department, (const char *)sqlite3_column_text(stmt, 4), sizeof(out_user->department) - 1);
        strncpy(out_user->role, (const char *)sqlite3_column_text(stmt, 5), sizeof(out_user->role) - 1);
        strncpy(out_user->account_status, (const char *)sqlite3_column_text(stmt, 6), sizeof(out_user->account_status) - 1);
        strncpy(out_user->created_at, (const char *)sqlite3_column_text(stmt, 7), sizeof(out_user->created_at) - 1);
        strncpy(out_user->updated_at, (const char *)sqlite3_column_text(stmt, 8), sizeof(out_user->updated_at) - 1);
    }

    sqlite3_finalize(stmt);
    return found;
}

bool db_update_user_password(int user_id, const char *new_hash, const char *new_salt)
{
    if (!g_db || !new_hash || !new_salt) return false;

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));

    const char *sql = "UPDATE users SET password_hash = ?, salt = ?, updated_at = ? WHERE user_id = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, new_hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, new_salt, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now_str, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, user_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool db_update_user_profile(int user_id, const char *full_name, const char *department)
{
    if (!g_db) return false;

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));

    const char *sql = "UPDATE users SET full_name = ?, department = ?, updated_at = ? WHERE user_id = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, full_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, department, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now_str, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, user_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool db_get_all_users(User **out_users, int *out_count)
{
    if (!g_db || !out_users || !out_count) return false;

    const char *count_sql = "SELECT COUNT(*) FROM users;";
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(g_db, count_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (count == 0) {
        *out_users = NULL;
        *out_count = 0;
        return true;
    }

    User *users = (User *)malloc(count * sizeof(User));
    if (!users) return false;
    memset(users, 0, count * sizeof(User));

    const char *sql = 
        "SELECT user_id, full_name, email, COALESCE(student_id, ''), department, role, account_status, created_at, updated_at "
        "FROM users ORDER BY user_id ASC;";

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        free(users);
        return false;
    }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < count) {
        users[idx].user_id = sqlite3_column_int(stmt, 0);
        strncpy(users[idx].full_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(users[idx].full_name) - 1);
        strncpy(users[idx].email, (const char *)sqlite3_column_text(stmt, 2), sizeof(users[idx].email) - 1);
        strncpy(users[idx].student_id, (const char *)sqlite3_column_text(stmt, 3), sizeof(users[idx].student_id) - 1);
        strncpy(users[idx].department, (const char *)sqlite3_column_text(stmt, 4), sizeof(users[idx].department) - 1);
        strncpy(users[idx].role, (const char *)sqlite3_column_text(stmt, 5), sizeof(users[idx].role) - 1);
        strncpy(users[idx].account_status, (const char *)sqlite3_column_text(stmt, 6), sizeof(users[idx].account_status) - 1);
        strncpy(users[idx].created_at, (const char *)sqlite3_column_text(stmt, 7), sizeof(users[idx].created_at) - 1);
        strncpy(users[idx].updated_at, (const char *)sqlite3_column_text(stmt, 8), sizeof(users[idx].updated_at) - 1);
        idx++;
    }

    sqlite3_finalize(stmt);
    *out_users = users;
    *out_count = idx;
    return true;
}

// ========================================================
// Session Management
// ========================================================

bool db_create_session(const char *token, int user_id, int duration_sec)
{
    if (!g_db || !token) return false;

    time_t now = time(NULL);
    time_t expires = now + duration_sec;

    const char *sql = "INSERT OR REPLACE INTO sessions (token, user_id, expires_at, created_at) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)expires);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool db_get_session(const char *token, User *out_user)
{
    if (!g_db || !token) return false;

    time_t now = time(NULL);
    const char *sql = 
        "SELECT u.user_id, u.full_name, u.email, COALESCE(u.student_id, ''), u.department, u.role, u.account_status, u.created_at, u.updated_at "
        "FROM sessions s "
        "JOIN users u ON s.user_id = u.user_id "
        "WHERE s.token = ? AND s.expires_at > ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        if (out_user) {
            memset(out_user, 0, sizeof(User));
            out_user->user_id = sqlite3_column_int(stmt, 0);
            strncpy(out_user->full_name, (const char *)sqlite3_column_text(stmt, 1), sizeof(out_user->full_name) - 1);
            strncpy(out_user->email, (const char *)sqlite3_column_text(stmt, 2), sizeof(out_user->email) - 1);
            strncpy(out_user->student_id, (const char *)sqlite3_column_text(stmt, 3), sizeof(out_user->student_id) - 1);
            strncpy(out_user->department, (const char *)sqlite3_column_text(stmt, 4), sizeof(out_user->department) - 1);
            strncpy(out_user->role, (const char *)sqlite3_column_text(stmt, 5), sizeof(out_user->role) - 1);
            strncpy(out_user->account_status, (const char *)sqlite3_column_text(stmt, 6), sizeof(out_user->account_status) - 1);
            strncpy(out_user->created_at, (const char *)sqlite3_column_text(stmt, 7), sizeof(out_user->created_at) - 1);
            strncpy(out_user->updated_at, (const char *)sqlite3_column_text(stmt, 8), sizeof(out_user->updated_at) - 1);
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

bool db_delete_session(const char *token)
{
    if (!g_db || !token) return false;

    const char *sql = "DELETE FROM sessions WHERE token = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, token, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

// ========================================================
// Complaint Management
// ========================================================

bool db_generate_complaint_id(char *out_id, size_t max_len)
{
    if (!g_db || !out_id || max_len < 16) return false;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int current_year = tm_info ? (tm_info->tm_year + 1900) : 2026;

    const char *sql = "SELECT COUNT(*) FROM complaints;";
    sqlite3_stmt *stmt;
    int next_seq = 1;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            next_seq = sqlite3_column_int(stmt, 0) + 1;
        }
        sqlite3_finalize(stmt);
    }

    snprintf(out_id, max_len, "CMP-%d-%04d", current_year, next_seq);
    return true;
}

bool db_create_complaint(Complaint *complaint)
{
    if (!g_db || !complaint) return false;

    if (strlen(complaint->complaint_id) == 0) {
        db_generate_complaint_id(complaint->complaint_id, sizeof(complaint->complaint_id));
    }

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));
    strncpy(complaint->created_at, now_str, sizeof(complaint->created_at) - 1);
    strncpy(complaint->updated_at, now_str, sizeof(complaint->updated_at) - 1);

    if (strlen(complaint->status) == 0) {
        strncpy(complaint->status, "pending", sizeof(complaint->status) - 1);
    }

    sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char *sql = 
        "INSERT INTO complaints (complaint_id, user_id, title, category, description, location, room_number, priority, status, assigned_department, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_text(stmt, 1, complaint->complaint_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, complaint->user_id);
    sqlite3_bind_text(stmt, 3, complaint->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, complaint->category, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, complaint->description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, complaint->location, -1, SQLITE_STATIC);
    if (strlen(complaint->room_number) > 0) {
        sqlite3_bind_text(stmt, 7, complaint->room_number, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 7);
    }
    sqlite3_bind_int(stmt, 8, complaint->priority);
    sqlite3_bind_text(stmt, 9, complaint->status, -1, SQLITE_STATIC);
    if (strlen(complaint->assigned_department) > 0) {
        sqlite3_bind_text(stmt, 10, complaint->assigned_department, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 10);
    }
    sqlite3_bind_text(stmt, 11, complaint->created_at, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, complaint->updated_at, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Insert initial status history record
    const char *hist_sql = 
        "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
        "VALUES (?, ?, NULL, ?, ?, ?);";

    if (sqlite3_prepare_v2(g_db, hist_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, complaint->complaint_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, complaint->user_id);
        sqlite3_bind_text(stmt, 3, complaint->status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, "Complaint submitted by student", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, now_str, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
    return true;
}

bool db_get_complaint(const char *complaint_id, Complaint *out_complaint)
{
    if (!g_db || !complaint_id || !out_complaint) return false;

    const char *sql = 
        "SELECT c.complaint_id, c.user_id, u.full_name, u.email, c.title, c.category, c.description, "
        "       c.location, COALESCE(c.room_number, ''), c.priority, c.status, COALESCE(c.assigned_department, ''), "
        "       c.created_at, c.updated_at, COALESCE(c.resolved_at, '') "
        "FROM complaints c "
        "JOIN users u ON c.user_id = u.user_id "
        "WHERE c.complaint_id = ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, complaint_id, -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        memset(out_complaint, 0, sizeof(Complaint));
        strncpy(out_complaint->complaint_id, (const char *)sqlite3_column_text(stmt, 0), sizeof(out_complaint->complaint_id) - 1);
        out_complaint->user_id = sqlite3_column_int(stmt, 1);
        strncpy(out_complaint->student_name, (const char *)sqlite3_column_text(stmt, 2), sizeof(out_complaint->student_name) - 1);
        strncpy(out_complaint->student_email, (const char *)sqlite3_column_text(stmt, 3), sizeof(out_complaint->student_email) - 1);
        strncpy(out_complaint->title, (const char *)sqlite3_column_text(stmt, 4), sizeof(out_complaint->title) - 1);
        strncpy(out_complaint->category, (const char *)sqlite3_column_text(stmt, 5), sizeof(out_complaint->category) - 1);
        strncpy(out_complaint->description, (const char *)sqlite3_column_text(stmt, 6), sizeof(out_complaint->description) - 1);
        strncpy(out_complaint->location, (const char *)sqlite3_column_text(stmt, 7), sizeof(out_complaint->location) - 1);
        strncpy(out_complaint->room_number, (const char *)sqlite3_column_text(stmt, 8), sizeof(out_complaint->room_number) - 1);
        out_complaint->priority = sqlite3_column_int(stmt, 9);
        strncpy(out_complaint->status, (const char *)sqlite3_column_text(stmt, 10), sizeof(out_complaint->status) - 1);
        strncpy(out_complaint->assigned_department, (const char *)sqlite3_column_text(stmt, 11), sizeof(out_complaint->assigned_department) - 1);
        strncpy(out_complaint->created_at, (const char *)sqlite3_column_text(stmt, 12), sizeof(out_complaint->created_at) - 1);
        strncpy(out_complaint->updated_at, (const char *)sqlite3_column_text(stmt, 13), sizeof(out_complaint->updated_at) - 1);
        strncpy(out_complaint->resolved_at, (const char *)sqlite3_column_text(stmt, 14), sizeof(out_complaint->resolved_at) - 1);
    }

    sqlite3_finalize(stmt);
    return found;
}

static bool parse_complaints_result(sqlite3_stmt *stmt, Complaint **out_complaints, int *out_count)
{
    int capacity = 64;
    int count = 0;
    Complaint *arr = (Complaint *)malloc(capacity * sizeof(Complaint));
    if (!arr) return false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            Complaint *new_arr = (Complaint *)realloc(arr, capacity * sizeof(Complaint));
            if (!new_arr) {
                free(arr);
                return false;
            }
            arr = new_arr;
        }

        memset(&arr[count], 0, sizeof(Complaint));
        strncpy(arr[count].complaint_id, (const char *)sqlite3_column_text(stmt, 0), sizeof(arr[count].complaint_id) - 1);
        arr[count].user_id = sqlite3_column_int(stmt, 1);
        strncpy(arr[count].student_name, (const char *)sqlite3_column_text(stmt, 2), sizeof(arr[count].student_name) - 1);
        strncpy(arr[count].student_email, (const char *)sqlite3_column_text(stmt, 3), sizeof(arr[count].student_email) - 1);
        strncpy(arr[count].title, (const char *)sqlite3_column_text(stmt, 4), sizeof(arr[count].title) - 1);
        strncpy(arr[count].category, (const char *)sqlite3_column_text(stmt, 5), sizeof(arr[count].category) - 1);
        strncpy(arr[count].description, (const char *)sqlite3_column_text(stmt, 6), sizeof(arr[count].description) - 1);
        strncpy(arr[count].location, (const char *)sqlite3_column_text(stmt, 7), sizeof(arr[count].location) - 1);
        strncpy(arr[count].room_number, (const char *)sqlite3_column_text(stmt, 8), sizeof(arr[count].room_number) - 1);
        arr[count].priority = sqlite3_column_int(stmt, 9);
        strncpy(arr[count].status, (const char *)sqlite3_column_text(stmt, 10), sizeof(arr[count].status) - 1);
        strncpy(arr[count].assigned_department, (const char *)sqlite3_column_text(stmt, 11), sizeof(arr[count].assigned_department) - 1);
        strncpy(arr[count].created_at, (const char *)sqlite3_column_text(stmt, 12), sizeof(arr[count].created_at) - 1);
        strncpy(arr[count].updated_at, (const char *)sqlite3_column_text(stmt, 13), sizeof(arr[count].updated_at) - 1);
        strncpy(arr[count].resolved_at, (const char *)sqlite3_column_text(stmt, 14), sizeof(arr[count].resolved_at) - 1);

        count++;
    }

    *out_complaints = arr;
    *out_count = count;
    return true;
}

bool db_get_complaints_by_user(int user_id, Complaint **out_complaints, int *out_count)
{
    if (!g_db || !out_complaints || !out_count) return false;

    const char *sql = 
        "SELECT c.complaint_id, c.user_id, u.full_name, u.email, c.title, c.category, c.description, "
        "       c.location, COALESCE(c.room_number, ''), c.priority, c.status, COALESCE(c.assigned_department, ''), "
        "       c.created_at, c.updated_at, COALESCE(c.resolved_at, '') "
        "FROM complaints c "
        "JOIN users u ON c.user_id = u.user_id "
        "WHERE c.user_id = ? "
        "ORDER BY c.created_at DESC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, user_id);
    bool ok = parse_complaints_result(stmt, out_complaints, out_count);
    sqlite3_finalize(stmt);
    return ok;
}

bool db_get_all_complaints(Complaint **out_complaints, int *out_count)
{
    if (!g_db || !out_complaints || !out_count) return false;

    const char *sql = 
        "SELECT c.complaint_id, c.user_id, u.full_name, u.email, c.title, c.category, c.description, "
        "       c.location, COALESCE(c.room_number, ''), c.priority, c.status, COALESCE(c.assigned_department, ''), "
        "       c.created_at, c.updated_at, COALESCE(c.resolved_at, '') "
        "FROM complaints c "
        "JOIN users u ON c.user_id = u.user_id "
        "ORDER BY c.created_at DESC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    bool ok = parse_complaints_result(stmt, out_complaints, out_count);
    sqlite3_finalize(stmt);
    return ok;
}

bool db_update_complaint_status(const char *complaint_id, const char *new_status, int changed_by, const char *remarks)
{
    if (!g_db || !complaint_id || !new_status) return false;

    Complaint existing;
    if (!db_get_complaint(complaint_id, &existing)) return false;

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));

    bool is_resolved = (strcmp(new_status, "resolved") == 0);

    sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char *update_sql = 
        "UPDATE complaints SET status = ?, updated_at = ?, resolved_at = ? WHERE complaint_id = ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, update_sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_text(stmt, 1, new_status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, now_str, -1, SQLITE_STATIC);
    if (is_resolved) {
        sqlite3_bind_text(stmt, 3, now_str, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, complaint_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    // Insert into history
    const char *hist_sql = 
        "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(g_db, hist_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, complaint_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, changed_by);
        sqlite3_bind_text(stmt, 3, existing.status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, new_status, -1, SQLITE_STATIC);
        if (remarks && strlen(remarks) > 0) {
            sqlite3_bind_text(stmt, 5, remarks, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 5);
        }
        sqlite3_bind_text(stmt, 6, now_str, -1, SQLITE_STATIC);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
    return true;
}

bool db_update_complaint_priority(const char *complaint_id, int new_priority, int changed_by, const char *remarks)
{
    if (!g_db || !complaint_id || new_priority < 1 || new_priority > 4) return false;

    Complaint existing;
    if (!db_get_complaint(complaint_id, &existing)) return false;

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));

    sqlite3_exec(g_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char *update_sql = "UPDATE complaints SET priority = ?, updated_at = ? WHERE complaint_id = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, update_sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    sqlite3_bind_int(stmt, 1, new_priority);
    sqlite3_bind_text(stmt, 2, now_str, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, complaint_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return false;
    }

    char priority_note[256];
    const char *pri_names[] = {"", "Low", "Medium", "High", "Critical"};
    snprintf(priority_note, sizeof(priority_note), "Priority changed from %s (%d) to %s (%d)%s%s",
             pri_names[existing.priority], existing.priority,
             pri_names[new_priority], new_priority,
             (remarks && strlen(remarks) > 0) ? ": " : "",
             (remarks && strlen(remarks) > 0) ? remarks : "");

    const char *hist_sql = 
        "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(g_db, hist_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, complaint_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, changed_by);
        sqlite3_bind_text(stmt, 3, existing.status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, existing.status, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, priority_note, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, now_str, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
    return true;
}

bool db_add_complaint_remarks(const char *complaint_id, int changed_by, const char *remarks)
{
    if (!g_db || !complaint_id || !remarks) return false;

    Complaint existing;
    if (!db_get_complaint(complaint_id, &existing)) return false;

    char now_str[32];
    get_current_utc_timestamp(now_str, sizeof(now_str));

    const char *hist_sql = 
        "INSERT INTO complaint_history (complaint_id, changed_by, old_status, new_status, remarks, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, hist_sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, complaint_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, changed_by);
    sqlite3_bind_text(stmt, 3, existing.status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, existing.status, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, remarks, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, now_str, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool db_get_complaint_history(const char *complaint_id, ComplaintHistory **out_history, int *out_count)
{
    if (!g_db || !complaint_id || !out_history || !out_count) return false;

    const char *sql = 
        "SELECT h.history_id, h.complaint_id, h.changed_by, u.full_name, u.role, "
        "       COALESCE(h.old_status, ''), h.new_status, COALESCE(h.remarks, ''), h.created_at "
        "FROM complaint_history h "
        "JOIN users u ON h.changed_by = u.user_id "
        "WHERE h.complaint_id = ? "
        "ORDER BY h.history_id ASC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, complaint_id, -1, SQLITE_STATIC);

    int capacity = 16;
    int count = 0;
    ComplaintHistory *arr = (ComplaintHistory *)malloc(capacity * sizeof(ComplaintHistory));
    if (!arr) {
        sqlite3_finalize(stmt);
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            ComplaintHistory *new_arr = (ComplaintHistory *)realloc(arr, capacity * sizeof(ComplaintHistory));
            if (!new_arr) {
                free(arr);
                sqlite3_finalize(stmt);
                return false;
            }
            arr = new_arr;
        }

        memset(&arr[count], 0, sizeof(ComplaintHistory));
        arr[count].history_id = sqlite3_column_int(stmt, 0);
        strncpy(arr[count].complaint_id, (const char *)sqlite3_column_text(stmt, 1), sizeof(arr[count].complaint_id) - 1);
        arr[count].changed_by = sqlite3_column_int(stmt, 2);
        strncpy(arr[count].changed_by_name, (const char *)sqlite3_column_text(stmt, 3), sizeof(arr[count].changed_by_name) - 1);
        strncpy(arr[count].changed_by_role, (const char *)sqlite3_column_text(stmt, 4), sizeof(arr[count].changed_by_role) - 1);
        strncpy(arr[count].old_status, (const char *)sqlite3_column_text(stmt, 5), sizeof(arr[count].old_status) - 1);
        strncpy(arr[count].new_status, (const char *)sqlite3_column_text(stmt, 6), sizeof(arr[count].new_status) - 1);
        strncpy(arr[count].remarks, (const char *)sqlite3_column_text(stmt, 7), sizeof(arr[count].remarks) - 1);
        strncpy(arr[count].created_at, (const char *)sqlite3_column_text(stmt, 8), sizeof(arr[count].created_at) - 1);
        count++;
    }

    sqlite3_finalize(stmt);
    *out_history = arr;
    *out_count = count;
    return true;
}

// ========================================================
// Real SQL Dashboard & Analytics Calculations
// ========================================================

bool db_get_dashboard_stats(int user_id, const char *role, cJSON **out_stats)
{
    if (!g_db || !out_stats) return false;

    cJSON *root = cJSON_CreateObject();
    bool is_student = (role && strcmp(role, "student") == 0);

    sqlite3_stmt *stmt;
    char sql[512];

    // Total complaints
    if (is_student) {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE user_id = %d;", user_id);
    } else {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints;");
    }
    int total = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    cJSON_AddNumberToObject(root, "total_complaints", total);

    // Pending
    if (is_student) {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE user_id = %d AND status = 'pending';", user_id);
    } else {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE status = 'pending';");
    }
    int pending = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) pending = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    cJSON_AddNumberToObject(root, "pending_complaints", pending);

    // In Progress
    if (is_student) {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE user_id = %d AND status = 'in_progress';", user_id);
    } else {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE status = 'in_progress';");
    }
    int in_progress = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) in_progress = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    cJSON_AddNumberToObject(root, "in_progress_complaints", in_progress);

    // Resolved
    if (is_student) {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE user_id = %d AND status = 'resolved';", user_id);
    } else {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE status = 'resolved';");
    }
    int resolved = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) resolved = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    cJSON_AddNumberToObject(root, "resolved_complaints", resolved);

    // Critical Priority
    if (is_student) {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE user_id = %d AND priority = 4;", user_id);
    } else {
        snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM complaints WHERE priority = 4;");
    }
    int critical = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) critical = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    cJSON_AddNumberToObject(root, "critical_complaints", critical);

    *out_stats = root;
    return true;
}

bool db_get_admin_analytics(cJSON **out_analytics)
{
    if (!g_db || !out_analytics) return false;

    cJSON *root = cJSON_CreateObject();
    sqlite3_stmt *stmt;

    // 1. Category distribution
    cJSON *cat_arr = cJSON_CreateArray();
    const char *cat_sql = "SELECT category, COUNT(*) FROM complaints GROUP BY category ORDER BY COUNT(*) DESC;";
    if (sqlite3_prepare_v2(g_db, cat_sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "category", (const char *)sqlite3_column_text(stmt, 0));
            cJSON_AddNumberToObject(item, "count", sqlite3_column_int(stmt, 1));
            cJSON_AddItemToArray(cat_arr, item);
        }
        sqlite3_finalize(stmt);
    }
    cJSON_AddItemToObject(root, "categories", cat_arr);

    // 2. Status distribution
    cJSON *status_arr = cJSON_CreateArray();
    const char *stat_sql = "SELECT status, COUNT(*) FROM complaints GROUP BY status;";
    if (sqlite3_prepare_v2(g_db, stat_sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "status", (const char *)sqlite3_column_text(stmt, 0));
            cJSON_AddNumberToObject(item, "count", sqlite3_column_int(stmt, 1));
            cJSON_AddItemToArray(status_arr, item);
        }
        sqlite3_finalize(stmt);
    }
    cJSON_AddItemToObject(root, "statuses", status_arr);

    // 3. Priority distribution
    cJSON *pri_arr = cJSON_CreateArray();
    const char *pri_sql = "SELECT priority, COUNT(*) FROM complaints GROUP BY priority ORDER BY priority DESC;";
    if (sqlite3_prepare_v2(g_db, pri_sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "priority", sqlite3_column_int(stmt, 0));
            cJSON_AddNumberToObject(item, "count", sqlite3_column_int(stmt, 1));
            cJSON_AddItemToArray(pri_arr, item);
        }
        sqlite3_finalize(stmt);
    }
    cJSON_AddItemToObject(root, "priorities", pri_arr);

    // 4. Average resolution time
    double avg_hours = 0.0;
    const char *res_sql = 
        "SELECT AVG((julianday(resolved_at) - julianday(created_at)) * 24.0) "
        "FROM complaints WHERE status = 'resolved' AND resolved_at IS NOT NULL;";
    if (sqlite3_prepare_v2(g_db, res_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            avg_hours = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    cJSON_AddNumberToObject(root, "average_resolution_hours", avg_hours);

    *out_analytics = root;
    return true;
}

bool db_export_complaints_csv(char **out_csv)
{
    if (!g_db || !out_csv) return false;

    Complaint *complaints = NULL;
    int count = 0;
    if (!db_get_all_complaints(&complaints, &count)) return false;

    // Estimate buffer size: 512 bytes per row
    size_t cap = (count + 1) * 512 + 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        if (complaints) free(complaints);
        return false;
    }

    strcpy(buf, "Complaint ID,Student Name,Student Email,Title,Category,Location,Room,Priority,Status,Assigned Department,Created At,Updated At,Resolved At\r\n");
    size_t len = strlen(buf);

    const char *pri_names[] = {"", "Low", "Medium", "High", "Critical"};

    for (int i = 0; i < count; i++) {
        char line[1024];
        int p = complaints[i].priority;
        const char *p_name = (p >= 1 && p <= 4) ? pri_names[p] : "Unknown";

        // Escape title and description commas if needed
        snprintf(line, sizeof(line), "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\r\n",
                 complaints[i].complaint_id,
                 complaints[i].student_name,
                 complaints[i].student_email,
                 complaints[i].title,
                 complaints[i].category,
                 complaints[i].location,
                 complaints[i].room_number,
                 p_name,
                 complaints[i].status,
                 complaints[i].assigned_department,
                 complaints[i].created_at,
                 complaints[i].updated_at,
                 complaints[i].resolved_at);

        size_t line_len = strlen(line);
        if (len + line_len + 64 > cap) {
            cap *= 2;
            char *new_buf = (char *)realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                free(complaints);
                return false;
            }
            buf = new_buf;
        }
        memcpy(buf + len, line, line_len);
        len += line_len;
        buf[len] = '\0';
    }

    if (complaints) free(complaints);
    *out_csv = buf;
    return true;
}

// ========================================================
// Seed Demo Data
// ========================================================

bool db_seed_demo_data(void)
{
    if (!g_db) return false;

    // 1. Create Demo Student 1
    User s1;
    memset(&s1, 0, sizeof(User));
    strncpy(s1.full_name, "Aarav Sharma", sizeof(s1.full_name) - 1);
    strncpy(s1.email, "student1@campus.edu", sizeof(s1.email) - 1);
    strncpy(s1.student_id, "CS2024-042", sizeof(s1.student_id) - 1);
    strncpy(s1.department, "Computer Science & Engineering", sizeof(s1.department) - 1);
    strncpy(s1.role, "student", sizeof(s1.role) - 1);
    strncpy(s1.account_status, "active", sizeof(s1.account_status) - 1);

    char salt1[65], hash1[65];
    auth_generate_salt(salt1, sizeof(salt1));
    auth_hash_password("Student@123", salt1, hash1, sizeof(hash1));
    int s1_id = 0;
    db_create_user(&s1, hash1, salt1, &s1_id);

    // 2. Create Demo Student 2
    User s2;
    memset(&s2, 0, sizeof(User));
    strncpy(s2.full_name, "Priya Patel", sizeof(s2.full_name) - 1);
    strncpy(s2.email, "student2@campus.edu", sizeof(s2.email) - 1);
    strncpy(s2.student_id, "EE2024-019", sizeof(s2.student_id) - 1);
    strncpy(s2.department, "Electrical & Electronics Engineering", sizeof(s2.department) - 1);
    strncpy(s2.role, "student", sizeof(s2.role) - 1);
    strncpy(s2.account_status, "active", sizeof(s2.account_status) - 1);

    char salt2[65], hash2[65];
    auth_generate_salt(salt2, sizeof(salt2));
    auth_hash_password("Student@123", salt2, hash2, sizeof(hash2));
    int s2_id = 0;
    db_create_user(&s2, hash2, salt2, &s2_id);

    // 3. Sample Complaints across categories and priorities
    struct {
        int user_id;
        const char *title;
        const char *category;
        const char *description;
        const char *location;
        const char *room;
        int priority;
        const char *status;
    } samples[] = {
        {s1_id, "Water pipe leakage flooding corridor", "Water Supply", "Urgent water pipe burst near Room 302 causing water buildup.", "Hostel Block A", "302", 4, "in_progress"},
        {s1_id, "High-voltage surge tripping floor circuit breakers", "Electricity", "Sparks observed at main distribution box on 2nd floor.", "Academic Complex", "204", 4, "pending"},
        {s2_id, "Wi-Fi access point disconnected after firmware glitch", "Internet and Wi-Fi", "Access point AP-L3 shows red LED indicator; no connectivity.", "Central Library", "3rd Floor", 3, "pending"},
        {s1_id, "Ceiling fan regulator damaged and vibrating loudly", "Hostel", "Regulator knob cracked and motor vibrating excessively.", "Hostel Block A", "118", 2, "resolved"},
        {s2_id, "Microscope halogen illumination bulb burnt out", "Laboratory", "Stage illumination lamp defective in Optical Physics Lab.", "Science Block", "Lab 2B", 3, "in_progress"},
        {s1_id, "Canteen water cooler compressor not cooling", "Canteen", "Drinking water cooler running continuously but water is warm.", "Main Canteen", "Ground", 2, "pending"},
        {s2_id, "Overhead projector HDMI port bent / no signal", "Classroom", "Classroom projector unable to detect presentation laptops.", "Engineering Block", "Room 405", 2, "resolved"},
        {s1_id, "Campus shuttle bus route 3 AC malfunction", "Transport", "Bus AC blowing warm air on afternoon commute routes.", "Transit Terminal", "Bay 3", 1, "pending"},
        {s2_id, "Corridor dustbin overflow near reading hall", "Cleanliness", "Dustbins overflowing requiring immediate custodial clearance.", "Library Annex", "Corridor", 1, "resolved"},
        {s1_id, "Fire extinguisher inspection tag expired", "Hostel", "Extinguisher pressure gauge needle in yellow refill zone.", "Hostel Block B", "Stairwell 2", 4, "pending"}
    };

    int num_samples = sizeof(samples) / sizeof(samples[0]);
    for (int i = 0; i < num_samples; i++) {
        Complaint c;
        memset(&c, 0, sizeof(Complaint));
        c.user_id = (samples[i].user_id > 0) ? samples[i].user_id : s1_id;
        strncpy(c.title, samples[i].title, sizeof(c.title) - 1);
        strncpy(c.category, samples[i].category, sizeof(c.category) - 1);
        strncpy(c.description, samples[i].description, sizeof(c.description) - 1);
        strncpy(c.location, samples[i].location, sizeof(c.location) - 1);
        strncpy(c.room_number, samples[i].room, sizeof(c.room_number) - 1);
        c.priority = samples[i].priority;
        strncpy(c.status, samples[i].status, sizeof(c.status) - 1);

        db_create_complaint(&c);
    }

    return true;
}
