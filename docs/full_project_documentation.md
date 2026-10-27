# Campus Complaint Tracker — Comprehensive Project Documentation
### Full-Stack Academic Web Application & Algorithmic Engineering Specification
**Course**: Design & Analysis of Algorithms (DAA)  
**Backend**: Pure C99 Native Engine | Mongoose Embedded HTTP | SQLite 3.53.3 Database  
**Frontend**: Modern HTML5 / Vanilla CSS3 / JavaScript (ES6+) / Chart.js  
**Test Suite**: 134 / 134 Tests Passed (100% Pass Rate)  
**Release**: v1.0.0 Production Ready  

---

## Executive Summary & Scope
The **Campus Complaint Tracker** is an academic collegiate facility grievance and emergency resolution platform. Developed specifically for empirical evaluation in **Design and Analysis of Algorithms (DAA)**, the platform replaces archaic paper logs and unindexed email threads with a high-performance native C backend. The system combines handcrafted DAA sorting and scheduling algorithms with persistent ACID SQLite storage and a modern 3D glassmorphic web dashboard.

---

## 1. What We Use: Complete Technology Stack & Tools

| Component | Technology / Tool | Version / Standard | Engineering Purpose & Justification |
| :--- | :--- | :--- | :--- |
| **Backend Language** | C Programming Language | ISO C99 | Deterministic memory governance, zero garbage collection latency, direct pointer arithmetic for DAA data structures, and raw hardware benchmark profiling. |
| **Compiler & Toolchain** | GCC (GNU Compiler Collection) | v16.1.0 (MSYS2 UCRT64) | Compiles native Windows x86_64 binary (`campus_server.exe`) linking `-lsqlite3`, `-lws2_32`, and `-lbcrypt`. |
| **Embedded Web Server** | Cesanta Mongoose | v7.14 | Asynchronous event-driven network engine. Handles HTTP request parsing, REST API dispatch, static asset delivery, and TCP socket lifecycle. |
| **Relational Database** | SQLite Embedded Engine | v3.53.3 C API | Serverless, zero-configuration ACID relational storage with foreign keys (`PRAGMA foreign_keys = ON;`), prepared statements, and WAL mode. |
| **JSON Serializer** | cJSON (Dave Gamble) | ANSI C | High-speed JSON serialization and deserialization in C. Parses incoming client payloads and constructs structured JSON responses with zero external dependencies. |
| **Cryptographic Security** | Salted Iterated SHA-256 + Win32 CryptoAPI | 5,000 Rounds Key Stretching | Uses `CryptGenRandom` for 16-byte cryptographically secure pseudorandom salts and 5,000-round iterated SHA-256 hashing to eliminate rainbow table vulnerabilities. |
| **Benchmarking Clock** | Win32 Performance Counter | `QueryPerformanceCounter` | Sub-microsecond timing accuracy to capture empirical runtimes of sorting and searching routines down to fractions of a microsecond. |
| **Frontend Architecture**| HTML5 + Vanilla JavaScript | Modern ES6+ Standards | Lightweight, modular JavaScript modules handle asynchronous REST communication (`fetch`), DOM updates, modal triage, and session storage. |
| **Visual Design System** | Vanilla CSS3 (Glassmorphism & 3D) | Custom Dark Theme | Deep dark cinematic palette (`#0B0F19` canvas, `#1E293B` cards, `#2DD4A7` emerald, `#5B8CFF` cyan accents) with smooth micro-interactions. |
| **Data Visualizations** | Chart.js | v4.4.1 (UMD Bundle) | Renders live interactive doughnut charts (status distribution), vertical bar charts (category breakdown), and trend curves in the Admin Console. |

---

## 2. How the System Works: Architectural Workflows

### 2.1 End-to-End Architectural Pipeline
```
[Client Web Browser (HTML5 / Vanilla JS / CSS3)]
                      |
                      | (HTTP / REST API with Bearer Tokens)
                      v
      [Mongoose Embedded HTTP Server (Port 8080)]
                      |
                      v
         [REST API Router (server.c)]
         /            |             \
        v             v              v
 [auth.c]      [algorithms.c]    [database.c]
(Salted SHA-256) (Merge Sort,      (Prepared Stmts,
(Sessions/RBAC)   Heap, Binary)    SQLite Driver)
                      |                  |
                      \------------------/
                                |
                                v
                [campus_complaints.db (SQLite WAL Mode)]
```

### 2.2 Core Subsystem Execution Flows

#### A. User Authentication & Session Security Flow
1. **Registration**: The student submits full name, roll number, department, email, and password. The backend checks format via regex, issues `CryptGenRandom` to create a unique 16-byte random salt, stretches the password through 5,000 iterative SHA-256 rounds, and persists the record into `users`.
2. **Login & Session Creation**: When credentials are submitted to `POST /api/auth/login`, the backend fetches the stored salt, computes the iterated hash, and compares it in constant time. Upon verification, it creates a 64-character hexadecimal cryptographically random session token stored in `sessions` with an explicit 24-hour expiration window.
3. **Role-Based Access Control (RBAC)**: Every protected endpoint extracts the `Authorization: Bearer <token>` header (or fallback session cookie). The server validates the token against SQLite, verifies expiration, and checks role permissions (rejecting unauthorized students from admin triage routes with HTTP 403 Forbidden).

#### B. Complaint Lifecycle & Automated Tracking Identifier
When a student submits a grievance, the backend assigns a deterministic, human-readable identifier formatted as: `CMP-<YYYY>-<XXXX>` (e.g., `CMP-2026-0001`).
- **Status Progression**: `Submitted` -> `Under Review` -> `In Progress` -> `Resolved` (or `Rejected`).
- **Transactional Audit Trail**: Every state change executed by an administrator triggers an atomic insert into the `complaint_history` table, capturing the previous status, new status, timestamp, administrator ID, and detailed official remarks. Students can view this live history timeline to know exactly who inspected their complaint and what action was taken.

#### C. Algorithmic In-Memory Processing Workflow
When an administrator views complaints, the backend executes pure C algorithmic procedures:
- **Merge Sort Execution**: Complaints are retrieved from the database into a contiguous heap-allocated array of `ComplaintRecord` structs. The handcrafted Merge Sort algorithm sorts them by priority, timestamp, or complaint ID with deterministic multi-level tie-breaking in $O(n \log n)$ time.
- **Priority Dispatch (Max-Heap)**: The active grievances are loaded into an in-memory binary Max-Heap using Floyd’s linear $\Theta(n)$ bottom-up heapification algorithm. The administrator can peek at the highest urgency emergency in $O(1)$ time and dispatch it in $O(\log n)$ extract-max time.
- **Binary Search Retrieval**: When looking up a complaint ID among sorted records, iterative Binary Search halves the search space at each iteration, completing in $O(\log n)$ time and $\Theta(1)$ auxiliary memory.

---

## 3. Complete System Functionality

### 3.1 Public Landing Portal (`public/index.html`)
- **Isometric 3D Card Stack**: Interactive 3D perspective cards highlighting key system pillars: Instant Unique Tracking IDs, Automated Priority Max-Heap Scheduling, Transparent Audit Timeline, and Provable DAA Algorithms.
- **Live Campus Aggregate Statistics**: Live REST polling of `GET /api/public/stats` displaying total campus complaints registered, issues actively in progress, resolved grievances, and microsecond-level algorithmic speed.
- **Seamless Navigation**: Direct links to Student Login, Staff Authentication, Complaint Tracking, and the DAA Benchmark Lab.

### 3.2 Secure Authentication Gateway (`public/auth.html`)
- **Dual-Mode Authentication Portal**: Single-page switching between Student/Admin Login and New Student Account Registration.
- **Demo Quick-Fill Chips**: Pre-seeded accounts can be populated with one click:
  - `admin@campus.edu` / `Admin@123` (Administrator)
  - `aarav.sharma@campus.edu` / `Student@123` (Student)
  - `priya.patel@campus.edu` / `Student@123` (Student)
- **Real-Time Input Validation**: Form validation with inline feedback for email formatting, password complexity, and required academic details.

### 3.3 Student Grievance Dashboard (`public/student.html`)
- **Interactive Ticket Submission Form**: Allows students to report grievances with structured categories (Hostel, Laboratory, Electricity, Wi-Fi / Network, Sanitation, Academic, Transport, Library), campus location, room number, urgency priority, and detailed descriptions.
- **Card-Based Grievance Grid**: Color-coded grievance cards representing severity (Critical = Red/Amber, High = Orange, Medium = Blue, Low = Green).
- **Audit Timeline Modal**: Clicking any complaint displays the complete lifecycle history, showing state transitions, timestamps, assigned technician departments, and administrator remarks.
- **Instant Client-Side Filtering**: Instant search and filtering by Category, Status, and Priority.

### 3.4 Administrator Triage Console (`public/admin.html`)
- **Max-Heap Urgency Queue**: Automatically highlights the highest priority grievance at the top of the queue using Max-Heap ordering ($O(1)$ peek).
- **Interactive Triage Modal**: Admins can adjust priority levels, reassign technician departments (e.g., Electrical Maintenance, Network Operations, Civil Works), update status (`In Progress`, `Resolved`, `Rejected`), and append official audit remarks.
- **Interactive Analytics Suite**: Embedded Chart.js visualizations showing complaint status breakdowns, department issue counts, and weekly resolution trends.
- **Administrative CSV Export**: One-click download of all campus grievance records as a sanitized CSV file for reporting.
- **Student User Directory**: Administrative table showing all registered users and their account statuses.

### 3.5 DAA Algorithm Benchmark Laboratory (`public/daa-lab.html`)
- **Live C Execution Sandbox**: Benchmarks pure C algorithm implementations against synthetic or live datasets ($N = 10$ to $N = 5,000$).
- **Algorithm Comparisons**:
  - Merge Sort vs. Quick Sort vs. Heap Sort (runtime in $\mu s$, comparison count, movement count, stability).
  - Iterative Binary Search vs. Linear Search across best, average, and worst cases.
  - Max-Heap Floyd's $\Theta(n)$ linear build vs. $O(n \log n)$ sequential insertions.
- **Big-O Empirical Visualization**: Dynamic Chart.js curve comparing measured hardware execution times against theoretical asymptotic curves.

---

## 4. DAA Mathematical Proofs & Theoretical Complexity

### 4.1 Complexity Matrix

| Algorithm | Routine | Best Case | Average Case | Worst Case | Auxiliary Space | Deterministic Stability |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Merge Sort** | `merge_sort_records()` | $\Theta(n \log n)$ | $\Theta(n \log n)$ | $\Theta(n \log n)$ | $\Theta(n)$ | Stable |
| **Binary Search** | `binary_search_records()` | $\Theta(1)$ | $\Theta(\log n)$ | $\Theta(\log n)$ | $\Theta(1)$ | N/A (Unique Keys) |
| **Max-Heap (Insert)** | `heap_insert()` | $\Theta(1)$ | $\Theta(\log n)$ | $\Theta(\log n)$ | $\Theta(1)$ | Dynamic Array |
| **Max-Heap (Extract)**| `heap_extract_max()` | $\Theta(\log n)$ | $\Theta(\log n)$ | $\Theta(\log n)$ | $\Theta(1)$ | Strict FIFO on Ties |
| **Max-Heap (Build)** | `heap_build_floyd()` | $\Theta(n)$ | $\Theta(n)$ | $\Theta(n)$ | $\Theta(1)$ | In-Place / Linear |
| **Linear Search** | `linear_search_records()`| $\Theta(1)$ | $\Theta(n)$ | $\Theta(n)$ | $\Theta(1)$ | Stable |

### 4.2 Merge Sort Derivation via Master Theorem
$$T(n) = 2T\left(\frac{n}{2}\right) + \Theta(n), \quad T(1) = \Theta(1)$$
Master Theorem: $a = 2, b = 2, f(n) = \Theta(n)$.  
Critical exponent: $\log_b a = \log_2 2 = 1 \implies f(n) = \Theta(n^1) = \Theta(n^{\log_b a})$.  
By Master Theorem Case 2:
$$T(n) = \Theta(n^{\log_b a} \log n) = \Theta(n \log n)$$

### 4.3 Floyd's Linear Time Heap Construction Proof
In a complete binary tree of height $h = \lfloor \log_2 n \rfloor$, there are at most $\lceil n / 2^{k+1} \rceil$ nodes at height $k$. A node at height $k$ can sift down at most $k$ levels:
$$S = \sum_{k=0}^{h} \frac{n}{2^{k+1}} O(k) = \frac{n}{2} \sum_{k=0}^{h} \frac{k}{2^k}$$
Using the infinite geometric series identity $\sum_{k=0}^{\infty} k x^k = \frac{x}{(1-x)^2}$ for $x = \frac{1}{2}$:
$$\sum_{k=0}^{\infty} k \left(\frac{1}{2}\right)^k = \frac{1/2}{(1 - 1/2)^2} = \frac{1/2}{1/4} = 2$$
$$S \le \frac{n}{2} \cdot 2 = n \implies T(n) = \Theta(n)$$

---

## 5. Relational Database Schema & Integrity

The SQLite database (`campus_complaints.db`) enforces relational referential integrity via:
`PRAGMA foreign_keys = ON;` and operates in high-performance Write-Ahead Logging (`PRAGMA journal_mode = WAL;`).

### Tables Overview
1. **`users`**: `user_id` (PK), `full_name`, `email` (UK), `student_id` (UK), `department`, `password_hash`, `salt`, `role` (`student`, `admin`), `account_status`, `created_at`, `updated_at`.
2. **`complaints`**: `complaint_id` (PK, `CMP-YYYY-XXXX`), `user_id` (FK), `title`, `category`, `description`, `location`, `room_number`, `priority` (1-4), `status` (`Submitted`, `Under Review`, `In Progress`, `Resolved`, `Rejected`), `assigned_department`, `created_at`, `updated_at`, `resolved_at`.
3. **`complaint_history`**: `history_id` (PK), `complaint_id` (FK), `changed_by` (FK), `old_status`, `new_status`, `remarks`, `created_at`.
4. **`sessions`**: `token` (PK, 64-char hex), `user_id` (FK), `expires_at`, `created_at`.

---

## 6. REST API Specification

| Method & Route | Auth Level | Payload / Parameters | Description |
| :--- | :--- | :--- | :--- |
| `POST /api/auth/register` | Public | `{ full_name, email, password, student_id, department }` | Registers student, hashes password with 5,000-round salt. |
| `POST /api/auth/login` | Public | `{ email, password }` | Authenticates credentials, creates 24-hr session token. |
| `POST /api/auth/logout` | Authenticated | None (Bearer Token) | Revokes active session token. |
| `GET /api/auth/me` | Authenticated | None | Returns active user profile and permissions. |
| `GET /api/complaints` | Authenticated | Query filters: `status`, `category`, `sort` | Lists student tickets (Student) or all campus tickets (Admin). |
| `POST /api/complaints` | Student / Admin | Complaint fields (title, category, priority, etc.) | Creates new complaint with deterministic `CMP-YYYY-XXXX` ID. |
| `GET /api/complaints/{id}` | Authenticated | Path parameter `id` | Retrieves complaint details and complete history audit timeline. |
| `PUT /api/complaints/{id}` | Admin Only | Status, priority, assigned dept, remarks | Triages ticket and inserts atomic audit history record. |
| `GET /api/admin/stats` | Admin Only | None | Administrative overview stats and category breakdowns. |
| `POST /api/daa/benchmark` | Public / DAA | `{ algorithm, size, dataset_type }` | Runs live C algorithm benchmark with microsecond timing. |
| `GET /api/public/stats` | Public | None | Aggregate counts for landing page dashboard. |

---

## 7. Verification & Test Suite Results (134 / 134 Passed)

- **Algorithm Unit Tests (`backend/tests/test_algorithms.c`)**: 104 / 104 Passed
  - Validated Merge Sort stability, multi-level tie breaking, binary search boundary conditions, heap invariants under 1,000 randomized operations, and Floyd's linear construction.
- **Integration & Persistence Tests (`backend/tests/test_api.c`)**: 30 / 30 Passed
  - Validated authentication, RBAC authorization boundaries, complaint creation, state progression, and **cold-reboot persistence** across process termination.

---

## 8. Compilation & Launch Instructions

### 8.1 Compile C99 Backend
```bash
cd c:\Users\vikas\Desktop\DAA_project
gcc -std=c99 -Wall -Wextra -O2 -Ibackend/include backend/src/main.c backend/src/server.c backend/src/database.c backend/src/auth.c backend/src/algorithms.c backend/src/cJSON.c backend/lib/mongoose.o -lsqlite3 -lws2_32 -lbcrypt -o campus_server.exe
```

### 8.2 Run the Server
```bash
.\campus_server.exe --port 8080
```

### 8.3 Web Endpoints
- **Landing Page**: `http://localhost:8080/`
- **Authentication**: `http://localhost:8080/auth.html`
- **Student Dashboard**: `http://localhost:8080/student.html`
- **Admin Console**: `http://localhost:8080/admin.html`
- **DAA Benchmark Lab**: `http://localhost:8080/daa-lab.html`

---
*Campus Complaint Tracker &bull; Academic DAA Project &bull; Prepared for Vikas*
