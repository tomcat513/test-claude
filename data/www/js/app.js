// WiFi Network Monitor - Dashboard JavaScript

const API = '';  // Same origin
let refreshInterval = null;
let pingOffset = 0;
const PING_PAGE_SIZE = 100;

// --- Tab Navigation ---
function showTab(name) {
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('nav .tab').forEach(el => el.classList.remove('active'));

    document.getElementById('tab-' + name).classList.add('active');
    // Find the tab button by text content
    document.querySelectorAll('nav .tab').forEach(el => {
        if (el.textContent.toLowerCase().includes(name.substring(0, 4))) {
            el.classList.add('active');
        }
    });

    // Load data for the tab
    switch(name) {
        case 'pings': loadPingHistory(); break;
        case 'speed': loadSpeedHistory(); break;
        case 'outages': loadOutages(); break;
        case 'settings': loadSettings(); break;
    }
}

// --- API Helpers ---
async function apiFetch(path, options = {}) {
    try {
        const resp = await fetch(API + path, {
            ...options,
            headers: {
                'Content-Type': 'application/json',
                ...options.headers
            }
        });
        if (resp.status === 401) {
            // Browser will show auth dialog
            return null;
        }
        if (!resp.ok) {
            console.error('API error:', resp.status, resp.statusText);
            return null;
        }
        return await resp.json();
    } catch(e) {
        console.error('Fetch error:', e);
        updateConnectionBadge(false);
        return null;
    }
}

async function apiPost(path, body = {}) {
    return apiFetch(path, {
        method: 'POST',
        body: JSON.stringify(body)
    });
}

// --- Format Helpers ---
function formatTime(ts) {
    if (!ts) return '--';
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit', second: '2-digit'});
}

function formatDateTime(ts) {
    if (!ts) return '--';
    const d = new Date(ts * 1000);
    return d.toLocaleDateString([], {month: 'short', day: 'numeric'}) + ' ' +
           d.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
}

function formatDuration(seconds) {
    if (!seconds || seconds < 0) return '--';
    if (seconds < 60) return seconds + 's';
    if (seconds < 3600) return Math.floor(seconds / 60) + 'm ' + (seconds % 60) + 's';
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return h + 'h ' + m + 'm';
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(2) + ' MB';
}

function formatUptime(seconds) {
    const d = Math.floor(seconds / 86400);
    const h = Math.floor((seconds % 86400) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
    if (h > 0) return h + 'h ' + m + 'm';
    return m + 'm';
}

// --- Connection Badge ---
function updateConnectionBadge(connected) {
    const badge = document.getElementById('connection-badge');
    if (connected) {
        badge.textContent = 'Connected';
        badge.className = 'badge badge-ok';
    } else {
        badge.textContent = 'Offline';
        badge.className = 'badge badge-err';
    }
}

// --- Dashboard ---
async function refreshDashboard() {
    const data = await apiFetch('/api/status');
    if (!data) return;

    updateConnectionBadge(true);

    // Device name
    document.getElementById('device-name').textContent = data.device_name || 'Network Monitor';
    document.title = (data.device_name || 'Network Monitor') + ' Dashboard';

    // Password warning
    document.getElementById('password-warning').style.display =
        data.password_changed ? 'none' : 'block';

    // WiFi
    const rssi = data.wifi_rssi;
    document.getElementById('wifi-rssi').textContent = rssi + ' dBm';
    document.getElementById('wifi-rssi').className = 'metric ' +
        (rssi > -50 ? 'text-ok' : rssi > -70 ? 'text-warn' : 'text-err');
    document.getElementById('wifi-quality').textContent = data.wifi_quality || '--';
    document.getElementById('wifi-ssid').textContent = data.wifi_ssid || '--';

    // Internet status
    const inetEl = document.getElementById('internet-status');
    if (data.internet_reachable) {
        inetEl.textContent = 'Online';
        inetEl.className = 'metric text-ok';
    } else {
        inetEl.textContent = 'Down';
        inetEl.className = 'metric text-err';
    }

    const gwEl = document.getElementById('gateway-status');
    gwEl.textContent = 'Gateway: ' + (data.gateway_reachable ? 'OK' : 'Unreachable');
    gwEl.className = 'metric-label ' + (data.gateway_reachable ? 'text-ok' : 'text-err');

    // Speed
    document.getElementById('last-speed').textContent =
        data.last_download_mbps > 0 ? data.last_download_mbps.toFixed(2) : '--';
    document.getElementById('speed-time').textContent =
        data.last_speed_test_time > 0 ? 'Last: ' + formatTime(data.last_speed_test_time) : 'No tests yet';

    // Device info
    document.getElementById('ip-address').textContent = data.ip_address || '--';
    document.getElementById('uptime').textContent = 'Uptime: ' + formatUptime(data.uptime_sec);
    document.getElementById('free-heap').textContent = 'Free RAM: ' + formatBytes(data.free_heap);

    document.getElementById('last-update').textContent = 'Last update: ' + new Date().toLocaleTimeString();

    // Update charts
    refreshLatencyChart();
    refreshSpeedChart();
}

// --- Simple Canvas Charts ---
// These are lightweight, no external dependencies needed

function drawChart(canvasId, labels, datasets, yLabel) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();

    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    const pad = {top: 20, right: 16, bottom: 30, left: 50};
    const plotW = w - pad.left - pad.right;
    const plotH = h - pad.top - pad.bottom;

    // Clear
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, w, h);

    if (!datasets.length || !datasets[0].data.length) {
        ctx.fillStyle = '#a0a0b0';
        ctx.font = '14px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('No data yet', w / 2, h / 2);
        return;
    }

    // Find data range
    let maxVal = 0;
    datasets.forEach(ds => {
        ds.data.forEach(v => { if (v > maxVal) maxVal = v; });
    });
    maxVal = maxVal * 1.1 || 10;

    // Grid lines
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 1;
    const gridLines = 4;
    for (let i = 0; i <= gridLines; i++) {
        const y = pad.top + (plotH / gridLines) * i;
        ctx.beginPath();
        ctx.moveTo(pad.left, y);
        ctx.lineTo(pad.left + plotW, y);
        ctx.stroke();

        // Y labels
        const val = maxVal - (maxVal / gridLines) * i;
        ctx.fillStyle = '#a0a0b0';
        ctx.font = '11px sans-serif';
        ctx.textAlign = 'right';
        ctx.fillText(val.toFixed(1), pad.left - 6, y + 4);
    }

    // Y axis label
    ctx.save();
    ctx.translate(12, pad.top + plotH / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillStyle = '#a0a0b0';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(yLabel, 0, 0);
    ctx.restore();

    // X labels (show ~6 labels)
    const labelStep = Math.max(1, Math.floor(labels.length / 6));
    ctx.fillStyle = '#a0a0b0';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    for (let i = 0; i < labels.length; i += labelStep) {
        const x = pad.left + (plotW / (labels.length - 1 || 1)) * i;
        ctx.fillText(labels[i], x, h - 8);
    }

    // Draw datasets
    const colors = ['#00b4d8', '#06d6a0', '#ffd166', '#ef476f'];
    datasets.forEach((ds, di) => {
        const color = ds.color || colors[di % colors.length];
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();

        const pointCount = ds.data.length;
        for (let i = 0; i < pointCount; i++) {
            const x = pad.left + (plotW / (pointCount - 1 || 1)) * i;
            const y = pad.top + plotH - (ds.data[i] / maxVal) * plotH;

            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // Dots for sparse data
        if (pointCount < 50) {
            ctx.fillStyle = color;
            for (let i = 0; i < pointCount; i++) {
                const x = pad.left + (plotW / (pointCount - 1 || 1)) * i;
                const y = pad.top + plotH - (ds.data[i] / maxVal) * plotH;
                ctx.beginPath();
                ctx.arc(x, y, 3, 0, Math.PI * 2);
                ctx.fill();
            }
        }
    });

    // Legend
    if (datasets.length > 1) {
        let lx = pad.left + 8;
        datasets.forEach((ds, di) => {
            const color = ds.color || colors[di % colors.length];
            ctx.fillStyle = color;
            ctx.fillRect(lx, pad.top - 14, 12, 10);
            ctx.fillStyle = '#a0a0b0';
            ctx.font = '10px sans-serif';
            ctx.textAlign = 'left';
            ctx.fillText(ds.label || '', lx + 16, pad.top - 5);
            lx += ctx.measureText(ds.label || '').width + 32;
        });
    }
}

async function refreshLatencyChart() {
    const data = await apiFetch('/api/ping/history?limit=120');
    if (!data || !data.pings) return;

    // Group pings by target
    const targets = {};
    data.pings.forEach(p => {
        if (!targets[p.target]) targets[p.target] = [];
        targets[p.target].push(p);
    });

    const labels = [];
    const datasets = [];
    const colors = ['#00b4d8', '#06d6a0', '#ffd166', '#ef476f'];
    let colorIdx = 0;

    // Use timestamps from first target for labels
    const firstTarget = Object.keys(targets)[0];
    if (firstTarget) {
        targets[firstTarget].forEach(p => {
            labels.push(formatTime(p.ts));
        });
    }

    for (const [target, pings] of Object.entries(targets)) {
        datasets.push({
            label: target,
            data: pings.map(p => p.ok ? p.ms : 0),
            color: colors[colorIdx % colors.length]
        });
        colorIdx++;
    }

    drawChart('latency-chart', labels, datasets, 'ms');
}

async function refreshSpeedChart() {
    const data = await apiFetch('/api/speed/history?limit=48');
    if (!data || !data.speeds) return;

    const labels = data.speeds.map(s => formatTime(s.ts));
    const datasets = [{
        label: 'Download',
        data: data.speeds.map(s => s.ok ? s.mbps : 0),
        color: '#00b4d8'
    }];

    drawChart('speed-chart', labels, datasets, 'Mbps');
}

// --- Ping History Table ---
async function loadPingHistory(append = false) {
    if (!append) pingOffset = 0;

    const data = await apiFetch(`/api/ping/history?limit=${PING_PAGE_SIZE}&since=0`);
    if (!data || !data.pings) return;

    const tbody = document.querySelector('#ping-table tbody');
    if (!append) tbody.innerHTML = '';

    // Show most recent first
    const pings = data.pings.reverse();

    pings.forEach(p => {
        const tr = document.createElement('tr');
        const lossPercent = p.sent > 0 ? Math.round((1 - p.recv / p.sent) * 100) : 100;
        tr.innerHTML = `
            <td>${formatDateTime(p.ts)}</td>
            <td>${p.target}</td>
            <td class="${p.ok ? 'text-ok' : 'text-err'}">${p.ok ? 'OK' : 'FAIL'}</td>
            <td>${p.ok ? p.ms.toFixed(1) + ' ms' : '--'}</td>
            <td class="${lossPercent > 0 ? 'text-err' : ''}">${lossPercent}%</td>
        `;
        tbody.appendChild(tr);
    });

    pingOffset += pings.length;
    document.getElementById('load-more-pings').style.display =
        pings.length < PING_PAGE_SIZE ? 'none' : 'inline-block';
}

function loadMorePings() {
    loadPingHistory(true);
}

// --- Speed History Table ---
async function loadSpeedHistory() {
    const data = await apiFetch('/api/speed/history?limit=100');
    if (!data || !data.speeds) return;

    const tbody = document.querySelector('#speed-table tbody');
    tbody.innerHTML = '';

    const speeds = data.speeds.reverse();

    speeds.forEach(s => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td>${formatDateTime(s.ts)}</td>
            <td class="${s.ok ? 'text-ok' : 'text-err'}">${s.ok ? s.mbps.toFixed(2) + ' Mbps' : '--'}</td>
            <td>${formatBytes(s.bytes)}</td>
            <td>${s.ms ? (s.ms / 1000).toFixed(1) + 's' : '--'}</td>
            <td class="${s.ok ? 'text-ok' : 'text-err'}">${s.ok ? 'OK' : (s.error || 'FAIL')}</td>
        `;
        tbody.appendChild(tr);
    });
}

// --- Outages Table ---
async function loadOutages() {
    const data = await apiFetch('/api/outages');
    if (!data) return;

    const tbody = document.querySelector('#outage-table tbody');
    tbody.innerHTML = '';

    if (!data.outages || data.outages.length === 0) {
        document.getElementById('no-outages').style.display = 'block';
        return;
    }
    document.getElementById('no-outages').style.display = 'none';

    const typeLabels = {
        'internet': 'Internet',
        'local_network': 'Local Network',
        'wifi': 'WiFi'
    };

    const outages = data.outages.reverse();

    outages.forEach(o => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td>${formatDateTime(o.start)}</td>
            <td>${o.ongoing ? '--' : formatDateTime(o.end)}</td>
            <td>${formatDuration(o.duration_sec)}</td>
            <td>${typeLabels[o.type] || o.type}</td>
            <td class="${o.ongoing ? 'text-err' : 'text-ok'}">${o.ongoing ? 'ONGOING' : 'Resolved'}</td>
        `;
        tbody.appendChild(tr);
    });
}

// --- Settings ---
async function loadSettings() {
    const data = await apiFetch('/api/config');
    if (!data) return;

    document.getElementById('cfg-ssid').value = data.wifi_ssid || '';
    document.getElementById('cfg-wifi-pass').value = '';
    document.getElementById('cfg-admin-user').value = data.admin_user || '';
    document.getElementById('cfg-admin-pass').value = '';

    const targets = (data.ping_targets || []).join('\n');
    document.getElementById('cfg-ping-targets').value = targets;
    document.getElementById('cfg-ping-interval').value = data.ping_interval_sec || 30;
    document.getElementById('cfg-ping-count').value = data.ping_count || 3;

    document.getElementById('cfg-speed-url').value = data.speed_test_url || '';
    document.getElementById('cfg-speed-interval').value = data.speed_test_interval_sec || 900;

    document.getElementById('cfg-device-name').value = data.device_name || '';
    document.getElementById('cfg-tz-offset').value = data.timezone_offset_hours || 0;
    document.getElementById('cfg-retention').value = data.data_retention_days || 7;
}

async function saveSettings(event) {
    event.preventDefault();

    const config = {};

    const ssid = document.getElementById('cfg-ssid').value.trim();
    if (ssid) config.wifi_ssid = ssid;

    const wifiPass = document.getElementById('cfg-wifi-pass').value;
    if (wifiPass) config.wifi_password = wifiPass;

    const adminUser = document.getElementById('cfg-admin-user').value.trim();
    if (adminUser) config.admin_user = adminUser;

    const adminPass = document.getElementById('cfg-admin-pass').value;
    if (adminPass) config.admin_pass = adminPass;

    const targets = document.getElementById('cfg-ping-targets').value.trim().split('\n').filter(t => t.trim());
    if (targets.length > 0) config.ping_targets = targets;

    config.ping_interval_sec = parseInt(document.getElementById('cfg-ping-interval').value) || 30;
    config.ping_count = parseInt(document.getElementById('cfg-ping-count').value) || 3;

    const speedUrl = document.getElementById('cfg-speed-url').value.trim();
    if (speedUrl) config.speed_test_url = speedUrl;
    config.speed_test_interval_sec = parseInt(document.getElementById('cfg-speed-interval').value) || 900;

    const deviceName = document.getElementById('cfg-device-name').value.trim();
    if (deviceName) config.device_name = deviceName;
    config.timezone_offset_hours = parseInt(document.getElementById('cfg-tz-offset').value) || 0;
    config.data_retention_days = parseInt(document.getElementById('cfg-retention').value) || 7;

    const result = await apiPost('/api/config', config);
    if (result && result.status === 'ok') {
        alert('Settings saved. Some changes may require a restart.');
    } else {
        alert('Failed to save settings.');
    }
}

// --- Actions ---
async function triggerPing() {
    const result = await apiPost('/api/ping/now');
    if (result) {
        // Wait a moment then refresh
        setTimeout(refreshDashboard, 3000);
    }
}

async function triggerSpeedTest() {
    const result = await apiPost('/api/speed/now');
    if (result) {
        alert('Speed test started. Results will appear shortly.');
        setTimeout(refreshDashboard, 15000);
    }
}

async function restartDevice() {
    if (!confirm('Restart the device? It will be unavailable for a few seconds.')) return;
    await apiPost('/api/restart');
    alert('Device is restarting...');
}

// --- Initialization ---
function init() {
    refreshDashboard();

    // Auto-refresh dashboard every 15 seconds
    refreshInterval = setInterval(() => {
        const dashboard = document.getElementById('tab-dashboard');
        if (dashboard.classList.contains('active')) {
            refreshDashboard();
        }
    }, 15000);
}

// Start when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
