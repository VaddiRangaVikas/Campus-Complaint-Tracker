#ifndef COMPLAINTS_H
#define COMPLAINTS_H

#include <stdbool.h>
#include <stddef.h>
#include "common.h"

// Generate next unique complaint ID (e.g. CMP-2026-0001)
bool complaints_generate_id(char *out_id, size_t id_size);

// Create a new complaint record with initial history entry in an ACID transaction
bool complaints_create(int user_id, const char *title, const char *category,
                       const char *description, const char *location,
                       const char *room_number, int priority,
                       char *out_complaint_id, size_t id_size,
                       char *err_msg, size_t err_size);

// Fetch a single complaint by ID along with its full audit timeline history
bool complaints_get_by_id(const char *complaint_id, Complaint *out_complaint,
                          ComplaintHistory *out_history, int max_history, int *out_history_count);

// Fetch complaints for a specific authenticated student with filtering, searching, and pagination
bool complaints_list_student(int user_id, const char *search, const char *category,
                             const char *status, int priority, const char *sort_by,
                             int page, int page_size,
                             Complaint *out_list, int max_list, int *out_count, int *out_total);

// Fetch all campus complaints for administrators with filtering, searching, and pagination
bool complaints_list_admin(const char *search, const char *category,
                           const char *status, int priority, const char *sort_by,
                           int page, int page_size,
                           Complaint *out_list, int max_list, int *out_count, int *out_total);

// Admin status change with transactional history recording
bool complaints_update_status(const char *complaint_id, int admin_id,
                              const char *new_status, const char *remarks,
                              char *err_msg, size_t err_size);

// Admin priority assignment with transactional history recording
bool complaints_update_priority(const char *complaint_id, int admin_id,
                                int new_priority, const char *remarks,
                                char *err_msg, size_t err_size);

// Admin adds general remark / progress update
bool complaints_add_remarks(const char *complaint_id, int admin_id,
                            const char *remarks,
                            char *err_msg, size_t err_size);

#endif // COMPLAINTS_H
