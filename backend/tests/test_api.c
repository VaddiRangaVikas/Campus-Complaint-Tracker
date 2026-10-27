#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/database.h"
#include "../include/auth.h"
#include "../include/common.h"

static int g_run = 0;
static int g_pass = 0;

#define API_TEST_ASSERT(cond, msg) do { \
    g_run++; \
    if (cond) { \
        g_pass++; \
    } else { \
        printf("[FAIL] Line %d: %s\n", __LINE__, msg); \
        assert(cond); \
    } \
} while(0)

int main(void)
{
    printf("==================================================\n");
    printf(" CAMPUS COMPLAINT TRACKER - BACKEND INTEGRATION \n");
    printf("==================================================\n");

    const char *test_db = "test_persistence.db";
    remove(test_db);

    // 1. Database Initialization
    bool init_ok = db_init(test_db);
    API_TEST_ASSERT(init_ok == true, "Database initializes test_persistence.db");

    // 2. Default Administrator auto-creation check
    User admin_user;
    char admin_hash[65] = {0}, admin_salt[65] = {0};
    bool found_admin = db_get_user_by_email("admin@campus.edu", &admin_user, admin_hash, admin_salt);
    API_TEST_ASSERT(found_admin == true, "Default Administrator account created");
    API_TEST_ASSERT(strcmp(admin_user.role, "admin") == 0, "Administrator role is 'admin'");
    API_TEST_ASSERT(auth_verify_password("Admin@123", admin_salt, admin_hash) == true, "Admin password matches 'Admin@123'");
    API_TEST_ASSERT(auth_verify_password("WrongPassword", admin_salt, admin_hash) == false, "Wrong password rejected");

    // 3. Student Registration
    User student;
    memset(&student, 0, sizeof(User));
    strcpy(student.full_name, "Rohit Verma");
    strcpy(student.email, "rohit@campus.edu");
    strcpy(student.student_id, "ME2025-088");
    strcpy(student.department, "Mechanical Engineering");
    strcpy(student.role, "student");
    strcpy(student.account_status, "active");

    char s_salt[65], s_hash[65];
    auth_generate_salt(s_salt, sizeof(s_salt));
    auth_hash_password("Rohit@2025", s_salt, s_hash, sizeof(s_hash));

    int student_id = 0;
    bool create_ok = db_create_user(&student, s_hash, s_salt, &student_id);
    API_TEST_ASSERT(create_ok == true && student_id > 0, "Student created with ID > 0");

    // Duplicate email registration prevention
    bool dup_ok = db_create_user(&student, s_hash, s_salt, NULL);
    API_TEST_ASSERT(dup_ok == false, "Duplicate email registration prevented");

    // 4. Session Creation and Verification
    char session_token[128];
    auth_generate_token(session_token, sizeof(session_token));
    API_TEST_ASSERT(strlen(session_token) == 64, "Generated 64-char crypto token");

    bool sess_ok = db_create_session(session_token, student_id, 3600);
    API_TEST_ASSERT(sess_ok == true, "Session stored in DB");

    User session_user;
    bool auth_ok = db_get_session(session_token, &session_user);
    API_TEST_ASSERT(auth_ok == true, "Session verified");
    API_TEST_ASSERT(session_user.user_id == student_id, "Session maps to correct student");

    // 5. Complaint Creation & Unique ID Generation
    Complaint comp;
    memset(&comp, 0, sizeof(Complaint));
    comp.user_id = student_id;
    strcpy(comp.title, "Broken pipe leaking in ME Workshop");
    strcpy(comp.category, "Water Supply");
    strcpy(comp.description, "Valve leaking under lathe machine #4.");
    strcpy(comp.location, "Mechanical Block");
    strcpy(comp.room_number, "Shop 2");
    comp.priority = 3;
    strcpy(comp.status, "pending");

    bool comp_ok = db_create_complaint(&comp);
    API_TEST_ASSERT(comp_ok == true, "Complaint created successfully");
    API_TEST_ASSERT(strncmp(comp.complaint_id, "CMP-", 4) == 0, "Complaint ID starts with 'CMP-'");

    // Verify complaint retrieved by ID
    Complaint retrieved;
    bool get_ok = db_get_complaint(comp.complaint_id, &retrieved);
    API_TEST_ASSERT(get_ok == true, "Retrieved complaint from SQLite");
    API_TEST_ASSERT(strcmp(retrieved.title, comp.title) == 0, "Retrieved title matches");
    API_TEST_ASSERT(retrieved.priority == 3, "Retrieved priority matches 3");
    API_TEST_ASSERT(strcmp(retrieved.student_name, "Rohit Verma") == 0, "JOIN fetched student name");

    // 6. History Record Created on Submission
    ComplaintHistory *hist = NULL;
    int h_count = 0;
    db_get_complaint_history(comp.complaint_id, &hist, &h_count);
    API_TEST_ASSERT(h_count >= 1, "Submission created initial history record");
    if (hist) free(hist);

    // 7. Admin Status Change & Transactional History Tracking
    bool status_ok = db_update_complaint_status(comp.complaint_id, "in_progress", admin_user.user_id, "Maintenance team assigned.");
    API_TEST_ASSERT(status_ok == true, "Status updated to in_progress");

    db_get_complaint_history(comp.complaint_id, &hist, &h_count);
    API_TEST_ASSERT(h_count == 2, "History now has 2 audit entries");
    API_TEST_ASSERT(strcmp(hist[1].new_status, "in_progress") == 0, "Second audit entry is 'in_progress'");
    API_TEST_ASSERT(strcmp(hist[1].remarks, "Maintenance team assigned.") == 0, "Remarks saved correctly");
    if (hist) free(hist);

    // 8. Admin Priority Update
    bool pri_ok = db_update_complaint_priority(comp.complaint_id, 4, admin_user.user_id, "Escalated due to water hazard.");
    API_TEST_ASSERT(pri_ok == true, "Priority updated to 4 (Critical)");

    db_get_complaint(comp.complaint_id, &retrieved);
    API_TEST_ASSERT(retrieved.priority == 4, "Updated priority persisted as 4");

    // 9. Real Aggregate Dashboard Statistics
    cJSON *stats = NULL;
    db_get_dashboard_stats(admin_user.user_id, "admin", &stats);
    API_TEST_ASSERT(stats != NULL, "Stats JSON generated");
    cJSON *tot = cJSON_GetObjectItem(stats, "total_complaints");
    API_TEST_ASSERT(tot && tot->valueint >= 1, "Aggregate total_complaints >= 1");
    cJSON_Delete(stats);

    // 10. Database Persistence Check After Restart
    db_close();

    bool reopen_ok = db_init(test_db);
    API_TEST_ASSERT(reopen_ok == true, "Database successfully re-opened");

    Complaint persistent_comp;
    bool persist_ok = db_get_complaint(comp.complaint_id, &persistent_comp);
    API_TEST_ASSERT(persist_ok == true, "Complaint persisted across database reboots");
    API_TEST_ASSERT(persistent_comp.priority == 4, "Persisted priority retained");

    // 11. Session Invalidation (Logout)
    db_delete_session(session_token);
    User post_logout;
    bool post_auth = db_get_session(session_token, &post_logout);
    API_TEST_ASSERT(post_auth == false, "Session invalidated after logout");

    db_close();
    remove(test_db);

    printf("\n==================================================\n");
    printf(" RESULTS: %d / %d integration tests passed! \n", g_pass, g_run);
    printf("==================================================\n");

    return (g_pass == g_run) ? 0 : 1;
}
