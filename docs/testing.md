# Test Suite & Verification Documentation
## Campus Complaint Tracker — Academic DAA Quality Assurance

### 1. Test Architecture Overview

The test harness is implemented natively in C with strict assertions (`assert.h`) and structured reporting across two primary suites:

1. **`backend/tests/test_algorithms.c`**: Pure algorithm correctness, mathematical boundary conditions, and invariant verification.
2. **`backend/tests/test_api.c`**: Database transactions, password hashing, session tokens, and reboot persistence.

```
+-------------------------------------------------------------+
|              Campus Complaint Tracker Test Harness          |
+-------------------------------------------------------------+
               |                               |
    [test_algorithms.c]                 [test_api.c]
    - Merge Sort (1000 items)           - DB Initialization
    - Multi-key Tie-Breaking            - Admin/Student Creation
    - Binary Search (log n bounds)      - Salted Password Hashing
    - Ground Truth Equivalence          - Unique ID CMP-YYYY-XXXX
    - Max-Heap Invariant                - Transactional History Audit
    - Floyd's O(n) Heap Construction    - Persistence Across Reboots
    (104 / 104 Passed)                  (30 / 30 Passed)
```

---

### 2. DAA Algorithm Unit Tests (`test_algorithms.c`)

#### Test Cases Covered
1. **Empty and Single Element Handling**:
   - `merge_sort(NULL, 0, ...)` safely completes without memory faults.
   - `merge_sort(single, 1, ...)` preserves item without modification.
2. **Multi-Key Deterministic Tie-Breaking**:
   - Verifies that two Critical (P4) complaints are deterministically ordered by their creation timestamp (FIFO principle).
3. **Large Dataset Correctness (1,000 items)**:
   - Validates that all adjacent items satisfy $arr[i] \le arr[i+1]$ after sorting.
4. **Binary Search Boundaries**:
   - Element found at index $0$, middle, and end.
   - Target missing below range, above range, and between existing keys.
   - Strict log comparison bound: comparisons $\le \lceil \log_2 n \rceil + 1$.
5. **Binary Search vs Linear Search Ground Truth**:
   - Validates that for 50 randomized search queries across 500 items, `binary_search_by_id` returns the exact same index as `linear_search_by_id`.
6. **Max-Heap Priority Queue Invariant**:
   - `heap_peek` returns root in $O(1)$ time.
   - `heap_extract_max` maintains priority monotonic non-increasing sequence.
   - Empty heap extract safely returns `false`.
   - Floyd's $O(n)$ `heap_build` satisfies heap order for 500 random elements.

**Result**: `104 / 104 tests passed successfully! (0 failures)`

---

### 3. Backend Integration & Persistence Tests (`test_api.c`)

#### Test Cases Covered
1. **SQLite Database Initialization**:
   - Creates tables, enables foreign keys (`PRAGMA foreign_keys = ON;`) and WAL mode.
2. **Default Administrator Auto-Creation**:
   - Automatically bootstraps `admin@campus.edu` with hashed password `Admin@123`.
3. **Password Hashing & Verification**:
   - Salted SHA-256 key stretching accepts correct password and rejects invalid attempts.
4. **Student Creation & Unique Constraint**:
   - Successfully registers a student and prevents duplicate email registration ($409$ conflict).
5. **Session Management**:
   - Generates 64-character crypto token, resolves user ID, and invalidates on logout.
6. **Complaint ID Generation**:
   - Verifies format `CMP-YYYY-XXXX` (e.g. `CMP-2026-0001`).
7. **Transactional Status History**:
   - When updating status, complaint and history table are updated within the same transaction.
8. **Cold Reboot Persistence**:
   - Closes the SQLite database (`db_close()`), opens a fresh connection, and verifies all complaints, priorities, and audit timelines persist unchanged.

**Result**: `30 / 30 integration tests passed successfully! (0 failures)`

---

### 4. How to Execute Tests

#### Using Windows PowerShell:
```powershell
# Run both algorithm and integration test suites
.\build.ps1 -Action test

# Or build entire application and run all tests
.\build.ps1 -Action all
```

#### Using GNU Make:
```bash
make test_algorithms
make test_api
```
