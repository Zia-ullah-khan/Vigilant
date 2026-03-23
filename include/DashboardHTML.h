#pragma once

constexpr const char* DASHBOARD_HTML = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Vigilant Dashboard</title>
    <style>
        :root {
            --bg: #0f172a;
            --surface: rgba(30, 41, 59, 0.7);
            --surface-hover: rgba(30, 41, 59, 0.9);
            --border: rgba(255, 255, 255, 0.1);
            --text: #f8fafc;
            --text-muted: #94a3b8;
            --accent: #38bdf8;
            --accent-glow: rgba(56, 189, 248, 0.5);
            --danger: #f43f5e;
            --success: #10b981;
        }
        body {
            font-family: 'Inter', system-ui, -apple-system, sans-serif;
            background-color: var(--bg);
            color: var(--text);
            margin: 0;
            padding: 2rem;
            min-height: 100vh;
            background: radial-gradient(circle at 15% 50%, rgba(56, 189, 248, 0.1), transparent 25%),
                        radial-gradient(circle at 85% 30%, rgba(139, 92, 246, 0.1), transparent 25%);
            background-attachment: fixed;
            box-sizing: border-box;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 2rem; }
        .title { margin: 0; font-size: 2.2rem; font-weight: 800; background: linear-gradient(to right, #38bdf8, #818cf8); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .status { padding: 0.5rem 1rem; background: rgba(16, 185, 129, 0.1); color: var(--success); border-radius: 9999px; font-size: 0.875rem; font-weight: 500; border: 1px solid rgba(16, 185, 129, 0.2); display: flex; align-items: center; gap: 8px;}
        .status::before { content: ''; display: block; width: 8px; height: 8px; background: var(--success); border-radius: 50%; box-shadow: 0 0 10px var(--success); }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.5rem; margin-bottom: 2rem; }
        .card { background: var(--surface); border: 1px solid var(--border); border-radius: 1.2rem; padding: 1.5rem; backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1); transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.3s; }
        .card:hover { transform: translateY(-4px); box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.2), 0 0 20px var(--accent-glow); border-color: rgba(255,255,255,0.2); }
        .card-title { color: var(--text-muted); font-size: 0.85rem; text-transform: uppercase; letter-spacing: 0.1em; margin: 0 0 0.75rem 0; font-weight: 600;}
        .card-value { font-size: 3rem; font-weight: 800; margin: 0; line-height: 1; font-variant-numeric: tabular-nums; }
        .card-unit { font-size: 1.2rem; color: var(--text-muted); font-weight: 500; margin-left: 4px;}
        .panels { display: grid; grid-template-columns: 1fr 1fr; gap: 1.5rem; }
        @media (max-width: 900px) { .panels { grid-template-columns: 1fr; } }
        .panel { background: var(--surface); border: 1px solid var(--border); border-radius: 1.2rem; backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); display: flex; flex-direction: column; overflow: hidden; height: 500px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); }
        .panel-header { padding: 1.25rem 1.5rem; border-bottom: 1px solid var(--border); font-weight: 600; display: flex; justify-content: space-between; background: rgba(0,0,0,0.2); }
        .panel-body { flex: 1; overflow-y: auto; padding: 0.5rem; }
        .panel-body::-webkit-scrollbar { width: 6px; }
        .panel-body::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.1); border-radius: 3px; }
        .log-row { display: flex; padding: 0.75rem 1rem; border-bottom: 1px solid rgba(255,255,255,0.03); font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 0.85rem; transition: background 0.15s; border-radius: 6px;}
        .log-row:hover { background: rgba(255,255,255,0.06); }
        .log-time { color: var(--text-muted); width: 85px; flex-shrink: 0; }
        .log-level { width: 65px; flex-shrink: 0; font-weight: 700; }
        .log-level.INFO { color: var(--accent); text-shadow: 0 0 8px var(--accent-glow); }
        .log-level.WARN { color: #fbbf24; }
        .log-level.ERROR { color: var(--danger); text-shadow: 0 0 8px rgba(244, 63, 94, 0.4); }
        .log-msg { flex: 1; word-break: break-all; color: #e2e8f0; }
        
        .req-row { display: flex; padding: 0.75rem 1rem; border-bottom: 1px solid rgba(255,255,255,0.03); font-size: 0.9rem; align-items: center; border-radius: 6px; transition: background 0.15s;}
        .req-row:hover { background: rgba(255,255,255,0.06); }
        .req-method { font-weight: 800; width: 65px; color: #a78bfa; font-family: ui-monospace, monospace; }
        .req-path { flex: 1; font-family: ui-monospace, monospace; color: #cbd5e1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; padding-right: 1rem; }
        .req-status { width: 55px; font-weight: 700; text-align: right; background: rgba(255,255,255,0.05); padding: 2px 6px; border-radius: 4px;}
        .req-status.ok { color: var(--success); background: rgba(16, 185, 129, 0.1); }
        .req-status.err { color: var(--danger); background: rgba(244, 63, 94, 0.1); }
        .req-status.warn { color: #fbbf24; background: rgba(251, 191, 36, 0.1); }
        .req-latency { width: 70px; text-align: right; color: var(--text-muted); font-variant-numeric: tabular-nums; }
        
        @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.7; } 100% { opacity: 1; } }
        .live-dot { display: inline-block; width: 8px; height: 8px; background: var(--accent); border-radius: 50%; box-shadow: 0 0 8px var(--accent); animation: pulse 2s infinite; }
    </style>
</head>
<body>
    <div class="container">
        <header class="header">
            <h1 class="title">Vigilant // Dashboard</h1>
            <div class="status">System Online</div>
        </header>

        <div class="grid">
            <div class="card">
                <p class="card-title">Total Requests</p>
                <h2 class="card-value" id="val-reqs">0</h2>
            </div>
            <div class="card">
                <p class="card-title">Blocked (DDoS)</p>
                <h2 class="card-value" id="val-blocked" style="color: var(--danger)">0</h2>
            </div>
            <div class="card">
                <p class="card-title">Traffic Served</p>
                <h2 class="card-value" id="val-bytes">0.00<span class="card-unit">MB</span></h2>
            </div>
        </div>

        <div class="panels">
            <div class="panel">
                <div class="panel-header">
                    <span><span class="live-dot" style="margin-right:8px;"></span>Live Traffic</span>
                    <span style="color: var(--accent); font-size: 0.85rem;">Last 100</span>
                </div>
                <div class="panel-body" id="list-reqs"></div>
            </div>
            <div class="panel">
                <div class="panel-header">
                    <span>System Logs</span>
                    <span style="color: var(--accent); font-size: 0.85rem;">Last 150</span>
                </div>
                <div class="panel-body" id="list-logs"></div>
            </div>
        </div>
    </div>

    <script>
        function html(str) { return String(str).replace(/[&<>'"]/g, match => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' }[match])); }
        function formatBytes(bytes) { return (bytes / (1024 * 1024)).toFixed(2); }
        
        async function fetchStats() {
            try {
                const res = await fetch('/api/stats');
                const data = await res.json();
                
                document.getElementById('val-reqs').textContent = data.totalRequests.toLocaleString();
                document.getElementById('val-blocked').textContent = data.blockedRequests.toLocaleString();
                document.getElementById('val-bytes').innerHTML = `${formatBytes(data.bytesTransferred)}<span class="card-unit">MB</span>`;
                
                const reqsHtml = data.recentRequests.reverse().map(r => {
                    let stClass = r.status >= 500 ? 'err' : (r.status >= 400 ? 'warn' : 'ok');
                    return `<div class="req-row">
                        <div class="req-method">${html(r.method)}</div>
                        <div class="req-path">${html(r.domain)}${html(r.path)}</div>
                        <div class="req-status ${stClass}">${r.status}</div>
                        <div class="req-latency">${r.latencyMs}ms</div>
                    </div>`;
                }).join('');
                if(reqsHtml) document.getElementById('list-reqs').innerHTML = reqsHtml;
                
                const logsHtml = data.recentLogs.reverse().map(l => `
                    <div class="log-row">
                        <div class="log-time">${html(l.time)}</div>
                        <div class="log-level ${html(l.level)}">${html(l.level)}</div>
                        <div class="log-msg">${html(l.message)}</div>
                    </div>
                `).join('');
                if(logsHtml) document.getElementById('list-logs').innerHTML = logsHtml;
                
            } catch (err) {
                console.error("Dashboard disconnected", err);
            }
        }
        
        fetchStats();
        setInterval(fetchStats, 1000);
    </script>
</body>
</html>
)=====";
