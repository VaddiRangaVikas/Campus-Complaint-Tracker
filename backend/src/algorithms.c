#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "algorithms.h"

// High-resolution timer utility using Windows QueryPerformanceCounter
double get_time_us(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return ((double)counter.QuadPart * 1000000.0) / (double)freq.QuadPart;
}

// Deterministic priority comparator:
// Returns:
//   > 0 if a has higher priority than b
//   < 0 if b has higher priority than a
//   0 if equal
// Priority: Critical (4) > High (3) > Medium (2) > Low (1)
// Tie-breaker 1: Oldest created_at first (chronological FIFO)
// Tie-breaker 2: Lexicographical complaint_id ascending
int compare_priority(const ComplaintRecord *a, const ComplaintRecord *b)
{
    if (a->priority != b->priority) {
        return a->priority - b->priority; // Higher priority is greater
    }

    // Tie-breaker 1: earlier submission date should have higher urgency (oldest first)
    int date_cmp = strcmp(a->created_at, b->created_at);
    if (date_cmp != 0) {
        return (date_cmp < 0) ? 1 : -1; // Earlier timestamp wins
    }

    // Tie-breaker 2: lower complaint ID
    int id_cmp = strcmp(a->complaint_id, b->complaint_id);
    if (id_cmp != 0) {
        return (id_cmp < 0) ? 1 : -1;
    }

    return 0;
}

// Comparator by SortOrder
// Returns: > 0 if a should come before b, < 0 if b should come before a
int compare_by_order(const ComplaintRecord *a, const ComplaintRecord *b, SortOrder order)
{
    switch (order) {
        case SORT_BY_PRIORITY_DESC:
            return compare_priority(a, b);

        case SORT_BY_DATE_DESC: {
            int d = strcmp(b->created_at, a->created_at); // Newest first
            if (d != 0) return d;
            return strcmp(b->complaint_id, a->complaint_id);
        }

        case SORT_BY_DATE_ASC: {
            int d = strcmp(a->created_at, b->created_at); // Oldest first
            if (d != 0) return -d; // Note: for merge_sort where >0 means a comes first
            return -strcmp(a->complaint_id, b->complaint_id);
        }

        case SORT_BY_ID_ASC: {
            int c = strcmp(a->complaint_id, b->complaint_id);
            return (c < 0) ? 1 : ((c > 0) ? -1 : 0);
        }

        default:
            return 0;
    }
}

// ==========================================
// Algorithm A: Handcrafted Merge Sort
// ==========================================

static void merge(
    ComplaintRecord *arr,
    ComplaintRecord *temp,
    int left,
    int mid,
    int right,
    SortOrder order,
    AlgorithmMetrics *metrics
) {
    int i = left;
    int j = mid + 1;
    int k = left;

    while (i <= mid && j <= right) {
        if (metrics) metrics->comparisons++;
        // If compare_by_order returns > 0, arr[i] comes before arr[j]
        if (compare_by_order(&arr[i], &arr[j], order) >= 0) {
            temp[k++] = arr[i++];
        } else {
            temp[k++] = arr[j++];
        }
        if (metrics) metrics->movements++;
    }

    while (i <= mid) {
        temp[k++] = arr[i++];
        if (metrics) metrics->movements++;
    }

    while (j <= right) {
        temp[k++] = arr[j++];
        if (metrics) metrics->movements++;
    }

    for (k = left; k <= right; k++) {
        arr[k] = temp[k];
        if (metrics) metrics->movements++;
    }
}

static void merge_sort_rec(
    ComplaintRecord *arr,
    ComplaintRecord *temp,
    int left,
    int right,
    SortOrder order,
    AlgorithmMetrics *metrics
) {
    if (left < right) {
        int mid = left + (right - left) / 2;
        merge_sort_rec(arr, temp, left, mid, order, metrics);
        merge_sort_rec(arr, temp, mid + 1, right, order, metrics);
        merge(arr, temp, left, mid, right, order, metrics);
    }
}

void merge_sort(ComplaintRecord *arr, int n, SortOrder order, AlgorithmMetrics *metrics)
{
    if (metrics) {
        metrics->comparisons = 0;
        metrics->movements = 0;
        metrics->duration_us = 0.0;
    }

    if (!arr || n <= 1) return;

    ComplaintRecord *temp = (ComplaintRecord *)malloc(n * sizeof(ComplaintRecord));
    if (!temp) return;

    double t0 = get_time_us();
    merge_sort_rec(arr, temp, 0, n - 1, order, metrics);
    double t1 = get_time_us();

    if (metrics) {
        metrics->duration_us = t1 - t0;
    }

    free(temp);
}

// ==========================================
// Algorithm B: Iterative Binary Search
// ==========================================

int binary_search_by_id(const ComplaintRecord *arr, int n, const char *target_id, AlgorithmMetrics *metrics)
{
    if (metrics) {
        metrics->comparisons = 0;
        metrics->movements = 0;
        metrics->duration_us = 0.0;
    }

    if (!arr || n <= 0 || !target_id) return -1;

    double t0 = get_time_us();
    int left = 0;
    int right = n - 1;
    int result = -1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (metrics) metrics->comparisons++;

        int cmp = strcmp(arr[mid].complaint_id, target_id);
        if (cmp == 0) {
            result = mid;
            break;
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    double t1 = get_time_us();
    if (metrics) {
        metrics->duration_us = t1 - t0;
    }

    return result;
}

// ==========================================
// Algorithm C: Max-Heap Priority Queue
// ==========================================

ComplaintHeap* heap_create(int initial_capacity)
{
    if (initial_capacity <= 0) initial_capacity = 32;

    ComplaintHeap *h = (ComplaintHeap *)malloc(sizeof(ComplaintHeap));
    if (!h) return NULL;

    h->data = (ComplaintRecord *)malloc(initial_capacity * sizeof(ComplaintRecord));
    if (!h->data) {
        free(h);
        return NULL;
    }

    h->size = 0;
    h->capacity = initial_capacity;
    h->metrics.comparisons = 0;
    h->metrics.movements = 0;
    h->metrics.duration_us = 0.0;

    return h;
}

void heap_free(ComplaintHeap *h)
{
    if (!h) return;
    if (h->data) free(h->data);
    free(h);
}

void heap_reset_metrics(ComplaintHeap *h)
{
    if (!h) return;
    h->metrics.comparisons = 0;
    h->metrics.movements = 0;
    h->metrics.duration_us = 0.0;
}

static void heap_swap(ComplaintRecord *a, ComplaintRecord *b)
{
    ComplaintRecord tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_up(ComplaintHeap *h, int idx)
{
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        h->metrics.comparisons++;
        if (compare_priority(&h->data[idx], &h->data[parent]) > 0) {
            heap_swap(&h->data[idx], &h->data[parent]);
            h->metrics.movements++;
            idx = parent;
        } else {
            break;
        }
    }
}

static void sift_down(ComplaintHeap *h, int idx)
{
    while (2 * idx + 1 < h->size) {
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;
        int largest = idx;

        h->metrics.comparisons++;
        if (compare_priority(&h->data[left], &h->data[largest]) > 0) {
            largest = left;
        }

        if (right < h->size) {
            h->metrics.comparisons++;
            if (compare_priority(&h->data[right], &h->data[largest]) > 0) {
                largest = right;
            }
        }

        if (largest != idx) {
            heap_swap(&h->data[idx], &h->data[largest]);
            h->metrics.movements++;
            idx = largest;
        } else {
            break;
        }
    }
}

bool heap_insert(ComplaintHeap *h, const ComplaintRecord *item)
{
    if (!h || !item) return false;

    if (h->size >= h->capacity) {
        int new_cap = h->capacity * 2;
        ComplaintRecord *new_data = (ComplaintRecord *)realloc(h->data, new_cap * sizeof(ComplaintRecord));
        if (!new_data) return false;
        h->data = new_data;
        h->capacity = new_cap;
    }

    double t0 = get_time_us();
    h->data[h->size] = *item;
    sift_up(h, h->size);
    h->size++;
    double t1 = get_time_us();

    h->metrics.duration_us += (t1 - t0);
    return true;
}

bool heap_peek(const ComplaintHeap *h, ComplaintRecord *out)
{
    if (!h || h->size == 0 || !out) return false;
    *out = h->data[0];
    return true;
}

bool heap_extract_max(ComplaintHeap *h, ComplaintRecord *out)
{
    if (!h || h->size == 0) return false;

    double t0 = get_time_us();
    if (out) *out = h->data[0];

    h->data[0] = h->data[h->size - 1];
    h->size--;
    h->metrics.movements++;

    if (h->size > 0) {
        sift_down(h, 0);
    }
    double t1 = get_time_us();

    h->metrics.duration_us += (t1 - t0);
    return true;
}

// Floyd's Algorithm for Linear-time O(n) Heap Construction
void heap_build(ComplaintHeap *h, const ComplaintRecord *items, int n)
{
    if (!h) return;

    if (n > h->capacity) {
        ComplaintRecord *new_data = (ComplaintRecord *)realloc(h->data, (n + 32) * sizeof(ComplaintRecord));
        if (!new_data) return;
        h->data = new_data;
        h->capacity = n + 32;
    }

    double t0 = get_time_us();
    if (items && n > 0) {
        memcpy(h->data, items, n * sizeof(ComplaintRecord));
    }
    h->size = n;

    for (int i = (n / 2) - 1; i >= 0; i--) {
        sift_down(h, i);
    }
    double t1 = get_time_us();

    h->metrics.duration_us = (t1 - t0);
}

// ==========================================
// Algorithm D: Linear Search Baseline
// ==========================================

int linear_search_by_id(const ComplaintRecord *arr, int n, const char *target_id, AlgorithmMetrics *metrics)
{
    if (metrics) {
        metrics->comparisons = 0;
        metrics->movements = 0;
        metrics->duration_us = 0.0;
    }

    if (!arr || n <= 0 || !target_id) return -1;

    double t0 = get_time_us();
    int result = -1;

    for (int i = 0; i < n; i++) {
        if (metrics) metrics->comparisons++;
        if (strcmp(arr[i].complaint_id, target_id) == 0) {
            result = i;
            break;
        }
    }

    double t1 = get_time_us();
    if (metrics) {
        metrics->duration_us = t1 - t0;
    }

    return result;
}

int linear_search_by_category(
    const ComplaintRecord *arr,
    int n,
    const char *category,
    int *match_indices,
    int max_matches,
    AlgorithmMetrics *metrics
) {
    if (metrics) {
        metrics->comparisons = 0;
        metrics->movements = 0;
        metrics->duration_us = 0.0;
    }

    if (!arr || n <= 0 || !category) return 0;

    double t0 = get_time_us();
    int found_count = 0;

    for (int i = 0; i < n; i++) {
        if (metrics) metrics->comparisons++;
        if (strcasecmp(arr[i].category, category) == 0) {
            if (match_indices && found_count < max_matches) {
                match_indices[found_count] = i;
            }
            found_count++;
        }
    }

    double t1 = get_time_us();
    if (metrics) {
        metrics->duration_us = t1 - t0;
    }

    return found_count;
}
