#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

// =====================================================
// CONFIGURACAO WI-FI
// =====================================================
const char* ssid = "ShowRoom";
const char* password = "Nevil@2026";

// =====================================================
// CONFIGURACAO TELEGRAM
// =====================================================
const char* botToken = "8483406161:AAHNJZBD9kaaXgP0173abnofAKi8HPfnL_s"; 
long long lastChatId = 0;
long lastMessageId = 0;

// =====================================================
// CONFIGURACAO NTP - HORARIO DE BRASILIA
// =====================================================
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.google.com";

const long gmtOffset_sec = -10800;
const int daylightOffset_sec = 0;

// =====================================================
// API DE TEMPERATURA E CLIMA
// =====================================================
const char* weatherApiUrl =
    "https://api.open-meteo.com/v1/forecast?latitude=-25.4284&longitude=-49.2733&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min,weather_code&timezone=America%2FSao_Paulo";

// =====================================================
// DISPLAY E LVGL
// =====================================================
TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 10];

static lv_obj_t* label_date;
static lv_obj_t* label_temp;
static lv_obj_t* label_forecast;
static lv_obj_t* label_status;
static lv_obj_t* label_hour;
static lv_obj_t* label_min;
static lv_obj_t* label_sec;
static lv_obj_t* label_msg_recebida;

// =====================================================
// CORES DA NOVA PALETA (Estilo Dark Moderno)
// =====================================================
#define COLOR_BACKGROUND 0x111111 // Fundo escuro elegante
#define COLOR_CARD       0x1E293B // Azul acinzentado escuro (Slate)
#define COLOR_BORDER     0x334155 // Borda sutil
#define COLOR_TEXT_MAIN  0xFFFFFF // Branco puro
#define COLOR_TEXT_MUTED 0x94A3B8 // Cinza claro
#define COLOR_ACCENT     0x38BDF8 // Azul claro vibrante

// =====================================================
// TEMPORIZADORES
// =====================================================
unsigned long lastSecondTick = 0;
unsigned long lastWeatherCheck = 0;
unsigned long lastMsgCheck = 0;
unsigned long lastLvglTick = 0;

float currentTemperature = 0.0;

// =====================================================
// TRADUÇÃO DO CLIMA (WMO)
// =====================================================
const char* getWeatherDescription(int code) {
    if (code == 0) return "Ensolarado";
    if (code >= 1 && code <= 3) return "Parc. Nublado";
    if (code >= 45 && code <= 48) return "Nevoeiro";
    if (code >= 51 && code <= 67) return "Chovendo";
    if (code >= 71 && code <= 77) return "Neve";
    if (code >= 80 && code <= 82) return "Pancadas de Chuva";
    if (code >= 95 && code <= 99) return "Tempestade";
    return "Nublado";
}

// =====================================================
// DRIVER DO DISPLAY
// =====================================================
void my_disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// =====================================================
// VERIFICAR MENSAGENS (TELEGRAM)
// =====================================================
void check_incoming_messages() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(botToken) + "/getUpdates?offset=-1";

    http.begin(client, url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            JsonArray results = doc["result"];
            if (!results.isNull() && results.size() > 0) {
                JsonObject latest = results[results.size() - 1];
                long msgId = latest["message"]["message_id"];

                if (msgId > lastMessageId) {
                    lastMessageId = msgId;
                    const char* text = latest["message"]["text"];
                    if (text) {
                        char displayBuffer[60];
                        snprintf(displayBuffer, sizeof(displayBuffer), "Msg: %s", text);
                        lv_label_set_text(label_msg_recebida, displayBuffer);
                    }
                }
            }
        }
    }
    http.end();
}

// =====================================================
// ATUALIZAR TEMPERATURA E PREVISÃO
// =====================================================
void update_weather() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, weatherApiUrl);
    int httpResponseCode = http.GET();

    if (httpResponseCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            if (!doc["current"]["temperature_2m"].isNull()) {
                currentTemperature = doc["current"]["temperature_2m"].as<float>();
                int currentWeatherCode = doc["current"]["weather_code"].as<int>();
                
                char tempStr[50];
                snprintf(tempStr, sizeof(tempStr), "Agora: %.1f°C - %s", currentTemperature, getWeatherDescription(currentWeatherCode));
                lv_label_set_text(label_temp, tempStr);
            }

            if (!doc["daily"]["temperature_2m_max"].isNull()) {
                float maxAmanha = doc["daily"]["temperature_2m_max"][1].as<float>();
                float minAmanha = doc["daily"]["temperature_2m_min"][1].as<float>();
                int codeAmanha = doc["daily"]["weather_code"][1].as<int>();

                char forecastStr[60];
                snprintf(forecastStr, sizeof(forecastStr), "Amanha: %.0f° / %.0f° - %s", minAmanha, maxAmanha, getWeatherDescription(codeAmanha));
                lv_label_set_text(label_forecast, forecastStr);
            }
        }
    }
    http.end();
}

// =====================================================
// ATUALIZAR RELOGIO E DATA
// =====================================================
void force_clock_update() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(label_status, "● Online");
        lv_obj_set_style_text_color(label_status, lv_color_hex(0x22C55E), LV_PART_MAIN); // Verde
    } else {
        lv_label_set_text(label_status, "○ Offline");
        lv_obj_set_style_text_color(label_status, lv_color_hex(0xEF4444), LV_PART_MAIN); // Vermelho
    }

    if (timeinfo.tm_year > 70) {
        char hourStr[4], minStr[4], secStr[4], dateStr[20];
        snprintf(hourStr, sizeof(hourStr), "%02d", timeinfo.tm_hour);
        snprintf(minStr, sizeof(minStr), "%02d", timeinfo.tm_min);
        snprintf(secStr, sizeof(secStr), "%02d", timeinfo.tm_sec);
        strftime(dateStr, sizeof(dateStr), "%d/%m/%Y", &timeinfo);

        lv_label_set_text(label_hour, hourStr);
        lv_label_set_text(label_min, minStr);
        lv_label_set_text(label_sec, secStr);
        lv_label_set_text(label_date, dateStr);
    } else {
        lv_label_set_text(label_hour, "--");
        lv_label_set_text(label_min, "--");
        lv_label_set_text(label_sec, "--");
        lv_label_set_text(label_date, "Sincronizando...");
    }
}

// =====================================================
// CRIAR INTERFACE GRAFICA MODERNA
// =====================================================
void create_gui() {
    // Fundo da tela
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    // --- CARD SUPERIOR (Relógio e Horário) ---
    lv_obj_t* card_top = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card_top, 305, 115);
    lv_obj_align(card_top, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(card_top, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_color(card_top, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_top, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_top, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card_top, LV_OBJ_FLAG_SCROLLABLE);

    // Data
    label_date = lv_label_create(card_top);
    lv_label_set_text(label_date, "--/--/----");
    lv_obj_set_style_text_color(label_date, lv_color_hex(COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_date, LV_ALIGN_TOP_LEFT, 0, 0);

    // Status Wi-Fi
    label_status = lv_label_create(card_top);
    lv_label_set_text(label_status, "Conectando...");
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_status, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Relógio Central dentro do Card Superior
    lv_obj_t* time_container = lv_obj_create(card_top);
    lv_obj_set_size(time_container, 260, 65);
    lv_obj_align(time_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(time_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(time_container, LV_OBJ_FLAG_SCROLLABLE);

    label_hour = lv_label_create(time_container);
    lv_label_set_text(label_hour, "--");
    lv_obj_set_style_text_color(label_hour, lv_color_hex(COLOR_TEXT_MAIN), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_hour, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(label_hour, LV_ALIGN_LEFT_MID, -5, -5);

    lv_obj_t* sep1 = lv_label_create(time_container);
    lv_label_set_text(sep1, ":");
    lv_obj_set_style_text_color(sep1, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(sep1, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(sep1, LV_ALIGN_CENTER, -30, -10);

    label_min = lv_label_create(time_container);
    lv_label_set_text(label_min, "--");
    lv_obj_set_style_text_color(label_min, lv_color_hex(COLOR_TEXT_MAIN), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_min, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(label_min, LV_ALIGN_CENTER, 5, -5);

    lv_obj_t* sep2 = lv_label_create(time_container);
    lv_label_set_text(sep2, ":");
    lv_obj_set_style_text_color(sep2, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(sep2, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(sep2, LV_ALIGN_CENTER, 35, -10);

    label_sec = lv_label_create(time_container);
    lv_label_set_text(label_sec, "--");
    lv_obj_set_style_text_color(label_sec, lv_color_hex(COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_sec, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(label_sec, LV_ALIGN_RIGHT_MID, 5, 0);

    // --- CARD INFERIOR (Clima, Previsão e Mensagens) ---
    lv_obj_t* card_bottom = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card_bottom, 305, 103);
    lv_obj_align(card_bottom, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(card_bottom, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_color(card_bottom, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(card_bottom, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card_bottom, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card_bottom, LV_OBJ_FLAG_SCROLLABLE);

    // Temperatura Atual
    label_temp = lv_label_create(card_bottom);
    lv_label_set_text(label_temp, "Agora: Carregando...");
    lv_obj_set_style_text_color(label_temp, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_temp, LV_ALIGN_TOP_LEFT, 0, -2);

    // Previsão Amanhã
    label_forecast = lv_label_create(card_bottom);
    lv_label_set_text(label_forecast, "Amanha: Carregando...");
    lv_obj_set_style_text_color(label_forecast, lv_color_hex(COLOR_TEXT_MAIN), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_forecast, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_forecast, LV_ALIGN_TOP_LEFT, 0, 20);

    // Mensagem Recebida do Telegram
    label_msg_recebida = lv_label_create(card_bottom);
    lv_label_set_text(label_msg_recebida, "Msg: Aguardando mensagens...");
    lv_obj_set_style_text_color(label_msg_recebida, lv_color_hex(COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_msg_recebida, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_msg_recebida, LV_ALIGN_BOTTOM_LEFT, 0, 2);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
        delay(500);
    }

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 320 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    create_gui();
    force_clock_update();
    update_weather();

    unsigned long now = millis();
    lastSecondTick = now;
    lastWeatherCheck = now;
    lastMsgCheck = now;
    lastLvglTick = now;
}

// =====================================================
// LOOP
// =====================================================
void loop() {
    unsigned long now = millis();

    lv_tick_inc(now - lastLvglTick);
    lastLvglTick = now;

    if (now - lastSecondTick >= 1000) {
        lastSecondTick = now;
        force_clock_update();
    }

    if (now - lastWeatherCheck >= 600000) {
        lastWeatherCheck = now;
        update_weather();
    }

    if (now - lastMsgCheck >= 5000) {
        lastMsgCheck = now;
        check_incoming_messages();
    }

    lv_timer_handler();
    delay(5);
}