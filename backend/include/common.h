#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define MAX_EMAIL_LEN 128
#define MAX_NAME_LEN 128
#define MAX_ID_LEN 32
#define MAX_TITLE_LEN 160
#define MAX_DESC_LEN 4096
#define MAX_LOCATION_LEN 128
#define MAX_ROOM_LEN 32
#define MAX_DEPT_LEN 64
#define MAX_STATUS_LEN 32
#define MAX_REMARKS_LEN 1024
#define SESSION_TOKEN_LEN 64
#define SESSION_DURATION_SEC (7 * 24 * 3600) // 7 days

typedef struct {
    int user_id;
    char full_name[MAX_NAME_LEN];
    char email[MAX_EMAIL_LEN];
    char student_id[MAX_ID_LEN];
    char department[MAX_DEPT_LEN];
    char role[16];           // "student" or "admin"
    char account_status[16]; // "active", "inactive"
    char created_at[32];
    char updated_at[32];
} User;

typedef struct {
    char complaint_id[MAX_ID_LEN];
    int user_id;
    char student_name[MAX_NAME_LEN];
    char student_email[MAX_EMAIL_LEN];
    char title[MAX_TITLE_LEN];
    char category[MAX_DEPT_LEN];
    char description[MAX_DESC_LEN];
    char location[MAX_LOCATION_LEN];
    char room_number[MAX_ROOM_LEN];
    int priority;            // 1: Low, 2: Medium, 3: High, 4: Critical
    char status[MAX_STATUS_LEN]; // "pending", "in_progress", "resolved", "rejected"
    char assigned_department[MAX_DEPT_LEN];
    char created_at[32];
    char updated_at[32];
    char resolved_at[32];
} Complaint;

typedef struct {
    int history_id;
    char complaint_id[MAX_ID_LEN];
    int changed_by;
    char changed_by_name[MAX_NAME_LEN];
    char changed_by_role[16];
    char old_status[MAX_STATUS_LEN];
    char new_status[MAX_STATUS_LEN];
    char remarks[MAX_REMARKS_LEN];
    char created_at[32];
} ComplaintHistory;

// Get current UTC timestamp in ISO 8601 format (e.g. 2026-10-26T16:50:00Z)
void get_current_utc_timestamp(char *buf, size_t size);

#endif // COMMON_H
