#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Complaint record used across DAA sorting, searching, and heap priority queue
typedef struct {
    char complaint_id[32];
    int user_id;
    char student_name[64];
    char title[128];
    char category[48];
    char location[48];
    int priority;           // 4: Critical, 3: High, 2: Medium, 1: Low
    char status[24];        // pending, in_progress, resolved, rejected
    char created_at[32];    // ISO8601 UTC timestamp
} ComplaintRecord;

typedef enum {
    SORT_BY_PRIORITY_DESC = 0,  // Critical (4) -> Low (1), tie-break: oldest first, then ID
    SORT_BY_DATE_DESC = 1,      // Newest first, tie-break: ID
    SORT_BY_DATE_ASC = 2,       // Oldest first, tie-break: ID
    SORT_BY_ID_ASC = 3          // CMP-2026-0001 -> ...
} SortOrder;

typedef struct {
    unsigned long long comparisons;
    unsigned long long movements;
    double duration_us;         // Elapsed execution time in microseconds
} AlgorithmMetrics;

// ==========================================
// Algorithm A: Handcrafted Merge Sort
// ==========================================
// Time Complexity: O(n log n) Best, Average, Worst
// Auxiliary Space: O(n)
void merge_sort(ComplaintRecord *arr, int n, SortOrder order, AlgorithmMetrics *metrics);

// ==========================================
// Algorithm B: Iterative Binary Search
// ==========================================
// Requirements: Array MUST be sorted by complaint_id
// Time Complexity: O(1) Best, O(log n) Average, O(log n) Worst
// Auxiliary Space: O(1)
int binary_search_by_id(const ComplaintRecord *arr, int n, const char *target_id, AlgorithmMetrics *metrics);

// ==========================================
// Algorithm C: Max-Heap Priority Queue
// ==========================================
// Time Complexity: Peek O(1), Insert O(log n), Extract-Max O(log n), Build-Heap O(n)
// Auxiliary Space: O(n)
typedef struct {
    ComplaintRecord *data;
    int size;
    int capacity;
    AlgorithmMetrics metrics;
} ComplaintHeap;

ComplaintHeap* heap_create(int initial_capacity);
void heap_free(ComplaintHeap *h);
bool heap_insert(ComplaintHeap *h, const ComplaintRecord *item);
bool heap_peek(const ComplaintHeap *h, ComplaintRecord *out);
bool heap_extract_max(ComplaintHeap *h, ComplaintRecord *out);
void heap_build(ComplaintHeap *h, const ComplaintRecord *items, int n);
void heap_reset_metrics(ComplaintHeap *h);

// Deterministic comparator used by Heap & Sort
// Returns: > 0 if a > b, < 0 if a < b, 0 if equal
int compare_priority(const ComplaintRecord *a, const ComplaintRecord *b);
int compare_by_order(const ComplaintRecord *a, const ComplaintRecord *b, SortOrder order);

// ==========================================
// Algorithm D: Linear Search Baseline
// ==========================================
// Time Complexity: O(1) Best, O(n) Average & Worst
// Auxiliary Space: O(1)
int linear_search_by_id(const ComplaintRecord *arr, int n, const char *target_id, AlgorithmMetrics *metrics);
int linear_search_by_category(const ComplaintRecord *arr, int n, const char *category, int *match_indices, int max_matches, AlgorithmMetrics *metrics);

// High-resolution timer utility
double get_time_us(void);

#ifdef __cplusplus
}
#endif

#endif // ALGORITHMS_H
