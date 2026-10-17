#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Piglet Wardriver (Waveshare C6 1.47)</title>
<style>
  :root{
    --bg:#0b0f14;
    --card:#121925;
    --text:#e6edf6;
    --muted:#9fb0c3;
    --border:#253043;
    --input:#0f1622;
    --inputBorder:#2b3a52;
    --btn:#1d2a3d;
    --btnHover:#253650;
    --btnText:#e6edf6;
    --code:#0f1622;
    --ok:#2dd4bf;
    --bad:#fb7185;
    --warn:#fbbf24;
    --bar:#e6edf6;
    --barBg:#0f1622;
  }
  html{ color-scheme: dark; }
  body{
    font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;
    margin:16px;
    max-width:900px;
    background:var(--bg);
    color:var(--text);
  }
  h2,h3,h4{ color:var(--text); }
  .card{
    border:1px solid var(--border);
    border-radius:12px;
    padding:14px;
    margin:12px 0;
    background:var(--card);
    box-shadow: 0 6px 24px rgba(0,0,0,.25);
  }
  input,select,button,pre{
    padding:10px;
    border-radius:10px;
    border:1px solid var(--inputBorder);
    width:100%;
    box-sizing:border-box;
    background:var(--input);
    color:var(--text);
  }
  label{ color:var(--muted); }
  button{
    cursor:pointer;
    background:var(--btn);
    border:1px solid var(--inputBorder);
    color:var(--btnText);
    transition: background .12s ease, transform .05s ease;
  }
  button:hover{ background:var(--btnHover); }
  button:active{ transform: translateY(1px); }
  button:disabled{ opacity:.55; cursor:not-allowed; }
  a{ color:var(--ok); text-decoration:none; }
  a:hover{ text-decoration:underline; }
  .row{
    display:grid;
    grid-template-columns:1fr 1fr;
    gap:12px
  }
  code{
    background:var(--code);
    padding:2px 6px;
    border-radius:6px;
    border:1px solid var(--border);
  }
  .barWrap{
    height:10px;
    border:1px solid var(--border);
    border-radius:8px;
    overflow:hidden;
    margin-top:8px;
    background:var(--barBg);
  }
  #wigleBar{
    height:10px;
    width:0%;
    background:var(--bar);
  }
  .ok{ color:var(--ok); }
  .bad{ color:var(--bad); }
  .warn{ color:var(--warn); }
  .muted{ color:var(--muted); }
  .statusGrid{
    display:flex;
    flex-wrap:wrap;
    gap:8px;
    margin:8px 0 12px 0;
  }
  .pill{
    display:inline-flex;
    align-items:center;
    gap:8px;
    border:1px solid var(--border);
    border-radius:999px;
    padding:6px 10px;
    font-size:14px;
    background:var(--input);
    color:var(--text);
  }
  .pill.ok{ border-color: rgba(45,212,191,.6); }
  .pill.bad{ border-color: rgba(251,113,133,.6); }
  .pill.warn{ border-color: rgba(251,191,36,.6); }
  .kv{
    display:grid;
    grid-template-columns:1fr;
    gap:8px;
  }
  @media (min-width:700px){
    .kv{ grid-template-columns:1fr 1fr; }
  }
  .kv > div{
    display:flex;
    justify-content:space-between;
    gap:12px;
    border:1px solid var(--border);
    border-radius:10px;
    padding:10px 12px;
    background:var(--input);
  }
  .k{ color:var(--muted); }
  .v{ font-weight:600; color:var(--text); }
</style>
</head>
<body>
  <h2>Piglet Wardriver (Waveshare ESP32-C6 1.47")</h2>

  <div class="card">
    <h3>Status</h3>

    <div class="statusGrid">
      <div class="pill" id="pillScan">Scan: —</div>
      <div class="pill" id="pillSd">SD: —</div>
      <div class="pill" id="pillGps">GPS: —</div>
      <div class="pill" id="pillSta">STA: —</div>
      <div class="pill" id="pillWigle">WiGLE: —</div>
    </div>

    <div class="kv">
      <div><span class="k">2.4G Found</span><span class="v" id="vFound2g">—</span></div>
      <div><span class="k">STA IP</span><span class="v" id="vStaIp">—</span></div>
      <div><span class="k">AP Clients Seen</span><span class="v" id="vApSeen">—</span></div>
      <div><span class="k">De-dupe</span><span class="v" id="vDedup">—</span></div>
      <div><span class="k">Last Upload</span><span class="v" id="vLastUpload">—</span></div>
    </div>

    <details style="margin-top:10px">
      <summary style="cursor:pointer">Advanced</summary>
       <div class="kv" style="margin-top:8px">
        <div><span class="k">Scan Mode</span><span class="v" id="vScanMode">—</span></div>
        <div><span class="k">GPS Baud</span><span class="v" id="vGpsBaud">—</span></div>
        <div><span class="k">Home SSID</span><span class="v" id="vHomeSsid">—</span></div>
        <div><span class="k">Wardriver SSID</span><span class="v" id="vApSsid">—</span></div>
      </div>
      <div style="margin-top:8px">
        <a href="/status.json" target="_blank" rel="noopener">Open raw status.json</a>
      </div>
    </details>

    <div class="row" style="margin-top:12px">
      <button onclick="fetch('/start',{method:'POST'}).then(loadStatus)">Start Scan</button>
      <button onclick="fetch('/stop',{method:'POST'}).then(loadStatus)">Stop Scan</button>
    </div>

    <div class="row" style="margin-top:10px">
      <button onclick="wigleTest()">Test WiGLE Token</button>
      <button onclick="wigleUploadAll()">Upload all CSVs to WiGLE</button>
    </div>

    <div class="card" style="margin-top:12px">
      <h4 style="margin:0 0 8px 0">WiGLE</h4>
      <div id="wigleMsg" class="muted">—</div>
      <div class="barWrap">
        <div id="wigleBar"></div>
      </div>
    </div>
  </div>

  <div class="card">
    <h3>Config</h3>
    <div class="row">
      <div><label>WiGLE Basic Token (Encoded for Use)</label><input id="wigleBasicToken" placeholder="BASE64(apiName:apiToken)"></div>
      <div><label>GPS Baud</label><input id="gpsBaud" type="number" value="9600"></div>
      <div><label>Home SSID</label><input id="homeSsid"></div>
      <div><label>Home PSK</label><input id="homePsk" type="password"></div>
      <div><label>Wardriver SSID</label><input id="wardriverSsid"></div>
      <div><label>Wardriver PSK</label><input id="wardriverPsk" type="password"></div>
      <div><label>Scan Mode</label>
        <select id="scanMode">
          <option value="aggressive">aggressive</option>
          <option value="powersaving">powersaving</option>
        </select>
      </div>
      <div><label>Speed Units</label>
        <select id="speedUnits">
          <option value="kmh">km/h</option>
          <option value="mph">mph</option>
        </select>
      </div>
      <div style="display:flex;align-items:flex-end;">
        <button onclick="saveCfg()">Save Config</button>
      </div>
    </div>
    <p style="margin-top:10px;">Config is stored at <code>/wardriver.cfg</code> on the SD card.</p>
  </div>

  <div class="card">
    <h3>SD Files</h3>
    <div id="files">Loading...</div>
  </div>

<script>
function setPill(id, text, cls){
  const el = document.getElementById(id);
  if(!el) return;
  el.classList.remove('ok','bad','warn');
  if (cls) el.classList.add(cls);
  el.textContent = text;
}
function wigleStatusText(s){
  if (s === 1) return "WiGLE: Valid";
  if (s === -1) return "WiGLE: Invalid";
  return "WiGLE: Unknown";
}

async function loadStatus(){
  const r = await fetch('/status.json');
  const j = await r.json();

  setPill('pillScan', `Scan: ${j.allowScan ? 'ACTIVE' : 'PAUSED'}`, j.allowScan ? 'ok' : 'warn');
  setPill('pillSd',  `SD: ${j.sdOk ? 'OK' : 'FAIL'}`, j.sdOk ? 'ok' : 'bad');
  setPill('pillGps', `GPS: ${j.gpsFix ? 'LOCK' : 'NO FIX'}`, j.gpsFix ? 'ok' : 'warn');
  setPill('pillSta', `STA: ${j.wifiConnected ? 'CONNECTED' : 'DISCONNECTED'}`, j.wifiConnected ? 'ok' : 'warn');

  const wigleCls = (j.wigleTokenStatus === 1) ? 'ok' : (j.wigleTokenStatus === -1 ? 'bad' : 'warn');
  setPill('pillWigle', wigleStatusText(j.wigleTokenStatus), wigleCls);

  const setText = (id, val) => {
    const el = document.getElementById(id);
    if (el) el.textContent = (val === undefined || val === null || val === "") ? "—" : String(val);
  };

  setText('vFound2g', j.found2g);
  setText('vStaIp', j.wifiConnected ? (j.staIp || '—') : '—');
  setText('vApSeen', j.apClientsSeen ? 'Yes' : 'No');
  setText('vDedup', `${j.seenCount || 0} seen, ${j.seenCollisions || 0} collisions`);

  const lastUp = j.uploadLastResult
    ? `${j.uploadLastResult} (HTTP ${j.wigleLastHttpCode || '—'})`
    : '—';
  setText('vLastUpload', lastUp);

  setText('vScanMode', (j && j.config && j.config.scanMode) ? j.config.scanMode : '—');
  setText('vGpsBaud',  (j && j.config && j.config.gpsBaud) ? j.config.gpsBaud : '—');
  setText('vHomeSsid', (j && j.config && j.config.homeSsid) ? j.config.homeSsid : '—');
  setText('vApSsid',   (j && j.config && j.config.wardriverSsid) ? j.config.wardriverSsid : '—');

  // IMPORTANT: do NOT auto-fill secrets from status.json (prevents "(set)" overwriting token/psk)
  for (const k of ['gpsBaud','homeSsid','wardriverSsid','wardriverPsk','scanMode','speedUnits']){
    if (j.config && (k in j.config)) {
      const el = document.getElementById(k);
      if (el) el.value = j.config[k];
    }
  }
}

async function loadFiles(){
  const r = await fetch('/files.json'); const j = await r.json();
  const el = document.getElementById('files');
  if(!j.ok){ el.textContent = 'SD not available'; return; }

  el.innerHTML = (j.files || []).map(f => `
    <div style="display:flex;gap:8px;align-items:center;margin:8px 0;flex-wrap:wrap">
      <a href="/download?name=${encodeURIComponent(f.name)}">${f.name}</a>
      <span class="muted">${f.size} bytes</span>
      <button style="width:auto" onclick="wigleUploadOne('${f.name}')">Upload</button>
      <button style="width:auto" onclick="delFile('${f.name}')">Delete</button>
    </div>
  `).join('') || '(no files)';
}
async function delFile(name){
  await fetch('/delete?name='+encodeURIComponent(name), {method:'POST'});
  loadFiles();
}

async function saveCfg(){
  const keys = ['wigleBasicToken','gpsBaud','homeSsid','homePsk','wardriverSsid','wardriverPsk','scanMode','speedUnits'];
  let body = "";
  body += "# Saved from Web UI\n";
  body += "# key=value\n";
  for (const k of keys){
    const el = document.getElementById(k);
    const v = el ? (el.value ?? "") : "";
    body += `${k}=${String(v).replace(/\r?\n/g, " ")}\n`;
  }
  await fetch('/saveConfig', { method:'POST', headers:{'Content-Type':'text/plain'}, body });
  await loadStatus();
  alert('Saved. Reboot device to apply GPS/WiFi changes.');
}

function setWigleMsg(t){ document.getElementById('wigleMsg').textContent = t; }

async function wigleTest(){
  setWigleMsg("Testing token...");
  const r = await fetch('/wigle/test', {method:'POST'});
  const j = await r.json().catch(()=>({ok:false,message:"Bad JSON"}));
  setWigleMsg(j.message || (j.ok ? "OK" : "FAIL"));
  await loadStatus();
}

async function wigleUploadAll(){
  setWigleMsg("Uploading all...");
  await fetch('/wigle/uploadAll', {method:'POST'});
}

async function wigleUploadOne(name){
  setWigleMsg("Uploading " + name + " ...");
  await fetch('/wigle/upload?name='+encodeURIComponent(name), {method:'POST'});
  await loadFiles();
  await loadStatus();
}

async function pollUpload(){
  const r = await fetch('/status.json'); const j = await r.json();
  const uploading = !!j.uploading;
  const done = j.uploadDoneFiles || 0;
  const total = j.uploadTotalFiles || 0;

  let pct = 0;
  if (total > 0) pct = Math.round((done/total)*100);

  const bar = document.getElementById('wigleBar');
  bar.style.width = pct + "%";
  bar.style.background = uploading ? "#333" : "#999";

  if (uploading){
    setWigleMsg(`Uploading... ${done}/${total} (${pct}%)`);
  } else if (j.uploadLastResult){
    setWigleMsg(j.uploadLastResult);
  }
}
setInterval(pollUpload, 1000);

loadStatus(); loadFiles();
</script>
</body>
</html>
)HTML";
