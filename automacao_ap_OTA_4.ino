/*********************************************************************
 * ESP32 - Controle de Relés v6.0 Final (Log Híbrido + HTTPClient)
 * 
 * Bibliotecas necessárias:
 * - ArduinoJson v6.x (Gerenciador de Bibliotecas)
 * 
 * Configuração:
 * - Altere LOG_SERVER_HOST para o IP do seu PC
 *********************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>  // ← NECESSÁRIO PARA HTTP POST

// === CONFIGURAÇÕES GERAIS ===
const char* DEFAULT_SSID = "NEXTRENTAL_GUEST";
const char* DEFAULT_PASS = "N3XT@2024";
const char* AP_SSID = "ESP32-Setup";
const char* AP_PASS = "12345678";

// === OTA ===
const char* OTA_HOSTNAME = "esp32_reles";
const char* OTA_PASSWORD = "admin1234";

// === NTP (Curitiba, UTC-3) ===
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET = -10800;
const int DAYLIGHT_OFFSET = 0;

// === SERVIDOR DE LOGS (ALTERE PARA O IP DO SEU PC) ===
const char* LOG_SERVER_HOST = "10.205.1.9";  // ← IP do seu PC com XAMPP
const uint16_t LOG_SERVER_PORT = 80;
const char* LOG_SERVER_PATH = "/esp32_logs/receive_log.php";

// === RELÉS ===
const uint8_t RELE_PINS[4] = {15, 2, 5, 18};
bool releStates[4] = {false, false, false, false};

// === TIPOS DE REPETIÇÃO ===
#define REPEAT_DAILY    0
#define REPEAT_WEEKDAYS 1
#define REPEAT_CUSTOM   2

// === AGENDAMENTOS ===
struct Schedule {
  uint8_t id;
  uint8_t relay;
  uint8_t hourOn;
  uint8_t minuteOn;
  uint8_t hourOff;
  uint8_t minuteOff;
  uint8_t repeatType;
  uint8_t customDays;
  bool enabled;
};

#define MAX_SCHEDULES 15
Schedule schedules[MAX_SCHEDULES];
uint8_t scheduleCount = 0;
uint32_t lastScheduleCheck = 0;

// === LOG LOCAL (30 registros) ===
struct LogEntry {
  uint32_t timestamp;
  uint8_t relay;
  uint8_t action;
  uint8_t type;
  char ip[16];
};

#define MAX_LOGS 30
LogEntry execLogs[MAX_LOGS];
uint8_t logCount = 0;
uint8_t logIndex = 0;

// === SYNC DE LOGS ===
uint32_t lastLogSync = 0;
#define LOG_SYNC_INTERVAL 300000  // 5 minutos

// === OBJETOS GLOBAIS ===
WebServer server(80);
Preferences prefs;
bool darkMode = false;

// ====================================================================
// === FUNÇÕES DE LOG =================================================
// ====================================================================

void addLog(uint8_t relay, uint8_t action, uint8_t type, const char* ip) {
  execLogs[logIndex].timestamp = time(nullptr);
  execLogs[logIndex].relay = relay;
  execLogs[logIndex].action = action;
  execLogs[logIndex].type = type;
  strncpy(execLogs[logIndex].ip, ip, 15);
  execLogs[logIndex].ip[15] = '\0';
  
  logIndex = (logIndex + 1) % MAX_LOGS;
  if (logCount < MAX_LOGS) logCount++;
  
  Serial.printf("[LOG] Relé %d: %s (%s) - IP: %s\n", relay, action ? "ON" : "OFF", type ? "SCHED" : "MANUAL", ip);
}

// ====================================================================
// === ENVIAR LOGS PARA SERVIDOR (CORRIGIDO) ==========================
// ====================================================================

void sendLogsToServer() {
  if (logCount == 0) {
    Serial.println("📊 Sem logs para enviar");
    return;
  }
  
  Serial.println("\n=== 📤 ENVIANDO LOGS ===");
  Serial.printf("Logs no buffer: %d\n", logCount);
  
  // === CRIAR JSON COMO OBJETO (NÃO ARRAY!) ===
  StaticJsonDocument<4096> doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray logsArray = root.createNestedArray("logs");
  
  uint8_t start = (logCount < MAX_LOGS) ? 0 : logIndex;
  uint8_t count = 0;
  while (count < logCount) {
    uint8_t idx = (start + count) % MAX_LOGS;
    JsonObject obj = logsArray.createNestedObject();
    obj["ts"] = execLogs[idx].timestamp;
    obj["relay"] = execLogs[idx].relay;
    obj["act"] = execLogs[idx].action;
    obj["type"] = execLogs[idx].type;
    obj["ip"] = execLogs[idx].ip;
    count++;
  }
  
  root["esp_id"] = OTA_HOSTNAME;
  root["device_ip"] = WiFi.localIP().toString();
  root["log_count"] = logCount;
  
  String jsonBody;
  serializeJson(doc, jsonBody);
  
  Serial.println("\n📦 JSON ENVIADO:");
  Serial.println(jsonBody);
  
  String url = "http://" + String(LOG_SERVER_HOST) + ":" + String(LOG_SERVER_PORT) + String(LOG_SERVER_PATH);
  
  Serial.printf("\n📡 URL: %s\n", url.c_str());
  Serial.printf("📦 JSON Size: %d bytes\n", jsonBody.length());
  
  // === USAR HTTPClient ===
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  Serial.println("🔵 Enviando POST...");
  
  int httpResponseCode = http.POST(jsonBody);
  
  Serial.printf("📥 Código HTTP: %d\n", httpResponseCode);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("📄 Resposta: " + response);
    
    if (httpResponseCode == 200) {
      Serial.println("\n✅✅✅ LOGS ENVIADOS COM SUCESSO! ✅✅✅");
    } else {
      Serial.println("\n⚠️ Erro HTTP: " + String(httpResponseCode));
    }
  } else {
    Serial.println("❌ Erro na conexão: " + http.errorToString(httpResponseCode));
  }
  
  http.end();
  Serial.println("==============================\n");
}

// ====================================================================
// === PREFERÊNCIAS ===================================================
// ====================================================================

String getSavedSSID() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  prefs.end();
  return ssid;
}

String getSavedPassword() {
  prefs.begin("wifi", true);
  String pass = prefs.getString("pass", "");
  prefs.end();
  return pass;
}

void saveCredentials(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void saveSchedules() {
  prefs.begin("schedules", false);
  prefs.putUChar("count", scheduleCount);
  for (uint8_t i = 0; i < scheduleCount; i++) {
    char key[12];
    snprintf(key, sizeof(key), "sch_%d", schedules[i].id);
    prefs.putBytes(key, &schedules[i], sizeof(Schedule));
  }
  prefs.end();
}

void loadSchedules() {
  prefs.begin("schedules", true);
  scheduleCount = prefs.getUChar("count", 0);
  if (scheduleCount > MAX_SCHEDULES) scheduleCount = MAX_SCHEDULES;
  for (uint8_t i = 0; i < scheduleCount; i++) {
    char key[12];
    snprintf(key, sizeof(key), "sch_%d", schedules[i].id);
    prefs.getBytes(key, &schedules[i], sizeof(Schedule));
  }
  prefs.end();
}

void saveDarkMode(bool mode) {
  prefs.begin("settings", false);
  prefs.putBool("darkmode", mode);
  prefs.end();
}

void loadDarkMode() {
  prefs.begin("settings", true);
  darkMode = prefs.getBool("darkmode", false);
  prefs.end();
}

uint8_t getNextScheduleId() {
  for (uint8_t id = 1; id < 255; id++) {
    bool exists = false;
    for (uint8_t i = 0; i < scheduleCount; i++) {
      if (schedules[i].id == id) { exists = true; break; }
    }
    if (!exists) return id;
  }
  return 0;
}

// ====================================================================
// === AGENDAMENTOS ===================================================
// ====================================================================

bool isDayEnabled(uint8_t repeatType, uint8_t customDays, uint8_t currentDay) {
  if (repeatType == REPEAT_DAILY) return true;
  if (repeatType == REPEAT_WEEKDAYS) return (currentDay >= 1 && currentDay <= 5);
  if (repeatType == REPEAT_CUSTOM) return (customDays & (1 << currentDay));
  return false;
}

void checkSchedules() {
  time_t now = time(nullptr);
  if (now < 1000000000) return;
  
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  uint8_t currentHour = timeinfo.tm_hour;
  uint8_t currentMinute = timeinfo.tm_min;
  uint8_t currentDay = timeinfo.tm_wday;
  uint32_t currentMinutes = currentHour * 60 + currentMinute;
  
  for (uint8_t i = 0; i < scheduleCount; i++) {
    if (!schedules[i].enabled) continue;
    if (!isDayEnabled(schedules[i].repeatType, schedules[i].customDays, currentDay)) continue;
    
    uint32_t onMinutes = schedules[i].hourOn * 60 + schedules[i].minuteOn;
    uint32_t offMinutes = schedules[i].hourOff * 60 + schedules[i].minuteOff;
    
    if (currentMinutes == onMinutes) {
      uint8_t idx = schedules[i].relay - 1;
      if (idx < 4 && !releStates[idx]) {
        releStates[idx] = true;
        digitalWrite(RELE_PINS[idx], HIGH);
        addLog(schedules[i].relay, 1, 1, "SYSTEM");
      }
    }
    else if (currentMinutes == offMinutes) {
      uint8_t idx = schedules[i].relay - 1;
      if (idx < 4 && releStates[idx]) {
        releStates[idx] = false;
        digitalWrite(RELE_PINS[idx], LOW);
        addLog(schedules[i].relay, 0, 1, "SYSTEM");
      }
    }
  }
}

// ====================================================================
// === PÁGINA DE CONFIGURAÇÃO WI-FI ===================================
// ====================================================================

const char* WIFI_CONFIG_PAGE = R"DELIM(
<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configurar Wi-Fi</title>
<style>
body{font-family:sans-serif;padding:20px;text-align:center;background:#f5f5f5}
.card{background:white;padding:25px;border-radius:16px;margin:20px auto;max-width:400px;box-shadow:0 4px 12px rgba(0,0,0,0.1)}
input{width:100%;padding:14px;margin:10px 0;border:1px solid #ddd;border-radius:10px;font-size:16px;box-sizing:border-box}
button{background:#4CAF50;color:white;padding:14px 28px;border:none;border-radius:10px;font-size:16px;cursor:pointer;width:100%}
</style></head><body>
<div class="card"><h2>📡 Configurar Wi-Fi</h2>
<form action="/save" method="post">
<input type="text" name="ssid" placeholder="Nome da rede (SSID)" required>
<input type="password" name="pass" placeholder="Senha" required>
<button type="submit">💾 Salvar e Conectar</button>
</form></div></body></html>
)DELIM";

void handleSaveWiFi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    saveCredentials(server.arg("ssid"), server.arg("pass"));
    server.send(200, "text/html", "<h2>Salvo! Reiniciando...</h2>");
    delay(1000);
    ESP.restart();
  }
  server.send(400, "text/plain", "Erro");
}

void startAPMode() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("\n[MODO AP] " + String(AP_SSID));
  
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", WIFI_CONFIG_PAGE); });
  server.on("/save", HTTP_POST, handleSaveWiFi);
  server.begin();
}

// ====================================================================
// === PÁGINA PRINCIPAL ===============================================
// ====================================================================

const char* MAIN_PAGE = R"DELIM(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>ESP32 Control</title>
<style>
:root{--bg:#f0f2f5;--card:#fff;--text:#333;--text-sec:#666;--primary:#2196F3;--success:#4CAF50;--danger:#f44336;--border:#e0e0e0}
[data-theme="dark"]{--bg:#1a1a2e;--card:#16213e;--text:#eee;--text-sec:#aaa;--border:#333}
body{font-family:-apple-system,sans-serif;margin:0;padding:15px;background:var(--bg);color:var(--text)}
.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}
.header h1{margin:0;font-size:20px}
.theme-btn{background:none;border:2px solid var(--border);border-radius:50%;width:44px;height:44px;font-size:20px;cursor:pointer;color:var(--text)}
.card{background:var(--card);border-radius:16px;padding:20px;margin-bottom:15px;box-shadow:0 4px 12px rgba(0,0,0,0.08)}
.relay-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
.relay-btn{padding:18px;font-size:16px;font-weight:600;border:none;border-radius:12px;cursor:pointer}
.relay-on{background:linear-gradient(135deg,var(--success),#43a047);color:white}
.relay-off{background:linear-gradient(135deg,var(--danger),#e53935);color:white}
.section-title{font-size:16px;margin:0 0 15px 0;display:flex;align-items:center;gap:8px}
input,select{width:100%;padding:12px;margin:8px 0;border:1px solid var(--border);border-radius:10px;background:var(--card);color:var(--text)}
.btn{padding:12px 20px;border:none;border-radius:10px;font-weight:600;cursor:pointer;width:100%;margin-top:8px}
.btn-primary{background:var(--primary);color:white}
.btn-danger{background:var(--danger);color:white}
.btn-small{padding:6px 12px;font-size:12px;width:auto}
.schedule-item{display:flex;justify-content:space-between;align-items:center;padding:12px;background:var(--bg);border-radius:10px;margin:8px 0}
.badge{padding:4px 10px;border-radius:12px;font-size:11px;font-weight:700}
.badge-on{background:var(--success);color:white}
.badge-off{background:var(--text-sec);color:white}
.log-item{display:flex;justify-content:space-between;align-items:center;padding:12px;border-bottom:1px solid var(--border);font-size:12px}
.log-left{display:flex;align-items:center;gap:10px}
.log-meta{text-align:right}
.log-datetime{font-weight:500}
.log-ip{font-size:10px;color:var(--primary);font-family:monospace}
.hidden{display:none}
.repeat-options{display:flex;gap:8px;margin:10px 0}
.repeat-btn{flex:1;padding:10px;border:2px solid var(--border);border-radius:8px;background:var(--card);color:var(--text);cursor:pointer}
.repeat-btn.active{background:var(--primary);color:white}
.day-checkboxes{display:grid;grid-template-columns:repeat(7,1fr);gap:4px;margin:10px 0}
.day-chk{display:flex;flex-direction:column;align-items:center}
.day-chk input{width:20px;height:20px}
.day-chk span{font-size:10px;color:var(--text-sec)}
</style></head><body>
<div class="header"><h1>🔌 ESP32 Control</h1><button class="theme-btn" onclick="toggleTheme()" id="themeBtn">🌙</button></div>
<div class="card"><h2 class="section-title">⚡ Relés</h2><div class="relay-grid" id="relays"></div></div>
<div class="card">
<h2 class="section-title">⏰ Novo Agendamento</h2>
<select id="schRelay"><option value="1">Relé 1</option><option value="2">Relé 2</option><option value="3">Relé 3</option><option value="4">Relé 4</option></select>
<div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
<div><label style="font-size:12px;color:var(--text-sec)">Ligar</label><input type="time" id="schOn" value="08:00"></div>
<div><label style="font-size:12px;color:var(--text-sec)">Desligar</label><input type="time" id="schOff" value="18:00"></div>
</div>
<label style="font-size:12px;color:var(--text-sec);display:block;margin:10px 0 5px">Repetir:</label>
<div class="repeat-options">
<button class="repeat-btn active" onclick="setRepeat(0,this)">📅 Todo dia</button>
<button class="repeat-btn" onclick="setRepeat(1,this)">💼 Seg-Sex</button>
<button class="repeat-btn" onclick="setRepeat(2,this)">⚙️ Personal</button>
</div>
<div id="customDays" class="hidden">
<div class="day-checkboxes">
<div class="day-chk"><input type="checkbox" id="d0" value="1"><span>Dom</span></div>
<div class="day-chk"><input type="checkbox" id="d1" value="2" checked><span>Seg</span></div>
<div class="day-chk"><input type="checkbox" id="d2" value="4" checked><span>Ter</span></div>
<div class="day-chk"><input type="checkbox" id="d3" value="8" checked><span>Qua</span></div>
<div class="day-chk"><input type="checkbox" id="d4" value="16" checked><span>Qui</span></div>
<div class="day-chk"><input type="checkbox" id="d5" value="32" checked><span>Sex</span></div>
<div class="day-chk"><input type="checkbox" id="d6" value="64"><span>Sáb</span></div>
</div>
</div>
<label style="display:flex;align-items:center;gap:8px;margin:10px 0"><input type="checkbox" id="schEnabled" checked style="width:auto"><span>Ativo</span></label>
<button class="btn btn-primary" onclick="addSchedule()">💾 Salvar</button>
</div>
<div class="card"><h2 class="section-title">📋 Agendamentos (<span id="schCount">0</span>)</h2><div id="schedulesList"></div></div>
<div class="card">
<h2 class="section-title">📊 Log Local (30 últimos)</h2>
<button class="btn btn-small btn-danger" onclick="clearLogs()" style="width:auto;float:right">Limpar</button>
<div style="clear:both"></div>
<div id="logsList" style="max-height:250px;overflow-y:auto"></div>
</div>
<div class="card">
<h2 class="section-title">🔧 Testes</h2>
<button class="btn btn-primary" onclick="fetch('/sendlogs').then(r=>alert('Logs enviados!'))">📤 Forçar Envio de Logs</button>
</div>
<script>
const $=id=>document.getElementById(id);
let isDark=false,repeatType=0;
function loadTheme(){isDark=localStorage.getItem('theme')==='dark';applyTheme()}
function toggleTheme(){isDark=!isDark;localStorage.setItem('theme',isDark?'dark':'light');applyTheme()}
function applyTheme(){document.documentElement.setAttribute('data-theme',isDark?'dark':'light');$('themeBtn').textContent=isDark?'☀️':'🌙'}
function setRepeat(type,btn){repeatType=type;document.querySelectorAll('.repeat-btn').forEach(b=>b.classList.remove('active'));btn.classList.add('active');if(type===2){$('customDays').classList.remove('hidden')}else{$('customDays').classList.add('hidden')}}
function toggleRelay(id){fetch('/toggle?id='+id)}
function updateRelays(){fetch('/states').then(r=>r.json()).then(s=>{let h='';s.forEach((on,i)=>{h+=`<button class="relay-btn ${on?'relay-on':'relay-off'}" onclick="toggleRelay(${i+1})">RELÉ ${i+1}<br><small>${on?'🟢':'🔴'}</small></button>`});$('relays').innerHTML=h})}
const repeatNames=['📅 Todo dia','💼 Seg-Sex','⚙️ Personal'];
function loadSchedules(){fetch('/schedules').then(r=>r.json()).then(list=>{$('schCount').textContent=list.length;if(list.length===0){$('schedulesList').innerHTML='<small>Nenhum agendamento</small>';return}let h='';list.forEach(s=>{h+=`<div class="schedule-item"><div><strong>Relé ${s.relay}</strong><br><small>${s.on}→${s.off}</small><br><small style="color:var(--primary)">${repeatNames[s.repeat]}</small></div><div><span class="badge ${s.enabled?'badge-on':'badge-off'}">${s.enabled?'ON':'OFF'}</span><br><button class="btn btn-small btn-danger" onclick="delSch(${s.id})">🗑️</button></div></div>`});$('schedulesList').innerHTML=h})}
function addSchedule(){let customDays=0;for(let i=0;i<7;i++){if($('d'+i)&&$('d'+i).checked)customDays|=(1<<i)}const data={id:0,relay:parseInt($('schRelay').value),on:$('schOn').value,off:$('schOff').value,enabled:$('schEnabled').checked,repeat:repeatType,customDays:customDays};fetch('/schedules',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}).then(()=>{loadSchedules();alert('Salvo!')})}
function delSch(id){if(confirm('Excluir?')){fetch('/schedules?id='+id,{method:'DELETE'}).then(loadSchedules)}}
function loadLogs(){fetch('/logs').then(r=>r.json()).then(logs=>{if(logs.length===0){$('logsList').innerHTML='<small>Sem registros</small>';return}let h='';logs.reverse().forEach(l=>{const d=new Date(l.ts*1000);const datetime=String(d.getDate()).padStart(2,'0')+'/'+String(d.getMonth()+1).padStart(2,'0')+' '+String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');const icon=l.type===0?'🖐️':'⏰';const ipDisplay=l.ip==='SYSTEM'?'🤖 SYSTEM':'👤 '+l.ip;h+=`<div class="log-item"><div class="log-left"><span>${icon}</span><div><strong>Relé ${l.relay}</strong> <span style="padding:2px 6px;border-radius:4px;font-size:10px;background:${l.act?'#4CAF50':'#f44336'};color:white">${l.act?'ON':'OFF'}</span></div></div><div class="log-meta"><div class="log-datetime">${datetime}</div><div class="log-ip">${ipDisplay}</div></div></div>`});$('logsList').innerHTML=h})}
function clearLogs(){fetch('/logs',{method:'DELETE'}).then(loadLogs)}
loadTheme();setInterval(updateRelays,1000);setInterval(loadSchedules,30000);setInterval(loadLogs,5000);updateRelays();loadSchedules();loadLogs();
</script></body></html>
)DELIM";

// ====================================================================
// === SERVIDOR WEB ===================================================
// ====================================================================

void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", MAIN_PAGE); });
  server.on("/states", HTTP_GET, []() {
    String json = "[";
    for (int i = 0; i < 4; i++) { json += (releStates[i] ? "true" : "false"); if (i < 3) json += ","; }
    server.send(200, "application/json", json + "]");
  });
  server.on("/toggle", HTTP_GET, []() {
    if (server.hasArg("id")) {
      int id = server.arg("id").toInt();
      if (id >= 1 && id <= 4) {
        uint8_t idx = id - 1;
        releStates[idx] = !releStates[idx];
        digitalWrite(RELE_PINS[idx], releStates[idx] ? HIGH : LOW);
        String clientIP = server.client().remoteIP().toString();
        addLog(id, releStates[idx] ? 1 : 0, 0, clientIP.c_str());
      }
    }
    server.send(200, "application/json", "{}");
  });
  server.on("/schedules", HTTP_GET, []() {
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < scheduleCount; i++) {
      JsonObject obj = arr.createNestedObject();
      obj["id"] = schedules[i].id;
      obj["relay"] = schedules[i].relay;
      obj["on"] = String(schedules[i].hourOn) + ":" + String(schedules[i].minuteOn < 10 ? "0" : "") + String(schedules[i].minuteOn);
      obj["off"] = String(schedules[i].hourOff) + ":" + String(schedules[i].minuteOff < 10 ? "0" : "") + String(schedules[i].minuteOff);
      obj["repeat"] = schedules[i].repeatType;
      obj["customDays"] = schedules[i].customDays;
      obj["enabled"] = schedules[i].enabled;
    }
    String json; serializeJson(doc, json);
    server.send(200, "application/json", json);
  });
  server.on("/schedules", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "Erro"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "JSON invalido"); return; }
    Schedule newSch;
    newSch.id = doc["id"] | getNextScheduleId();
    newSch.relay = doc["relay"] | 1;
    String onTime = doc["on"] | "00:00";
    String offTime = doc["off"] | "00:00";
    newSch.hourOn = onTime.substring(0, 2).toInt();
    newSch.minuteOn = onTime.substring(3, 5).toInt();
    newSch.hourOff = offTime.substring(0, 2).toInt();
    newSch.minuteOff = offTime.substring(3, 5).toInt();
    newSch.repeatType = doc["repeat"] | 0;
    newSch.customDays = doc["customDays"] | 0;
    newSch.enabled = doc["enabled"] | true;
    bool updated = false;
    for (uint8_t i = 0; i < scheduleCount; i++) {
      if (schedules[i].id == newSch.id) { schedules[i] = newSch; updated = true; break; }
    }
    if (!updated && scheduleCount < MAX_SCHEDULES) {
      if (newSch.id == 0) newSch.id = getNextScheduleId();
      schedules[scheduleCount++] = newSch;
    }
    saveSchedules();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server.on("/schedules", HTTP_DELETE, []() {
    if (!server.hasArg("id")) { server.send(400, "text/plain", "ID necessario"); return; }
    uint8_t id = server.arg("id").toInt();
    for (uint8_t i = 0; i < scheduleCount; i++) {
      if (schedules[i].id == id) {
        for (uint8_t j = i; j < scheduleCount - 1; j++) schedules[j] = schedules[j+1];
        scheduleCount--;
        saveSchedules();
        server.send(200, "application/json", "{\"status\":\"deleted\"}");
        return;
      }
    }
    server.send(404, "text/plain", "Nao encontrado");
  });
  server.on("/logs", HTTP_GET, []() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    uint8_t start = (logCount < MAX_LOGS) ? 0 : logIndex;
    uint8_t count = 0;
    while (count < logCount) {
      uint8_t idx = (start + count) % MAX_LOGS;
      JsonObject obj = arr.createNestedObject();
      obj["ts"] = execLogs[idx].timestamp;
      obj["relay"] = execLogs[idx].relay;
      obj["act"] = execLogs[idx].action;
      obj["type"] = execLogs[idx].type;
      obj["ip"] = execLogs[idx].ip;
      count++;
    }
    String json; serializeJson(doc, json);
    server.send(200, "application/json", json);
  });
  server.on("/logs", HTTP_DELETE, []() {
    logCount = 0;
    logIndex = 0;
    memset(execLogs, 0, sizeof(execLogs));
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
  });
  server.on("/sendlogs", HTTP_GET, []() {
    Serial.println("\n🔘 Envio de logs forçado via HTTP");
    sendLogsToServer();
    server.send(200, "text/plain", "Logs enviados (veja Serial Monitor)");
  });
}

// ====================================================================
// === MODO ESTAÇÃO ===================================================
// ====================================================================

void startStationMode(const String& ssid, const String& pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("\nConectando: " + ssid);
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) { delay(400); Serial.print("."); attempts++; }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
    if (MDNS.begin("esp32")) Serial.println("mDNS: http://esp32.local");
    configTime(GMT_OFFSET, DAYLIGHT_OFFSET, NTP_SERVER);
    time_t now = time(nullptr);
    for (uint8_t i = 0; now < 1000000000 && i < 20; i++) { delay(500); now = time(nullptr); }
    if (now >= 1000000000) {
      struct tm timeinfo; localtime_r(&now, &timeinfo);
      Serial.printf("Horario: %04d-%02d-%02d %02d:%02d:%02d\n", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() { for (uint8_t i = 0; i < 4; i++) digitalWrite(RELE_PINS[i], LOW); });
    ArduinoOTA.begin();
    loadDarkMode();
    setupWebServer();
    server.begin();
    loadSchedules();
    Serial.println("Servidor iniciado");
    Serial.printf("📊 Logs serão enviados para: http://%s:%d%s\n", LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
  } else {
    Serial.println("\nFalha Wi-Fi. Modo AP...");
    startAPMode();
  }
}

// ====================================================================
// === SETUP & LOOP ===================================================
// ====================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Control v6.0 Final ===");
  for (int i = 0; i < 4; i++) { pinMode(RELE_PINS[i], OUTPUT); digitalWrite(RELE_PINS[i], LOW); }
  String savedSSID = getSavedSSID();
  String savedPASS = getSavedPassword();
  if (savedSSID.length() > 0) startStationMode(savedSSID, savedPASS);
  else startStationMode(DEFAULT_SSID, DEFAULT_PASS);
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if (millis() - lastScheduleCheck >= 60000) { checkSchedules(); lastScheduleCheck = millis(); }
  if (millis() - lastLogSync >= LOG_SYNC_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED && logCount > 0) { sendLogsToServer(); }
    lastLogSync = millis();
  }
  delay(10);
}
