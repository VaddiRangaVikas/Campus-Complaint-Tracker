#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/algorithms.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        printf("[FAIL] Line %d: %s\n", __LINE__, msg); \
        assert(cond); \
    } \
} while(0)

static void test_merge_sort(void)
{
    printf("--> Testing Merge Sort...\n");
    AlgorithmMetrics m;

    // 1. Empty array
    merge_sort(NULL, 0, SORT_BY_ID_ASC, &m);
    TEST_ASSERT(m.comparisons == 0, "Merge sort handles NULL array");

    // 2. Single element
    ComplaintRecord single[1];
    strcpy(single[0].complaint_id, "CMP-001");
    single[0].priority = 3;
    strcpy(single[0].created_at, "2026-10-26T10:00:00Z");
    merge_sort(single, 1, SORT_BY_ID_ASC, &m);
    TEST_ASSERT(strcmp(single[0].complaint_id, "CMP-001") == 0, "Single element unchanged");

    // 3. Unsorted array by ID
    ComplaintRecord arr[5] = {
        {.complaint_id = "CMP-005", .priority = 2, .created_at = "2026-10-26T12:00:00Z"},
        {.complaint_id = "CMP-001", .priority = 4, .created_at = "2026-10-26T10:00:00Z"},
        {.complaint_id = "CMP-004", .priority = 1, .created_at = "2026-10-26T11:00:00Z"},
        {.complaint_id = "CMP-002", .priority = 3, .created_at = "2026-10-26T10:30:00Z"},
        {.complaint_id = "CMP-003", .priority = 4, .created_at = "2026-10-26T09:00:00Z"}
    };

    merge_sort(arr, 5, SORT_BY_ID_ASC, &m);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(strcmp(arr[i].complaint_id, arr[i+1].complaint_id) < 0, "Array sorted ascending by ID");
    }

    // 4. Sort by Priority with Deterministic Tie-Breaking
    // arr currently has: CMP-001 (pri 4, 10:00), CMP-002 (pri 3, 10:30), CMP-003 (pri 4, 09:00), CMP-004 (pri 1), CMP-005 (pri 2)
    // Priority 4 items: CMP-003 (09:00) vs CMP-001 (10:00). CMP-003 was created earlier, so it must come first!
    merge_sort(arr, 5, SORT_BY_PRIORITY_DESC, &m);
    TEST_ASSERT(strcmp(arr[0].complaint_id, "CMP-003") == 0, "Priority 4 with earlier timestamp is first");
    TEST_ASSERT(strcmp(arr[1].complaint_id, "CMP-001") == 0, "Priority 4 with later timestamp is second");
    TEST_ASSERT(arr[2].priority == 3, "Priority 3 is third");
    TEST_ASSERT(arr[3].priority == 2, "Priority 2 is fourth");
    TEST_ASSERT(arr[4].priority == 1, "Priority 1 is fifth");

    // 5. Large array of 1,000 items
    int n_large = 1000;
    ComplaintRecord *large = (ComplaintRecord *)malloc(n_large * sizeof(ComplaintRecord));
    for (int i = 0; i < n_large; i++) {
        snprintf(large[i].complaint_id, sizeof(large[i].complaint_id), "CMP-%04d", (n_large - i));
        large[i].priority = (i % 4) + 1;
        snprintf(large[i].created_at, sizeof(large[i].created_at), "2026-10-%02dT10:00:00Z", (i % 28) + 1);
    }

    merge_sort(large, n_large, SORT_BY_ID_ASC, &m);
    bool sorted_ok = true;
    for (int i = 0; i < n_large - 1; i++) {
        if (strcmp(large[i].complaint_id, large[i+1].complaint_id) > 0) {
            sorted_ok = false;
            break;
        }
    }
    TEST_ASSERT(sorted_ok == true, "1,000 items sorted in O(n log n)");
    TEST_ASSERT(m.comparisons > 0, "Comparisons counted");
    TEST_ASSERT(m.movements > 0, "Movements counted");
    free(large);

    printf("   [PASS] Merge Sort passed all assertions!\n");
}

static void test_binary_search(void)
{
    printf("--> Testing Binary Search...\n");
    AlgorithmMetrics m;

    // 1. Empty array
    int idx = binary_search_by_id(NULL, 0, "CMP-001", &m);
    TEST_ASSERT(idx == -1, "Empty array returns -1");

    // 2. Single item
    ComplaintRecord single[1];
    strcpy(single[0].complaint_id, "CMP-007");
    TEST_ASSERT(binary_search_by_id(single, 1, "CMP-007", &m) == 0, "Single item found");
    TEST_ASSERT(binary_search_by_id(single, 1, "CMP-001", &m) == -1, "Missing item returns -1");

    // 3. Array of sorted IDs
    ComplaintRecord arr[10];
    for (int i = 0; i < 10; i++) {
        snprintf(arr[i].complaint_id, sizeof(arr[i].complaint_id), "CMP-%03d", (i + 1) * 10);
    }

    // Verify finding each element
    for (int i = 0; i < 10; i++) {
        char target[32];
        snprintf(target, sizeof(target), "CMP-%03d", (i + 1) * 10);
        int res = binary_search_by_id(arr, 10, target, &m);
        TEST_ASSERT(res == i, "Binary search found correct index");
        TEST_ASSERT(m.comparisons <= 4, "Binary search comparison bound O(log n) satisfied");
    }

    // Check missing elements
    TEST_ASSERT(binary_search_by_id(arr, 10, "CMP-005", &m) == -1, "Before range");
    TEST_ASSERT(binary_search_by_id(arr, 10, "CMP-150", &m) == -1, "After range");
    TEST_ASSERT(binary_search_by_id(arr, 10, "CMP-025", &m) == -1, "Between elements");

    // 4. Ground truth equivalence with linear search on 500 items
    int n = 500;
    ComplaintRecord *items = (ComplaintRecord *)malloc(n * sizeof(ComplaintRecord));
    for (int i = 0; i < n; i++) {
        snprintf(items[i].complaint_id, sizeof(items[i].complaint_id), "CMP-%04d", (i + 1) * 2);
    }

    for (int q = 0; q < 50; q++) {
        char test_id[32];
        snprintf(test_id, sizeof(test_id), "CMP-%04d", rand() % (n * 2 + 10));
        AlgorithmMetrics bm, lm;
        int b_res = binary_search_by_id(items, n, test_id, &bm);
        int l_res = linear_search_by_id(items, n, test_id, &lm);
        TEST_ASSERT(b_res == l_res, "Binary search identical to linear search ground truth");
    }
    free(items);

    printf("   [PASS] Binary Search passed all assertions!\n");
}

static void test_max_heap(void)
{
    printf("--> Testing Max-Heap Priority Queue...\n");

    ComplaintHeap *h = heap_create(4);
    TEST_ASSERT(h != NULL, "Heap created");
    TEST_ASSERT(h->size == 0, "Initial size 0");

    ComplaintRecord out;
    TEST_ASSERT(heap_peek(h, &out) == false, "Peek on empty heap is false");
    TEST_ASSERT(heap_extract_max(h, &out) == false, "Extract on empty heap is false");

    // Insert 4 items with distinct priorities
    ComplaintRecord c1 = {.complaint_id = "CMP-001", .priority = 1, .created_at = "2026-10-26T10:00:00Z"};
    ComplaintRecord c2 = {.complaint_id = "CMP-002", .priority = 4, .created_at = "2026-10-26T11:00:00Z"};
    ComplaintRecord c3 = {.complaint_id = "CMP-003", .priority = 3, .created_at = "2026-10-26T09:00:00Z"};
    ComplaintRecord c4 = {.complaint_id = "CMP-004", .priority = 4, .created_at = "2026-10-26T08:00:00Z"}; // earlier pri 4

    heap_insert(h, &c1);
    heap_insert(h, &c2);
    heap_insert(h, &c3);
    heap_insert(h, &c4);

    TEST_ASSERT(h->size == 4, "Heap size is 4");

    // Peek should give CMP-004 (pri 4, earlier timestamp 08:00)
    TEST_ASSERT(heap_peek(h, &out) == true, "Peek succeeds");
    TEST_ASSERT(strcmp(out.complaint_id, "CMP-004") == 0, "Highest priority item peeked");

    // Extract order: CMP-004, CMP-002, CMP-003, CMP-001
    heap_extract_max(h, &out);
    TEST_ASSERT(strcmp(out.complaint_id, "CMP-004") == 0, "1st extraction");
    heap_extract_max(h, &out);
    TEST_ASSERT(strcmp(out.complaint_id, "CMP-002") == 0, "2nd extraction");
    heap_extract_max(h, &out);
    TEST_ASSERT(strcmp(out.complaint_id, "CMP-003") == 0, "3rd extraction");
    heap_extract_max(h, &out);
    TEST_ASSERT(strcmp(out.complaint_id, "CMP-001") == 0, "4th extraction");
    TEST_ASSERT(h->size == 0, "Heap empty after extracts");

    heap_free(h);

    // Test Floyd's O(n) heap_build with 500 items
    int n = 500;
    ComplaintRecord *items = (ComplaintRecord *)malloc(n * sizeof(ComplaintRecord));
    for (int i = 0; i < n; i++) {
        snprintf(items[i].complaint_id, sizeof(items[i].complaint_id), "CMP-%04d", i + 1);
        items[i].priority = (rand() % 4) + 1;
        snprintf(items[i].created_at, sizeof(items[i].created_at), "2026-10-%02dT%02d:00:00Z", (i % 20) + 1, (i % 24));
    }

    ComplaintHeap *bh = heap_create(n);
    heap_build(bh, items, n);
    TEST_ASSERT(bh->size == n, "Heap built with n items");

    // Verify priority queue extraction invariant
    ComplaintRecord prev;
    heap_extract_max(bh, &prev);
    bool heap_property_held = true;

    for (int i = 1; i < n; i++) {
        ComplaintRecord curr;
        heap_extract_max(bh, &curr);
        if (compare_priority(&prev, &curr) < 0) {
            heap_property_held = false;
            break;
        }
        prev = curr;
    }
    TEST_ASSERT(heap_property_held == true, "Heap extracted in strictly monotonic priority order");

    heap_free(bh);
    free(items);

    printf("   [PASS] Max-Heap passed all assertions!\n");
}

int main(void)
{
    printf("==================================================\n");
    printf(" CAMPUS COMPLAINT TRACKER - DAA TEST SUITE \n");
    printf("==================================================\n");

    test_merge_sort();
    test_binary_search();
    test_max_heap();

    printf("\n==================================================\n");
    printf(" RESULTS: %d / %d tests passed successfully! \n", g_tests_passed, g_tests_run);
    printf("==================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
