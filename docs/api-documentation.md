# REST API Documentation
## Campus Complaint Tracker — C Backend API

All API endpoints return standard JSON responses adhering to this schema:
```json
{
  "success": true,
  "data": {},
  "message": "Operation completed description"
}
```

---

### 1. Authentication Endpoints

#### `POST /api/auth/register`
Creates a new student account and automatically starts an authenticated session.
- **Request Body**:
  ```json
  {
    "full_name": "Aarav Sharma",
    "email": "aarav@campus.edu",
    "student_id": "CS2025-042",
    "department": "Computer Science & Engineering",
    "password": "Password123"
  }
  ```
- **Response**: `201 Created`
  ```json
  {
    "success": true,
    "message": "Registration successful",
    "data": {
      "token": "64_char_hex_session_token",
      "user": {
        "user_id": 2,
        "full_name": "Aarav Sharma",
        "email": "aarav@campus.edu",
        "role": "student"
      }
    }
  }
  ```
- **Headers Set**: `Set-Cookie: session_id=...; Path=/; HttpOnly; SameSite=Lax`

#### `POST /api/auth/login`
Authenticates a user with email and password.
- **Request Body**:
  ```json
  {
    "email": "admin@campus.edu",
    "password": "Admin@123"
  }
  ```
- **Response**: `200 OK`

#### `POST /api/auth/logout`
Invalidates the active session token in SQLite and clears the cookie.
- **Response**: `200 OK`

#### `GET /api/auth/me`
Retrieves the currently authenticated user's profile.
- **Headers Required**: `Authorization: Bearer <token>` or `Cookie: session_id=<token>`
- **Response**: `200 OK`

#### `POST /api/auth/change-password`
Updates the authenticated user's password.
- **Request Body**:
  ```json
  {
    "old_password": "Admin@123",
    "new_password": "NewSecretPassword@123"
  }
  ```

#### `POST /api/auth/reset-dev`
Local development password reset workflow without external email SMTP delivery.
- **Request Body**:
  ```json
  {
    "email": "student1@campus.edu",
    "new_password": "StudentNewPass@123"
  }
  ```

---

### 2. Student Endpoints

#### `GET /api/student/dashboard`
Fetches personalized metrics for the logged-in student (total, pending, in-progress, resolved, and recent complaints).

#### `GET /api/student/complaints`
Retrieves all complaints authored by the authenticated student.

#### `POST /api/student/complaints`
Registers a new campus facility issue.
- **Request Body**:
  ```json
  {
    "title": "Water leakage in 3rd floor washroom",
    "category": "Water Supply",
    "location": "Hostel Block A",
    "room_number": "Washroom 3B",
    "priority": 3,
    "description": "High pressure pipe leaking water onto corridor."
  }
  ```
- **Response**: `201 Created` (Returns generated `complaint_id` e.g. `CMP-2026-0012`).

#### `GET /api/student/complaints/:id`
Retrieves full details of a complaint, including its transactional audit timeline history.

#### `PATCH /api/student/profile`
Updates full name and department for the authenticated student.

---

### 3. Administrator Endpoints

#### `GET /api/admin/dashboard`
Returns high-level aggregate KPIs across the entire campus, including critical emergency counts and urgent tickets.

#### `GET /api/admin/complaints`
Retrieves all campus complaints across all students with full relational data.

#### `PATCH /api/admin/complaints/:id/status`
Updates status and writes to `complaint_history` in an atomic SQLite transaction.
- **Request Body**:
  ```json
  {
    "status": "in_progress",
    "remarks": "Assigned technician Rajesh from plumbing unit."
  }
  ```

#### `PATCH /api/admin/complaints/:id/priority`
Updates authoritative priority level ($1$ to $4$) and logs audit remarks.
- **Request Body**:
  ```json
  {
    "priority": 4,
    "remarks": "Escalated to Critical due to electrical fire hazard."
  }
  ```

#### `GET /api/admin/users`
Retrieves registered student and staff accounts.

#### `GET /api/admin/analytics`
Returns aggregate distributions by category, status, and priority, plus average resolution hours.

#### `GET /api/admin/export.csv`
Downloads compliant CSV file containing all complaint records.
- **Headers**: `Content-Type: text/csv`, `Content-Disposition: attachment; filename="campus_complaints.csv"`

---

### 4. DAA Algorithm Benchmarking Endpoints

#### `GET /api/algorithms`
Returns theoretical time and space complexity metadata for all 5 algorithms.

#### `POST /api/algorithms/sort-demo`
Executes handcrafted C `merge_sort` on dynamic dataset.
- **Request Body**:
  ```json
  {
    "size": 1000,
    "sort_by": "priority"
  }
  ```
- **Response**:
  ```json
  {
    "success": true,
    "data": {
      "algorithm": "Merge Sort",
      "input_size": 1000,
      "comparisons": 8684,
      "movements": 19952,
      "duration_microseconds": 382.1,
      "time_complexity": "O(n log n)",
      "space_complexity": "O(n)",
      "before_sample": [...],
      "after_sample": [...]
    }
  }
  ```

#### `POST /api/algorithms/search-demo`
Runs side-by-side comparison between C `binary_search_by_id` and C `linear_search_by_id`.
- **Request Body**:
  ```json
  {
    "size": 1000,
    "target_id": "CMP-2026-0500"
  }
  ```

#### `POST /api/algorithms/priority-demo`
Runs C `heap_build` (Floyd's algorithm) and extracts top urgent elements.
- **Request Body**:
  ```json
  {
    "size": 100
  }
  ```

---

### 5. Health Check

#### `GET /api/health`
- **Response**: `200 OK`
  ```json
  {
    "success": true,
    "data": {
      "status": "UP",
      "database": "SQLite 3.53.3",
      "architecture": "C Embedded Server",
      "version": "1.0.0"
    }
  }
  ```
