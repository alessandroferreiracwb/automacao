#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// === CONFIGURAÇÕES ===
const char* ssid = "NEXTRENTAL_GUEST";
const char* password = "N3XT@2024";
const char* host = "esp32";

// Pinos dos relés
const uint8_t RELE_PINS[4] = {15, 2, 5, 18}; // RELE1, RELE2, RELE3, RELE4
bool releStates[4] = {false, false, false, false}; // Estados iniciais: todos desligados

WebServer server(80);

// === HTML + JS (tudo em uma string) ===
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Controle de Relés</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; text-align: center; margin: 0; padding: 20px; background: #f5f5f5; }
    h1 { color: #333; }
    .relay { margin: 15px auto; padding: 15px; max-width: 300px; background: white; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }
    .btn {
      width: 100%%; padding: 12px; font-size: 18px; font-weight: bold; border: none; border-radius: 8px;
      cursor: pointer; transition: opacity 0.2s;
    }
    .on { background: #4CAF50; color: white; }
    .off { background: #f44336; color: white; }
    .btn:active { opacity: 0.8; }
    .status { margin-top: 10px; font-size: 14px; color: #666; }
  </style>
</head>
<body>
  <h1>Controle de Relés (Local)</h1>
  <div id="relays"></div>

  <script>
    function toggle(id) {
      fetch('/toggle?id=' + id)
        .then(() => updateUI())
        .catch(err => console.error('Erro:', err));
    }

    function updateUI() {
      fetch('/states')
        .then(res => res.json())
        .then(states => {
          let html = '';
          states.forEach((on, i) => {
            const cls = on ? 'on' : 'off';
            const text = on ? 'LIGADO' : 'DESLIGADO';
            html += `<div class="relay">
              <button class="btn ${cls}" onclick="toggle(${i + 1})">RELÉ ${i + 1}</button>
              <div class="status">${text}</div>
            </div>`;
          });
          document.getElementById('relays').innerHTML = html;
        });
    }

    // Atualiza ao carregar
    window.onload = updateUI;
  </script>
</body>
</html>
)rawliteral";

// === HANDLERS ===

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleToggle() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 1 && id <= 4) {
      releStates[id - 1] = !releStates[id - 1];
      digitalWrite(RELE_PINS[id - 1], releStates[id - 1] ? HIGH : LOW);
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleStates() {
  String json = "[";
  for (int i = 0; i < 4; i++) {
    json += (releStates[i] ? "true" : "false");
    if (i < 3) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// === SETUP ===
void setup() {
  Serial.begin(115200);

  // Configura relés como saída e desliga
  for (int i = 0; i < 4; i++) {
    pinMode(RELE_PINS[i], OUTPUT);
    digitalWrite(RELE_PINS[i], LOW);
  }

  // Conecta ao Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }

  Serial.println("IP: " + WiFi.localIP().toString());

  // Inicia mDNS (http://esp32.local)
  if (MDNS.begin(host)) {
    Serial.println("mDNS ativo");
  }

  // Rotas
  server.on("/", HTTP_GET, handleRoot);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/states", HTTP_GET, handleStates);

  server.begin();
  Serial.println("Servidor iniciado");
}

// === LOOP ===
void loop() {
  server.handleClient(); // Não usa delay → 100% responsivo
}
