# System Architecture Specification
## Campus Complaint Tracker — Full-Stack DAA Web Application

### 1. Architectural Overview

The **Campus Complaint Tracker** is engineered as a monolithic, high-performance, embedded C web system with a responsive vanilla web frontend and SQLite database. The application is specifically designed to demonstrate **Design and Analysis of Algorithms (DAA)** academic principles in a real-world campus facility grievance workflow.

```mermaid
graph TD
    subgraph Client["Client Browser Layer (HTML5 / Vanilla JS / CSS3)"]
        UI_Land["Landing Page (index.html)"]
        UI_Auth["Authentication Portal (auth.html)"]
        UI_Student["Student Dashboard (student.html)"]
        UI_Admin["Admin Console (admin.html)"]
        UI_DAA["DAA Laboratory (daa-lab.html)"]
        UI_Chart["Chart.js Data Visualizations"]
    end

    subgraph Server["C Application Server (campus_server.exe)"]
        HTTP_Engine["Mongoose Embedded HTTP Engine (v7.14)"]
        Router["REST API Router & JSON Controller (server.c)"]
        Auth_Engine["Crypto & Session Manager (auth.c)"]
        DAA_Engine["Handcrafted DAA Algorithms (algorithms.c)"]
        DB_Adapter["SQLite Prepared Statements Driver (database.c)"]
    end

    subgraph Storage["Persistent Storage Layer (SQLite WAL)"]
        T_Users["users Table"]
        T_Complaints["complaints Table"]
        T_History["complaint_history Audit Table"]
        T_Sessions["sessions Table"]
        Indexes["B-Tree Indexes"]
    end

    UI_Land -->|HTTP GET/POST| HTTP_Engine
    UI_Auth -->|JSON Auth| HTTP_Engine
    UI_Student -->|JSON REST| HTTP_Engine
    UI_Admin -->|JSON REST / CSV| HTTP_Engine
    UI_DAA -->|Benchmark REST| HTTP_Engine

    HTTP_Engine --> Router
    Router --> Auth_Engine
    Router --> DAA_Engine
    Router --> DB_Adapter

    Auth_Engine --> DB_Adapter
    DAA_Engine -.->|In-Memory Execution| Router
    DB_Adapter --> T_Users
    DB_Adapter --> T_Complaints
    DB_Adapter --> T_History
    DB_Adapter --> T_Sessions
    DB_Adapter --> Indexes
```

---

### 2. Core Subsystems

#### 2.1 C Embedded Web Server (`backend/src/server.c`)
- **Library**: Mongoose Embedded C Network Library (v7.14).
- **Socket Driver**: Windows Sockets 2 (`ws2_32`).
- **Static Assets Serving**: Static handler delivering `HTML`, `CSS`, `JS`, and minified libraries from `public/`.
- **JSON Serialization**: Powered by `cJSON.c` for standard JSON parsing and generation.
- **CORS Handling**: Automatic `OPTIONS` preflight response with proper `Access-Control-Allow-*` headers.

#### 2.2 DAA Algorithms Module (`backend/src/algorithms.c`)
Manual implementations of academic algorithms with hardware timer instrumentation:
1. **Merge Sort**: Deterministic sorting with multi-level tie-breaking. Time: $O(n \log n)$, Space: $O(n)$.
2. **Binary Search**: Iterative search on sorted complaint IDs. Time: $O(\log n)$, Space: $O(1)$.
3. **Max-Heap Priority Queue**: Dynamic binary heap for urgent dispatching. Insert/Extract: $O(\log n)$, Floyd's Build: $O(n)$, Peek: $O(1)$.
4. **Linear Search**: Baseline ground-truth scan. Time: $O(n)$, Space: $O(1)$.
5. **High-Precision Timing**: Utilizes Windows `QueryPerformanceCounter` & `QueryPerformanceFrequency` for accurate sub-microsecond ($\mu s$) timing.

#### 2.3 Authentication & Cryptography (`backend/src/auth.c`)
- **Password Security**: Salted key stretching using 5,000 iterations of SHA-256 (PBKDF2 equivalent).
- **Salt Generation**: 128-bit cryptographically secure pseudo-random salt generated via Windows CryptoAPI (`CryptGenRandom`).
- **Timing Attack Prevention**: Constant-time string comparison (`constant_time_cmp`) across computed and stored hashes.
- **Session Tokens**: 256-bit cryptographically random tokens stored in SQLite with sliding expiration.
- **Rate Limiting**: Sliding time window tracking failed login attempts per IP address.

#### 2.4 Database Access Layer (`backend/src/database.c`)
- **Database Engine**: SQLite 3.53.3 C API.
- **Integrity**: Foreign key enforcement enabled (`PRAGMA foreign_keys = ON;`).
- **Concurrency**: Write-Ahead Logging (`PRAGMA journal_mode = WAL;`) for concurrent read access.
- **SQL Injection Elimination**: 100% of user-supplied variables are bound via parameterized SQLite prepared statements (`sqlite3_prepare_v2`, `sqlite3_bind_*`).
- **ACID Transactions**: Status and priority updates execute atomic two-table transactions (updating `complaints` and creating an immutable entry in `complaint_history`).

---

### 3. Workflows & Sequence Diagrams

#### 3.1 Student Complaint Submission Workflow

```mermaid
sequenceDiagram
    autonumber
    actor Student
    participant Browser as Frontend (student.js)
    participant Server as C Backend (server.c)
    participant DB as SQLite Engine (database.c)
    participant Heap as C Max-Heap Queue

    Student->>Browser: Fill title, category, location, priority suggestion
    Browser->>Browser: Client-side validation
    Browser->>Server: POST /api/student/complaints (Bearer Token / Cookie)
    Server->>Server: Verify Student Session & Permissions
    Server->>DB: Atomically query MAX(complaint_id) & generate CMP-2026-XXXX
    Server->>DB: BEGIN TRANSACTION
    Server->>DB: INSERT INTO complaints (...)
    Server->>DB: INSERT INTO complaint_history (status: 'pending', 'Submitted by student')
    Server->>DB: COMMIT
    Server->>Heap: heap_insert(complaint) [O(log n)]
    Server-->>Browser: HTTP 201 Created {"success": true, "complaint_id": "CMP-2026-0005"}
    Browser-->>Student: Display Success Modal & Update KPI Dashboard
```

#### 3.2 Administrator Complaint Resolution Workflow

```mermaid
sequenceDiagram
    autonumber
    actor Admin
    participant Browser as Frontend (admin.js)
    participant Server as C Backend (server.c)
    participant DB as SQLite Engine (database.c)

    Admin->>Browser: Open Manage Complaints Tab
    Browser->>Server: GET /api/admin/complaints
    Server->>DB: SELECT * FROM complaints ORDER BY created_at DESC
    DB-->>Server: Array of Complaints
    Server-->>Browser: JSON Array
    Admin->>Browser: Select Complaint & change status to 'resolved' with remarks
    Browser->>Server: PATCH /api/admin/complaints/CMP-2026-0005/status
    Server->>Server: Verify Admin Role in Session
    Server->>DB: BEGIN TRANSACTION
    Server->>DB: UPDATE complaints SET status = 'resolved', resolved_at = UTC_NOW
    Server->>DB: INSERT INTO complaint_history (old_status, new_status, remarks, changed_by)
    Server->>DB: COMMIT
    Server-->>Browser: HTTP 200 OK {"success": true}
    Browser-->>Admin: Show Success Toast & Refresh Table
```

---

### 4. Hardware and Performance Characteristics

| Component | Technology | Performance Metric |
| :--- | :--- | :--- |
| **C Compiler** | GCC 16.1.0 (MSYS2 UCRT64 64-bit) | Native compiled x86_64 binary |
| **HTTP Engine** | Mongoose v7.14 Event-Driven Poll | Non-blocking async event loop |
| **Sorting Engine** | Handcrafted C Merge Sort | $\approx 380\ \mu s$ for 1,000 complaints |
| **Search Engine** | Iterative C Binary Search | $\approx 0.35\ \mu s$ ($< 11$ comparisons for 1,000 items) |
| **Priority Queue**| Contiguous Dynamic Max-Heap | $\approx 165\ \mu s$ Floyd construction for 1,000 items |
| **Database** | SQLite 3.53.3 WAL Mode | Parameterized, sub-millisecond query execution |
