-- Campus Complaint Tracker Database Schema
-- DAA Academic Project

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- Users Table
CREATE TABLE IF NOT EXISTS users (
    user_id INTEGER PRIMARY KEY AUTOINCREMENT,
    full_name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE COLLATE NOCASE,
    student_id TEXT UNIQUE COLLATE NOCASE,
    department TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('student', 'admin')),
    account_status TEXT NOT NULL DEFAULT 'active' CHECK(account_status IN ('active', 'suspended')),
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now'))
);

-- Complaints Table
CREATE TABLE IF NOT EXISTS complaints (
    complaint_id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    title TEXT NOT NULL,
    category TEXT NOT NULL CHECK(category IN (
        'Hostel', 'Classroom', 'Electricity', 'Water Supply',
        'Internet and Wi-Fi', 'Cleanliness', 'Library',
        'Laboratory', 'Transport', 'Canteen', 'Other'
    )),
    description TEXT NOT NULL,
    location TEXT NOT NULL,
    room_number TEXT,
    priority INTEGER NOT NULL DEFAULT 2 CHECK(priority IN (1, 2, 3, 4)),
    status TEXT NOT NULL DEFAULT 'Submitted' CHECK(status IN (
        'Submitted', 'Pending', 'In Progress', 'Resolved', 'Rejected'
    )),
    assigned_department TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now')),
    resolved_at TEXT,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
);

-- Complaint History & Audit Trail Table
CREATE TABLE IF NOT EXISTS complaint_history (
    history_id INTEGER PRIMARY KEY AUTOINCREMENT,
    complaint_id INTEGER NOT NULL,
    changed_by INTEGER NOT NULL,
    old_status TEXT NOT NULL,
    new_status TEXT NOT NULL,
    remarks TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now')),
    FOREIGN KEY (complaint_id) REFERENCES complaints(complaint_id) ON DELETE CASCADE,
    FOREIGN KEY (changed_by) REFERENCES users(user_id)
);

-- Active Server Sessions Table
CREATE TABLE IF NOT EXISTS sessions (
    session_id TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL,
    role TEXT NOT NULL,
    expires_at INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now')),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
);

-- Strategic Indexes for Fast DAA / DB Query Performance
CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);
CREATE INDEX IF NOT EXISTS idx_complaints_user ON complaints(user_id);
CREATE INDEX IF NOT EXISTS idx_complaints_status ON complaints(status);
CREATE INDEX IF NOT EXISTS idx_complaints_category ON complaints(category);
CREATE INDEX IF NOT EXISTS idx_complaints_priority ON complaints(priority);
CREATE INDEX IF NOT EXISTS idx_complaints_created ON complaints(created_at);
CREATE INDEX IF NOT EXISTS idx_history_complaint ON complaint_history(complaint_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);
