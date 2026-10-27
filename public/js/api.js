/**
 * Campus Complaint Tracker - Frontend API Client & State Manager
 */

const API = {
    BASE_URL: '',

    // Token management
    getToken() {
        return localStorage.getItem('cct_session_token') || '';
    },

    setToken(token) {
        if (token) {
            localStorage.setItem('cct_session_token', token);
        } else {
            localStorage.removeItem('cct_session_token');
        }
    },

    getUser() {
        try {
            return JSON.parse(localStorage.getItem('cct_user') || 'null');
        } catch (e) {
            return null;
        }
    },

    setUser(user) {
        if (user) {
            localStorage.setItem('cct_user', JSON.stringify(user));
        } else {
            localStorage.removeItem('cct_user');
        }
    },

    clearAuth() {
        localStorage.removeItem('cct_session_token');
        localStorage.removeItem('cct_user');
    },

    // HTTP Request helper
    async request(endpoint, options = {}) {
        const url = `${this.BASE_URL}${endpoint}`;
        const headers = {
            'Content-Type': 'application/json',
            ...(options.headers || {})
        };

        const token = this.getToken();
        if (token) {
            headers['Authorization'] = `Bearer ${token}`;
        }

        try {
            const response = await fetch(url, {
                ...options,
                headers,
                credentials: 'same-origin'
            });

            if (response.status === 401 && !endpoint.includes('/api/auth/')) {
                this.clearAuth();
                window.location.href = '/auth.html';
                return null;
            }

            const data = await response.json();
            if (!response.ok || !data.success) {
                throw new Error(data.message || `Request failed with status ${response.status}`);
            }

            return data;
        } catch (err) {
            console.error(`[API Error] ${endpoint}:`, err);
            throw err;
        }
    },

    // --- Authentication ---
    async login(email, password) {
        const res = await this.request('/api/auth/login', {
            method: 'POST',
            body: JSON.stringify({ email, password })
        });
        if (res.data?.token) {
            this.setToken(res.data.token);
            this.setUser(res.data.user);
        }
        return res;
    },

    async register(userData) {
        const res = await this.request('/api/auth/register', {
            method: 'POST',
            body: JSON.stringify(userData)
        });
        if (res.data?.token) {
            this.setToken(res.data.token);
            this.setUser(res.data.user);
        }
        return res;
    },

    async logout() {
        try {
            await this.request('/api/auth/logout', { method: 'POST' });
        } catch (e) {
            // ignore error on logout
        } finally {
            this.clearAuth();
            window.location.href = '/auth.html';
        }
    },

    async getMe() {
        const res = await this.request('/api/auth/me');
        if (res.data) {
            this.setUser(res.data);
        }
        return res.data;
    },

    async changePassword(old_password, new_password) {
        return await this.request('/api/auth/change-password', {
            method: 'POST',
            body: JSON.stringify({ old_password, new_password })
        });
    },

    async resetPasswordDev(email, new_password) {
        return await this.request('/api/auth/reset-dev', {
            method: 'POST',
            body: JSON.stringify({ email, new_password })
        });
    },

    // --- Student ---
    async getStudentDashboard() {
        return await this.request('/api/student/dashboard');
    },

    async getStudentComplaints() {
        return await this.request('/api/student/complaints');
    },

    async submitComplaint(data) {
        return await this.request('/api/student/complaints', {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },

    async getComplaintDetail(id) {
        return await this.request(`/api/student/complaints/${id}`);
    },

    async updateStudentProfile(data) {
        return await this.request('/api/student/profile', {
            method: 'PATCH',
            body: JSON.stringify(data)
        });
    },

    // --- Administrator ---
    async getAdminDashboard() {
        return await this.request('/api/admin/dashboard');
    },

    async getAdminComplaints() {
        return await this.request('/api/admin/complaints');
    },

    async updateComplaintStatus(id, status, remarks) {
        return await this.request(`/api/admin/complaints/${id}/status`, {
            method: 'PATCH',
            body: JSON.stringify({ status, remarks })
        });
    },

    async updateComplaintPriority(id, priority, remarks) {
        return await this.request(`/api/admin/complaints/${id}/priority`, {
            method: 'PATCH',
            body: JSON.stringify({ priority: parseInt(priority, 10), remarks })
        });
    },

    async addComplaintRemarks(id, remarks) {
        return await this.request(`/api/admin/complaints/${id}/remarks`, {
            method: 'POST',
            body: JSON.stringify({ remarks })
        });
    },

    async getAdminUsers() {
        return await this.request('/api/admin/users');
    },

    async getAdminAnalytics() {
        return await this.request('/api/admin/analytics');
    },

    getExportCsvUrl() {
        return '/api/admin/export.csv';
    },

    // --- DAA Algorithms ---
    async getAlgorithmsMeta() {
        return await this.request('/api/algorithms');
    },

    async runSortDemo(size, sortBy) {
        return await this.request('/api/algorithms/sort-demo', {
            method: 'POST',
            body: JSON.stringify({ size, sort_by: sortBy })
        });
    },

    async runSearchDemo(size, targetId) {
        return await this.request('/api/algorithms/search-demo', {
            method: 'POST',
            body: JSON.stringify({ size, target_id: targetId })
        });
    },

    async runPriorityDemo(size) {
        return await this.request('/api/algorithms/priority-demo', {
            method: 'POST',
            body: JSON.stringify({ size })
        });
    }
};

// UI Notification Toast System
function showToast(message, type = 'info') {
    let container = document.getElementById('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        document.body.appendChild(container);
    }

    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;

    let icon = `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg>`;
    if (type === 'success') {
        icon = `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#2DD4A7" stroke-width="2"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline></svg>`;
    } else if (type === 'error') {
        icon = `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#FF647C" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="15" y1="9" x2="9" y2="15"></line><line x1="9" y1="9" x2="15" y2="15"></line></svg>`;
    }

    toast.innerHTML = `${icon}<span>${message}</span>`;
    container.appendChild(toast);

    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateX(100%)';
        toast.style.transition = 'all 0.3s ease';
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

// Format ISO date to local human-friendly string
function formatTimestamp(isoStr) {
    if (!isoStr) return 'N/A';
    try {
        const d = new Date(isoStr);
        return d.toLocaleString(undefined, {
            year: 'numeric',
            month: 'short',
            day: 'numeric',
            hour: '2-digit',
            minute: '2-digit'
        });
    } catch (e) {
        return isoStr;
    }
}

// Priority name and styling helper
function getPriorityBadge(priority) {
    const p = parseInt(priority, 10);
    switch (p) {
        case 4: return `<span class="badge badge-critical"><span class="badge-dot"></span>Critical (P4)</span>`;
        case 3: return `<span class="badge badge-high"><span class="badge-dot"></span>High (P3)</span>`;
        case 2: return `<span class="badge badge-medium"><span class="badge-dot"></span>Medium (P2)</span>`;
        case 1: return `<span class="badge badge-low"><span class="badge-dot"></span>Low (P1)</span>`;
        default: return `<span class="badge badge-low">Normal</span>`;
    }
}

// Status badge helper
function getStatusBadge(status) {
    const s = (status || 'pending').toLowerCase();
    switch (s) {
        case 'resolved':
            return `<span class="badge badge-resolved"><span class="badge-dot"></span>Resolved</span>`;
        case 'in_progress':
            return `<span class="badge badge-progress"><span class="badge-dot"></span>In Progress</span>`;
        case 'rejected':
            return `<span class="badge badge-critical"><span class="badge-dot"></span>Rejected</span>`;
        default:
            return `<span class="badge badge-pending"><span class="badge-dot"></span>Pending</span>`;
    }
}
