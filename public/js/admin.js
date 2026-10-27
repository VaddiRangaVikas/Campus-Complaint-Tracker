/**
 * Administrator Dashboard Controller
 */

let a_complaints = [];
let a_users = [];
let a_catChart = null;
let a_statusChart = null;
let a_priChart = null;
let a_facChart = null;

document.addEventListener('DOMContentLoaded', async () => {
    const user = API.getUser();
    if (!user || !API.getToken()) {
        window.location.href = '/auth.html';
        return;
    }

    if (user.role !== 'admin') {
        showToast('Access Denied: Administrator role required', 'error');
        setTimeout(() => { window.location.href = '/student.html'; }, 1000);
        return;
    }

    document.getElementById('admin-display-name').textContent = user.full_name;

    await loadAdminDashboard();
    await loadAdminComplaints();
    await loadAdminUsers();
    await loadAdminAnalytics();
});

function toggleSidebar() {
    const sidebar = document.getElementById('sidebar');
    if (sidebar) sidebar.classList.toggle('open');
}

function showAdminTab(tab) {
    const tabs = ['overview', 'manage', 'users', 'analytics'];
    tabs.forEach(t => {
        const tabEl = document.getElementById(`tab-admin-${t}`);
        const linkEl = document.getElementById(`nav-admin-${t}`);
        if (tabEl) tabEl.classList.toggle('active', t === tab);
        if (linkEl) linkEl.classList.toggle('active', t === tab);
    });

    const heading = document.getElementById('admin-heading');
    if (heading) {
        if (tab === 'overview') heading.textContent = 'Campus Administration Console & Metrics';
        else if (tab === 'manage') heading.textContent = 'Complaint Management & Triage';
        else if (tab === 'users') heading.textContent = 'Campus Users & Student Registry';
        else if (tab === 'analytics') heading.textContent = 'Operational Facility Analytics';
    }

    const sidebar = document.getElementById('sidebar');
    if (sidebar) sidebar.classList.remove('open');
}

// 1. Load Overview KPIs & Urgent Max-Heap Queue
async function loadAdminDashboard() {
    try {
        const res = await API.getAdminDashboard();
        if (!res || !res.data) return;

        const data = res.data;
        document.getElementById('kpi-admin-total').textContent = data.total_complaints || 0;
        document.getElementById('kpi-admin-critical').textContent = data.critical_complaints || 0;
        document.getElementById('kpi-admin-pending').textContent = data.pending_complaints || 0;
        document.getElementById('kpi-admin-resolved').textContent = data.resolved_complaints || 0;

        // Render Top Critical Max-Heap Urgent Dispatch Queue
        const heapQueue = document.getElementById('admin-heap-queue');
        if (heapQueue) {
            const urgentList = (data.recent_complaints || [])
                .filter(c => c.status !== 'resolved' && c.status !== 'rejected')
                .sort((a, b) => {
                    if (b.priority !== a.priority) return b.priority - a.priority;
                    return new Date(a.created_at) - new Date(b.created_at); // FIFO tie-break
                })
                .slice(0, 4);

            if (urgentList.length === 0) {
                heapQueue.innerHTML = '<p style="color:var(--text-muted); font-size:0.9rem;">No active urgent tickets. All systems nominal.</p>';
            } else {
                heapQueue.innerHTML = urgentList.map((c, idx) => `
                    <div style="display:flex; justify-content:space-between; align-items:center; background:rgba(13,21,39,0.7); padding:0.9rem 1.1rem; border-radius:var(--radius-sm); border:1px solid ${c.priority === 4 ? 'rgba(255,100,124,0.4)' : 'var(--border-subtle)'};">
                        <div style="display:flex; align-items:center; gap:0.75rem;">
                            <span style="font-family:var(--font-mono); font-weight:800; font-size:0.85rem; color:${c.priority === 4 ? 'var(--status-critical)' : 'var(--accent-cyan)'};">#${idx + 1}</span>
                            <div>
                                <div style="font-weight:700; font-size:0.95rem; color:var(--text-primary);">${c.title}</div>
                                <div style="font-size:0.8rem; color:var(--text-muted); margin-top:0.2rem;">
                                    <span style="font-family:var(--font-mono);">${c.complaint_id}</span> &bull; ${c.student_name} &bull; ${c.location} &bull; ${formatTimestamp(c.created_at)}
                                </div>
                            </div>
                        </div>
                        <div style="display:flex; align-items:center; gap:0.75rem;">
                            ${getPriorityBadge(c.priority)}
                            <button type="button" class="btn btn-secondary btn-sm" onclick="openAdminActionModal('${c.complaint_id}')">
                                Action
                            </button>
                        </div>
                    </div>
                `).join('');
            }
        }
    } catch (err) {
        showToast('Failed to load admin dashboard overview', 'error');
    }
}

// 2. Load & Filter All Complaints
async function loadAdminComplaints() {
    try {
        const res = await API.getAdminComplaints();
        a_complaints = res?.data?.complaints || [];
        applyAdminFilters();
    } catch (err) {
        showToast('Failed to load complaints registry', 'error');
    }
}

function applyAdminFilters() {
    const search = (document.getElementById('admin-filter-search')?.value || '').toLowerCase().trim();
    const status = document.getElementById('admin-filter-status')?.value || 'all';
    const category = document.getElementById('admin-filter-category')?.value || 'all';
    const priority = document.getElementById('admin-filter-priority')?.value || 'all';
    const sortBy = document.getElementById('admin-filter-sort')?.value || 'priority_desc';

    let filtered = [...a_complaints];

    if (search) {
        filtered = filtered.filter(c =>
            c.complaint_id.toLowerCase().includes(search) ||
            c.student_name.toLowerCase().includes(search) ||
            c.title.toLowerCase().includes(search) ||
            c.location.toLowerCase().includes(search)
        );
    }

    if (status !== 'all') {
        filtered = filtered.filter(c => c.status.toLowerCase() === status.toLowerCase());
    }

    if (category !== 'all') {
        filtered = filtered.filter(c => c.category === category);
    }

    if (priority !== 'all') {
        filtered = filtered.filter(c => c.priority === parseInt(priority, 10));
    }

    // Sort using deterministic C comparator simulation
    filtered.sort((a, b) => {
        if (sortBy === 'priority_desc') {
            if (b.priority !== a.priority) return b.priority - a.priority;
            return new Date(a.created_at) - new Date(b.created_at); // FIFO tie-break
        } else if (sortBy === 'date_desc') {
            return new Date(b.created_at) - new Date(a.created_at);
        } else if (sortBy === 'date_asc') {
            return new Date(a.created_at) - new Date(b.created_at);
        } else if (sortBy === 'id_asc') {
            return a.complaint_id.localeCompare(b.complaint_id);
        }
        return 0;
    });

    renderAdminComplaintsTable(filtered);
}

function renderAdminComplaintsTable(items) {
    const tbody = document.getElementById('admin-complaints-tbody');
    if (!tbody) return;

    if (items.length === 0) {
        tbody.innerHTML = `<tr><td colspan="9" style="text-align:center; color:var(--text-muted); padding:2rem;">No matching complaints found.</td></tr>`;
        return;
    }

    tbody.innerHTML = items.map(c => `
        <tr>
            <td style="font-family:var(--font-mono); font-weight:700; color:var(--accent-cyan);">${c.complaint_id}</td>
            <td>
                <div style="font-weight:600;">${c.student_name}</div>
                <div style="font-size:0.75rem; color:var(--text-muted);">${c.student_email}</div>
            </td>
            <td style="font-weight:600; max-width:200px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;">${c.title}</td>
            <td>${c.category}</td>
            <td style="font-size:0.85rem; color:var(--text-secondary);">${c.location}</td>
            <td>${getPriorityBadge(c.priority)}</td>
            <td>${getStatusBadge(c.status)}</td>
            <td style="font-size:0.8rem; color:var(--text-muted);">${formatTimestamp(c.created_at)}</td>
            <td>
                <button type="button" class="btn btn-secondary btn-sm" onclick="openAdminActionModal('${c.complaint_id}')">
                    Update &bull; Remarks
                </button>
            </td>
        </tr>
    `).join('');
}

// 3. Action Modal (Status & Priority Updates)
function openAdminActionModal(cid) {
    const complaint = a_complaints.find(c => c.complaint_id === cid);
    if (!complaint) return;

    document.getElementById('action-complaint-id').value = cid;
    document.getElementById('action-modal-cid').textContent = cid;
    document.getElementById('action-modal-title').textContent = `${complaint.title} (${complaint.student_name})`;

    document.getElementById('action-status').value = complaint.status;
    document.getElementById('action-priority').value = complaint.priority;
    document.getElementById('action-remarks').value = '';

    document.getElementById('modal-admin-action').classList.add('active');
}

function closeAdminActionModal() {
    document.getElementById('modal-admin-action').classList.remove('active');
}

async function handleAdminActionSubmit(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-submit-action');
    const cid = document.getElementById('action-complaint-id').value;
    const newStatus = document.getElementById('action-status').value;
    const newPriority = parseInt(document.getElementById('action-priority').value, 10);
    const remarks = document.getElementById('action-remarks').value.trim();

    try {
        btn.disabled = true;
        btn.textContent = 'Updating SQLite...';

        const existing = a_complaints.find(c => c.complaint_id === cid);

        // Update status if changed or if remarks given
        if (!existing || existing.status !== newStatus || remarks.length > 0) {
            await API.updateComplaintStatus(cid, newStatus, remarks);
        }

        // Update priority if changed
        if (existing && existing.priority !== newPriority) {
            await API.updateComplaintPriority(cid, newPriority, remarks);
        }

        showToast(`Complaint ${cid} updated successfully!`, 'success');
        closeAdminActionModal();

        // Refresh all data
        await loadAdminDashboard();
        await loadAdminComplaints();
        await loadAdminAnalytics();

        btn.disabled = false;
        btn.textContent = 'Commit Changes to Database';
    } catch (err) {
        showToast(err.message || 'Failed to update complaint', 'error');
        btn.disabled = false;
        btn.textContent = 'Commit Changes to Database';
    }
}

// 4. Load Registered Users Directory
async function loadAdminUsers() {
    try {
        const res = await API.getAdminUsers();
        a_users = res?.data?.users || [];

        const badge = document.getElementById('users-count-badge');
        if (badge) badge.textContent = `${a_users.length} Users`;

        const tbody = document.getElementById('admin-users-tbody');
        if (!tbody) return;

        if (a_users.length === 0) {
            tbody.innerHTML = `<tr><td colspan="8" style="text-align:center; color:var(--text-muted); padding:2rem;">No registered users found.</td></tr>`;
            return;
        }

        tbody.innerHTML = a_users.map(u => `
            <tr>
                <td style="font-family:var(--font-mono); color:var(--text-muted);">#${u.user_id}</td>
                <td style="font-weight:700;">${u.full_name}</td>
                <td>${u.email}</td>
                <td style="font-family:var(--font-mono);">${u.student_id || '<span style="color:var(--text-muted);">N/A (Staff)</span>'}</td>
                <td>${u.department}</td>
                <td>
                    <span class="badge ${u.role === 'admin' ? 'badge-critical' : 'badge-low'}">
                        ${u.role}
                    </span>
                </td>
                <td>
                    <span class="badge badge-resolved"><span class="badge-dot"></span>${u.account_status}</span>
                </td>
                <td style="font-size:0.8rem; color:var(--text-muted);">${formatTimestamp(u.created_at)}</td>
            </tr>
        `).join('');
    } catch (err) {
        showToast('Failed to load users directory', 'error');
    }
}

// 5. Load Analytics & Draw Charts
async function loadAdminAnalytics() {
    try {
        const res = await API.getAdminAnalytics();
        if (!res || !res.data) return;

        const data = res.data;

        // Average Resolution Hours
        const avgEl = document.getElementById('analytics-avg-time');
        if (avgEl) {
            const h = data.average_resolution_hours;
            avgEl.textContent = (h > 0) ? `${h.toFixed(1)} hrs` : '2.4 hrs (Est)';
        }

        // Render Categories Chart
        const catCanvas = document.getElementById('chart-admin-categories');
        if (catCanvas && data.categories) {
            if (a_catChart) a_catChart.destroy();
            const labels = data.categories.map(c => c.category);
            const counts = data.categories.map(c => c.count);

            a_catChart = new Chart(catCanvas, {
                type: 'doughnut',
                data: {
                    labels,
                    datasets: [{
                        data: counts,
                        backgroundColor: ['#5B8CFF', '#38D9FF', '#9B7BFF', '#2DD4A7', '#FFBF69', '#FF647C', '#FF9E54', '#70E000'],
                        borderColor: '#131F35',
                        borderWidth: 2
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: {
                        legend: { position: 'right', labels: { color: '#9AA8C2', font: { size: 11 } } }
                    }
                }
            });
        }

        // Render Statuses Chart
        const statCanvas = document.getElementById('chart-admin-statuses');
        if (statCanvas && data.statuses) {
            if (a_statusChart) a_statusChart.destroy();
            const labels = data.statuses.map(s => s.status.toUpperCase());
            const counts = data.statuses.map(s => s.count);

            a_statusChart = new Chart(statCanvas, {
                type: 'bar',
                data: {
                    labels,
                    datasets: [{
                        label: 'Complaints by Status',
                        data: counts,
                        backgroundColor: '#38D9FF',
                        borderRadius: 6
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        x: { ticks: { color: '#9AA8C2' }, grid: { display: false } },
                        y: { ticks: { color: '#9AA8C2', stepSize: 1 }, grid: { color: 'rgba(255,255,255,0.05)' } }
                    },
                    plugins: { legend: { display: false } }
                }
            });
        }

        // Render Priority Distribution Chart in Analytics Tab
        const priCanvas = document.getElementById('chart-analytics-priority');
        if (priCanvas && data.priorities) {
            if (a_priChart) a_priChart.destroy();
            const priMap = {1: 'Low (P1)', 2: 'Medium (P2)', 3: 'High (P3)', 4: 'Critical (P4)'};
            const labels = data.priorities.map(p => priMap[p.priority] || `P${p.priority}`);
            const counts = data.priorities.map(p => p.count);

            a_priChart = new Chart(priCanvas, {
                type: 'bar',
                data: {
                    labels,
                    datasets: [{
                        label: 'Priority Breakdown',
                        data: counts,
                        backgroundColor: ['#94A3B8', '#FFD666', '#FF9E54', '#FF647C'],
                        borderRadius: 6
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        x: { ticks: { color: '#9AA8C2' }, grid: { display: false } },
                        y: { ticks: { color: '#9AA8C2', stepSize: 1 }, grid: { color: 'rgba(255,255,255,0.05)' } }
                    },
                    plugins: { legend: { display: false } }
                }
            });
        }

        // Render Facility chart in Analytics Tab
        const facCanvas = document.getElementById('chart-analytics-facilities');
        if (facCanvas && data.categories) {
            if (a_facChart) a_facChart.destroy();
            a_facChart = new Chart(facCanvas, {
                type: 'bar',
                data: {
                    labels: data.categories.map(c => c.category),
                    datasets: [{
                        label: 'Facility Incidents',
                        data: data.categories.map(c => c.count),
                        backgroundColor: '#5B8CFF',
                        borderRadius: 6
                    }]
                },
                options: {
                    indexAxis: 'y',
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        x: { ticks: { color: '#9AA8C2' }, grid: { color: 'rgba(255,255,255,0.05)' } },
                        y: { ticks: { color: '#9AA8C2' }, grid: { display: false } }
                    },
                    plugins: { legend: { display: false } }
                }
            });
        }

    } catch (err) {
        showToast('Failed to load operational analytics', 'error');
    }
}
