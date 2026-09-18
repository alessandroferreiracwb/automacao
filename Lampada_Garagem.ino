#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <sys/time.h> 
#include <Preferences.h>
#include <HTTPClient.h>
#include <Update.h>

// === CONFIGURAÇÃO DE REDE ===
const char* DEFAULT_SSID = "ShowRoom";
const char* DEFAULT_PASS = "Nevil@2026";
const char* AP_SSID = "ESP32-Setup";
const char* AP_PASS = "12345678";

// === CONFIGURAÇÃO OTA GITHUB ===
const char* firmwareUrl = "https://raw.githubusercontent.com/alessandroferreiracwb/automacao/OTA/Lampada_Garagem.ino.esp32.bin";

// === CONFIGURAÇÃO NTP (HORA DA INTERNET) ===
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = -10800;    // Ajuste para o seu fuso horário (ex: -10800 para Brasília)
const int   daylightOffset_sec = 0;

WebServer server(2080);
Preferences preferences;

// === RELÉ E GATILHO ===
const uint8_t RELE_PIN = 25;        
const uint8_t PINO_GATILHO = 15;    
bool releState = false;             
String lastTrigger = "Inicializado"; 

// === ESTRUTURA PARA AGENDAMENTO COM DOIS TURNOS DIÁRIOS ===
struct Turno {
  bool ativo;
  int horaOn;
  int minOn;
  int horaOff;
  int minOff;
};

struct AgendaDia {
  Turno t1;
  Turno t2;
};

AgendaDia agendaSemana[7] = {
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}},
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}},
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}},
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}},
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}},
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}},
  {{false, 6, 0, 7, 0}, {false, 18, 0, 19, 0}}
};

// === FUNÇÕES DE SALVAMENTO E CARREGAMENTO DA AGENDA ===
void salvarAgendaNaMemoria() {
  preferences.begin("agenda_esp", false);
  for (int i = 0; i < 7; i++) {
    String keyPrefix = "d" + String(i) + "_";
    preferences.putBool((keyPrefix + "t1_act").c_str(), agendaSemana[i].t1.ativo);
    preferences.putInt((keyPrefix + "t1_hon").c_str(), agendaSemana[i].t1.horaOn);
    preferences.putInt((keyPrefix + "t1_mon").c_str(), agendaSemana[i].t1.minOn);
    preferences.putInt((keyPrefix + "t1_hoff").c_str(), agendaSemana[i].t1.horaOff);
    preferences.putInt((keyPrefix + "t1_moff").c_str(), agendaSemana[i].t1.minOff);

    preferences.putBool((keyPrefix + "t2_act").c_str(), agendaSemana[i].t2.ativo);
    preferences.putInt((keyPrefix + "t2_hon").c_str(), agendaSemana[i].t2.horaOn);
    preferences.putInt((keyPrefix + "t2_mon").c_str(), agendaSemana[i].t2.minOn);
    preferences.putInt((keyPrefix + "t2_hoff").c_str(), agendaSemana[i].t2.horaOff);
    preferences.putInt((keyPrefix + "t2_moff").c_str(), agendaSemana[i].t2.minOff);
  }
  preferences.end();
}

void carregarAgendaDaMemoria() {
  preferences.begin("agenda_esp", true);
  for (int i = 0; i < 7; i++) {
    String keyPrefix = "d" + String(i) + "_";
    agendaSemana[i].t1.ativo  = preferences.getBool((keyPrefix + "t1_act").c_str(), agendaSemana[i].t1.ativo);
    agendaSemana[i].t1.horaOn = preferences.getInt((keyPrefix + "t1_hon").c_str(), agendaSemana[i].t1.horaOn);
    agendaSemana[i].t1.minOn  = preferences.getInt((keyPrefix + "t1_mon").c_str(), agendaSemana[i].t1.minOn);
    agendaSemana[i].t1.horaOff= preferences.getInt((keyPrefix + "t1_hoff").c_str(), agendaSemana[i].t1.horaOff);
    agendaSemana[i].t1.minOff = preferences.getInt((keyPrefix + "t1_moff").c_str(), agendaSemana[i].t1.minOff);

    agendaSemana[i].t2.ativo  = preferences.getBool((keyPrefix + "t2_act").c_str(), agendaSemana[i].t2.ativo);
    agendaSemana[i].t2.horaOn = preferences.getInt((keyPrefix + "t2_hon").c_str(), agendaSemana[i].t2.horaOn);
    agendaSemana[i].t2.minOn  = preferences.getInt((keyPrefix + "t2_mon").c_str(), agendaSemana[i].t2.minOn);
    agendaSemana[i].t2.horaOff= preferences.getInt((keyPrefix + "t2_hoff").c_str(), agendaSemana[i].t2.horaOff);
    agendaSemana[i].t2.minOff = preferences.getInt((keyPrefix + "t2_moff").c_str(), agendaSemana[i].t2.minOff);
  }
  preferences.end();
}

// === FUNÇÃO DE ATUALIZAÇÃO OTA VIA GITHUB ===
void realizarAtualizacaoOTA() {
  Serial.println("Iniciando verificação/atualização OTA via GitHub...");
  
  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.begin(client, firmwareUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000); // Aumenta o timeout para conexões lentas ou grandes
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.println("Tamanho do firmware a baixar: " + String(contentLength) + " bytes");
    
    if (contentLength <= 0) {
      Serial.println("Erro: Tamanho do firmware inválido.");
      http.end();
      return;
    }

    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      Serial.println("Baixando e gravando novo firmware em blocos...");
      WiFiClient *espClient = http.getStreamPtr();
      
      size_t written = 0;
      uint8_t buff[128];
      unsigned long timeoutMillis = millis();

      // Loop seguro de leitura em blocos para evitar queda de buffer
      while (http.connected() && (written < contentLength)) {
        size_t availableSize = espClient->available();
        if (availableSize) {
          int toRead = size_t(availableSize) > sizeof(buff) ? sizeof(buff) : size_t(availableSize);
          int bytesRead = espClient->read(buff, toRead);
          
          if (bytesRead > 0) {
            Update.write(buff, bytesRead);
            written += bytesRead;
            timeoutMillis = millis(); // Reseta o timeout a cada bloco lido com sucesso
          }
        } else {
          // Se passar de 10 segundos sem receber dados, aborta para evitar travamento
          if (millis() - timeoutMillis > 10000) {
            Serial.println("Erro: Timeout durante o download do firmware.");
            break;
          }
          delay(10);
        }
      }

      Serial.println("Aviso: Escrito " + String(written) + " de " + String(contentLength) + " bytes.");
      
      if (written == contentLength && Update.end(true)) {
        Serial.println("Atualização concluída com sucesso! Reiniciando o ESP32...");
        ESP.restart();
      } else {
        Serial.println("Erro: Atualização falhou. Código do Erro Update: " + String(Update.getError()));
        Update.abort();
      }
    } else {
      Serial.println("Erro: Sem espaço suficiente para iniciar o update OTA.");
    }
  } else {
    Serial.println("Erro ao conectar no GitHub. Código HTTP: " + String(httpCode));
  }
  http.end();
}

// === PÁGINA 1: INDEX.HTML ===
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP32 Control</title>
  <style>
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: white; margin: 0; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
    .container { width: 100%; max-width: 450px; padding: 20px; }
    .card { background: #1e293b; padding: 30px 25px; border-radius: 32px; text-align: center; box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5); }
    h3 { margin-top: 0; color: #94a3b8; font-size: 14px; text-transform: uppercase; letter-spacing: 1px; }
    .datetime-box { background: #0f172a; padding: 10px; border-radius: 14px; font-size: 14px; color: #38bdf8; margin-bottom: 15px; font-weight: bold; }
    .btn { width: 55vw; height: 55vw; max-width: 190px; max-height: 190px; border-radius: 50%; border: 10px solid #334155; font-size: 20px; font-weight: 900; cursor: pointer; transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); margin: 20px auto; display: flex; align-items: center; justify-content: center; outline: none; }
    .btn:active { transform: scale(0.92); }
    .on { background: #22c55e; color: #052e16; border-color: #4ade80; box-shadow: 0 0 40px rgba(34,197,94,0.4); }
    .off { background: #ef4444; color: #450a0a; border-color: #f87171; box-shadow: 0 0 20px rgba(239,68,68,0.1); }
    .info { background: #0f172a; padding: 16px; border-radius: 20px; font-size: 14px; color: #94a3b8; line-height: 1.4; margin-top: 10px; }
    .info strong { display: block; color: #38bdf8; font-size: 16px; margin-top: 4px; }
    .footer-ip { font-size: 11px; color: #38bdf8; margin-top: 15px; font-weight: bold; }
    .nav-link { display: block; margin-top: 12px; color: #38bdf8; text-decoration: none; font-size: 14px; font-weight: bold; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <div class="datetime-box" id="datetime-disp">Carregando data/hora...</div>
      <h3>Controle de Relé 1</h3>
      <button id="btn" class="btn" onclick="toggle()">...</button>
      <div class="info">Status: <span id="st-txt">-</span><strong><span id="last">Aguardando...</span></strong></div>
      <a href="/agenda" class="nav-link">🗓️ Configurar Agenda Semanal</a>
      <a href="/update-ota" class="nav-link" onclick="return confirm('Deseja iniciar a atualização pelo GitHub?')">🚀 Atualizar Firmware (OTA GitHub)</a>
      <div class="footer-ip" id="ip-disp">Conectando...</div>
    </div>
  </div>
<script>
  function update() {
    fetch('/status').then(r => r.json()).then(data => {
      const b = document.getElementById('btn');
      b.className = 'btn ' + (data.state ? 'on' : 'off');
      b.innerText = data.state ? 'LIGADO' : 'DESLIGADO';
      document.getElementById('st-txt').innerText = data.state ? 'ATIVO' : 'INATIVO';
      document.getElementById('last').innerText = data.last;
      if(data.datetime) {
        document.getElementById('datetime-disp').innerText = data.datetime;
      }
      if(data.ip) {
        document.getElementById('ip-disp').innerText = "Conectado em: " + data.ip + ":2080";
      }
    }).catch(err => { document.getElementById('st-txt').innerText = "OFFLINE"; });
  }

  function updateClockOnly() {
    fetch('/status').then(r => r.json()).then(data => {
      if(data.datetime) {
        document.getElementById('datetime-disp').innerText = data.datetime;
      }
    });
  }

  function toggle() { 
    document.getElementById('btn').style.opacity = "0.7";
    fetch('/toggle').then(() => { document.getElementById('btn').style.opacity = "1"; update(); });
  }

  setInterval(updateClockOnly, 1000); 
  setInterval(update, 5000);          
  update();
</script>
</body>
</html>
)rawliteral";

// === PÁGINA 2: AGENDA.HTML ===
const char agenda_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Agendamento Turnos</title>
  <style>
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: white; margin: 0; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
    .container { width: 100%; max-width: 480px; padding: 12px; }
    .card { background: #1e293b; padding: 25px 15px; border-radius: 32px; box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5); }
    h3 { margin: 15px 0 5px 0; color: #94a3b8; font-size: 13px; text-transform: uppercase; letter-spacing: 1px; text-align: center; }
    .clock-info { background: #0f172a; padding: 12px; border-radius: 16px; text-align: center; margin-bottom: 20px; font-size: 15px; color: #38bdf8; font-weight: bold; }
    .clock-info span { display: block; font-size: 22px; color: white; margin-top: 3px; }
    .btn-rtc { width: 100%; background: transparent; color: #38bdf8; border: 2px dashed #38bdf8; padding: 10px; font-size: 12px; font-weight: bold; border-radius: 14px; cursor: pointer; margin-bottom: 20px; }
    
    .day-block { background: #0f172a; padding: 15px 12px; border-radius: 20px; margin-bottom: 12px; }
    .day-header { font-weight: 900; font-size: 14px; color: #38bdf8; text-transform: uppercase; border-bottom: 1px solid #1e293b; padding-bottom: 6px; margin-bottom: 10px; text-align: left; }
    .turno-row { display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px; gap: 5px; }
    .turno-row:last-child { margin-bottom: 0; }
    .turno-label { font-size: 11px; color: #64748b; width: 50px; text-transform: uppercase; }
    .time-inputs { display: flex; align-items: center; gap: 4px; }
    .time-inputs input[type="time"] { background: #1e293b; border: 1px solid #334155; color: #e2e8f0; font-weight: bold; font-size: 13px; padding: 5px; border-radius: 6px; outline: none; color-scheme: dark; text-align: center; }
    .time-separator { color: #475569; font-size: 11px; }
    
    .switch { position: relative; display: inline-block; width: 38px; height: 20px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #334155; transition: .2s; border-radius: 20px; }
    .slider:before { position: absolute; content: ""; height: 14px; width: 14px; left: 3px; bottom: 3px; background-color: white; transition: .2s; border-radius: 50%; }
    input:checked + .slider { background-color: #22c55e; }
    input:checked + .slider:before { transform: translateX(18px); }

    .btn-save { width: 100%; background: #22c55e; color: #052e16; border: none; padding: 16px; font-size: 16px; font-weight: bold; border-radius: 16px; cursor: pointer; margin-top: 15px; }
    .btn-save:active { transform: scale(0.97); }
    .nav-link { display: block; text-align: center; margin-top: 20px; color: #94a3b8; text-decoration: none; font-size: 14px; }
    hr { border: 0; height: 1px; background: #334155; margin: 20px 0; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <div class="clock-info">HORA DO ESP32<span id="esp-clock">--:--:--</span></div>
      <button class="btn-rtc" onclick="setESPClock()">⚙️ ATUALIZAR RELÓGIO COM A HORA DO TELEFONE</button>

      <hr>
      <h3>Programação de Turnos Semanal</h3>
      
      <div id="agenda-container"></div>

      <button class="btn-save" onclick="saveSchedule()">💾 SALVAR AGENDA COMPLETA</button>
      <a href="/" class="nav-link">← Voltar ao Painel Geral</a>
    </div>
  </div>

<script>
  const nomesDias = ["Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"];

  function setESPClock() {
    const agora = new Date();
    const dStr = `${agora.getFullYear()}-${String(agora.getMonth() + 1).padStart(2, '0')}-${String(agora.getDate()).padStart(2, '0')}`;
    const tStr = `${String(agora.getHours()).padStart(2, '0')}:${String(agora.getMinutes()).padStart(2, '0')}`;
    fetch(`/set-rtc?date=${dStr}&time=${tStr}`).then(r => r.text()).then(res => { alert(res); updateClock(); });
  }

  function renderAgenda(data) {
    const container = document.getElementById('agenda-container');
    container.innerHTML = "";
    nomesDias.forEach((dia, idx) => {
      const d = data[idx];
      const block = document.createElement('div');
      block.className = 'day-block';
      block.innerHTML = `
        <div class="day-header">${dia}</div>
        <div class="turno-row">
          <div class="turno-label">Turno 1</div>
          <div class="time-inputs">
            <input type="time" id="on-1-${idx}" value="${d.t1.on}">
            <span class="time-separator">às</span>
            <input type="time" id="off-1-${idx}" value="${d.t1.off}">
          </div>
          <label class="switch">
            <input type="checkbox" id="chk-1-${idx}" ${d.t1.active ? 'checked' : ''}>
            <span class="slider"></span>
          </label>
        </div>
        <div class="turno-row">
          <div class="turno-label">Turno 2</div>
          <div class="time-inputs">
            <input type="time" id="on-2-${idx}" value="${d.t2.on}">
            <span class="time-separator">às</span>
            <input type="time" id="off-2-${idx}" value="${d.t2.off}">
          </div>
          <label class="switch">
            <input type="checkbox" id="chk-2-${idx}" ${d.t2.active ? 'checked' : ''}>
            <span class="slider"></span>
          </label>
        </div>
      `;
      container.appendChild(block);
    });
  }

  function loadSchedule() {
    fetch('/get-schedule').then(r => r.json()).then(data => { renderAgenda(data); });
  }

  function updateClock() {
    fetch('/status').then(r => r.json()).then(data => { if(data.clock) document.getElementById('esp-clock').innerText = data.clock; });
  }

  function saveSchedule() {
    let queryParams = [];
    for(let i=0; i<7; i++) {
      const act1 = document.getElementById(`chk-1-${i}`).checked ? "1" : "0";
      const tOn1 = document.getElementById(`on-1-${i}`).value;
      const tOff1 = document.getElementById(`off-1-${i}`).value;
      
      const act2 = document.getElementById(`chk-2-${i}`).checked ? "1" : "0";
      const tOn2 = document.getElementById(`on-2-${i}`).value;
      const tOff2 = document.getElementById(`off-2-${i}`).value;

      queryParams.push(`d${i}=${act1},${tOn1},${tOff1},${act2},${tOn2},${tOff2}`);
    }
    fetch(`/set-schedule?${queryParams.join('&')}`).then(r => r.text()).then(res => { alert("Agendamentos salvos!"); });
  }

  setInterval(updateClock, 1000);
  window.onload = () => { loadSchedule(); updateClock(); };
</script>
</body>
</html>
)rawliteral";

// === LOGICA DE HORARIO EM C++ ===
String getFormatedDateTime() {
  time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year < 100) return "Data/Hora Não Ajustada";
  char buffer[50]; 
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

void verificarESincronizarNTP() {
  static unsigned long ultimaTentativa = 0;
  time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
  
  if (timeinfo.tm_year < 100 && (millis() - ultimaTentativa > 60000 || ultimaTentativa == 0)) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Tentando sincronizar NTP em segundo plano...");
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
    }
    ultimaTentativa = millis();
  }
}

void gerenciarAgendamento() {
  static unsigned long ultimoSegundo = 0;
  if (millis() - ultimoSegundo < 1000) return; 
  ultimoSegundo = millis();

  verificarESincronizarNTP();

  time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year < 100) return; 

  int diaDaSemana = timeinfo.tm_wday; 
  int horaAtual = timeinfo.tm_hour;
  int minutoAtual = timeinfo.tm_min;
  int segundoAtual = timeinfo.tm_sec;

  if (segundoAtual == 0) {
    if (agendaSemana[diaDaSemana].t1.ativo) {
      if (horaAtual == agendaSemana[diaDaSemana].t1.horaOn && minutoAtual == agendaSemana[diaDaSemana].t1.minOn && !releState) {
        releState = true; digitalWrite(RELE_PIN, releState);
        lastTrigger = "Agenda T1";
      }
      else if (horaAtual == agendaSemana[diaDaSemana].t1.horaOff && minutoAtual == agendaSemana[diaDaSemana].t1.minOff && releState) {
        releState = false; digitalWrite(RELE_PIN, releState);
        lastTrigger = "Agenda T1 (Desl.)";
      }
    }
    
    if (agendaSemana[diaDaSemana].t2.ativo) {
      if (horaAtual == agendaSemana[diaDaSemana].t2.horaOn && minutoAtual == agendaSemana[diaDaSemana].t2.minOn && !releState) {
        releState = true; digitalWrite(RELE_PIN, releState);
        lastTrigger = "Agenda T2";
      }
      else if (horaAtual == agendaSemana[diaDaSemana].t2.horaOff && minutoAtual == agendaSemana[diaDaSemana].t2.minOff && releState) {
        releState = false; digitalWrite(RELE_PIN, releState);
        lastTrigger = "Agenda T2 (Desl.)";
      }
    }
  }
}

// === ROTAS WEB ===
void handleRoot() { server.send_P(200, "text/html", index_html); }
void handleAgenda() { server.send_P(200, "text/html", agenda_html); }

void handleToggle() {
  releState = !releState; digitalWrite(RELE_PIN, releState);
  lastTrigger = "Comando Web"; server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"state\":" + String(releState ? "true" : "false") + 
                ",\"last\":\"" + lastTrigger + 
                "\",\"clock\":\"" + getFormatedDateTime() + 
                "\",\"datetime\":\"" + getFormatedDateTime() + 
                "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", json);
}

void handleSetRTC() {
  if (server.hasArg("date") && server.hasArg("time")) {
    String dateParam = server.arg("date"); String timeParam = server.arg("time"); 
    struct tm tm_input; memset(&tm_input, 0, sizeof(struct tm));
    tm_input.tm_year = dateParam.substring(0, 4).toInt() - 1900; 
    tm_input.tm_mon = dateParam.substring(5, 7).toInt() - 1;     
    tm_input.tm_mday = dateParam.substring(8, 10).toInt();
    tm_input.tm_hour = timeParam.substring(0, 2).toInt();
    tm_input.tm_min = timeParam.substring(3, 5).toInt();
    time_t t = mktime(&tm_input); struct timeval tv = { .tv_sec = t, .tv_usec = 0 }; settimeofday(&tv, NULL);      
    server.send(200, "text/plain", "Relogio Atualizado!");
  } else { server.send(400, "text/plain", "Erro"); }
}

void handleSetSchedule() {
  for(int i=0; i<7; i++) {
    String argName = "d" + String(i);
    if(server.hasArg(argName)) {
      String val = server.arg(argName); 
      
      agendaSemana[i].t1.ativo = (val.charAt(0) == '1');
      agendaSemana[i].t1.horaOn = val.substring(2, 4).toInt();
      agendaSemana[i].t1.minOn = val.substring(5, 7).toInt();
      agendaSemana[i].t1.horaOff = val.substring(8, 10).toInt();
      agendaSemana[i].t1.minOff = val.substring(11, 13).toInt();

      agendaSemana[i].t2.ativo = (val.charAt(14) == '1');
      agendaSemana[i].t2.horaOn = val.substring(16, 18).toInt();
      agendaSemana[i].t2.minOn = val.substring(19, 21).toInt();
      agendaSemana[i].t2.horaOff = val.substring(22, 24).toInt();
      agendaSemana[i].t2.minOff = val.substring(25, 27).toInt();
    }
  }
  salvarAgendaNaMemoria();
  server.send(200, "text/plain", "OK");
}

void handleGetSchedule() {
  String json = "[";
  for(int i=0; i<7; i++) {
    char bOn1[6], bOff1[6], bOn2[6], bOff2[6];
    sprintf(bOn1, "%02d:%02d", agendaSemana[i].t1.horaOn, agendaSemana[i].t1.minOn);
    sprintf(bOff1, "%02d:%02d", agendaSemana[i].t1.horaOff, agendaSemana[i].t1.minOff);
    sprintf(bOn2, "%02d:%02d", agendaSemana[i].t2.horaOn, agendaSemana[i].t2.minOn);
    sprintf(bOff2, "%02d:%02d", agendaSemana[i].t2.horaOff, agendaSemana[i].t2.minOff);
    
    json += "{\"t1\":{\"active\":" + String(agendaSemana[i].t1.ativo ? "true" : "false") + ",\"on\":\"" + String(bOn1) + "\",\"off\":\"" + String(bOff1) + "\"},";
    json += "\"t2\":{\"active\":" + String(agendaSemana[i].t2.ativo ? "true" : "false") + ",\"on\":\"" + String(bOn2) + "\",\"off\":\"" + String(bOff2) + "\"}}";
    if(i < 6) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleUpdateOTAWeb() {
  server.send(200, "text/plain", "Atualizacao OTA iniciada! Acompanhe o progresso pelo Monitor Serial.");
  realizarAtualizacaoOTA();
}

void setup() {
  Serial.begin(115200);
  pinMode(RELE_PIN, OUTPUT); digitalWrite(RELE_PIN, LOW);
  pinMode(PINO_GATILHO, INPUT_PULLUP);

  carregarAgendaDaMemoria();

  // REMOVIDO O IP FIXO: Agora usa DHCP (IP automático)
  Serial.print("Conectando-se à rede Wi-Fi via DHCP: ");
  Serial.println(DEFAULT_SSID);

  WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
  
  int t = 0; 
  while (WiFi.status() != WL_CONNECTED && t < 20) { 
    delay(500); 
    Serial.print(".");
    t++; 
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado com sucesso via DHCP!");
    Serial.print("Endereço IP obtido: ");
    Serial.println(WiFi.localIP());

    Serial.println("Iniciando tentativa de sincronização de hora via NTP (Internet)...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

    int tentativasNTP = 0;
    struct tm timeinfo;
    memset(&timeinfo, 0, sizeof(struct tm));
    
    while (tentativasNTP < 10) {
      if (getLocalTime(&timeinfo)) {
        if (timeinfo.tm_year >= 100) { 
          Serial.println("SUCESSO: Hora sincronizada com sucesso pela Internet!");
          break;
        }
      }
      delay(500);
      Serial.print(".");
      tentativasNTP++;
    }
    Serial.println();

    if (timeinfo.tm_year < 100) {
      Serial.println("AVISO: A tentativa inicial de pegar a hora falhou. O sistema continuará tentando em segundo plano a cada 1 minuto.");
    }

  } else {
    Serial.println("Não conseguiu conectar ao Wi-Fi! Iniciando modo Access Point (AP)...");
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP iniciado. IP do Access Point: ");
    Serial.println(WiFi.softAPIP());
  }

  server.on("/", handleRoot);
  server.on("/agenda", handleAgenda);
  server.on("/agenda.html", handleAgenda);
  server.on("/toggle", handleToggle);
  server.on("/status", handleStatus);
  server.on("/set-schedule", handleSetSchedule);
  server.on("/get-schedule", handleGetSchedule);
  server.on("/set-rtc", handleSetRTC);
  server.on("/update-ota", handleUpdateOTAWeb);
  
  server.begin(); 
  ArduinoOTA.begin();
  Serial.println("Servidor HTTP e OTA iniciados.");
}

void loop() {
  server.handleClient(); ArduinoOTA.handle(); gerenciarAgendamento();

  if (digitalRead(PINO_GATILHO) == LOW) {
    delay(50); if (digitalRead(PINO_GATILHO) == LOW) {
      releState = !releState; digitalWrite(RELE_PIN, releState);
      lastTrigger = "Botao Fisico"; while(digitalRead(PINO_GATILHO) == LOW); 
    }
  }
}
