#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ESPDashboardPlus.h"
#include "dashboard_html.h"
#include "wifi_config.h"           // WiFi через NVS + AP-fallback

// ==========================================
// RGB-СВЕТОДИОД (ESP32-S3-DevKitC-1, WS2812)
// ==========================================
// На официальной плате RGB LED подключён к GPIO48.
// На клонах может быть GPIO38 или GPIO21 — проверьте свою плату!
#define RGB_LED_PIN 48

// ==========================================
// НАСТРОЙКИ ПИНОВ ESP32-S3
// ==========================================
#define ESP32_CAN_TX_PIN GPIO_NUM_5
#define ESP32_CAN_RX_PIN GPIO_NUM_4

const int PIN_RPM_SENSE     = 1;
const int PIN_TEMP_SENSE    = 2;
const int PIN_VOLTAGE_SENSE = 3;

// ==========================================
// КОНСТАНТЫ ДЛЯ РАСЧЕТОВ
// ==========================================
const float VOLTAGE_DIVIDER_RATIO = 5.0;
const float TEMP_NOMINAL          = 25.0;
const float TEMP_NOMINAL_RES      = 10000;
const float TEMP_BETA             = 3950;
const float SERIES_RESISTOR       = 10000;

// ==========================================
// ОБЪЕКТЫ
// ==========================================
AsyncWebServer server(80);
ESPDashboardPlus dashboard("Hangkai 30 Gateway");

// ==========================================
// NMEA 2000
// ==========================================
#include <NMEA2000_esp32_twai.h>
#include <N2kMessages.h>

tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai());

// ==========================================
// ПЕРЕМЕННЫЕ ДЛЯ ДАННЫХ
// ==========================================
double currentRPM     = 0.0;
double currentTemp    = 0.0;
double currentVoltage = 0.0;
double engineHours    = 0.0;

unsigned long lastNMEAUpdate = 0;
unsigned long lastWebUpdate  = 0;
bool          engineRunning  = false;
unsigned long lastEngineStartTime = 0;

// ==========================================
// ФЛАГИ И ПЕРЕМЕННЫЕ ДЛЯ LED
// ==========================================
bool canInitialized = false;
bool ledState = false;
unsigned long lastLedToggle = 0;

// ==========================================
// УПРАВЛЕНИЕ RGB-СВЕТОДИОДОМ
// ==========================================
void setLed(uint8_t r, uint8_t g, uint8_t b) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  rgbLedWrite(RGB_LED_PIN, r, g, b);
#else
  neopixelWrite(RGB_LED_PIN, r, g, b);
#endif
}

void ledOff()    { setLed(0, 0, 0); }
void ledPurple() { setLed(80, 0, 160); }

// ==========================================
// ФУНКЦИИ СЧИТЫВАНИЯ ДАТЧИКОВ
// ==========================================
volatile unsigned long pulseCount = 0;
unsigned long lastRPMTime = 0;
const unsigned long PULSES_PER_REV = 6;

void IRAM_ATTR rpmISR() {
  pulseCount = pulseCount + 1;
}

double readRPM() {
  unsigned long now = millis();
  if (now - lastRPMTime >= 500) {
    noInterrupts();
    unsigned long count = pulseCount;
    pulseCount = 0;
    interrupts();

    double rpm = (count / (double)PULSES_PER_REV) * (60000.0 / (now - lastRPMTime));
    lastRPMTime = now;

    if (rpm < 300) rpm = 0;
    return rpm;
  }
  return currentRPM;
}

double readVoltage() {
  int raw  = analogRead(PIN_VOLTAGE_SENSE);
  int raw2 = analogRead(PIN_VOLTAGE_SENSE);
  int raw3 = analogRead(PIN_VOLTAGE_SENSE);
  int median = max(min(raw, raw2), min(max(raw, raw2), raw3));

  return (median / 4095.0) * 3.3 * VOLTAGE_DIVIDER_RATIO;
}

double readTemperature() {
  int raw = analogRead(PIN_TEMP_SENSE);
  double voltage = (raw / 4095.0) * 3.3;

  if (voltage <= 0.01 || voltage >= 3.29) return currentTemp;

  double resistance = SERIES_RESISTOR * (3.3 / voltage - 1.0);
  if (resistance <= 0) return currentTemp;

  double steinhart;
  steinhart = resistance / TEMP_NOMINAL_RES;
  steinhart = log(steinhart);
  steinhart /= TEMP_BETA;
  steinhart += 1.0 / (TEMP_NOMINAL + 273.15);
  steinhart = 1.0 / steinhart;
  steinhart -= 273.15;

  return steinhart;
}

// ==========================================
// НАСТРОЙКА (SETUP)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // 1. Индикация старта
  setLed(80, 0, 160);
  Serial.println("Boot: purple LED ON");

  // 2. Пины и прерывание RPM
  pinMode(PIN_RPM_SENSE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_SENSE), rpmISR, FALLING);

  // 3. WiFi (NVS + STA + AP-fallback)
  wifiInit();

  // 4. Регистрируем /wifi и /api/wifi ДО dashboard.begin()
  wifiRegisterWebHandlers(server);

  // 5. Веб-дашборд
  dashboard.begin(&server, DASHBOARD_HTML_DATA, DASHBOARD_HTML_SIZE);

  StatCard* rpmCard = dashboard.addStatCard("rpm", "Engine RPM", "0", "RPM");
  rpmCard->setVariant(CardVariant::PRIMARY);

  StatCard* tempCard = dashboard.addStatCard("temp", "Engine Temp", "0.0", "°C");
  tempCard->setVariant(CardVariant::DANGER);

  StatCard* voltCard = dashboard.addStatCard("voltage", "Battery Voltage", "0.0", "V");
  voltCard->setVariant(CardVariant::SUCCESS);

  dashboard.addStatCard("hours", "Engine Hours", "0.0", "h");

  server.begin();

  if (wifiIsStaConnected()) {
    Serial.printf("Dashboard ready at http://%s\n",   WiFi.localIP().toString().c_str());
    Serial.printf("WiFi config at  http://%s/wifi\n",  WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("AP mode. Dashboard ready at http://%s\n",  WiFi.softAPIP().toString().c_str());
    Serial.printf("AP mode. WiFi config at  http://%s/wifi\n", WiFi.softAPIP().toString().c_str());
  }

  // 6. NMEA 2000 (не критично)
  NMEA2000.SetProductInformation("123456", 100, "Hangkai 30 Gateway", "1.0.0", "1.0.0");
  NMEA2000.SetDeviceInformation(1, 130, 20, 2046);
  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, 22);

  const unsigned long TransmitMessages[] PROGMEM = {
    127488L,
    127489L,
    0
  };
  NMEA2000.ExtendTransmitMessages(TransmitMessages);

  canInitialized = NMEA2000.Open();
  if (canInitialized) {
    Serial.println("NMEA2000 Gateway started (CAN OK).");
  } else {
    Serial.println("WARNING: NMEA2000 CAN init FAILED. Working in dashboard-only mode.");
  }
}

// ==========================================
// ОСНОВНОЙ ЦИКЛ (LOOP)
// ==========================================
void loop() {
  // Проверка отложенной перезагрузки после смены WiFi-creds
  wifiCheckPendingReboot();

  // Дашборд
  dashboard.loop();

  // Мигание фиолетовым LED (heartbeat) — работает всегда
  if (millis() - lastLedToggle > 500) {
    lastLedToggle = millis();
    ledState = !ledState;
    if (ledState) ledPurple();
    else          ledOff();
  }

  // 1. Датчики
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 100) {
    lastSensorRead = millis();

    currentRPM     = readRPM();
    currentVoltage = readVoltage();
    currentTemp    = readTemperature();

    if (currentRPM > 300) {
      if (!engineRunning) {
        engineRunning = true;
        lastEngineStartTime = millis();
      }
      engineHours += (millis() - lastEngineStartTime) / 3600000.0;
      lastEngineStartTime = millis();
    } else {
      engineRunning = false;
    }
  }

  // 2. Отправка PGN (только если CAN работает)
  if (canInitialized && (millis() - lastNMEAUpdate > 1000)) {
    lastNMEAUpdate = millis();

    tN2kMsg N2kMsgRapid;
    SetN2kEngineParamRapid(N2kMsgRapid, 0, currentRPM, 0);
    NMEA2000.SendMsg(N2kMsgRapid);

    tN2kMsg N2kMsgDynamic;
    SetN2kPGN127489(N2kMsgDynamic,
                    0,
                    N2kDoubleNA,
                    N2kDoubleNA,
                    currentTemp + 273.15,
                    currentVoltage,
                    N2kDoubleNA,
                    N2kDoubleNA,
                    N2kDoubleNA,
                    N2kDoubleNA,
                    N2kInt8NA,
                    N2kInt8NA,
                    (tN2kEngineDiscreteStatus1)0,
                    (tN2kEngineDiscreteStatus2)0
    );
    NMEA2000.SendMsg(N2kMsgDynamic);
  }

  // 3. Обновление веб-дашборда
  if (millis() - lastWebUpdate > 500) {
    lastWebUpdate = millis();

    dashboard.updateStatCard("rpm",     String(currentRPM, 0));
    dashboard.updateStatCard("temp",    String(currentTemp, 1));
    dashboard.updateStatCard("voltage", String(currentVoltage, 2));
    dashboard.updateStatCard("hours",   String(engineHours, 1));
  }

  // 4. Приём из шины (только если CAN работает)
  if (canInitialized) {
    NMEA2000.ParseMessages();
  }

  delay(10);
}