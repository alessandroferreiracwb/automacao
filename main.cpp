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
// CONFIGURACAO TELEGRAM (Substituto ideal para receber mensagens)
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
// API DE TEMPERATURA - CURITIBA
// =====================================================
const char* weatherApiUrl =
    "https://api.open-meteo.com/v1/forecast?latitude=-25.4284&longitude=-49.2733&current=temperature_2m";

// =====================================================
// DISPLAY E LVGL
// =====================================================
TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 10];

static lv_obj_t* label_date;
static lv_obj_t* label_temp;
static lv_obj_t* label_status;
static lv_obj_t* label_hour;
static lv_obj_t* label_min;
static lv_obj_t* label_sec;
static lv_obj_t* label_msg_recebida; // Exibirá a última mensagem recebida

// =====================================================
// CORES
// =====================================================
#define COLOR_BACKGROUND 0xFFFFFF
#define COLOR_CARD       0x000000
#define COLOR_BORDER     0x374151
#define COLOR_WHITE      0x9CA3AF
#define COLOR_GRAY       0x9CA3AF
#define COLOR_BLUE       0x60A5FA
#define COLOR_GREEN      0x00FF00
#define COLOR_RED        0xFF0000
#define COLOR_ORANGE     0xF59E0B
#define COLOR_PINK       0xFC18

// =====================================================
// TEMPORIZADORES
// =====================================================
unsigned long lastSecondTick = 0;
unsigned long lastWeatherCheck = 0;
unsigned long lastMsgCheck = 0;
unsigned long lastLvglTick = 0;

float currentTemperature = 0.0;

// =====================================================
// DRIVER DO DISPLAY
// =====================================================
void my_disp_flush(
    lv_disp_drv_t* disp,
    const lv_area_t* area,
    lv_color_t* color_p
) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// =====================================================
// VERIFICAR MENSAGENS RECEBIDAS (TELEGRAM / MENSAGERIA)
// =====================================================
void check_incoming_messages() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    Serial.println("Verificando novas mensagens...");
    
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
                        Serial.printf("Nova mensagem recebida: %s\n", text);
                        char displayBuffer[60];
                        snprintf(displayBuffer, sizeof(displayBuffer), "Msg: %s", text);
                        lv_label_set_text(label_msg_recebida, displayBuffer);
                        lv_obj_set_style_text_color(label_msg_recebida, lv_color_hex(COLOR_GRAY), LV_PART_MAIN);
                    }
                }
            }
        }
    }
    http.end();
}

// =====================================================
// ATUALIZAR TEMPERATURA
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
        DynamicJsonDocument doc(2048);
        if (!deserializeJson(doc, payload) && !doc["current"]["temperature_2m"].isNull()) {
            currentTemperature = doc["current"]["temperature_2m"].as<float>();
            char tempStr[40];
            snprintf(tempStr, sizeof(tempStr), "Curitiba: %.1f C", currentTemperature);
            lv_label_set_text(label_temp, tempStr);
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
        lv_label_set_text(label_status, "Conectado");
        lv_obj_set_style_text_color(label_status, lv_color_hex(COLOR_GRAY), LV_PART_MAIN);
    } else {
        lv_label_set_text(label_status, "Nao Conectado");
        lv_obj_set_style_text_color(label_status, lv_color_hex(COLOR_BLUE), LV_PART_MAIN);
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
        lv_label_set_text(label_date, "Sincronizando NTP...");
    }
}

// =====================================================
// CRIAR INTERFACE GRAFICA
// =====================================================
void create_gui() {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t* card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, 310, 220);
    lv_obj_center(card);

    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_date = lv_label_create(card);
    lv_label_set_text(label_date, "Aguardando NTP...");
    lv_obj_set_style_text_color(label_date, lv_color_hex(COLOR_GRAY), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_date, LV_ALIGN_TOP_LEFT, 0, 0);

    label_status = lv_label_create(card);
    lv_label_set_text(label_status, "Conectando...");
    lv_obj_set_style_text_color(label_status, lv_color_hex(COLOR_GRAY), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_status, LV_ALIGN_TOP_RIGHT, 0, 0);

    label_temp = lv_label_create(card);
    lv_label_set_text(label_temp, "Curitiba: -- C");
    lv_obj_set_style_text_color(label_temp, lv_color_hex(COLOR_BLUE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_temp, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t* time_container = lv_obj_create(card);
    lv_obj_set_size(time_container, 290, 80);
    lv_obj_align(time_container, LV_ALIGN_CENTER, 0, 5);
    lv_obj_set_style_bg_color(time_container, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(time_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(time_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(time_container, LV_OBJ_FLAG_SCROLLABLE);

    label_hour = lv_label_create(time_container);
    lv_obj_set_size(label_hour, 80, 70);
    lv_label_set_text(label_hour, "--");
    lv_obj_set_style_text_align(label_hour, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_hour, lv_color_hex(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_hour, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(label_hour, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* sep1 = lv_label_create(time_container);
    lv_label_set_text(sep1, " ");
    lv_obj_set_style_text_color(sep1, lv_color_hex(COLOR_BLUE), LV_PART_MAIN);
    lv_obj_set_style_text_font(sep1, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(sep1, LV_ALIGN_CENTER, -35, -5);

    label_min = lv_label_create(time_container);
    lv_obj_set_size(label_min, 80, 70);
    lv_label_set_text(label_min, "--");
    lv_obj_set_style_text_align(label_min, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_min, lv_color_hex(COLOR_WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_min, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(label_min, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* sep2 = lv_label_create(time_container);
    lv_label_set_text(sep2, " ");
    lv_obj_set_style_text_color(sep2, lv_color_hex(COLOR_BLUE), LV_PART_MAIN);
    lv_obj_set_style_text_font(sep2, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(sep2, LV_ALIGN_CENTER, 35, -5);

    label_sec = lv_label_create(time_container);
    lv_obj_set_size(label_sec, 80, 70);
    lv_label_set_text(label_sec, "--");
    lv_obj_set_style_text_align(label_sec, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_sec, lv_color_hex(COLOR_GRAY), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_sec, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(label_sec, LV_ALIGN_RIGHT_MID, 0, 0);

    // LABEL PARA EXIBIR MENSAGEM RECEBIDA NA TELA
    label_msg_recebida = lv_label_create(card);
    lv_label_set_text(label_msg_recebida, "Aguardando mensagens...");
    lv_obj_set_style_text_color(label_msg_recebida, lv_color_hex(COLOR_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_msg_recebida, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_msg_recebida, LV_ALIGN_BOTTOM_MID, 0, -8);
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

    // VERIFICAR NOVAS MENSAGENS A CADA 5 SEGUNDOS
    if (now - lastMsgCheck >= 5000) {
        lastMsgCheck = now;
        check_incoming_messages();
    }

    lv_timer_handler();
    delay(5);
}