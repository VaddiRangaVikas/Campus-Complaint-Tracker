/**
 * Student Dashboard Controller
 */

let s_complaints = [];
let s_statusChart = null;

// Initialize on DOM Ready
document.addEventListener('DOMContentLoaded', async () => {
    const user = API.getUser();
    if (!user || !API.getToken()) {
        window.location.href = '/auth.html';
        return;
    }

    // Populate user profile in sidebar
    document.getElementById('user-display-name').textContent = user.full_name;
    document.getElementById('user-display-role').textContent = user.student_id ? `${user.student_id} &bull; ${user.role}` : user.role;
    document.getElementById('user-avatar').textContent = user.full_name.charAt(0).toUpperCase();

    // Populate profile form inputs
    const pName = document.getElementById('prof-fullname');
    const pEmail = document.getElementById('prof-email');
    const pSid = document.getElementById('prof-studentid');
    const pDept = document.getElementById('prof-dept');
    if (pName) pName.value = user.full_name;
    if (pEmail) pEmail.value = user.email;
    if (pSid) pSid.value = user.student_id || 'N/A';
    if (pDept) pDept.value = user.department;

    await loadDashboardData();
    await loadComplaints();
});

function toggleSidebar() {
    const sidebar = document.getElementById('sidebar');
    if (sidebar) sidebar.classList.toggle('open');
}

function showTab(tabName) {
    const tabs = ['overview', 'submit', 'complaints', 'profile'];
    tabs.forEach(t => {
        const tabEl = document.getElementById(`tab-${t}`);
        const linkEl = document.getElementById(`nav-tab-${t}`);
        if (tabEl) tabEl.classList.toggle('active', t === tabName);
        if (linkEl) linkEl.classList.toggle('active', t === tabName);
    });

    const pageHeading = document.getElementById('page-heading');
    if (pageHeading) {
        if (tabName === 'overview') pageHeading.textContent = 'Student Dashboard Overview';
        else if (tabName === 'submit') pageHeading.textContent = 'Submit New Facility Complaint';
        else if (tabName === 'complaints') pageHeading.textContent = 'My Registered Complaints';
        else if (tabName === 'profile') pageHeading.textContent = 'Account & Academic Profile';
    }

    // Close mobile drawer if open
    const sidebar = document.getElementById('sidebar');
    if (sidebar) sidebar.classList.remove('open');
}

// 1. Load Real Dashboard Stats
async function loadDashboardData() {
    try {
        const res = await API.getStudentDashboard();
        if (!res || !res.data) return;

        const data = res.data;
        document.getElementById('kpi-student-total').textContent = data.total_complaints || 0;
        document.getElementById('kpi-student-pending').textContent = data.pending_complaints || 0;
        document.getElementById('kpi-student-progress').textContent = data.in_progress_complaints || 0;
        document.getElementById('kpi-student-resolved').textContent = data.resolved_complaints || 0;

        renderStatusChart(data.pending_complaints || 0, data.in_progress_complaints || 0, data.resolved_complaints || 0);

        // Render Recent Complaints
        const recentContainer = document.getElementById('student-recent-list');
        if (recentContainer) {
            if (!data.recent_complaints || data.recent_complaints.length === 0) {
                recentContainer.innerHTML = '<p style="color:var(--text-muted); font-size:0.9rem;">No complaints registered yet. Use "+ New Complaint" to lodge an issue.</p>';
            } else {
                recentContainer.innerHTML = data.recent_complaints.map(c => `
                    <div style="display:flex; justify-content:space-between; align-items:center; background:rgba(13,21,39,0.5); padding:0.85rem 1rem; border-radius:var(--radius-sm); border:1px solid var(--border-subtle); cursor:pointer;" onclick="viewComplaintDetail('${c.complaint_id}')">
                        <div>
                            <div style="font-weight:700; font-size:0.92rem; color:var(--text-primary);">${c.title}</div>
                            <div style="font-size:0.78rem; color:var(--text-muted); margin-top:0.2rem;">
                                <span style="font-family:var(--font-mono); color:var(--accent-cyan);">${c.complaint_id}</span> &bull; ${c.location} &bull; ${formatTimestamp(c.created_at)}
                            </div>
                        </div>
                        <div>${getStatusBadge(c.status)}</div>
                    </div>
                `).join('');
            }
        }
    } catch (err) {
        showToast('Failed to load dashboard data', 'error');
    }
}

function renderStatusChart(pending, inProgress, resolved) {
    const ctx = document.getElementById('chart-student-status');
    if (!ctx) return;

    if (s_statusChart) {
        s_statusChart.destroy();
    }

    s_statusChart = new Chart(ctx, {
        type: 'doughnut',
        data: {
            labels: ['Pending', 'In Progress', 'Resolved'],
            datasets: [{
                data: [pending, inProgress, resolved],
                backgroundColor: ['#FFBF69', '#38D9FF', '#2DD4A7'],
                borderColor: '#131F35',
                borderWidth: 3
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'bottom',
                    labels: { color: '#9AA8C2', font: { family: 'Plus Jakarta Sans', size: 12 } }
                }
            },
            cutout: '72%'
        }
    });
}

// 2. Submit New Complaint
async function handleComplaintSubmit(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-submit-ticket');

    const title = document.getElementById('comp-title').value.trim();
    const category = document.getElementById('comp-category').value;
    const priority = parseInt(document.getElementById('comp-priority').value, 10);
    const location = document.getElementById('comp-location').value.trim();
    const room_number = document.getElementById('comp-room').value.trim();
    const description = document.getElementById('comp-description').value.trim();

    try {
        btn.disabled = true;
        btn.textContent = 'Recording in SQLite...';

        const res = await API.submitComplaint({
            title,
            category,
            priority,
            location,
            room_number,
            description
        });

        // Clear form
        document.getElementById('form-submit-complaint').reset();

        // Show confirmation modal
        document.getElementById('confirm-complaint-id').textContent = res.data.complaint_id;
        document.getElementById('modal-success-confirm').classList.add('active');

        // Reload data
        await loadDashboardData();
        await loadComplaints();

        btn.disabled = false;
        btn.textContent = 'Submit Complaint to SQLite Backend';
    } catch (err) {
        showToast(err.message || 'Failed to submit complaint', 'error');
        btn.disabled = false;
        btn.textContent = 'Submit Complaint to SQLite Backend';
    }
}

function closeSuccessModal() {
    document.getElementById('modal-success-confirm').classList.remove('active');
    showTab('complaints');
}

// 3. Load & Filter Student Complaints
async function loadComplaints() {
    try {
        const res = await API.getStudentComplaints();
        s_complaints = res?.data?.complaints || [];
        applyFilters();
    } catch (err) {
        showToast('Failed to fetch complaint history', 'error');
    }
}

function applyFilters() {
    const search = (document.getElementById('filter-search')?.value || '').toLowerCase().trim();
    const status = document.getElementById('filter-status')?.value || 'all';
    const priority = document.getElementById('filter-priority')?.value || 'all';
    const sortBy = document.getElementById('filter-sort')?.value || 'date_desc';

    let filtered = [...s_complaints];

    // Search filter
    if (search) {
        filtered = filtered.filter(c =>
            c.complaint_id.toLowerCase().includes(search) ||
            c.title.toLowerCase().includes(search) ||
            c.location.toLowerCase().includes(search) ||
            c.category.toLowerCase().includes(search)
        );
    }

    // Status filter
    if (status !== 'all') {
        filtered = filtered.filter(c => c.status.toLowerCase() === status.toLowerCase());
    }

    // Priority filter
    if (priority !== 'all') {
        filtered = filtered.filter(c => c.priority === parseInt(priority, 10));
    }

    // Sort order (Using client-side representation of the C Merge Sort logic)
    filtered.sort((a, b) => {
        if (sortBy === 'date_desc') {
            return new Date(b.created_at) - new Date(a.created_at);
        } else if (sortBy === 'date_asc') {
            return new Date(a.created_at) - new Date(b.created_at);
        } else if (sortBy === 'priority_desc') {
            if (b.priority !== a.priority) return b.priority - a.priority;
            return new Date(a.created_at) - new Date(b.created_at); // Tie-break oldest first
        } else if (sortBy === 'id_asc') {
            return a.complaint_id.localeCompare(b.complaint_id);
        }
        return 0;
    });

    renderComplaintsTable(filtered);
}

function renderComplaintsTable(items) {
    const tbody = document.getElementById('student-complaints-tbody');
    if (!tbody) return;

    if (items.length === 0) {
        tbody.innerHTML = `<tr><td colspan="8" style="text-align:center; color:var(--text-muted); padding:2rem;">No matching complaints found.</td></tr>`;
        return;
    }

    tbody.innerHTML = items.map(c => `
        <tr>
            <td style="font-family:var(--font-mono); font-weight:700; color:var(--accent-cyan);">${c.complaint_id}</td>
            <td style="font-weight:600; max-width:240px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;">${c.title}</td>
            <td>${c.category}</td>
            <td style="font-size:0.85rem; color:var(--text-secondary);">${c.location}</td>
            <td>${getPriorityBadge(c.priority)}</td>
            <td>${getStatusBadge(c.status)}</td>
            <td style="font-size:0.82rem; color:var(--text-muted);">${formatTimestamp(c.created_at)}</td>
            <td>
                <button type="button" class="btn btn-secondary btn-sm" onclick="viewComplaintDetail('${c.complaint_id}')">
                    View Details
                </button>
            </td>
        </tr>
    `).join('');
}

// 4. View Complaint Details & Audit Timeline Modal
async function viewComplaintDetail(id) {
    try {
        const res = await API.getComplaintDetail(id);
        if (!res || !res.data) return;

        const c = res.data;
        document.getElementById('modal-comp-id').textContent = c.complaint_id;
        document.getElementById('modal-comp-title').textContent = c.title;
        document.getElementById('modal-comp-category').textContent = c.category;
        document.getElementById('modal-comp-priority').innerHTML = getPriorityBadge(c.priority);
        document.getElementById('modal-comp-location').textContent = `${c.location}${c.room_number ? ' (' + c.room_number + ')' : ''}`;
        document.getElementById('modal-comp-status').innerHTML = getStatusBadge(c.status);
        document.getElementById('modal-comp-desc').textContent = c.description;

        // Render audit timeline
        const timeline = document.getElementById('modal-comp-timeline');
        if (c.history && c.history.length > 0) {
            timeline.innerHTML = c.history.map(h => `
                <div class="timeline-item">
                    <div class="timeline-dot"></div>
                    <div class="timeline-content">
                        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:0.35rem;">
                            <span style="font-weight:700; font-size:0.88rem; color:var(--text-primary); text-transform:capitalize;">
                                Status: ${h.new_status}
                            </span>
                            <span class="timeline-time">${formatTimestamp(h.created_at)}</span>
                        </div>
                        <div style="font-size:0.8rem; color:var(--text-secondary);">
                            Updated by: <strong>${h.changed_by_name}</strong> (${h.changed_by_role})
                        </div>
                        ${h.remarks ? `<div style="font-size:0.82rem; color:var(--accent-cyan); margin-top:0.35rem; background:rgba(56,217,255,0.08); padding:0.4rem 0.6rem; border-radius:4px;">"${h.remarks}"</div>` : ''}
                    </div>
                </div>
            `).join('');
        } else {
            timeline.innerHTML = `<p style="color:var(--text-muted); font-size:0.85rem;">No historical status changes recorded.</p>`;
        }

        document.getElementById('modal-complaint-detail').classList.add('active');
    } catch (err) {
        showToast('Failed to load complaint details', 'error');
    }
}

function closeDetailModal() {
    document.getElementById('modal-complaint-detail').classList.remove('active');
}

// 5. Update Profile
async function handleProfileUpdate(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-save-profile');
    const full_name = document.getElementById('prof-fullname').value.trim();
    const department = document.getElementById('prof-dept').value.trim();

    try {
        btn.disabled = true;
        await API.updateStudentProfile({ full_name, department });
        showToast('Profile updated successfully!', 'success');
        document.getElementById('user-display-name').textContent = full_name;
        btn.disabled = false;
    } catch (err) {
        showToast(err.message || 'Failed to update profile', 'error');
        btn.disabled = false;
    }
}

// 6. Change Password
async function handleChangePassword(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-save-pw');
    const old_password = document.getElementById('pw-current').value;
    const new_password = document.getElementById('pw-new').value;

    try {
        btn.disabled = true;
        await API.changePassword(old_password, new_password);
        showToast('Password changed successfully!', 'success');
        document.getElementById('form-change-password').reset();
        btn.disabled = false;
    } catch (err) {
        showToast(err.message || 'Failed to change password', 'error');
        btn.disabled = false;
    }
}
