#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../backend/include/algorithms.h"

// Test harness macros
static int total_tests = 0;
static int passed_tests = 0;

#define TEST_CASE(name) \
    do { \
        total_tests++; \
        printf("[RUNNING] %s ... ", name); \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAILED! %s (Line %d)\n", msg, __LINE__); \
            return; \
        } \
    } while(0)

#define TEST_PASSED() \
    do { \
        passed_tests++; \
        printf("PASSED\n"); \
    } while(0)

// Helper to fill a complaint record
static ComplaintRecord make_record(const char *id, int priority, const char *date, const char *title) {
    ComplaintRecord r;
    memset(&r, 0, sizeof(r));
    strncpy(r.complaint_id, id, sizeof(r.complaint_id) - 1);
    r.priority = priority;
    strncpy(r.created_at, date, sizeof(r.created_at) - 1);
    strncpy(r.title, title, sizeof(r.title) - 1);
    strncpy(r.status, "pending", sizeof(r.status) - 1);
    strncpy(r.category, "Hostel", sizeof(r.category) - 1);
    return r;
}

// ==========================================
// Test 1: Merge Sort Edge Cases (Empty & Single)
// ==========================================
void test_mergesort_edge_cases(void) {
    TEST_CASE("MergeSort on empty and single element array");
    AlgorithmMetrics m;

    // Empty array (null or n=0)
    merge_sort(NULL, 0, SORT_BY_PRIORITY_DESC, &m);
    ASSERT_TRUE(m.comparisons == 0, "Empty array should have 0 comparisons");

    ComplaintRecord single[1];
    single[0] = make_record("CMP-001", 3, "2026-10-20T10:00:00Z", "Fan broken");
    merge_sort(single, 1, SORT_BY_PRIORITY_DESC, &m);
    ASSERT_TRUE(strcmp(single[0].complaint_id, "CMP-001") == 0, "Single element untouched");
    ASSERT_TRUE(m.comparisons == 0, "Single element should have 0 comparisons");

    TEST_PASSED();
}

// ==========================================
// Test 2: Merge Sort Priority & Deterministic Tie-Breaking
// ==========================================
void test_mergesort_priority_and_tie_breaking(void) {
    TEST_CASE("MergeSort by Priority with deterministic FIFO tie-breaking");
    AlgorithmMetrics m;

    ComplaintRecord arr[4];
    // Two with priority 2, different dates; one priority 4; one priority 1
    arr[0] = make_record("CMP-001", 2, "2026-10-21T12:00:00Z", "Issue A"); // Medium, later
    arr[1] = make_record("CMP-002", 4, "2026-10-22T08:00:00Z", "Issue B"); // Critical
    arr[2] = make_record("CMP-003", 1, "2026-10-19T09:00:00Z", "Issue C"); // Low
    arr[3] = make_record("CMP-004", 2, "2026-10-20T11:00:00Z", "Issue D"); // Medium, earlier

    merge_sort(arr, 4, SORT_BY_PRIORITY_DESC, &m);

    // Expected order:
    // 1st: CMP-002 (Priority 4 - Critical)
    // 2nd: CMP-004 (Priority 2 - submitted on 2026-10-20, earlier than CMP-001)
    // 3rd: CMP-001 (Priority 2 - submitted on 2026-10-21)
    // 4th: CMP-003 (Priority 1 - Low)
    ASSERT_TRUE(strcmp(arr[0].complaint_id, "CMP-002") == 0, "Index 0 must be Priority 4");
    ASSERT_TRUE(strcmp(arr[1].complaint_id, "CMP-004") == 0, "Index 1 must be Priority 2 (earlier date)");
    ASSERT_TRUE(strcmp(arr[2].complaint_id, "CMP-001") == 0, "Index 2 must be Priority 2 (later date)");
    ASSERT_TRUE(strcmp(arr[3].complaint_id, "CMP-003") == 0, "Index 3 must be Priority 1");
    ASSERT_TRUE(m.comparisons > 0, "Comparisons should be positive");

    TEST_PASSED();
}

// ==========================================
// Test 3: Merge Sort by Complaint ID & Reverse-Sorted
// ==========================================
void test_mergesort_reverse_and_sorted(void) {
    TEST_CASE("MergeSort on reverse-sorted input by ID");
    AlgorithmMetrics m;

    ComplaintRecord arr[5];
    arr[0] = make_record("CMP-005", 2, "2026-10-05T00:00:00Z", "5");
    arr[1] = make_record("CMP-004", 3, "2026-10-04T00:00:00Z", "4");
    arr[2] = make_record("CMP-003", 1, "2026-10-03T00:00:00Z", "3");
    arr[3] = make_record("CMP-002", 4, "2026-10-02T00:00:00Z", "2");
    arr[4] = make_record("CMP-001", 2, "2026-10-01T00:00:00Z", "1");

    merge_sort(arr, 5, SORT_BY_ID_ASC, &m);

    for (int i = 0; i < 5; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "CMP-00%d", i + 1);
        ASSERT_TRUE(strcmp(arr[i].complaint_id, expected) == 0, "IDs must be strictly ascending");
    }

    TEST_PASSED();
}

// ==========================================
// Test 4: Binary Search for Existing and Missing IDs
// ==========================================
void test_binary_search(void) {
    TEST_CASE("Binary Search with existing, missing, and empty data");
    AlgorithmMetrics m;

    // Empty array
    int res = binary_search_by_id(NULL, 0, "CMP-001", &m);
    ASSERT_TRUE(res == -1, "Empty array returns -1");

    // Sorted array of 6 items
    ComplaintRecord arr[6];
    for (int i = 0; i < 6; i++) {
        char id[16];
        snprintf(id, sizeof(id), "CMP-00%d", i + 1);
        arr[i] = make_record(id, 2, "2026-10-20T00:00:00Z", "Test");
    }

    // Search existing middle
    res = binary_search_by_id(arr, 6, "CMP-003", &m);
    ASSERT_TRUE(res == 2, "Target CMP-003 should be found at index 2");
    ASSERT_TRUE(m.comparisons <= 3, "Binary search takes <= ceil(log2(6)) comparisons");

    // Search existing boundary (first and last)
    res = binary_search_by_id(arr, 6, "CMP-001", &m);
    ASSERT_TRUE(res == 0, "Target CMP-001 at index 0");
    res = binary_search_by_id(arr, 6, "CMP-006", &m);
    ASSERT_TRUE(res == 5, "Target CMP-006 at index 5");

    // Search missing items
    res = binary_search_by_id(arr, 6, "CMP-000", &m);
    ASSERT_TRUE(res == -1, "Smaller than min returns -1");
    res = binary_search_by_id(arr, 6, "CMP-999", &m);
    ASSERT_TRUE(res == -1, "Greater than max returns -1");
    res = binary_search_by_id(arr, 6, "CMP-0025", &m);
    ASSERT_TRUE(res == -1, "Intermediate missing returns -1");

    TEST_PASSED();
}

// ==========================================
// Test 5: Max-Heap Priority Queue (Insert, Peek, Extract, Underflow)
// ==========================================
void test_max_heap_operations(void) {
    TEST_CASE("Max-Heap priority queue insert, peek, extract, underflow");

    ComplaintHeap *h = heap_create(4);
    ASSERT_TRUE(h != NULL, "Heap must be created");

    // Underflow on empty heap
    ComplaintRecord peek_rec;
    ASSERT_TRUE(!heap_peek(h, &peek_rec), "Peek on empty heap returns false");
    ASSERT_TRUE(!heap_extract_max(h, &peek_rec), "Extract on empty heap returns false");

    // Insert items in mixed priority
    ComplaintRecord r1 = make_record("CMP-101", 1, "2026-10-20T10:00:00Z", "Low");
    ComplaintRecord r2 = make_record("CMP-102", 3, "2026-10-20T11:00:00Z", "High");
    ComplaintRecord r3 = make_record("CMP-103", 4, "2026-10-20T12:00:00Z", "Critical");
    ComplaintRecord r4 = make_record("CMP-104", 2, "2026-10-20T13:00:00Z", "Medium");
    ComplaintRecord r5 = make_record("CMP-105", 4, "2026-10-20T09:00:00Z", "Critical Earlier"); // Should beat CMP-103!

    heap_insert(h, &r1);
    heap_insert(h, &r2);
    heap_insert(h, &r3);
    heap_insert(h, &r4);
    heap_insert(h, &r5); // Triggers capacity expansion

    ASSERT_TRUE(h->size == 5, "Heap size should be 5");

    // Peek should reveal the highest priority: CMP-105 (both are priority 4, but 105 has earlier date)
    ASSERT_TRUE(heap_peek(h, &peek_rec), "Peek succeeds");
    ASSERT_TRUE(strcmp(peek_rec.complaint_id, "CMP-105") == 0, "Highest priority must be CMP-105");

    // Extract all items in strictly prioritized order
    ComplaintRecord out;
    heap_extract_max(h, &out);
    ASSERT_TRUE(strcmp(out.complaint_id, "CMP-105") == 0, "1st extracted is CMP-105 (Critical, 09:00)");

    heap_extract_max(h, &out);
    ASSERT_TRUE(strcmp(out.complaint_id, "CMP-103") == 0, "2nd extracted is CMP-103 (Critical, 12:00)");

    heap_extract_max(h, &out);
    ASSERT_TRUE(strcmp(out.complaint_id, "CMP-102") == 0, "3rd extracted is CMP-102 (High 3)");

    heap_extract_max(h, &out);
    ASSERT_TRUE(strcmp(out.complaint_id, "CMP-104") == 0, "4th extracted is CMP-104 (Medium 2)");

    heap_extract_max(h, &out);
    ASSERT_TRUE(strcmp(out.complaint_id, "CMP-101") == 0, "5th extracted is CMP-101 (Low 1)");

    ASSERT_TRUE(h->size == 0, "Heap should be empty after extracting all");
    ASSERT_TRUE(!heap_extract_max(h, &out), "Underflow on drained heap handled");

    heap_free(h);
    TEST_PASSED();
}

// ==========================================
// Test 6: Bottom-Up Heap Construction (O(n) build)
// ==========================================
void test_heap_build_bottom_up(void) {
    TEST_CASE("Heap bottom-up construction O(n)");

    const int N = 8;
    ComplaintRecord items[8];
    for (int i = 0; i < N; i++) {
        char id[16];
        snprintf(id, sizeof(id), "CMP-%03d", i + 1);
        int prio = (i % 4) + 1;
        items[i] = make_record(id, prio, "2026-10-20T10:00:00Z", "Bulk item");
    }

    ComplaintHeap *h = heap_create(N);
    heap_build(h, items, N);
    ASSERT_TRUE(h->size == N, "Heap size must equal N");

    // Verify heap property: parent >= left child && parent >= right child
    for (int i = 0; i <= (N / 2) - 1; i++) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        if (left < N) {
            ASSERT_TRUE(compare_priority(&h->data[i], &h->data[left]) >= 0, "Heap property violated on left child");
        }
        if (right < N) {
            ASSERT_TRUE(compare_priority(&h->data[i], &h->data[right]) >= 0, "Heap property violated on right child");
        }
    }

    heap_free(h);
    TEST_PASSED();
}

// ==========================================
// Test 7: Large Dataset Stress Test & Comparison with Linear Search
// ==========================================
void test_large_dataset_and_search_comparison(void) {
    TEST_CASE("Large dataset (1,000 items) MergeSort and Binary vs Linear Search");

    const int N = 1000;
    ComplaintRecord *arr = (ComplaintRecord *)malloc(sizeof(ComplaintRecord) * N);
    ASSERT_TRUE(arr != NULL, "Memory allocated for 1,000 items");

    // Fill pseudo-randomly
    for (int i = 0; i < N; i++) {
        char id[16];
        snprintf(id, sizeof(id), "CMP-%05d", (i * 37) % N); // Scrambled IDs
        int prio = (i % 4) + 1;
        char date[32];
        snprintf(date, sizeof(date), "2026-10-%02dT%02d:00:00Z", (i % 28) + 1, (i % 24));
        arr[i] = make_record(id, prio, date, "Stress test item");
    }

    AlgorithmMetrics sort_m;
    merge_sort(arr, N, SORT_BY_ID_ASC, &sort_m);

    // Verify sortedness
    for (int i = 0; i < N - 1; i++) {
        ASSERT_TRUE(strcmp(arr[i].complaint_id, arr[i + 1].complaint_id) <= 0, "Array must be sorted ascending by ID");
    }

    // Now test Binary Search vs Linear Search
    AlgorithmMetrics bin_m, lin_m;
    const char *target = arr[742].complaint_id;

    int bin_idx = binary_search_by_id(arr, N, target, &bin_m);
    int lin_idx = linear_search_by_id(arr, N, target, &lin_m);

    ASSERT_TRUE(bin_idx == 742, "Binary search found target at correct index");
    ASSERT_TRUE(lin_idx == 742, "Linear search found target at correct index");
    ASSERT_TRUE(bin_m.comparisons <= 11, "Binary search comparisons on N=1000 must be <= 11 (ceil(log2(1000)) = 10)");
    ASSERT_TRUE(lin_m.comparisons == 743, "Linear search should have examined exactly 743 items");

    free(arr);
    TEST_PASSED();
}

int main(void) {
    printf("====================================================\n");
    printf(" CAMPUS COMPLAINT TRACKER - DAA ALGORITHMS TEST SUITE\n");
    printf("====================================================\n\n");

    test_mergesort_edge_cases();
    test_mergesort_priority_and_tie_breaking();
    test_mergesort_reverse_and_sorted();
    test_binary_search();
    test_max_heap_operations();
    test_heap_build_bottom_up();
    test_large_dataset_and_search_comparison();

    printf("\n====================================================\n");
    printf(" RESULTS: %d / %d Tests Passed Successfully!\n", passed_tests, total_tests);
    printf("====================================================\n");

    return (passed_tests == total_tests) ? 0 : 1;
}
