/*********************************************************************
 * ESP32 - Controle de Relés v8.1 (IP Fixo + Porta 2080 + Botão Físico)
 * 
 * Bibliotecas necessárias:
 * - ArduinoJson v6.x
 *********************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

// === CONFIGURAÇÕES GERAIS ===
//const char* DEFAULT_SSID = "NEXTRENTAL_GUEST";
//const char* DEFAULT_PASS = "N3XT@2024";
const char* DEFAULT_SSID = "Alessandro_2G";
const char* DEFAULT_PASS = "Van@1981";
const char* AP_SSID = "ESP32-Setup";
const char* AP_PASS = "12345678";

// === OTA ===
const char* OTA_HOSTNAME = "esp32_reles";
const char* OTA_PASSWORD = "admin1234";

// === NTP ===
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET = -10800;
const int DAYLIGHT_OFFSET = 0;

// === REDES ===
char ESP_IP[16] = "192.168.1.200";
char ESP_GATEWAY[16] = "192.168.1.1";
char ESP_SUBNET[16] = "255.255.255.0";
char ESP_DNS_SERVER[16] = "8.8.8.8";

// === SERVIDOR DE LOGS ===
char LOG_SERVER_HOST[16] = "192.168.1.100";
const uint16_t LOG_SERVER_PORT = 80;
const char* LOG_SERVER_PATH = "/esp32_logs/receive_log.php";

// === RELÉS ===
const uint8_t RELE_PINS[4] = {25, 2, 5, 18};  // Relé 1 agora no pino 25
const uint8_t PINO_GATILHO = 15;               // Pino 15 como entrada física
bool releStates[4] = {false, false, false, false};  // Estado global dos relés

// === VARIÁVEIS PARA BOTÃO FÍSICO (DEBOUNCE + BORDA) ===
bool lastButtonRaw = HIGH;
bool lastButtonStable = HIGH;
uint32_t lastDebounceTime = 0;
const uint32_t debounceDelay = 50;

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

// === LOG LOCAL ===
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

// === SYNC ===
uint32_t lastLogSync = 0;
#define LOG_SYNC_INTERVAL 300000

// === OBJETOS GLOBAIS ===
WebServer server(2080);
Preferences prefs;
bool darkMode = false;
bool useStaticIP = true;

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

// === Configurações de Rede ===
String getESP_IP() {
  prefs.begin("network", true);
  String ip = prefs.getString("esp_ip", "172.16.1.50");
  prefs.end();
  return ip;
}

void saveESP_IP(const String& ip) {
  prefs.begin("network", false);
  prefs.putString("esp_ip", ip);
  prefs.end();
  ip.toCharArray(ESP_IP, sizeof(ESP_IP));
}

String getESP_Gateway() {
  prefs.begin("network", true);
  String gw = prefs.getString("esp_gateway", "172.16.1.1");
  prefs.end();
  return gw;
}

void saveESP_Gateway(const String& gw) {
  prefs.begin("network", false);
  prefs.putString("esp_gateway", gw);
  prefs.end();
  gw.toCharArray(ESP_GATEWAY, sizeof(ESP_GATEWAY));
}

String getESP_Subnet() {
  prefs.begin("network", true);
  String sn = prefs.getString("esp_subnet", "255.255.255.0");
  prefs.end();
  return sn;
}

void saveESP_Subnet(const String& sn) {
  prefs.begin("network", false);
  prefs.putString("esp_subnet", sn);
  prefs.end();
  sn.toCharArray(ESP_SUBNET, sizeof(ESP_SUBNET));
}

String getESP_DNS_Server() {
  prefs.begin("network", true);
  String dns = prefs.getString("esp_dns", "8.8.8.8");
  prefs.end();
  return dns;
}

void saveESP_DNS_Server(const String& dns) {
  prefs.begin("network", false);
  prefs.putString("esp_dns", dns);
  prefs.end();
  dns.toCharArray(ESP_DNS_SERVER, sizeof(ESP_DNS_SERVER));
}

bool getUseStaticIP() {
  prefs.begin("network", true);
  bool use = prefs.getBool("use_static_ip", true);
  prefs.end();
  return use;
}

void saveUseStaticIP(bool use) {
  prefs.begin("network", false);
  prefs.putBool("use_static_ip", use);
  prefs.end();
  useStaticIP = use;
}

// === Configurações do Servidor de Logs ===
String getServerIP() {
  prefs.begin("settings", true);
  String ip = prefs.getString("server_ip", "192.168.1.100");
  prefs.end();
  return ip;
}

void saveServerIP(const String& ip) {
  prefs.begin("settings", false);
  prefs.putString("server_ip", ip);
  prefs.end();
  ip.toCharArray(LOG_SERVER_HOST, sizeof(LOG_SERVER_HOST));
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
// === ENVIAR LOGS ====================================================
// ====================================================================

void sendLogsToServer() {
  if (logCount == 0) {
    Serial.println("📊 Sem logs para enviar");
    return;
  }
  
  Serial.println("\n=== 📤 ENVIANDO LOGS ===");
  Serial.printf("Logs no buffer: %d\n", logCount);
  Serial.printf("Servidor: %s:%d\n", LOG_SERVER_HOST, LOG_SERVER_PORT);
  
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
  
  String url = "http://" + String(LOG_SERVER_HOST) + ":" + String(LOG_SERVER_PORT) + String(LOG_SERVER_PATH);
  
  Serial.printf("📡 URL: %s\n", url.c_str());
  
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
    }
  } else {
    Serial.println("❌ Erro: " + http.errorToString(httpResponseCode));
  }
  
  http.end();
  Serial.println("==============================\n");
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
// === BOTÃO FÍSICO: DETECÇÃO DE BORDA + TOGGLE RELÉ ==================
// ====================================================================

void checkPhysicalButton() {
  int rawReading = digitalRead(PINO_GATILHO);
  
  // === DEBOUNCE: Filtra ruídos mecânicos ===
  if (rawReading != lastButtonRaw) {
    lastDebounceTime = millis();
  }
  
  // Se passou do tempo de debounce, considera o estado como estável
  if ((millis() - lastDebounceTime) > debounceDelay) {
    int stableReading = rawReading;
    
    // === DETECÇÃO DE BORDA: Só age se houve transição REAL ===
    if (stableReading != lastButtonStable) {
      
      // 🎯 QUALQUER transição (HIGH→LOW ou LOW→HIGH) inverte o relé!
      releStates[0] = !releStates[0];  // Inverte estado do Relé 1 (índice 0 = pino 25)
      digitalWrite(RELE_PINS[0], releStates[0] ? HIGH : LOW);
      
      // 📝 Registra no log como ação física
      addLog(1, releStates[0] ? 1 : 0, 0, "PHYSICAL");
      
      Serial.printf("🔘 Borda detectada! Relé 1: %s (via físico)\n", 
                    releStates[0] ? "ON" : "OFF");
      
      // Atualiza estado estável para próxima comparação
      lastButtonStable = stableReading;
    }
  }
  
  // Atualiza estado bruto para próxima leitura
  lastButtonRaw = rawReading;
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
.config-row{display:flex;gap:10px;align-items:center;margin:10px 0}
.config-row label{min-width:140px;font-size:13px}
.config-row input{flex:1}
.config-row button{width:auto;margin:0}
.status-ok{color:var(--success);font-weight:bold}
.status-err{color:var(--danger);font-weight:bold}
.info-box{background:var(--bg);padding:10px;border-radius:8px;margin:10px 0;font-size:12px}
.info-box strong{color:var(--primary)}
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
<h2 class="section-title">🌐 Configurações de Rede</h2>
<div class="info-box">
<strong>IP Atual:</strong> <span id="currentIP">-</span> | 
<strong>MAC:</strong> <span id="currentMAC">-</span> |
<strong>Porta:</strong> 2080
</div>
<div class="config-row">
<label>🔘 Modo IP:</label>
<select id="ipMode" style="flex:1">
<option value="1">IP Fixo (Estático)</option>
<option value="0">IP Automático (DHCP)</option>
</select>
</div>
<div id="staticIPConfig">
<div class="config-row">
<label>📍 IP do ESP32:</label>
<input type="text" id="espIP" placeholder="192.168.1.200">
</div>
<div class="config-row">
<label>🚪 Gateway:</label>
<input type="text" id="espGateway" placeholder="192.168.1.1">
</div>
<div class="config-row">
<label>📐 Subnet Mask:</label>
<input type="text" id="espSubnet" placeholder="255.255.255.0">
</div>
<div class="config-row">
<label>🔍 DNS:</label>
<input type="text" id="espDNS" placeholder="8.8.8.8">
</div>
</div>
<button class="btn btn-primary" onclick="saveNetworkConfig()">💾 Salvar Configurações de Rede</button>
<p style="font-size:11px;color:var(--text-sec);margin-top:10px">⚠️ Após salvar, o ESP32 reiniciará</p>
</div>

<div class="card">
<h2 class="section-title">⚙️ Servidor de Logs</h2>
<div class="config-row">
<label>IP do Servidor:</label>
<input type="text" id="serverIP" placeholder="192.168.1.100">
<button class="btn btn-primary" onclick="saveServerIP()" style="width:auto">💾 Salvar</button>
</div>
<div style="margin-top:10px;font-size:12px;color:var(--text-sec)">
Status: <span id="syncStatus">-</span>
</div>
</div>

<div class="card">
<h2 class="section-title">🔧 Testes</h2>
<button class="btn btn-primary" onclick="testSync()">📤 Testar Envio de Logs</button>
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
function loadNetworkConfig(){fetch('/config/network').then(r=>r.json()).then(c=>{$('currentIP').textContent=c.current_ip||'-';$('currentMAC').textContent=c.mac||'-';$('ipMode').value=c.use_static?'1':'0';$('espIP').value=c.esp_ip||'192.168.1.200';$('espGateway').value=c.esp_gateway||'192.168.1.1';$('espSubnet').value=c.esp_subnet||'255.255.255.0';$('espDNS').value=c.esp_dns||'8.8.8.8';toggleStaticIPFields(c.use_static)})}
function toggleStaticIPFields(show){$('staticIPConfig').style.display=show?'block':'none'}
$('ipMode').addEventListener('change',e=>toggleStaticIPFields(e.target.value=='1'))
function saveNetworkConfig(){const data={use_static:$('ipMode').value=='1',esp_ip:$('espIP').value,esp_gateway:$('espGateway').value,esp_subnet:$('espSubnet').value,esp_dns:$('espDNS').value};if(!confirm('Salvar? ESP32 irá reiniciar.'))return;fetch('/config/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}).then(r=>r.json()).then(res=>{alert(res.msg);if(res.status=='ok')setTimeout(()=>location.reload(),2000)})}
function loadServerIP(){fetch('/config').then(r=>r.json()).then(c=>{$('serverIP').value=c.server_ip||''})}
function saveServerIP(){const ip=$('serverIP').value;if(!/^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$/.test(ip)){alert('IP inválido!');return}fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({server_ip:ip})}).then(r=>r.json()).then(res=>{alert(res.msg);loadServerIP()})}
function testSync(){$('syncStatus').innerHTML='⏳ Enviando...';fetch('/sendlogs').then(r=>r.text()).then(t=>{$('syncStatus').innerHTML='<span class="status-ok">✅ Sucesso!</span>';setTimeout(()=>{$('syncStatus').innerHTML='-'},3000)}).catch(e=>{$('syncStatus').innerHTML='<span class="status-err">❌ Erro</span>'})}
loadTheme();loadNetworkConfig();loadServerIP();
setInterval(updateRelays,1000);setInterval(loadSchedules,30000);setInterval(loadLogs,5000);
updateRelays();loadSchedules();loadLogs();
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

  server.on("/config", HTTP_GET, []() {
    String json = "{\"server_ip\":\"" + String(getServerIP()) + "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "Erro"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "JSON invalido"); return; }
    if (doc.containsKey("server_ip")) {
      String newIP = doc["server_ip"].as<String>();
      saveServerIP(newIP);
      server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"IP salvo: " + newIP + "\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"Campo server_ip ausente\"}");
    }
  });

  server.on("/config/network", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["current_ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["use_static"] = getUseStaticIP();
    doc["esp_ip"] = getESP_IP();
    doc["esp_gateway"] = getESP_Gateway();
    doc["esp_subnet"] = getESP_Subnet();
    doc["esp_dns"] = getESP_DNS_Server();
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/config/network", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "Erro"); return; }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "text/plain", "JSON invalido");
      return;
    }
    
    saveUseStaticIP(doc["use_static"] | true);
    saveESP_IP(doc["esp_ip"] | "192.168.1.200");
    saveESP_Gateway(doc["esp_gateway"] | "192.168.1.1");
    saveESP_Subnet(doc["esp_subnet"] | "255.255.255.0");
    saveESP_DNS_Server(doc["esp_dns"] | "8.8.8.8");
    
    server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Configurações salvas! Reiniciando...\"}");
    delay(1000);
    ESP.restart();
  });

  server.on("/sendlogs", HTTP_GET, []() {
    Serial.println("\n🔘 Envio de logs forçado via HTTP");
    sendLogsToServer();
    server.send(200, "text/plain", "Logs enviados (veja Serial Monitor)");
  });
}

// ====================================================================
// === CONEXÃO WI-FI COM IP FIXO ======================================
// ====================================================================

void connectWiFi(const String& ssid, const String& pass) {
  WiFi.mode(WIFI_STA);
  
  if (useStaticIP) {
    Serial.println("\n📍 Configurando IP Fixo...");
    Serial.printf("IP: %s\n", ESP_IP);
    Serial.printf("Gateway: %s\n", ESP_GATEWAY);
    Serial.printf("Subnet: %s\n", ESP_SUBNET);
    Serial.printf("DNS: %s\n", ESP_DNS_SERVER);
    
    IPAddress local_IP, gateway, subnet, dns;
    
    if (local_IP.fromString(ESP_IP) && 
        gateway.fromString(ESP_GATEWAY) && 
        subnet.fromString(ESP_SUBNET) && 
        dns.fromString(ESP_DNS_SERVER)) {
      
      if (!WiFi.config(local_IP, gateway, subnet, dns)) {
        Serial.println("❌ Falha ao configurar IP fixo!");
      } else {
        Serial.println("✅ IP fixo configurado!");
      }
    } else {
      Serial.println("❌ IPs em formato inválido!");
    }
  } else {
    Serial.println("\n📍 Usando IP Automático (DHCP)");
  }
  
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("\nConectando: " + ssid);
  
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Conectado!");
    Serial.print("🌐 IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("🔗 MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.print("🔌 Porta: ");
    Serial.println("2080");
    
    if (MDNS.begin("esp32")) {
      Serial.println("🔍 mDNS: http://esp32.local:2080");
    }
    
    configTime(GMT_OFFSET, DAYLIGHT_OFFSET, NTP_SERVER);
    time_t now = time(nullptr);
    for (uint8_t i = 0; now < 1000000000 && i < 20; i++) {
      delay(500);
      now = time(nullptr);
    }
    if (now >= 1000000000) {
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      Serial.printf("🕐 Horário: %04d-%02d-%02d %02d:%02d:%02d\n",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
      for (uint8_t i = 0; i < 4; i++) digitalWrite(RELE_PINS[i], LOW);
    });
    ArduinoOTA.begin();

    loadDarkMode();
    setupWebServer();
    server.begin();
    loadSchedules();
    
    String savedServerIP = getServerIP();
    savedServerIP.toCharArray(LOG_SERVER_HOST, sizeof(LOG_SERVER_HOST));
    
    Serial.println("\n🌐 Servidor iniciado na PORTA 2080");
    Serial.printf("📊 Logs: http://%s:%d%s\n", LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
    Serial.printf("💻 Acesse: http://%s:2080\n", WiFi.localIP().toString().c_str());
    
  } else {
    Serial.println("\n❌ Falha na conexão Wi-Fi");
    Serial.println("🔄 Entrando em modo AP...");
    startAPMode();
  }
}

// ====================================================================
// === MODO ESTAÇÃO ===================================================
// ====================================================================

void startStationMode(const String& ssid, const String& pass) {
  useStaticIP = getUseStaticIP();
  
  if (useStaticIP) {
    String ip = getESP_IP();
    String gw = getESP_Gateway();
    String sn = getESP_Subnet();
    String dns = getESP_DNS_Server();
    
    ip.toCharArray(ESP_IP, sizeof(ESP_IP));
    gw.toCharArray(ESP_GATEWAY, sizeof(ESP_GATEWAY));
    sn.toCharArray(ESP_SUBNET, sizeof(ESP_SUBNET));
    dns.toCharArray(ESP_DNS_SERVER, sizeof(ESP_DNS_SERVER));
  }
  
  connectWiFi(ssid, pass);
}

// ====================================================================
// === SETUP & LOOP ===================================================
// ====================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Control v8.1 (Porta 2080 + Botão Físico) ===");
  Serial.printf("📅 Boot: %s %s\n", __DATE__, __TIME__);
  
  // Inicializa pinos dos relés como saída
  for (int i = 0; i < 4; i++) {
    pinMode(RELE_PINS[i], OUTPUT);
    digitalWrite(RELE_PINS[i], LOW);
  }
  Serial.println("🔌 Relés inicializados");
  
  // Configura pino 15 como entrada com Pull-Up
  pinMode(PINO_GATILHO, INPUT_PULLUP);
  lastButtonRaw = digitalRead(PINO_GATILHO);
  lastButtonStable = lastButtonRaw;
  Serial.println("🔘 Pino 15 configurado como entrada (PULL-UP)");
  
  String savedSSID = getSavedSSID();
  String savedPASS = getSavedPassword();
  
  if (savedSSID.length() > 0) {
    Serial.printf("📋 Credenciais salvas: %s\n", savedSSID.c_str());
    startStationMode(savedSSID, savedPASS);
  } else {
    Serial.printf("📋 Sem credenciais. Padrão: %s\n", DEFAULT_SSID);
    startStationMode(DEFAULT_SSID, DEFAULT_PASS);
  }
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
  // Verifica agendamentos a cada 60s
  if (millis() - lastScheduleCheck >= 60000) {
    checkSchedules();
    lastScheduleCheck = millis();
  }
  
  // Sincroniza logs periodicamente
  if (millis() - lastLogSync >= LOG_SYNC_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED && logCount > 0) {
      sendLogsToServer();
    }
    lastLogSync = millis();
  }

  // ✅ Verifica botão físico a cada ciclo (detecção de borda)
  checkPhysicalButton();
  
  delay(10);
}

