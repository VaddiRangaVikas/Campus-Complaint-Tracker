#ifndef VALIDATION_H
#define VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

// Validate user registration parameters
bool validate_registration(const char *full_name, const char *email,
                           const char *student_id, const char *department,
                           const char *password, const char *confirm_password,
                           char *err_msg, size_t err_size);

// Validate user login parameters
bool validate_login(const char *email, const char *password,
                    char *err_msg, size_t err_size);

// Validate complaint submission
bool validate_complaint(const char *title, const char *category,
                        const char *description, const char *location,
                        const char *room_number, int priority,
                        char *err_msg, size_t err_size);

// Validate status transition (pending -> in_progress -> resolved / rejected)
bool validate_status_transition(const char *current_status, const char *new_status,
                                char *err_msg, size_t err_size);

// Validate priority value (1 to 4)
bool validate_priority(int priority, char *err_msg, size_t err_size);

// Clean / sanitize string: strip leading/trailing spaces
void sanitize_string(char *str);

#endif // VALIDATION_H
