#include "validation.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void sanitize_string(char *str) {
    if (!str) return;
    // Trim leading whitespace
    char *p = str;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != str) {
        memmove(str, p, strlen(p) + 1);
    }
    // Trim trailing whitespace
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

static bool is_valid_email(const char *email) {
    if (!email || strlen(email) < 5 || strlen(email) > 120) return false;
    const char *at = strchr(email, '@');
    if (!at || at == email) return false;
    const char *dot = strchr(at, '.');
    if (!dot || dot == at + 1 || *(dot + 1) == '\0') return false;
    return true;
}

bool validate_registration(const char *full_name, const char *email,
                           const char *student_id, const char *department,
                           const char *password, const char *confirm_password,
                           char *err_msg, size_t err_size) {
    if (!full_name || strlen(full_name) < 2 || strlen(full_name) > 80) {
        snprintf(err_msg, err_size, "Full name must be between 2 and 80 characters.");
        return false;
    }

    if (!is_valid_email(email)) {
        snprintf(err_msg, err_size, "A valid college email address is required.");
        return false;
    }

    if (!department || strlen(department) < 2 || strlen(department) > 60) {
        snprintf(err_msg, err_size, "Department is required.");
        return false;
    }

    if (student_id && strlen(student_id) > 0) {
        if (strlen(student_id) < 2 || strlen(student_id) > 30) {
            snprintf(err_msg, err_size, "Student ID must be between 2 and 30 characters.");
            return false;
        }
    }

    if (!password || strlen(password) < 6) {
        snprintf(err_msg, err_size, "Password must be at least 6 characters long.");
        return false;
    }

    if (!confirm_password || strcmp(password, confirm_password) != 0) {
        snprintf(err_msg, err_size, "Passwords do not match.");
        return false;
    }

    return true;
}

bool validate_login(const char *email, const char *password,
                    char *err_msg, size_t err_size) {
    if (!email || !is_valid_email(email)) {
        snprintf(err_msg, err_size, "Please enter a valid email address.");
        return false;
    }

    if (!password || strlen(password) < 1) {
        snprintf(err_msg, err_size, "Password is required.");
        return false;
    }

    return true;
}

static const char *VALID_CATEGORIES[] = {
    "Hostel", "Classroom", "Electricity", "Water Supply",
    "Internet and Wi-Fi", "Cleanliness", "Library", "Laboratory",
    "Transport", "Canteen", "Other", NULL
};

bool validate_complaint(const char *title, const char *category,
                        const char *description, const char *location,
                        const char *room_number, int priority,
                        char *err_msg, size_t err_size) {
    if (!title || strlen(title) < 5 || strlen(title) > 150) {
        snprintf(err_msg, err_size, "Title must be between 5 and 150 characters.");
        return false;
    }

    if (!category || strlen(category) == 0) {
        snprintf(err_msg, err_size, "Please select a valid complaint category.");
        return false;
    }

    bool cat_found = false;
    for (int i = 0; VALID_CATEGORIES[i] != NULL; i++) {
        if (strcasecmp(category, VALID_CATEGORIES[i]) == 0) {
            cat_found = true;
            break;
        }
    }
    if (!cat_found) {
        snprintf(err_msg, err_size, "Selected category '%s' is not recognized.", category);
        return false;
    }

    if (!description || strlen(description) < 10 || strlen(description) > 3500) {
        snprintf(err_msg, err_size, "Description must be between 10 and 3,500 characters.");
        return false;
    }

    if (!location || strlen(location) < 2 || strlen(location) > 100) {
        snprintf(err_msg, err_size, "Campus location or building is required.");
        return false;
    }

    if (room_number && strlen(room_number) > 30) {
        snprintf(err_msg, err_size, "Room number exceeds 30 characters.");
        return false;
    }

    if (priority < 1 || priority > 4) {
        snprintf(err_msg, err_size, "Priority must be between 1 (Low) and 4 (Critical).");
        return false;
    }

    return true;
}

bool validate_status_transition(const char *current_status, const char *new_status,
                                char *err_msg, size_t err_size) {
    if (!new_status) {
        snprintf(err_msg, err_size, "Target status is required.");
        return false;
    }

    // Supported statuses: pending, in_progress, resolved, rejected
    if (strcmp(new_status, "pending") != 0 &&
        strcmp(new_status, "in_progress") != 0 &&
        strcmp(new_status, "resolved") != 0 &&
        strcmp(new_status, "rejected") != 0) {
        snprintf(err_msg, err_size, "Invalid status '%s'. Must be pending, in_progress, resolved, or rejected.", new_status);
        return false;
    }

    if (current_status && strcmp(current_status, new_status) == 0) {
        snprintf(err_msg, err_size, "Complaint is already in '%s' status.", current_status);
        return false;
    }

    return true;
}

bool validate_priority(int priority, char *err_msg, size_t err_size) {
    if (priority < 1 || priority > 4) {
        snprintf(err_msg, err_size, "Invalid priority %d. Must be 1 (Low), 2 (Medium), 3 (High), or 4 (Critical).", priority);
        return false;
    }
    return true;
}
