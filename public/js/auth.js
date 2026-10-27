/**
 * Authentication Portal Controller
 */

function switchAuthTab(tab) {
    const tabs = ['login', 'register', 'reset'];
    tabs.forEach(t => {
        const btn = document.getElementById(`tab-${t}`);
        const form = document.getElementById(`form-${t}`);
        if (btn) btn.classList.toggle('active', t === tab);
        if (form) form.style.display = (t === tab) ? 'block' : 'none';
    });

    const subtitle = document.getElementById('auth-subtitle');
    if (subtitle) {
        if (tab === 'login') subtitle.textContent = 'Sign in to your campus account';
        else if (tab === 'register') subtitle.textContent = 'Register a new student account';
        else if (tab === 'reset') subtitle.textContent = 'Local development password reset';
    }
}

function togglePasswordVisibility(inputId) {
    const input = document.getElementById(inputId);
    if (!input) return;
    input.type = (input.type === 'password') ? 'text' : 'password';
}

function quickFillCredentials(email, password) {
    switchAuthTab('login');
    const emailInput = document.getElementById('login-email');
    const passInput = document.getElementById('login-password');
    if (emailInput && passInput) {
        emailInput.value = email;
        passInput.value = password;
        showToast(`Filled credentials for ${email}`, 'info');
    }
}

async function handleLoginSubmit(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-submit-login');
    const email = document.getElementById('login-email').value.trim();
    const password = document.getElementById('login-password').value;

    try {
        btn.disabled = true;
        btn.textContent = 'Authenticating...';

        const res = await API.login(email, password);
        showToast('Login successful! Redirecting...', 'success');

        setTimeout(() => {
            if (res.data?.user?.role === 'admin') {
                window.location.href = '/admin.html';
            } else {
                window.location.href = '/student.html';
            }
        }, 600);
    } catch (err) {
        showToast(err.message || 'Login failed', 'error');
        btn.disabled = false;
        btn.textContent = 'Sign In to Portal';
    }
}

async function handleRegisterSubmit(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-submit-register');
    const full_name = document.getElementById('reg-fullname').value.trim();
    const email = document.getElementById('reg-email').value.trim();
    const student_id = document.getElementById('reg-studentid').value.trim();
    const department = document.getElementById('reg-department').value;
    const password = document.getElementById('reg-password').value;
    const confirm = document.getElementById('reg-confirm').value;

    if (password !== confirm) {
        showToast('Passwords do not match', 'error');
        return;
    }

    try {
        btn.disabled = true;
        btn.textContent = 'Creating Account...';

        await API.register({
            full_name,
            email,
            student_id,
            department,
            password,
            role: 'student'
        });

        showToast('Registration successful! Redirecting to student portal...', 'success');
        setTimeout(() => {
            window.location.href = '/student.html';
        }, 700);
    } catch (err) {
        showToast(err.message || 'Registration failed', 'error');
        btn.disabled = false;
        btn.textContent = 'Create Student Account';
    }
}

async function handleResetSubmit(e) {
    e.preventDefault();
    const btn = document.getElementById('btn-submit-reset');
    const email = document.getElementById('reset-email').value.trim();
    const new_password = document.getElementById('reset-password').value;

    try {
        btn.disabled = true;
        btn.textContent = 'Updating Password...';

        await API.resetPasswordDev(email, new_password);
        showToast('Password updated! You can now sign in.', 'success');

        setTimeout(() => {
            switchAuthTab('login');
            document.getElementById('login-email').value = email;
            document.getElementById('login-password').value = new_password;
            btn.disabled = false;
            btn.textContent = 'Update Password in SQLite';
        }, 800);
    } catch (err) {
        showToast(err.message || 'Password reset failed', 'error');
        btn.disabled = false;
        btn.textContent = 'Update Password in SQLite';
    }
}

// Check URL params for ?tab=register
document.addEventListener('DOMContentLoaded', () => {
    const params = new URLSearchParams(window.location.search);
    const tab = params.get('tab');
    if (tab === 'register') {
        switchAuthTab('register');
    } else if (tab === 'reset') {
        switchAuthTab('reset');
    }

    // If user is already logged in, redirect them
    const user = API.getUser();
    if (user && API.getToken()) {
        if (user.role === 'admin') {
            window.location.href = '/admin.html';
        } else {
            window.location.href = '/student.html';
        }
    }
});
