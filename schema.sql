-- Campus Complaint Tracker - SQLite Database Schema
-- Academic DAA Project

PRAGMA foreign_keys = ON;

-- Users Table (Students & Administrators)
CREATE TABLE IF NOT EXISTS users (
    user_id INTEGER PRIMARY KEY AUTOINCREMENT,
    full_name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE COLLATE NOCASE,
    student_id TEXT UNIQUE,
    department TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    salt TEXT NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('student', 'admin')),
    account_status TEXT NOT NULL DEFAULT 'active' CHECK(account_status IN ('active', 'inactive', 'suspended')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- Complaints Table
CREATE TABLE IF NOT EXISTS complaints (
    complaint_id TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    title TEXT NOT NULL,
    category TEXT NOT NULL,
    description TEXT NOT NULL,
    location TEXT NOT NULL,
    room_number TEXT,
    priority INTEGER NOT NULL CHECK(priority BETWEEN 1 AND 4),
    status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending', 'in_progress', 'resolved', 'rejected')),
    assigned_department TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    resolved_at TEXT
);

-- Complaint Audit & Status History Timeline
CREATE TABLE IF NOT EXISTS complaint_history (
    history_id INTEGER PRIMARY KEY AUTOINCREMENT,
    complaint_id TEXT NOT NULL REFERENCES complaints(complaint_id) ON DELETE CASCADE,
    changed_by INTEGER NOT NULL REFERENCES users(user_id),
    old_status TEXT,
    new_status TEXT NOT NULL,
    remarks TEXT,
    created_at TEXT NOT NULL
);

-- Authenticated Sessions
CREATE TABLE IF NOT EXISTS sessions (
    token TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    expires_at INTEGER NOT NULL,
    created_at INTEGER NOT NULL
);

-- Rate Limiting Tracker
CREATE TABLE IF NOT EXISTS login_attempts (
    ip_address TEXT NOT NULL,
    attempt_time INTEGER NOT NULL
);

-- Strategic Indexes for DAA Comparison and High Performance
CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);
CREATE INDEX IF NOT EXISTS idx_users_role ON users(role);
CREATE INDEX IF NOT EXISTS idx_complaints_user_id ON complaints(user_id);
CREATE INDEX IF NOT EXISTS idx_complaints_status ON complaints(status);
CREATE INDEX IF NOT EXISTS idx_complaints_priority ON complaints(priority);
CREATE INDEX IF NOT EXISTS idx_complaints_category ON complaints(category);
CREATE INDEX IF NOT EXISTS idx_complaints_created_at ON complaints(created_at);
CREATE INDEX IF NOT EXISTS idx_history_complaint_id ON complaint_history(complaint_id);
CREATE INDEX IF NOT EXISTS idx_sessions_token ON sessions(token);
CREATE INDEX IF NOT EXISTS idx_sessions_expires_at ON sessions(expires_at);
