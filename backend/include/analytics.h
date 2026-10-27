#ifndef ANALYTICS_H
#define ANALYTICS_H

#include <stdbool.h>
#include <stddef.h>
#include "common.h"

typedef struct {
    char category[48];
    int count;
} CategoryStat;

typedef struct {
    char date[16]; // YYYY-MM-DD
    int count;
} DailyStat;

typedef struct {
    int total_complaints;
    int pending_complaints;
    int in_progress_complaints;
    int resolved_complaints;
    int rejected_complaints;
    int critical_complaints;
    int high_priority_complaints;
    double avg_resolution_hours;

    int priority_counts[5]; // index 1: Low, 2: Medium, 3: High, 4: Critical

    CategoryStat category_stats[16];
    int category_count;

    DailyStat recent_trends[7];
    int trend_count;

    Complaint recent_complaints[5];
    int recent_count;
} AdminDashboardData;

typedef struct {
    int total_complaints;
    int pending_complaints;
    int in_progress_complaints;
    int resolved_complaints;
    Complaint recent_complaints[5];
    int recent_count;
} StudentDashboardData;

// Fetch aggregate dashboard metrics for admin
bool analytics_get_admin_dashboard(AdminDashboardData *data);

// Fetch dashboard metrics for specific student
bool analytics_get_student_dashboard(int user_id, StudentDashboardData *data);

// Generate CSV export for complaints into an allocated string buffer
char* analytics_export_complaints_csv(void);

#endif // ANALYTICS_H
