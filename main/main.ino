/*
  ==============================
  AttendX RFID Attendance System
  ==============================

  a Project for FCI, Tanta University Provided to Dr. Aida Naser

  Parts:
    ESP32
    RFID Scanner
    DS1302 RTC
    0.96 I2C OLED Display at Address 0x3C
    a Small Phone's Speaker
    12mm*12mm Push Button
    400mah Li-ion Battery (good for about 3 Hours of contiuous Use)
    TP4056 Micro USB BMS Board
    400 Points Breadboard
    Custom 3D-Printed Shell

  Contributers:
    Ahmed Barakat - Team Leader / Backend Embedded Developer (Responsible for the Arduino / Website's Backend Code)
    Abdulrahman Yousef - Shell Designer (Responsible for the 3D Printed Housing)
    Menna Khattab - Frontend Web Developer (Responsible for the Design of the Admin Dashboard and some elemnets of the Backend (JSON Database and Password Entry))
    Hana Nasef - Hardware Engineer (Responsible for Connecting all the Parts Together)
    Hana ElBirmawy - Hardware Engineer (Responsible for Connecting all the Parts Together)
*/

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <vector>
#include <ThreeWire.h>
#include <RtcDS1302.h>

const char *AP_SSID    = "AttendX";          
const char *ADMIN_PASS = "1202";             
const long  GMT_OFFSET = 7200;               
const int   DST_OFFSET = 0;
const char *NTP_SERVER = "pool.ntp.org";

#define RFID_RST_PIN  4
#define RFID_SS_PIN   5
#define BUTTON_PIN    15
#define OLED_SDA      21
#define OLED_SCL      22
#define SPEAKER_PIN   2
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C
#define ATTEND_FILE   "/attendance.json"
#define STUDENTS_FILE "/students.json"
#define str String
#define DS1302_CLK_PIN  17   
#define DS1302_DAT_PIN  16   
#define DS1302_RST_PIN   1   


MFRC522            rfid(RFID_SS_PIN, RFID_RST_PIN);
Adafruit_SSD1306   display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
AsyncWebServer     server(80);
AsyncEventSource   sseSource("/events");
ThreeWire  rtcWire(DS1302_DAT_PIN, DS1302_CLK_PIN, DS1302_RST_PIN);
RtcDS1302<ThreeWire> rtc(rtcWire);

enum DisplayMode {
  MODE_READY,
  MODE_SCAN_RESULT,
  MODE_ENROLL,
  MODE_ATTENDED,
  MODE_ABSENT,
  MODE_WEBLINK,
  MODE_SETTINGS,
  MODE_RESTART,
  MODE_SET_TIME
};


bool useSta      = false;
String staSsid   = "";
String staPassword = "";
bool staConnected  = false;

unsigned long buttonPressStart = 0;
bool buttonHeld = false;
int settingsSelection = 0;


int tsField = 0;
int tsHour = 12, tsMin = 0, tsDay = 1, tsMonth = 1, tsYear = 2026;

DisplayMode   currentMode   = MODE_READY;
unsigned long lastScanTime  = 0;
unsigned long lastButtonPress = 0;
int           totalScansToday = 0;

str  enrollName    = "";
str  enrollSubject = "";
str  enrollStudId  = "";
bool enrollPending = false;
str  attendanceCache = "";
bool cacheDirty   = true;
str  activeToken  = "";
volatile bool pendingRestart = false;


void setupRoutes();
void drawCenteredText(const char *t, int y, bool large = false);
void showReady();
void showScanResult(bool ok, str name, str msg);
void showEnroll();
void showAttended();
void showAbsent();
void showWebLink();
void showRestart();
void showSettings();
void showSetTime();
void handleButton();
void connectWiFi();
void syncTime();
void writeRtcFromSystem();
void readRtcToSystem();
str  getCurrentTimestamp();
str  getCurrentDate();
str  getCurrentTimeDisplay();
str  getCurrentDateDisplay();
str  uidToString(byte *buf, byte len);
void loadConfig();
void saveConfig();
void applyManualTime();
bool processRFID(str uid, str &name, str &msg);



const char LOGIN_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Admin Login</title>
<style>
body{background:linear-gradient(135deg,#00bcd4,#0288d1);font-family:'Segoe UI',sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}
.card{background:white;padding:40px 30px;border-radius:20px;box-shadow:0 15px 35px rgba(0,0,0,0.2);width:320px;text-align:center}
.title{color:#0288d1;margin-bottom:20px;font-size:26px;font-weight:bold}
.subtitle{color:#888;font-size:13px;margin-bottom:25px}
.rfid-icon{font-size:48px;margin-bottom:10px}
input{width:100%;padding:12px;margin:8px 0;border:1px solid #ddd;border-radius:10px;outline:none;transition:.3s;box-sizing:border-box;font-size:15px}
input:focus{border-color:#03a9f4;box-shadow:0 0 5px rgba(3,169,244,.5)}
button{background:linear-gradient(135deg,#03a9f4,#0288d1);color:white;border:none;padding:14px;border-radius:10px;cursor:pointer;font-weight:bold;width:100%;transition:.3s;font-size:15px;margin-top:8px}
button:hover{transform:scale(1.03);opacity:.9}
.err{color:#dc3545;font-size:13px;margin-top:10px;display:none}
.links{margin-top:20px;font-size:13px;color:#888}
.links a{color:#0288d1;text-decoration:none;font-weight:bold}
.links a:hover{text-decoration:underline}
</style>
</head>
<body>
<div class="card">
  <h2 class="title">AttendX</h2>
  <p class="subtitle">Admin Login</p>
  <input type="password" id="adminPass" placeholder="Enter Password" onkeydown="if(event.key==='Enter')login()">
  <button onclick="login()">Login</button>
  <div class="err" id="err">Incorrect Password!</div>
  <div class="links">
    Student? <a href="/student">Submit Attendance</a>
  </div>
</div>
<script>
async function login(){
  const pass=document.getElementById('adminPass').value;
  if(!pass){ document.getElementById('err').style.display='block'; document.getElementById('err').textContent='Enter password.'; return; }
  const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:pass})});
  if(r.ok){
    const d=await r.json();
    sessionStorage.setItem('token',d.token);
    window.location.href='/admin';
  } else {
    document.getElementById('err').style.display='block';
    document.getElementById('err').textContent='Incorrect password.';
    document.getElementById('adminPass').value='';
    document.getElementById('adminPass').focus();
  }
}
if(sessionStorage.getItem('token')) window.location.href='/admin';
</script>
</body>
</html>)rawhtml";
const char ADMIN_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Admin Dashboard</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#00bcd4;font-family:'Segoe UI',sans-serif;min-height:100vh;padding:20px 10px}
.page{max-width:800px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
.card{background:#fff;border-radius:16px;box-shadow:0 6px 20px rgba(0,0,0,.12);padding:20px}
h2{color:#333;font-size:20px;margin-bottom:4px}
.sub{color:#888;font-size:12px;margin-bottom:14px}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:6px}
.stat{background:#f8f9fa;border-radius:10px;padding:12px;text-align:center}
.stat-n{font-size:30px;font-weight:800;font-family:monospace}
.stat-n.green{color:#2ecc71}.stat-n.red{color:#e74c3c}.stat-n.blue{color:#3498db}
.stat-l{font-size:10px;text-transform:uppercase;letter-spacing:.06em;color:#888;margin-top:2px}
.tabs{display:flex;gap:6px;margin-bottom:14px}
.tab{flex:1;padding:9px;border:none;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;background:#f0f0f0;color:#666;transition:.2s}
.tab.active{background:#00bcd4;color:#fff}
.tbl-wrap{overflow-x:auto}
table{width:100%;border-collapse:collapse;font-size:13px}
th{padding:8px 10px;text-align:left;font-size:10px;text-transform:uppercase;letter-spacing:.08em;color:#888;border-bottom:2px solid #eee}
td{padding:10px 10px;border-bottom:1px solid #f0f0f0}
tr:last-child td{border-bottom:none}
tr:hover td{background:#f9fdff}
.badge{display:inline-flex;align-items:center;gap:4px;padding:3px 10px;border-radius:20px;font-size:11px;font-weight:700}
.badge.present{background:#d4edda;color:#155724}
.badge.absent{background:#f8d7da;color:#721c24}
.btn{padding:7px 14px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;border:none;transition:.15s;font-family:inherit}
.btn:hover{filter:brightness(.93)}
.btn-teal{background:#00bcd4;color:#fff}
.btn-green{background:#2ecc71;color:#fff}
.btn-red{background:#e74c3c;color:#fff}
.btn-yellow{background:#f39c12;color:#fff}
.btn-gray{background:#ecf0f1;color:#555}
.btn-sm{padding:4px 10px;font-size:12px}
.action-bar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px;align-items:center}
.action-bar input{flex:1;min-width:140px;padding:8px 12px;border:1px solid #ddd;border-radius:8px;font-size:13px;outline:none}
.action-bar input:focus{border-color:#00bcd4}
.overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.5);z-index:200;align-items:center;justify-content:center}
.overlay.open{display:flex}
.modal{background:#fff;padding:28px;border-radius:18px;width:360px;max-width:95vw;box-shadow:0 20px 50px rgba(0,0,0,.25)}
.modal h3{font-size:16px;margin-bottom:18px;color:#333}
.fg{margin-bottom:13px}
.fg label{display:block;font-size:11px;color:#888;text-transform:uppercase;letter-spacing:.07em;margin-bottom:5px;font-weight:600}
.fg input{width:100%;padding:10px 12px;border:1px solid #ddd;border-radius:8px;font-size:14px;outline:none;font-family:inherit}
.fg input:focus{border-color:#00bcd4}
.mfooter{display:flex;gap:8px;justify-content:flex-end;margin-top:18px}
.scan-wait{text-align:center;padding:20px}
.scan-icon{font-size:52px;margin-bottom:12px;animation:pulse 1.2s ease-in-out infinite}
@keyframes pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.1)}}
.scan-name{font-size:18px;font-weight:700;color:#333;margin-bottom:6px}
.scan-hint{font-size:13px;color:#888}
#toast{position:fixed;bottom:20px;right:20px;background:#2ecc71;color:#fff;padding:12px 20px;border-radius:10px;font-weight:700;font-size:13px;z-index:999;transform:translateY(60px);opacity:0;transition:.3s;pointer-events:none}
#toast.show{transform:none;opacity:1}
#toast.err{background:#e74c3c}
#sse-alert{position:fixed;top:18px;right:18px;background:#fff;border-left:5px solid #2ecc71;border-radius:10px;padding:12px 16px;min-width:200px;box-shadow:0 6px 20px rgba(0,0,0,.15);z-index:999;transform:translateX(130%);transition:.3s;font-size:13px}
#sse-alert.show{transform:none}
#sse-alert.fail{border-left-color:#e74c3c}
.sa-name{font-weight:700;margin-bottom:2px}
.sa-msg{color:#888;font-size:12px}
.header-row{display:flex;align-items:flex-start;justify-content:space-between;gap:10px;flex-wrap:wrap}
.header-btns{display:flex;gap:8px;flex-shrink:0}
.btn-logout{background:#fff;color:#e74c3c;border:1px solid #e74c3c;padding:7px 16px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;transition:.15s;font-family:inherit}
.btn-logout:hover{background:#fdf2f2}
.btn-settime{background:#fff;color:#f39c12;border:1px solid #f39c12;padding:7px 16px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;transition:.15s;font-family:inherit}
.btn-settime:hover{background:#fffbf0}
.btn-clear{background:#fff;color:#888;border:1px solid #ccc;padding:7px 16px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;transition:.15s;font-family:inherit}
.btn-clear:hover{background:#f8f8f8;border-color:#999}
.btn-wifi{background:#fff;color:#00838f;border:1px solid #00bcd4;padding:7px 16px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;transition:.15s;font-family:inherit;text-decoration:none;display:inline-flex;align-items:center;gap:4px}
.btn-wifi:hover{background:#e0f7fa}
</style>
</head>
<body>
<div class="page">

  <div class="card">
    <div class="header-row">
      <div>
        <h2>AttendX</h2>
        <div class="sub" id="dateLabel"></div>
      </div>
      <div class="header-btns">
        <button class="btn-settime" onclick="openSetTime()">Set Time</button>
        <button class="btn-wifi" onclick="openWifiSetup()">WiFi Setup</button>
        <button class="btn-clear" onclick="clearAll()">Clear All Data</button>
        <button class="btn-logout" onclick="logout()">Logout</button>
      </div>
    </div>
    <div class="stats">
      <div class="stat"><div class="stat-n blue" id="stTotal">—</div><div class="stat-l">Students</div></div>
      <div class="stat"><div class="stat-n green" id="stPresent">—</div><div class="stat-l">Present</div></div>
      <div class="stat"><div class="stat-n red" id="stAbsent">—</div><div class="stat-l">Absent</div></div>
    </div>
  </div>

  <div class="card">
    <div class="tabs">
      <button class="tab active" onclick="switchTab('all')">All Students</button>
      <button class="tab" onclick="switchTab('present')">Present</button>
      <button class="tab" onclick="switchTab('absent')">Absent</button>
    </div>

    <div class="action-bar">
      <input type="text" id="searchInput" placeholder="Search name or ID..." oninput="renderTable()">
      <button class="btn btn-teal" onclick="openEnroll()">+ Add Student</button>
      <button class="btn btn-green" onclick="attendAll()">✓ Attend All</button>
      <button class="btn btn-red"   onclick="absentAll()">✗ Absent All</button>
      <button class="btn btn-gray"  onclick="exportCSV()">⬇ CSV</button>
    </div>

    <!-- Table -->
    <div class="tbl-wrap">
      <table>
        <thead>
          <tr>
            <th>#</th>
            <th>Name</th>
            <th>ID / UID</th>
            <th>Subject</th>
            <th>Status</th>
            <th>Time</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody id="tBody"></tbody>
      </table>
    </div>
  </div>

</div>

<div class="overlay" id="enrollModal">
  <div class="modal" id="enrollStep1">
    <h3>Add New Student</h3>
    <div class="fg"><label>Full Name</label><input id="eName" placeholder="Ahmed Hassan" autofocus></div>
    <div class="fg"><label>Student ID (optional)</label><input id="eStudId" placeholder="S001"></div>
    <div class="fg"><label>Subject</label><input id="eSubject" placeholder="IT"></div>
    <div class="mfooter">
      <button class="btn btn-gray" onclick="closeEnroll()">Cancel</button>
      <button class="btn btn-teal" onclick="goScanStep()">Next → Scan Card</button>
    </div>
  </div>
  <div class="modal" id="enrollStep2" style="display:none">
    <div class="scan-wait">
      <div class="scan-name" id="enrollWaitName"></div>
      <div class="scan-hint">Tap the card to the reader now<br>The OLED will show "Scan Card"</div>
    </div>
    <div class="mfooter">
      <button class="btn btn-gray" onclick="cancelEnroll()">Cancel</button>
    </div>
  </div>
</div>

<div class="overlay" id="setTimeModal">
  <div class="modal">
    <h3>Set Device Time</h3>
    <div style="display:flex;gap:10px;margin-bottom:13px">
      <div class="fg" style="flex:1"><label>Date</label><input type="date" id="stDate"></div>
      <div class="fg" style="flex:1"><label>Time</label><input type="time" id="stTime"></div>
    </div>
    <div style="font-size:12px;color:#aaa;margin-bottom:10px">Sends your browser's current date/time to the device. Use this in AP-only mode when there's no internet for NTP.</div>
    <div class="mfooter">
      <button class="btn btn-gray" onclick="closeSetTime()">Cancel</button>
      <button class="btn btn-yellow" onclick="sendTime()">Apply to Device</button>
    </div>
  </div>
</div>

<div id="sse-alert"><div class="sa-name" id="sa-name"></div><div class="sa-msg" id="sa-msg"></div></div>
<div id="toast"></div>

<script>
let students = [];
let currentTab = 'all';
const today = new Date().toISOString().split('T')[0];
const token = sessionStorage.getItem('token');

if(!token){ window.location.href='/'; throw new Error('no token'); }

document.getElementById('dateLabel').textContent =
  new Date().toLocaleDateString('en-GB',{weekday:'long',day:'numeric',month:'long',year:'numeric'});

function authHeaders(){ return {'Content-Type':'application/json','X-Token':token}; }

async function apiFetch(url, opts){
  opts = opts || {};
  opts.headers = Object.assign({}, authHeaders(), opts.headers || {});
  const r = await fetch(url, opts);
  if(r.status === 401){ sessionStorage.removeItem('token'); window.location.href='/'; return null; }
  return r;
}

async function loadAll() {
  try {
    const r = await apiFetch('/api/students/today');
    if(!r) return;
    students = await r.json();
    updateStats();
    renderTable();
  } catch(e) { toast('Connection error','err'); }
}

function updateStats() {
  const present = students.filter(s=>s.attended).length;
  document.getElementById('stTotal').textContent   = students.length;
  document.getElementById('stPresent').textContent = present;
  document.getElementById('stAbsent').textContent  = students.length - present;
}
function switchTab(tab) {
  currentTab = tab;
  document.querySelectorAll('.tab').forEach((t,i)=>{
    t.classList.toggle('active', ['all','present','absent'][i]===tab);
  });
  renderTable();
}
function renderTable() {
  const q   = document.getElementById('searchInput').value.toLowerCase();
  const tb  = document.getElementById('tBody');
  let list  = students;
  if (currentTab==='present') list = list.filter(s=>s.attended);
  if (currentTab==='absent')  list = list.filter(s=>!s.attended);
  if (q) list = list.filter(s=>(s.name||'').toLowerCase().includes(q)||(s.studentId||'').toLowerCase().includes(q));

  if (!list.length) {
    tb.innerHTML=`<tr><td colspan="7"><div class="empty">No students found</div></td></tr>`;
    return;
  }

  tb.innerHTML = list.map((s,i) => {
    const t = s.time ? new Date(s.time).toLocaleTimeString('en-GB',{hour:'2-digit',minute:'2-digit'}) : '—';
    return `<tr>
      <td style="color:#bbb;font-size:12px">${i+1}</td>
      <td><strong>${s.name||'—'}</strong></td>
      <td style="font-family:monospace;font-size:11px">${s.studentId||s.uid||'—'}</td>
      <td>${s.subject||'—'}</td>
      <td>
        ${s.attended
          ? '<span class="badge present">● Present</span>'
          : '<span class="badge absent">✕ Absent</span>'}
      </td>
      <td style="font-size:12px;color:#888">${t}</td>
      <td>
        ${s.attended
          ? `<button class="btn btn-red btn-sm" onclick="markAbsent('${s.uid}')">Mark Absent</button>`
          : `<button class="btn btn-green btn-sm" onclick="markPresent('${s.uid}')">Mark Present</button>`}
        <button class="btn btn-gray btn-sm" style="margin-left:4px" onclick="deleteStudent('${s.uid}')">✕</button>
      </td>
    </tr>`;
  }).join('');
}
async function markPresent(uid) {
  await apiFetch('/api/students/attend', {
    method:'POST', body: JSON.stringify({uid, date: today})
  });
  toast('Marked present');
  await loadAll();
}

async function markAbsent(uid) {
  await apiFetch('/api/students/unattend', {
    method:'POST', body: JSON.stringify({uid, date: today})
  });
  toast('Marked absent');
  await loadAll();
}
async function attendAll() {
  if (!confirm('Mark ALL students as present today?')) return;
  await apiFetch('/api/students/attend-all', {
    method:'POST', body: JSON.stringify({date: today})
  });
  toast('All students marked present');
  await loadAll();
}

async function absentAll() {
  if (!confirm('Mark ALL students as absent today?')) return;
  await apiFetch('/api/students/absent-all', {
    method:'POST', body: JSON.stringify({date: today})
  });
  toast('All students marked absent');
  await loadAll();
}
async function deleteStudent(uid) {
  if (!confirm('Remove this student from the registry?')) return;
  await apiFetch('/api/students/' + encodeURIComponent(uid), {method:'DELETE'});
  toast('Student removed');
  await loadAll();
}
function openEnroll() {
  document.getElementById('eName').value     = '';
  document.getElementById('eStudId').value   = '';
  document.getElementById('eSubject').value  = '';
  document.getElementById('enrollStep1').style.display = '';
  document.getElementById('enrollStep2').style.display = 'none';
  document.getElementById('enrollModal').classList.add('open');
  setTimeout(()=>document.getElementById('eName').focus(), 100);
}

function closeEnroll()  { document.getElementById('enrollModal').classList.remove('open'); }

async function cancelEnroll() {
  await apiFetch('/api/enroll/cancel', {method:'POST'});
  closeEnroll();
}

async function goScanStep(){
  const name=document.getElementById('eName').value.trim();
  const studId=document.getElementById('eStudId').value.trim();
  const subject=document.getElementById('eSubject').value.trim();
  if(!name){alert('Please enter a name');return;}
  const r=await apiFetch('/api/enroll/start',{method:'POST',body:JSON.stringify({name,studentId:studId,subject})});
  if(!r||!r.ok){toast('Error starting enrollment','err');return;}
  document.getElementById('enrollStep1').style.display='none';
  document.getElementById('enrollStep2').style.display='';
  document.getElementById('enrollWaitName').textContent='Enrolling: '+name;
}

function exportCSV(){
  const rows=[['Name','Student ID','UID','Subject','Status','Time']];
  students.forEach(s=>{
    const t=s.time?new Date(s.time).toLocaleString('en-GB'):'';
    rows.push([s.name||'',s.studentId||'',s.uid||'',s.subject||'',s.attended?'Present':'Absent',t]);
  });
  const csv=rows.map(r=>r.map(v=>'"'+v+'"').join(',')).join('\n');
  const a=document.createElement('a');
  a.href='data:text/csv;charset=utf-8,\uFEFF'+encodeURIComponent(csv);
  a.download='attendance_'+today+'.csv';
  a.click();
}

async function clearAll(){
  if(!confirm('This will delete ALL students and ALL attendance records. Are you sure?'))return;
  if(!confirm('This cannot be undone. Confirm again to proceed.'))return;
  const r=await apiFetch('/api/reset',{method:'POST'});
  if(r&&r.ok){toast('All data cleared');await loadAll();}
  else toast('Error clearing data','err');
}

async function logout(){
  await apiFetch('/api/logout',{method:'POST'});
  sessionStorage.removeItem('token');
  window.location.href='/';
}

function connectSSE(){
  const es=new EventSource('/events');
  es.onmessage=e=>{
    try{
      const d=JSON.parse(e.data);
      const el=document.getElementById('sse-alert');
      document.getElementById('sa-name').textContent=d.name||'Unknown';
      document.getElementById('sa-msg').textContent=d.message||'';
      el.className=d.ok?'show':'show fail';
      setTimeout(()=>{el.className='';},4000);
      if(d.type==='enrolled'){closeEnroll();toast('Student enrolled: '+d.name);}
      setTimeout(loadAll,600);
    }catch(err){}
  };
  es.onerror=()=>setTimeout(connectSSE,5000);
}

function toast(msg,type){
  const t=document.getElementById('toast');
  t.textContent=msg;
  t.className='show'+(type?' '+type:'');
  setTimeout(()=>{t.className='';},3000);
}

document.getElementById('enrollModal').addEventListener('click',function(e){
  if(e.target===document.getElementById('enrollModal'))closeEnroll();
});

loadAll();connectSSE();setInterval(loadAll,6000);


fetch('/api/settime',{method:'POST',headers:{'Content-Type':'application/json','X-Token':token},body:JSON.stringify({ts:Math.floor(Date.now()/1000)})});

function openSetTime(){
  const now=new Date();
  document.getElementById('stDate').value=now.toISOString().split('T')[0];
  document.getElementById('stTime').value=now.toTimeString().slice(0,5);
  document.getElementById('setTimeModal').classList.add('open');
}
function closeSetTime(){ document.getElementById('setTimeModal').classList.remove('open'); }
async function sendTime(){
  const d=document.getElementById('stDate').value;
  const t=document.getElementById('stTime').value;
  if(!d||!t){toast('Pick a date and time','err');return;}
  const ts=Math.floor(new Date(d+'T'+t).getTime()/1000);
  const r=await apiFetch('/api/settime',{method:'POST',body:JSON.stringify({ts})});
  if(r&&r.ok){toast('Device time updated ✓');closeSetTime();}
  else toast('Failed to set time','err');
}

function openWifiSetup(){
  window.open('/wifi-setup?token='+encodeURIComponent(token),'_blank');
}
</script>
</body>
</html>)rawhtml";

const char STUDENT_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Student Attendance</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background-color:#00bcd4;font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}
.card{background:white;padding:35px 28px;border-radius:20px;box-shadow:0 10px 25px rgba(0,0,0,.2);width:100%;max-width:400px;text-align:center}
.icon{font-size:44px;margin-bottom:10px}
h2{color:#333;margin-bottom:6px;font-size:22px}
.subtitle{color:#888;font-size:13px;margin-bottom:22px}
.form-group{text-align:left;margin-bottom:14px}
label{display:block;margin-bottom:5px;font-size:13px;color:#555;font-weight:bold}
input,select{width:100%;padding:12px;border:1px solid #ddd;border-radius:10px;font-size:14px;outline:none;transition:.3s}
input:focus,select:focus{border-color:#00bcd4;box-shadow:0 0 5px rgba(0,188,212,.3)}
button{background-color:#2ecc71;color:white;border:none;padding:15px;border-radius:10px;cursor:pointer;font-weight:bold;width:100%;font-size:16px;margin-top:8px;transition:.3s}
button:hover{background-color:#27ae60;transform:scale(1.02)}
button:disabled{background:#aaa;cursor:not-allowed;transform:none}
.msg{margin-top:14px;padding:12px;border-radius:10px;font-size:14px;display:none}
.msg.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
.msg.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}
.back-link{display:block;margin-top:16px;color:#0288d1;text-decoration:none;font-size:13px}
.back-link:hover{text-decoration:underline}
.subjects{display:flex;flex-wrap:wrap;gap:6px;margin-top:6px}
.subject-chip{padding:6px 12px;border-radius:20px;background:#e0f7fa;color:#006064;font-size:13px;cursor:pointer;border:2px solid transparent;transition:.2s;user-select:none}
.subject-chip.selected{background:#00bcd4;color:white;border-color:#00838f}
</style>
</head>
<body>
<div class="card">
  <h2>Student Attendance</h2>
  <p class="subtitle">Fill in your details to mark attendance</p>

  <div class="form-group">
    <label for="studentName">Full Name</label>
    <input type="text" id="studentName" placeholder="Enter Full Name">
  </div>

  <div class="form-group">
    <label for="studentID">Student ID</label>
    <input type="text" id="studentID" placeholder="Enter Student ID">
  </div>

  <div class="form-group">
    <label>Subject</label>
    <div class="subjects" id="subjectChips">
      <div class="subject-chip" onclick="selectSubject(this)">IT</div>
      <div class="subject-chip" onclick="selectSubject(this)">Math</div>
      <div class="subject-chip" onclick="selectSubject(this)">Physics</div>
      <div class="subject-chip" onclick="selectSubject(this)">English</div>
      <div class="subject-chip" onclick="selectSubject(this)">Chemistry</div>
      <div class="subject-chip" onclick="selectSubject(this)">Biology</div>
    </div>
    <input type="text" id="subjectName" placeholder="Or type subject name..." style="margin-top:10px">
  </div>

  <button type="button" id="submitBtn" onclick="submitAttendance()">Submit Attendance</button>

  <div class="msg" id="msg"></div>
  <a class="back-link" href="/">← Back to Login</a>
</div>

<script>
function selectSubject(el){
  document.querySelectorAll('.subject-chip').forEach(c=>c.classList.remove('selected'));
  el.classList.add('selected');
  document.getElementById('subjectName').value=el.textContent;
}

async function submitAttendance(){
  const name=document.getElementById('studentName').value.trim();
  const id=document.getElementById('studentID').value.trim();
  const subject=document.getElementById('subjectName').value.trim();
  const msg=document.getElementById('msg');
  const btn=document.getElementById('submitBtn');

  if(!name||!id||!subject){
    msg.className='msg error';msg.style.display='block';
    msg.textContent='Please fill in all fields before submitting.';return;
  }

  btn.disabled=true;btn.textContent='Submitting...';
  try{
    const r=await fetch('/api/attendance/manual',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({name,id,subject,source:'manual'})
    });
    const d=await r.json();
    if(r.ok){
      msg.className='msg success';msg.style.display='block';
      msg.textContent=`✓ Attendance submitted for ${name} in ${subject}!`;
      document.getElementById('studentName').value='';
      document.getElementById('studentID').value='';
      document.getElementById('subjectName').value='';
      document.querySelectorAll('.subject-chip').forEach(c=>c.classList.remove('selected'));
    } else {
      msg.className='msg error';msg.style.display='block';
      msg.textContent=d.message||'Submission failed. Please try again.';
    }
  }catch(e){
    msg.className='msg error';msg.style.display='block';
    msg.textContent='Connection error. Please try again.';
  }
  btn.disabled=false;btn.textContent='Submit Attendance';
}
</script>
</body>
</html>)rawhtml";
const char WIFI_SETUP_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Setup – AttendX</title>
<style>
body{background:linear-gradient(135deg,#00bcd4,#0288d1);font-family:'Segoe UI',sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:20px;box-sizing:border-box}
.card{background:#fff;padding:36px 28px;border-radius:20px;box-shadow:0 15px 40px rgba(0,0,0,.2);width:100%;max-width:380px;text-align:center}
h2{color:#0288d1;font-size:24px;margin-bottom:4px}
.sub{color:#888;font-size:13px;margin-bottom:28px}
.fg{text-align:left;margin-bottom:16px}
label{display:block;font-size:12px;font-weight:700;color:#555;text-transform:uppercase;letter-spacing:.06em;margin-bottom:6px}
input{width:100%;padding:12px;border:1px solid #ddd;border-radius:10px;font-size:15px;outline:none;transition:.3s;box-sizing:border-box}
input:focus{border-color:#03a9f4;box-shadow:0 0 5px rgba(3,169,244,.4)}
button{background:linear-gradient(135deg,#03a9f4,#0288d1);color:#fff;border:none;padding:14px;border-radius:10px;width:100%;font-size:15px;font-weight:700;cursor:pointer;margin-top:6px;transition:.3s}
button:hover{opacity:.9;transform:scale(1.02)}
.btn-scan{background:linear-gradient(135deg,#f39c12,#d68910);margin-bottom:15px}
.msg{margin-top:14px;padding:12px;border-radius:10px;font-size:14px;display:none}
.msg.ok{background:#d4edda;color:#155724}
.msg.err{background:#f8d7da;color:#721c24}
.back{display:block;margin-top:18px;color:#0288d1;font-size:13px;text-decoration:none}
.back:hover{text-decoration:underline}
.eye{position:relative}
.eye input{padding-right:40px}
.eye-btn{position:absolute;right:10px;top:50%;transform:translateY(-50%);background:none;border:none;cursor:pointer;font-size:18px;width:auto;padding:0;margin:0;color:#888}
.net-item{padding:10px;border-bottom:1px solid #eee;cursor:pointer;display:flex;justify-content:space-between;align-items:center}
.net-item:hover{background:#f9f9f9}
.net-ssid{font-weight:bold;color:#333;font-size:14px}
.net-rssi{color:#888;font-size:12px}
#scanList{max-height:160px;overflow-y:auto;border:1px solid #eee;border-radius:8px;margin-bottom:15px;display:none;text-align:left}
</style>
</head>
<body>
<div class="card">
  <h2>WiFi Setup</h2>
  <p class="sub">Select a network or enter manually</p>
  
  <button id="scanBtn" class="btn-scan" onclick="scanWifi()">Scan For Networks</button>
  <div id="scanList"></div>

  <div id="manualSec" style="display:none">
    <div class="fg">
      <label for="ssid">Network Name (SSID)</label>
      <input type="text" id="ssid" placeholder="My Home WiFi" autocomplete="off">
    </div>
    <div class="fg">
      <label for="pwd">Password</label>
      <div class="eye">
        <input type="password" id="pwd" placeholder="••••••••" autocomplete="new-password">
        <button class="eye-btn" type="button" onclick="togglePwd()" title="Show/hide">👁</button>
      </div>
    </div>
    <button onclick="save()">Save &amp; Apply</button>
  </div>
  <a href="#" id="showManBtn" class="back" onclick="showManual()" style="margin-top:5px">Add Manually...</a>
  <div class="msg" id="msg"></div>
  <a class="back" href="/">← Back to Login</a>
</div>
<script>
const tk=new URLSearchParams(window.location.search).get('token')||'';
if(!tk) window.location.href='/';

function togglePwd(){
  const i=document.getElementById('pwd');
  i.type=i.type==='password'?'text':'password';
}
function showManual(){
  document.getElementById('manualSec').style.display='block';
  document.getElementById('showManBtn').style.display='none';
  document.getElementById('scanList').style.display='none';
}
function selectNet(ssid){
  showManual();
  document.getElementById('ssid').value=ssid;
  document.getElementById('pwd').focus();
}
async function scanWifi(){
  const btn=document.getElementById('scanBtn');
  const lst=document.getElementById('scanList');
  btn.disabled=true; btn.textContent='Scanning...';
  try{
    const r=await fetch('/api/wifi/scan',{headers:{'X-Token':tk}});
    const nets=await r.json();
    lst.innerHTML='';
    if(nets.length===0){
      lst.innerHTML='<div style="padding:10px;text-align:center;color:#888">No networks found</div>';
    } else {
      nets.forEach(n=>{
        const d=document.createElement('div');
        d.className='net-item';
        d.onclick=()=>selectNet(n.ssid);
        d.innerHTML='<span class="net-ssid">'+n.ssid+(n.isOpen?' (Open)':'')+'</span><span class="net-rssi">'+n.rssi+' dBm</span>';
        lst.appendChild(d);
      });
    }
    lst.style.display='block';
    document.getElementById('manualSec').style.display='none';
    document.getElementById('showManBtn').style.display='block';
  }catch(e){
    alert('Scan failed');
  }
  btn.disabled=false; btn.textContent='Scan For Networks';
}
async function save(){
  const ssid=document.getElementById('ssid').value.trim();
  const pwd=document.getElementById('pwd').value;
  const msg=document.getElementById('msg');
  if(!ssid){msg.className='msg err';msg.style.display='block';msg.textContent='Please enter a network name.';return;}
  msg.className='msg';msg.style.display='none';
  try{
    const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json','X-Token':tk},body:JSON.stringify({ssid,password:pwd})});
    const d=await r.json();
    if(r.ok){
      msg.className='msg ok';msg.style.display='block';
      msg.textContent='✓ Saved! The device will connect to "'+ssid+'" on next boot. You can also hold the button now to apply immediately.';
    } else {
      msg.className='msg err';msg.style.display='block';
      msg.textContent=d.error||'Failed to save.';
    }
  }catch(e){
    msg.className='msg err';msg.style.display='block';
    msg.textContent='Connection error.';
  }
}

fetch('/api/wifi').then(r=>r.json()).then(d=>{
  if(d.ssid) {
    document.getElementById('ssid').value=d.ssid;
  }
}).catch(()=>{});


scanWifi();
</script>
</body>
</html>)rawhtml";



str readFile(const char *path) {
  if (!SPIFFS.exists(path))
    return "[]";
  File f = SPIFFS.open(path, "r");
  if (!f)
    return "[]";
  str s = f.readString();
  f.close();
  return s.length() > 0 ? s : "[]";
}

bool writeFile(const char *path, const str &data) {
  File f = SPIFFS.open(path, "w");
  if (!f)
    return false;
  f.print(data);
  f.close();
  return true;
}
DynamicJsonDocument loadStudents() {
  DynamicJsonDocument doc(16384);
  str raw = readFile(STUDENTS_FILE);
  DeserializationError err = deserializeJson(doc, raw);
  if (err || !doc.is<JsonArray>())
    doc.to<JsonArray>();
  return doc;
}
void saveStudents(DynamicJsonDocument &doc) {
  str out;
  serializeJson(doc, out);
  writeFile(STUDENTS_FILE, out);
}
DynamicJsonDocument loadAttendance() {
  DynamicJsonDocument doc(32768);
  if (cacheDirty || attendanceCache.isEmpty()) {
    attendanceCache = readFile(ATTEND_FILE);
    cacheDirty = false;
  }
  DeserializationError err = deserializeJson(doc, attendanceCache);
  if (err || !doc.is<JsonArray>())
    doc.to<JsonArray>();
  return doc;
}
void saveAttendanceDoc(DynamicJsonDocument &doc) {
  str out;
  serializeJson(doc, out);
  writeFile(ATTEND_FILE, out);
  attendanceCache = out;
  cacheDirty = false;
}
bool hasAttended(const str &uid, const str &date) {
  DynamicJsonDocument doc = loadAttendance();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject r : arr) {
    if (r["uid"].as<str>() == uid && r["date"].as<str>() == date)
      return true;
  }
  return false;
}
void recordAttendance(const str &uid, const str &date, const str &ts) {
  DynamicJsonDocument doc = loadAttendance();
  JsonArray arr = doc.as<JsonArray>();
  JsonObject r = arr.createNestedObject();
  r["uid"] = uid;
  r["date"] = date;
  r["timestamp"] = ts;
  saveAttendanceDoc(doc);
}
void removeAttendance(const str &uid, const str &date) {
  DynamicJsonDocument doc = loadAttendance();
  JsonArray arr = doc.as<JsonArray>();
  DynamicJsonDocument newDoc(32768);
  JsonArray newArr = newDoc.to<JsonArray>();
  for (JsonObject r : arr) {
    if (!(r["uid"].as<str>() == uid && r["date"].as<str>() == date))
      newArr.add(r);
  }
  saveAttendanceDoc(newDoc);
}

void seedIfEmpty() {
  DynamicJsonDocument doc = loadStudents();
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() > 0)
    return;
  JsonObject s = arr.createNestedObject();
  s["uid"] = "AA:BB:CC:DD";
  s["name"] = "Sample Student";
  s["studentId"] = "S001";
  s["subject"] = "IT";
  saveStudents(doc);
  Serial.println("Seeded sample student");
}


bool processRFID(str uid, str &name, str &msg) {
  str today = getCurrentDate();
  str ts = getCurrentTimestamp();
  if (enrollPending) {
    DynamicJsonDocument studs = loadStudents();
    JsonArray arr = studs.as<JsonArray>();
    for (JsonObject s : arr) {
      if (s["uid"].as<str>() == uid) {
        name = s["name"].as<str>();
        msg = "Card already registered";
        enrollPending = false;
        currentMode = MODE_READY;
        return false;
      }
    }
    JsonObject ns = arr.createNestedObject();
    ns["uid"] = uid;
    ns["name"] = enrollName;
    ns["studentId"] = enrollStudId;
    ns["subject"] = enrollSubject;
    saveStudents(studs);
    name = enrollName;
    msg = "Enrolled!";
    enrollPending = false;
    currentMode = MODE_READY;
    str sse = "{\"type\":\"enrolled\",\"ok\":true,\"name\":\"" + enrollName +
              "\",\"uid\":\"" + uid + "\",\"message\":\"Enrolled!\"}";
    sseSource.send(sse.c_str(), NULL, millis());
    totalScansToday++;
    return true;
  }
  DynamicJsonDocument studs = loadStudents();
  JsonArray arr = studs.as<JsonArray>();
  bool found = false;
  for (JsonObject s : arr) {
    if (s["uid"].as<str>() == uid) {
      name = s["name"].as<str>();
      found = true;
      break;
    }
  }

  if (!found) {
    msg = "Unknown Card";
    return false;
  }

  if (hasAttended(uid, today)) {
    msg = "Already checked in";
    return false;
  }

  recordAttendance(uid, today, ts);
  msg = "Checked In!";
  totalScansToday++;
  return true;
}
void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", LOGIN_HTML);
  });
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", ADMIN_HTML);
  });
  server.on("/student", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", STUDENT_HTML);
  });
  server.on(
      "/api/login", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(256);
        deserializeJson(inp, body);
        str pass = inp["password"].as<str>();
        if (pass == ADMIN_PASS) {
          activeToken = str(millis()) + str(random(100000, 999999));
          str resp = "{\"token\":\"" + activeToken + "\"}";
          req->send(200, "application/json", resp);
        } else {
          req->send(401, "application/json", "{\"error\":\"wrong password\"}");
        }
      });

  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest *req) {
    activeToken = "";
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!req->hasHeader("X-Token") ||
        req->getHeader("X-Token")->value() != activeToken ||
        activeToken.isEmpty()) {
      req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    DynamicJsonDocument empty(64);
    empty.to<JsonArray>();
    str out;
    serializeJson(empty, out);
    writeFile(STUDENTS_FILE, out);
    writeFile(ATTEND_FILE, out);
    attendanceCache = out;
    cacheDirty = false;
    totalScansToday = 0;
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/students/today", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasHeader("X-Token") ||
        req->getHeader("X-Token")->value() != activeToken ||
        activeToken.isEmpty()) {
      req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    str today = getCurrentDate();
    DynamicJsonDocument studs = loadStudents();
    DynamicJsonDocument att = loadAttendance();
    JsonArray sArr = studs.as<JsonArray>();
    JsonArray aArr = att.as<JsonArray>();

    
    std::vector<str> presentUIDs;
    str presentTimes[100];
    int pCount = 0;
    for (JsonObject r : aArr) {
      if (r["date"].as<str>() == today) {
        presentUIDs.push_back(r["uid"].as<str>());
      }
    }

    DynamicJsonDocument out(16384);
    JsonArray oArr = out.to<JsonArray>();
    for (JsonObject s : sArr) {
      str uid = s["uid"].as<str>();
      bool attended = false;
      str ts = "";
      for (JsonObject r : aArr) {
        if (r["uid"].as<str>() == uid && r["date"].as<str>() == today) {
          attended = true;
          ts = r["timestamp"].as<str>();
          break;
        }
      }
      JsonObject row = oArr.createNestedObject();
      row["uid"] = uid;
      row["name"] = s["name"];
      row["studentId"] = s["studentId"];
      row["subject"] = s["subject"];
      row["attended"] = attended;
      row["time"] = ts;
    }
    str resp;
    serializeJson(out, resp);
    req->send(200, "application/json", resp);
  });
  server.on(
      "/api/students/attend", HTTP_POST, [](AsyncWebServerRequest *req) {},
      NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        if (!req->hasHeader("X-Token") ||
            req->getHeader("X-Token")->value() != activeToken ||
            activeToken.isEmpty()) {
          req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
          return;
        }
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(512);
        deserializeJson(inp, body);
        str uid = inp["uid"].as<str>();
        str date = inp["date"].as<str>();
        if (uid.isEmpty() || date.isEmpty()) {
          req->send(400, "application/json", "{\"error\":\"missing\"}");
          return;
        }
        if (!hasAttended(uid, date))
          recordAttendance(uid, date, getCurrentTimestamp());
        req->send(200, "application/json", "{\"ok\":true}");
      });
  server.on(
      "/api/students/unattend", HTTP_POST, [](AsyncWebServerRequest *req) {},
      NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        if (!req->hasHeader("X-Token") ||
            req->getHeader("X-Token")->value() != activeToken ||
            activeToken.isEmpty()) {
          req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
          return;
        }
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(512);
        deserializeJson(inp, body);
        str uid = inp["uid"].as<str>();
        str date = inp["date"].as<str>();
        if (uid.isEmpty() || date.isEmpty()) {
          req->send(400, "application/json", "{\"error\":\"missing\"}");
          return;
        }
        removeAttendance(uid, date);
        req->send(200, "application/json", "{\"ok\":true}");
      });
  server.on(
      "/api/students/attend-all", HTTP_POST, [](AsyncWebServerRequest *req) {},
      NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(256);
        deserializeJson(inp, body);
        str date = inp["date"].as<str>();
        str ts = getCurrentTimestamp();
        DynamicJsonDocument studs = loadStudents();
        JsonArray arr = studs.as<JsonArray>();
        for (JsonObject s : arr) {
          str uid = s["uid"].as<str>();
          if (!hasAttended(uid, date))
            recordAttendance(uid, date, ts);
        }
        req->send(200, "application/json", "{\"ok\":true}");
      });
  server.on(
      "/api/students/absent-all", HTTP_POST, [](AsyncWebServerRequest *req) {},
      NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(256);
        deserializeJson(inp, body);
        str date = inp["date"].as<str>();
        DynamicJsonDocument studs = loadStudents();
        JsonArray arr = studs.as<JsonArray>();
        for (JsonObject s : arr)
          removeAttendance(s["uid"].as<str>(), date);
        req->send(200, "application/json", "{\"ok\":true}");
      });

  
  server.on("/api/students/*", HTTP_DELETE, [](AsyncWebServerRequest *req) {
    str uid = req->url().substring(14); 
    DynamicJsonDocument doc = loadStudents();
    JsonArray arr = doc.as<JsonArray>();
    DynamicJsonDocument newDoc(16384);
    JsonArray newArr = newDoc.to<JsonArray>();
    for (JsonObject s : arr) {
      if (s["uid"].as<str>() != uid)
        newArr.add(s);
    }
    saveStudents(newDoc);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on(
      "/api/enroll/start", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(512);
        deserializeJson(inp, body);
        enrollName = inp["name"].as<str>();
        enrollStudId = inp["studentId"].as<str>();
        enrollSubject = inp["subject"] | "General";
        enrollPending = true;
        currentMode = MODE_ENROLL;
        showEnroll();
        Serial.println("Enroll mode: waiting for card for " + enrollName);
        req->send(200, "application/json", "{\"ok\":true}");
      });
  server.on("/api/enroll/cancel", HTTP_POST, [](AsyncWebServerRequest *req) {
    enrollPending = false;
    currentMode = MODE_READY;
    showReady();
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on(
      "/api/attendance/manual", HTTP_POST, [](AsyncWebServerRequest *req) {},
      NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(512);
        deserializeJson(inp, body);
        str name = inp["name"].as<str>();
        str id = inp["id"].as<str>();
        str subject = inp["subject"].as<str>();
        if (name.isEmpty() || id.isEmpty()) {
          req->send(400, "application/json",
                    "{\"message\":\"Fields required\"}");
          return;
        }
        DynamicJsonDocument studs = loadStudents();
        JsonArray arr = studs.as<JsonArray>();
        bool found = false;
        str uid = "";
        for (JsonObject s : arr) {
          if (s["studentId"].as<str>() == id) {
            found = true;
            uid = s["uid"].as<str>();
            break;
          }
        }
        if (!found) {
          uid = "M:" + id; 
          JsonObject ns = arr.createNestedObject();
          ns["uid"] = uid;
          ns["name"] = name;
          ns["studentId"] = id;
          ns["subject"] = subject;
          saveStudents(studs);
        }
        if (!hasAttended(uid, getCurrentDate()))
          recordAttendance(uid, getCurrentDate(), getCurrentTimestamp());
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on(
      "/api/settime", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(128);
        deserializeJson(inp, body);
        long ts = inp["ts"].as<long>();
        if (ts > 1000000000) {
          struct timeval tv;
          tv.tv_sec = ts;
          tv.tv_usec = 0;
          settimeofday(&tv, NULL);
          writeRtcFromSystem();  
          
          req->send(200, "application/json", "{\"ok\":true}");
        } else {
          req->send(400, "application/json", "{\"error\":\"bad timestamp\"}");
        }
      });

  
  server.on("/wifi-setup", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("token") ||
        req->getParam("token")->value() != activeToken ||
        activeToken.isEmpty()) {
      req->redirect("/");
      return;
    }
    req->send_P(200, "text/html", WIFI_SETUP_HTML);
  });

  
  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasHeader("X-Token") ||
        req->getHeader("X-Token")->value() != activeToken ||
        activeToken.isEmpty()) {
      req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0)
        json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json +=
          "\"isOpen\":" +
          String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") +
          "}";
    }
    json += "]";
    req->send(200, "application/json", json);
  });
  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
    String resp = "{\"ssid\":\"" + staSsid +
                  "\",\"useSta\":" + (useSta ? "true" : "false") + "}";
    req->send(200, "application/json", resp);
  });
  server.on(
      "/api/wifi", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t idx,
         size_t total) {
        if (!req->hasHeader("X-Token") ||
            req->getHeader("X-Token")->value() != activeToken ||
            activeToken.isEmpty()) {
          req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
          return;
        }
        str body = str((char *)data).substring(0, len);
        DynamicJsonDocument inp(256);
        deserializeJson(inp, body);
        str newSsid = inp["ssid"].as<str>();
        str newPass = inp["password"].as<str>();
        if (newSsid.isEmpty()) {
          req->send(400, "application/json", "{\"error\":\"SSID required\"}");
          return;
        }
        staSsid = newSsid;
        staPassword = newPass;
        saveConfig();
        Serial.println("WiFi credentials updated: " + staSsid);
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "Not found");
  });
}
void writeRtcFromSystem() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  RtcDateTime dt(
    (uint16_t)(ti.tm_year + 1900),
    (uint8_t) (ti.tm_mon + 1),
    (uint8_t)  ti.tm_mday,
    (uint8_t)  ti.tm_hour,
    (uint8_t)  ti.tm_min,
    (uint8_t)  ti.tm_sec
  );
  rtc.SetDateTime(dt);
  
}
void readRtcToSystem() {
  if (!rtc.IsDateTimeValid()) {
    
    return;
  }
  RtcDateTime dt = rtc.GetDateTime();

  struct tm ti = {};
  ti.tm_year = dt.Year() - 1900;
  ti.tm_mon  = dt.Month() - 1;
  ti.tm_mday = dt.Day();
  ti.tm_hour = dt.Hour();
  ti.tm_min  = dt.Minute();
  ti.tm_sec  = dt.Second();

  time_t t = mktime(&ti);
  struct timeval tv = { t, 0 };
  settimeofday(&tv, NULL);
  
  dt.Year(), dt.Month(), dt.Day(), dt.Hour(), dt.Minute(), dt.Second());
}
void syncTime() {
  if (useSta && staConnected) {
    
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    
    struct tm ti;
    int attempts = 0;
    while (!getLocalTime(&ti) && attempts < 20) {
      delay(500);
      
      attempts++;
    }
    if (getLocalTime(&ti)) {
      
      writeRtcFromSystem();   
    } else {
      
      readRtcToSystem();
    }
  } else {
    
    configTime(GMT_OFFSET, DST_OFFSET, ""); 
    readRtcToSystem();
  }
}


void applyManualTime() {
  struct tm ti = {};
  ti.tm_hour = tsHour;
  ti.tm_min  = tsMin;
  ti.tm_sec  = 0;
  ti.tm_mday = tsDay;
  ti.tm_mon  = tsMonth - 1;
  ti.tm_year = tsYear - 1900;

  time_t t = mktime(&ti);
  struct timeval tv = { t, 0 };
  settimeofday(&tv, NULL);
  configTime(GMT_OFFSET, DST_OFFSET, ""); 

  writeRtcFromSystem();  
  
}


str getCurrentTimestamp() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "1970-01-01T00:00:00";
  char b[25];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &ti);
  return str(b);
}
str getCurrentDate() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "1970-01-01";
  char b[12];
  strftime(b, sizeof(b), "%Y-%m-%d", &ti);
  return str(b);
}
str getCurrentTimeDisplay() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "--:--";
  char b[8];
  strftime(b, sizeof(b), "%H:%M", &ti);
  return str(b);
}
str getCurrentDateDisplay() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "----";
  char b[20];
  strftime(b, sizeof(b), "%d %b %Y", &ti);
  return str(b);
}
void setup() {
  rtc.Begin();
  if (!rtc.GetIsRunning()) {
    
    rtc.SetIsRunning(true);
  }
  
  rtc.SetIsWriteProtected(false);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  drawCenteredText("AttendX", 24, true);
  display.display();
  delay(800);
  if (SPIFFS.begin(true)) {
    seedIfEmpty();
  }
  loadConfig();  
  SPI.begin();
  rfid.PCD_Init();
  speakerSetup();
  connectWiFi();
  syncTime();   
  setupRoutes();
  sseSource.onConnect([](AsyncEventSourceClient *c) {
  });
  server.addHandler(&sseSource);
  server.begin();
  soundBoot();
  showReady();
}

void loop() {
  handleButton();

  if (currentMode == MODE_SCAN_RESULT && millis() - lastScanTime > 4000) {
    currentMode = MODE_READY;
    showReady();
  }
  static unsigned long lastClockUpdate = 0;
  if (currentMode == MODE_READY && millis() - lastClockUpdate > 30000) {
    lastClockUpdate = millis();
    showReady();
  }
  static unsigned long lastEnrollPulse = 0;
  if (currentMode == MODE_ENROLL && millis() - lastEnrollPulse > 1000) {
    lastEnrollPulse = millis();
    showEnroll();
  }
  static unsigned long lastSettingsRefresh = 0;
  if (currentMode == MODE_SETTINGS && millis() - lastSettingsRefresh > 1000) {
    lastSettingsRefresh = millis();
    showSettings();
  }
  static unsigned long lastRFIDReinit = 0;
  if (millis() - lastRFIDReinit > 5000) {
    lastRFIDReinit = millis();
    rfid.PCD_Init();
  }

  if (currentMode != MODE_READY && currentMode != MODE_ENROLL &&
      currentMode != MODE_SETTINGS)
    return;

  if (!rfid.PICC_IsNewCardPresent()) { rfid.PCD_StopCrypto1(); return; }
  if (!rfid.PICC_ReadCardSerial())   { rfid.PICC_HaltA();      return; }

  str uid = uidToString(rfid.uid.uidByte, rfid.uid.size);
  

  display.clearDisplay();
  drawCenteredText(enrollPending ? "ENROLLING..." : "SCANNING...", 18);
  display.setTextSize(1);
  drawCenteredText(uid.c_str(), 36);
  display.display();

  str name, msg;
  bool ok = processRFID(uid, name, msg);

  if (ok) {
    soundSuccess();
    if (!enrollPending) totalScansToday++;
  } else if (msg == "Already checked in") {
    soundAlreadyScanned();
  } else {
    soundFail();
  }

  if (!enrollPending || !ok) {
    str sse = "{\"type\":\"scan\",\"ok\":" + str(ok ? "true" : "false") +
              ",\"name\":\"" + name + "\",\"message\":\"" + msg + "\"}";
    sseSource.send(sse.c_str(), NULL, millis());
  }

  lastScanTime = millis();
  if (currentMode != MODE_ENROLL) currentMode = MODE_SCAN_RESULT;
  showScanResult(ok, name, msg);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}



void connectWiFi() {
  if (useSta && staSsid.length() > 0) {
    display.clearDisplay();
    drawCenteredText("Connecting WiFi", 3);
    String ssidShort = staSsid.length() > 14 ? staSsid.substring(0, 12) + ".." : staSsid;
    drawCenteredText(ssidShort.c_str(), 26);
    display.display();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);          
    WiFi.begin(staSsid.c_str(), staPassword.c_str());
    staConnected = false;

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
      delay(200);
    }
    staConnected = (WiFi.status() == WL_CONNECTED);

    str apIp  = WiFi.softAPIP().toString();
    str staIp = staConnected ? WiFi.localIP().toString() : "";
    
    if (staConnected) Serial.println("STA IP: " + staIp);
    else              Serial.println("STA connection failed");

    display.clearDisplay();
    drawCenteredText(staConnected ? "WiFi Connected!" : "WiFi Failed!", 4);
    display.setTextSize(1);
    display.setCursor(0, 18); display.print("AP:  "); display.println(apIp);
    if (staConnected) {
      display.setCursor(0, 30); display.print("STA: "); display.println(staIp);
    } else {
      display.setCursor(0, 30); display.print("SSID: "); display.println(ssidShort.c_str());
    }
    display.setCursor(0, 42); display.print("HotS: "); display.println(AP_SSID);
    display.display();
    delay(3500);
  } else {
    display.clearDisplay();
    drawCenteredText("Starting Hotspot", 3);
    drawCenteredText(AP_SSID, 26);
    display.display();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);          

    str ip = WiFi.softAPIP().toString();
    

    display.clearDisplay();
    drawCenteredText("Hotspot Ready!", 4);
    display.setTextSize(1);
    display.setCursor(0, 18); display.print("SSID: "); display.println(AP_SSID);
    display.setCursor(0, 30); display.print("Open Access");
    display.setCursor(0, 42); display.print("IP:   "); display.println(ip);
    display.display();
    delay(3500);
  }
}


void loadConfig() {
  if (!SPIFFS.exists("/config.json")) {
    useSta = false; staSsid = ""; staPassword = ""; return;
  }
  File f = SPIFFS.open("/config.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(512);
  if (!deserializeJson(doc, f)) {
    useSta      = doc["useSta"] | false;
    staSsid     = doc["ssid"].as<String>();
    staPassword = doc["password"].as<String>();
  }
  f.close();
}
void saveConfig() {
  DynamicJsonDocument doc(512);
  doc["useSta"]   = useSta;
  doc["ssid"]     = staSsid;
  doc["password"] = staPassword;
  File f = SPIFFS.open("/config.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}


str uidToString(byte *buf, byte len) {
  str s = "";
  for (byte i = 0; i < len; i++) {
    if (buf[i] < 0x10) s += "0";
    s += str(buf[i], HEX);
    if (i < len - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}


void drawCenteredText(const char *text, int y, bool large) {
  display.setTextSize(large ? 2 : 1);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}


void showReady() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("AttendX");
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(getCurrentTimeDisplay().c_str(), 20, true);
  display.setTextSize(1);
  drawCenteredText(getCurrentDateDisplay().c_str(), 38);
  display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
  drawCenteredText("Scan your card...", 55);
  display.display();
}
void showEnroll() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("ENROLL STUDENT");
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  drawCenteredText("SCAN", 18, true);
  display.setTextSize(1);
  drawCenteredText("CARD", 38);
  display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
  str n = "For: " + enrollName;
  if (n.length() > 18) n = n.substring(0, 17) + "..";
  drawCenteredText(n.c_str(), 55);
  display.display();
}
void showScanResult(bool ok, str name, str msg) {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1);
  display.print(ok ? "ACCESS GRANTED" : (name.length() > 0 ? "ALREADY SCANNED" : "ACCESS DENIED"));
  display.setTextColor(SSD1306_WHITE);
  if (ok) {
    display.drawLine(10, 35, 16, 42, SSD1306_WHITE); display.drawLine(16, 42, 28, 28, SSD1306_WHITE);
    display.drawLine(11, 35, 17, 43, SSD1306_WHITE); display.drawLine(17, 43, 29, 28, SSD1306_WHITE);
  } else {
    display.drawLine(10, 28, 26, 44, SSD1306_WHITE); display.drawLine(26, 28, 10, 44, SSD1306_WHITE);
    display.drawLine(11, 28, 27, 44, SSD1306_WHITE); display.drawLine(27, 28, 11, 44, SSD1306_WHITE);
  }
  display.setTextSize(1);
  if (name.length() > 0) { display.setCursor(35, 26); display.print(name.substring(0, 14)); }
  display.setCursor(35, 38); display.print(msg.substring(0, 14));
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  drawCenteredText(getCurrentTimeDisplay().c_str(), 56);
  display.display();
}
void showAttended() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("ATTENDED TODAY");
  display.setTextColor(SSD1306_WHITE);
  DynamicJsonDocument studs = loadStudents();
  DynamicJsonDocument att   = loadAttendance();
  JsonArray sArr = studs.as<JsonArray>(), aArr = att.as<JsonArray>();
  str today = getCurrentDate(); int count = 0;
  for (JsonObject s : sArr) {
    str uid = s["uid"].as<str>();
    for (JsonObject r : aArr)
      if (r["uid"].as<str>() == uid && r["date"].as<str>() == today) { count++; break; }
  }
  display.drawLine(10,35,16,42,SSD1306_WHITE); display.drawLine(16,42,28,28,SSD1306_WHITE);
  display.drawLine(11,35,17,43,SSD1306_WHITE); display.drawLine(17,43,29,28,SSD1306_WHITE);
  display.setTextSize(3);
  str cs = str(count); int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(cs.c_str(),0,0,&x1,&y1,&w,&h);
  display.setCursor(128-w-6,22); display.print(cs);
  display.setTextSize(1); display.drawLine(0,48,128,48,SSD1306_WHITE);
  drawCenteredText("[BTN] Next", 56); display.display();
}
void showAbsent() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("ABSENT TODAY");
  display.setTextColor(SSD1306_WHITE);
  DynamicJsonDocument studs = loadStudents();
  DynamicJsonDocument att   = loadAttendance();
  JsonArray sArr = studs.as<JsonArray>(), aArr = att.as<JsonArray>();
  str today = getCurrentDate(); int total = sArr.size(), presentCount = 0;
  for (JsonObject s : sArr) {
    str uid = s["uid"].as<str>();
    for (JsonObject r : aArr)
      if (r["uid"].as<str>() == uid && r["date"].as<str>() == today) { presentCount++; break; }
  }
  int absent = max(0, total - presentCount);
  display.drawLine(10,28,26,44,SSD1306_WHITE); display.drawLine(26,28,10,44,SSD1306_WHITE);
  display.drawLine(11,28,27,44,SSD1306_WHITE); display.drawLine(27,28,11,44,SSD1306_WHITE);
  display.setTextSize(3);
  str cs = str(absent); int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(cs.c_str(),0,0,&x1,&y1,&w,&h);
  display.setCursor(128-w-6,22); display.print(cs);
  display.setTextSize(1); display.drawLine(0,48,128,48,SSD1306_WHITE);
  drawCenteredText("[BTN] Next", 56); display.display();
}
void showWebLink() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("ADMIN PANEL");
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("Open browser:", 18);
  drawCenteredText("http://", 30);
  str ip = WiFi.softAPIP().toString();
  drawCenteredText(ip.c_str(), 40);
  if (staConnected) {
    display.setCursor(0, 52); display.print("STA: "); display.print(WiFi.localIP().toString());
  }
  display.display();
}
void showRestart() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("WIFI SAVED");
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("Settings saved!", 18);
  drawCenteredText("Please restart", 32);
  drawCenteredText("the device.", 42);
  display.display();
}
void showSettings() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("SETTINGS");
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("WiFi Mode:", 18);
  display.setCursor(2, 28); display.print(settingsSelection == 0 ? "[x] AP Only " : "[ ] AP Only ");
  display.setCursor(2, 38); display.print(settingsSelection == 1 ? "[x] AP+WiFi" : "[ ] AP+WiFi");
  display.setCursor(2, 50); display.print(settingsSelection == 2 ? "> Set Time  " : "  Set Time  ");
  display.display();
}
void showSetTime() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3); display.setTextSize(1); display.print("SET TIME");
  display.setTextColor(SSD1306_WHITE);
  const char *labels[] = {"Hour", "Min", "Day", "Month", "Year"};
  int vals[] = {tsHour, tsMin, tsDay, tsMonth, tsYear};
  char valStr[8];
  if (tsField == 4) sprintf(valStr, "%d",   tsYear);
  else              sprintf(valStr, "%02d",  vals[tsField]);
  display.setTextSize(1);
  drawCenteredText(labels[tsField], 18);
  display.setTextSize(3);
  drawCenteredText(valStr, 26, true);
  display.setTextSize(1);
  for (int i = 0; i < 5; i++) {
    if (i == tsField) display.fillCircle(44 + i*10, 56, 3, SSD1306_WHITE);
    else              display.drawCircle(44 + i*10, 56, 3, SSD1306_WHITE);
  }
  display.display();
}


void handleButton() {
  static bool last = HIGH;
  bool cur = digitalRead(BUTTON_PIN);
  unsigned long now = millis();
  if (last == HIGH && cur == LOW) { buttonPressStart = now; buttonHeld = false; }
  if (last == LOW  && cur == HIGH) {
    unsigned long dur = now - buttonPressStart;
    if (!buttonHeld && dur >= 200 && dur < 1000) {
      soundButton();
      if (currentMode == MODE_SETTINGS) {
        settingsSelection = (settingsSelection + 1) % 3;
        showSettings();
      } else if (currentMode == MODE_SET_TIME) {
        if      (tsField == 0) tsHour  = (tsHour  + 1) % 24;
        else if (tsField == 1) tsMin   = (tsMin   + 1) % 60;
        else if (tsField == 2) tsDay   = (tsDay   % 31) + 1;
        else if (tsField == 3) tsMonth = (tsMonth % 12) + 1;
        else if (tsField == 4) tsYear  = (tsYear < 2099) ? tsYear + 1 : 2024;
        showSetTime();
      } else if (enrollPending) {
        enrollPending = false; currentMode = MODE_READY; showReady();
        sseSource.send("{\"type\":\"enrollCancelled\"}", NULL, millis());
      } else {
        switch (currentMode) {
          case MODE_READY: case MODE_SCAN_RESULT: currentMode = MODE_ATTENDED; showAttended(); break;
          case MODE_ATTENDED: currentMode = MODE_ABSENT;  showAbsent();  break;
          case MODE_ABSENT:   currentMode = MODE_WEBLINK; showWebLink(); break;
          case MODE_WEBLINK:  currentMode = MODE_READY;   showReady();   break;
          default:            currentMode = MODE_READY;   showReady();   break;
        }
      }
    }
    buttonPressStart = 0;
  }
  if (!buttonHeld && buttonPressStart != 0 && now - buttonPressStart >= 1000) {
    buttonHeld = true;
    soundButton();
    if (currentMode == MODE_SET_TIME) {
      tsField++;
      if (tsField >= 5) {
        applyManualTime();   
        tsField = 0; currentMode = MODE_READY; showReady();
      } else { showSetTime(); }
    } else if (currentMode == MODE_SETTINGS) {
      if (settingsSelection == 2) {
        struct tm ti;
        if (getLocalTime(&ti)) {
          tsHour = ti.tm_hour; tsMin = ti.tm_min; tsDay = ti.tm_mday;
          tsMonth = ti.tm_mon+1; tsYear = ti.tm_year+1900;
        }
        tsField = 0; currentMode = MODE_SET_TIME; showSetTime();
      } else {
        useSta = (settingsSelection == 1);
        saveConfig(); connectWiFi(); syncTime();
        currentMode = MODE_READY; showReady();
      }
    } else if (!enrollPending && currentMode != MODE_SETTINGS) {
      currentMode = MODE_SETTINGS;
      settingsSelection = useSta ? 1 : 0;
      showSettings();
    }
  }
  last = cur;
}


void speakerSetup() { 
  pinMode(SPEAKER_PIN, OUTPUT); digitalWrite(SPEAKER_PIN, LOW); 
}
void beep(int freq, int durationMs) {
  if (freq <= 0) { delay(durationMs); return; }
  long halfUs = 1000000L / freq / 2;
  long cycles = (long)freq * durationMs / 1000;
  for (long i = 0; i < cycles; i++) {
    digitalWrite(SPEAKER_PIN, HIGH); delayMicroseconds(halfUs);
    digitalWrite(SPEAKER_PIN, LOW);  delayMicroseconds(halfUs);
  }
}
void soundBoot(){           
  beep(523,80); 
  delay(30); 
  beep(659,80); 
  delay(30); 
  beep(784,80); 
  delay(30); 
  beep(1047,150); 
}
void soundSuccess(){
   beep(880,100); 
   delay(50); 
   beep(1047,180); 
}
void soundFail(){ 
  beep(300,150); 
  delay(40); 
  beep(200,250); 
}
void soundAlreadyScanned(){
   beep(500,300); 
}
void soundButton(){ 
  beep(1200,40); 
}
