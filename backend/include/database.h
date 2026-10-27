#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <sqlite3.h>
#include "common.h"
#include "cJSON.h"

// Lifecycle
bool db_init(const char *db_path);
void db_close(void);
sqlite3* db_get_handle(void);

// Users
bool db_create_user(const User *user, const char *password_hash, const char *salt, int *out_user_id);
bool db_get_user_by_email(const char *email, User *out_user, char *out_hash, char *out_salt);
bool db_get_user_by_id(int user_id, User *out_user);
bool db_update_user_password(int user_id, const char *new_hash, const char *new_salt);
bool db_update_user_profile(int user_id, const char *full_name, const char *department);
bool db_get_all_users(User **out_users, int *out_count);

// Sessions
bool db_create_session(const char *token, int user_id, int duration_sec);
bool db_get_session(const char *token, User *out_user);
bool db_delete_session(const char *token);

// Complaints
bool db_generate_complaint_id(char *out_id, size_t max_len);
bool db_create_complaint(Complaint *complaint);
bool db_get_complaint(const char *complaint_id, Complaint *out_complaint);
bool db_get_complaints_by_user(int user_id, Complaint **out_complaints, int *out_count);
bool db_get_all_complaints(Complaint **out_complaints, int *out_count);
bool db_update_complaint_status(const char *complaint_id, const char *new_status, int changed_by, const char *remarks);
bool db_update_complaint_priority(const char *complaint_id, int new_priority, int changed_by, const char *remarks);
bool db_add_complaint_remarks(const char *complaint_id, int changed_by, const char *remarks);
bool db_get_complaint_history(const char *complaint_id, ComplaintHistory **out_history, int *out_count);

// Analytics & Reports
bool db_get_dashboard_stats(int user_id, const char *role, cJSON **out_stats);
bool db_get_admin_analytics(cJSON **out_analytics);
bool db_export_complaints_csv(char **out_csv);

// Demo Seeding
bool db_seed_demo_data(void);

#endif // DATABASE_H
