#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <sys/time.h> 

// === CONFIGURAÇÃO DE REDE ===
const char* DEFAULT_SSID = "Alessandro_2G";
const char* DEFAULT_PASS = "Van@1981";
const char* AP_SSID = "ESP32-Setup";
const char* AP_PASS = "12345678";

IPAddress local_IP(172, 16, 0, 50);
IPAddress gateway(172, 16, 0, 1);    
IPAddress subnet(255, 255, 255, 0);

WebServer server(2080);

// === RELÉ E GATILHO ===
const uint8_t RELE_PIN = 25;        
const uint8_t PINO_GATILHO = 15;    
bool releState = false;             
String lastTrigger = "Inicializado"; 

// === ESTRUTURA PARA AGENDAMENTO DIÁRIO COMPLETO ===
struct AgendaDia {
  bool ativo;
  int horaOn;
  int minOn;
  int horaOff;
  int minOff;
};

// Inicializa os 7 dias (0 = Domingo, 1 = Segunda ... 6 = Sábado)
// Padrão inicial: das 08:00 às 18:00, todos desativados
AgendaDia agendaSemana[7] = {
  {false, 8, 0, 18, 0}, // Dom
  {false, 8, 0, 18, 0}, // Seg
  {false, 8, 0, 18, 0}, // Ter
  {false, 8, 0, 18, 0}, // Qua
  {false, 8, 0, 18, 0}, // Qui
  {false, 8, 0, 18, 0}, // Sex
  {false, 8, 0, 18, 0}  // Sáb
};

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
    .card { background: #1e293b; padding: 40px 25px; border-radius: 32px; text-align: center; box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5); }
    h3 { margin-top: 0; color: #94a3b8; font-size: 14px; text-transform: uppercase; letter-spacing: 1px; }
    .btn { width: 60vw; height: 60vw; max-width: 220px; max-height: 220px; border-radius: 50%; border: 10px solid #334155; font-size: 22px; font-weight: 900; cursor: pointer; transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); margin: 30px auto; display: flex; align-items: center; justify-content: center; outline: none; }
    .btn:active { transform: scale(0.92); }
    .on { background: #22c55e; color: #052e16; border-color: #4ade80; box-shadow: 0 0 40px rgba(34,197,94,0.4); }
    .off { background: #ef4444; color: #450a0a; border-color: #f87171; box-shadow: 0 0 20px rgba(239,68,68,0.1); }
    .info { background: #0f172a; padding: 20px; border-radius: 20px; font-size: 15px; color: #94a3b8; line-height: 1.5; margin-top: 10px; }
    .info strong { display: block; color: #38bdf8; font-size: 18px; margin-top: 5px; }
    .footer-ip { font-size: 10px; color: #475569; margin-top: 20px; }
    .nav-link { display: block; margin-top: 20px; color: #38bdf8; text-decoration: none; font-size: 15px; font-weight: bold; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h3>Controle de Relé 1</h3>
      <button id="btn" class="btn" onclick="toggle()">...</button>
      <div class="info">Status: <span id="st-txt">-</span><strong><span id="last">Aguardando...</span></strong></div>
      <a href="/agenda" class="nav-link">🗓️ Configurar Agenda Semanal</a>
      <div class="footer-ip">Conectado em: 172.16.0.50:2080</div>
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
    }).catch(err => { document.getElementById('st-txt').innerText = "OFFLINE"; });
  }
  function toggle() { 
    document.getElementById('btn').style.opacity = "0.7";
    fetch('/toggle').then(() => { document.getElementById('btn').style.opacity = "1"; update(); });
  }
  setInterval(update, 2000); update();
</script>
</body>
</html>
)rawliteral";

// === PÁGINA 2: AGENDA.HTML (MÚLTIPLOS HORÁRIOS PERSONALIZADOS) ===
const char agenda_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Agendamento Personalizado</title>
  <style>
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: white; margin: 0; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
    .container { width: 100%; max-width: 460px; padding: 15px; }
    .card { background: #1e293b; padding: 25px 20px; border-radius: 32px; box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5); }
    h3 { margin: 15px 0 5px 0; color: #94a3b8; font-size: 13px; text-transform: uppercase; letter-spacing: 1px; text-align: center; }
    .clock-info { background: #0f172a; padding: 12px; border-radius: 16px; text-align: center; margin-bottom: 20px; font-size: 15px; color: #38bdf8; font-weight: bold; }
    .clock-info span { display: block; font-size: 22px; color: white; margin-top: 3px; }
    .btn-rtc { width: 100%; background: transparent; color: #38bdf8; border: 2px dashed #38bdf8; padding: 10px; font-size: 12px; font-weight: bold; border-radius: 14px; cursor: pointer; margin-bottom: 20px; }
    
    /* Linhas da Agenda */
    .day-row { background: #0f172a; padding: 12px; border-radius: 16px; margin-bottom: 10px; display: flex; align-items: center; justify-content: space-between; gap: 8px; }
    .day-label { font-weight: bold; font-size: 13px; color: #e2e8f0; width: 45px; }
    .time-inputs { display: flex; align-items: center; gap: 5px; }
    .time-inputs input[type="time"] { background: #1e293b; border: 1px solid #334155; color: #38bdf8; font-weight: bold; font-size: 13px; padding: 6px; border-radius: 8px; outline: none; color-scheme: dark; text-align: center; }
    .time-separator { color: #64748b; font-size: 11px; }
    
    /* Toggle Switch */
    .switch { position: relative; display: inline-block; width: 44px; height: 24px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #334155; transition: .3s; border-radius: 24px; }
    .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; }
    input:checked + .slider { background-color: #22c55e; }
    input:checked + .slider:before { transform: translateX(20px); }

    .btn-save { width: 100%; background: #22c55e; color: #052e16; border: none; padding: 16px; font-size: 16px; font-weight: bold; border-radius: 16px; cursor: pointer; margin-top: 15px; }
    .btn-save:active { transform: scale(0.97); }
    .nav-link { display: block; text-align: center; margin-top: 20px; color: #94a3b8; text-decoration: none; font-size: 14px; }
    hr { border: 0; height: 1px; background: #334155; margin: 20px 0; }
    .hidden-inputs { display: none; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <div class="clock-info">HORA DO ESP32<span id="esp-clock">--:--:--</span></div>
      
      <div class="hidden-inputs">
        <input type="date" id="manual-date">
        <input type="time" id="manual-time">
      </div>
      <button class="btn-rtc" onclick="setESPClock()">⚙️ ATUALIZAR RELÓGIO COM A HORA DO TELEFONE</button>

      <hr>
      <h3>Agenda Semanal Personalizada</h3>
      
      <div id="agenda-container">
        </div>

      <button class="btn-save" onclick="saveSchedule()">💾 SALVAR COFIGURAÇÕES</button>
      <a href="/" class="nav-link">← Voltar ao Painel Geral</a>
    </div>
  </div>

<script>
  const nomesDias = ["DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"];

  function carregarHoraDispositivo() {
    const agora = new Date();
    document.getElementById('manual-date').value = `${agora.getFullYear()}-${String(agora.getMonth() + 1).padStart(2, '0')}-${String(agora.getDate()).padStart(2, '0')}`;
    document.getElementById('manual-time').value = `${String(agora.getHours()).padStart(2, '0')}:${String(agora.getMinutes()).padStart(2, '0')}`;
  }

  function setESPClock() {
    carregarHoraDispositivo();
    fetch(`/set-rtc?date=${document.getElementById('manual-date').value}&time=${document.getElementById('manual-time').value}`)
      .then(r => r.text()).then(res => { alert(res); updateClock(); });
  }

  function renderAgenda(data) {
    const container = document.getElementById('agenda-container');
    container.innerHTML = "";
    nomesDias.forEach((dia, idx) => {
      const d = data[idx];
      const row = document.createElement('div');
      row.className = 'day-row';
      row.innerHTML = `
        <div class="day-label">${dia}</div>
        <div class="time-inputs">
          <input type="time" id="on-${idx}" value="${d.on}">
          <span class="time-separator">até</span>
          <input type="time" id="off-${idx}" value="${d.off}">
        </div>
        <label class="switch">
          <input type="checkbox" id="chk-${idx}" ${d.active ? 'checked' : ''}>
          <span class="slider"></span>
        </label>
      `;
      container.appendChild(row);
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
      const active = document.getElementById(`chk-${i}`).checked ? "1" : "0";
      const tOn = document.getElementById(`on-${i}`).value;
      const tOff = document.getElementById(`off-${i}`).value;
      queryParams.push(`d${i}=${active},${tOn},${tOff}`);
    }
    fetch(`/set-schedule?${queryParams.join('&')}`).then(r => r.text()).then(res => { alert("Configurações diárias salvas!"); });
  }

  setInterval(updateClock, 1000);
  window.onload = () => { loadSchedule(); updateClock(); carregarHoraDispositivo(); };
</script>
</body>
</html>
)rawliteral";

// === LOGICA DE HORARIO EM C++ ===
String getFormatedTime() {
  time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year < 100) return "Não Ajustado";
  char timeStringBuff[50]; strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void gerenciarAgendamento() {
  static unsigned long ultimoSegundo = 0;
  if (millis() - ultimoSegundo < 1000) return; 
  ultimoSegundo = millis();

  time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year < 100) return; 

  int diaDaSemana = timeinfo.tm_wday; 
  int horaAtual = timeinfo.tm_hour;
  int minutoAtual = timeinfo.tm_min;
  int segundoAtual = timeinfo.tm_sec;

  // Verifica as regras específicas do dia de hoje
  if (segundoAtual == 0 && agendaSemana[diaDaSemana].ativo) {
    if (horaAtual == agendaSemana[diaDaSemana].horaOn && minutoAtual == agendaSemana[diaDaSemana].minOn && !releState) {
      releState = true;
      digitalWrite(RELE_PIN, releState);
      lastTrigger = "Agenda (" + String(timeinfo.tm_mday) + ")";
    }
    else if (horaAtual == agendaSemana[diaDaSemana].horaOff && minutoAtual == agendaSemana[diaDaSemana].minOff && releState) {
      releState = false;
      digitalWrite(RELE_PIN, releState);
      lastTrigger = "Agenda (Desligar)";
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
  String json = "{\"state\":" + String(releState ? "true" : "false") + ",\"last\":\"" + lastTrigger + "\",\"clock\":\"" + getFormatedTime() + "\"}";
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
    server.send(200, "text/plain", "Relógio sincronizado!");
  } else { server.send(400, "text/plain", "Erro"); }
}

// Recebe o agendamento completo e divide dia a dia
void handleSetSchedule() {
  for(int i=0; i<7; i++) {
    String argName = "d" + String(i);
    if(server.hasArg(argName)) {
      String val = server.arg(argName); // Ex: "1,08:00,18:00"
      agendaSemana[i].ativo = (val.charAt(0) == '1');
      agendaSemana[i].horaOn = val.substring(2, 4).toInt();
      agendaSemana[i].minOn = val.substring(5, 7).toInt();
      agendaSemana[i].horaOff = val.substring(8, 10).toInt();
      agendaSemana[i].minOff = val.substring(11, 13).toInt();
    }
  }
  server.send(200, "text/plain", "OK");
}

// Envia os horários individuais de cada dia para preencher a tabela no navegador
void handleGetSchedule() {
  String json = "[";
  for(int i=0; i<7; i++) {
    char bOn[6], bOff[6];
    sprintf(bOn, "%02d:%02d", agendaSemana[i].horaOn, agendaSemana[i].minOn);
    sprintf(bOff, "%02d:%02d", agendaSemana[i].horaOff, agendaSemana[i].minOff);
    json += "{\"active\":" + String(agendaSemana[i].ativo ? "true" : "false") + ",\"on\":\"" + String(bOn) + "\",\"off\":\"" + String(bOff) + "\"}";
    if(i < 6) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELE_PIN, OUTPUT); digitalWrite(RELE_PIN, LOW);
  pinMode(PINO_GATILHO, INPUT_PULLUP);

  if (!WiFi.config(local_IP, gateway, subnet)) Serial.println("Falha IP Fixo");
  WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
  int t = 0; while (WiFi.status() != WL_CONNECTED && t < 12) { delay(500); t++; }
  if (WiFi.status() != WL_CONNECTED) WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/agenda", handleAgenda);
  server.on("/agenda.html", handleAgenda);
  server.on("/toggle", handleToggle);
  server.on("/status", handleStatus);
  server.on("/set-schedule", handleSetSchedule);
  server.on("/get-schedule", handleGetSchedule);
  server.on("/set-rtc", handleSetRTC);
  
  server.begin(); ArduinoOTA.begin();
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