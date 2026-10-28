#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "ESPDashboardPlus.h"
#include "dashboard_html.h" // Файл, созданный на предыдущем шаге

// ==========================================
// НАСТРОЙКИ Wi-Fi
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";         // <-- ЗАМЕНИТЕ
const char* password = "YOUR_WIFI_PASSWORD"; // <-- ЗАМЕНИТЕ

// ==========================================
// НАСТРОЙКИ ПИНОВ ESP32-S3
// ==========================================
// Пины для CAN-трансивера MCP2562
#define ESP32_CAN_TX_PIN GPIO_NUM_5
#define ESP32_CAN_RX_PIN GPIO_NUM_4

// Аналоговые пины для датчиков Hangkai 30
const int PIN_RPM_SENSE = 1;      // GPIO1 (ADC1_CH0) - вход для сигнала с катушки/генератора
const int PIN_TEMP_SENSE = 2;     // GPIO2 (ADC1_CH1) - вход для термистора
const int PIN_VOLTAGE_SENSE = 3;  // GPIO3 (ADC1_CH2) - вход для делителя напряжения

// ==========================================
// КОНСТАНТЫ ДЛЯ РАСЧЕТОВ
// ==========================================
// Коэффициенты делителя напряжения для датчика напряжения
const float VOLTAGE_DIVIDER_RATIO = 5.0; // Пример: если R1=10к, R2=2.2к, то (10+2.2)/2.2 = 5.54

// Настройки термистора (пример для NTC 10к)
const float TEMP_NOMINAL = 25.0;      // Номинальная температура
const float TEMP_NOMINAL_RES = 10000; // Сопротивление при 25°C
const float TEMP_BETA = 3950;         // Бета-коэффициент
const float SERIES_RESISTOR = 10000;  // Сопротивление последовательного резистора

// ==========================================
// ОБЪЕКТЫ
// ==========================================
AsyncWebServer server(80);
ESPDashboardPlus dashboard("Hangkai 30 Gateway");

// NMEA 2000
#include <NMEA2000_esp32xx.h>
#include <NMEA2000_CAN.h>
tNMEA2000 &NMEA2000 = tNMEA2000::NMEA2000_1;

// ==========================================
// ПЕРЕМЕННЫЕ ДЛЯ ДАННЫХ
// ==========================================
double currentRPM = 0.0;
double currentTemp = 0.0;
double currentVoltage = 0.0;
double engineHours = 0.0;

unsigned long lastNMEAUpdate = 0;
unsigned long lastWebUpdate = 0;
unsigned long engineStartTime = 0;
bool engineRunning = false;

// ==========================================
// ФУНКЦИИ СЧИТЫВАНИЯ ДАТЧИКОВ
// ==========================================

// Считывание оборотов: используется прерывание для подсчета импульсов
volatile unsigned long pulseCount = 0;
unsigned long lastRPMTime = 0;
const unsigned long PULSES_PER_REV = 6; // Количество импульсов на оборот (зависит от мотора, подберите экспериментально)

void IRAM_ATTR rpmISR() {
  pulseCount++;
}

double readRPM() {
  unsigned long now = millis();
  if (now - lastRPMTime >= 500) { // Считаем обороты каждые 500 мс
    noInterrupts();
    unsigned long count = pulseCount;
    pulseCount = 0;
    interrupts();
    
    // Расчет RPM: (импульсы за интервал / импульсов на оборот) / время в минутах
    double rpm = (count / (double)PULSES_PER_REV) * (60000.0 / (now - lastRPMTime));
    lastRPMTime = now;
    
    // Фильтрация и отсечение шума на холостом ходу
    if (rpm < 300) rpm = 0; // Если обороты ниже 300, считаем, что двигатель заглох
    return rpm;
  }
  return currentRPM;
}

// Считывание напряжения бортсети
double readVoltage() {
  int raw = analogRead(PIN_VOLTAGE_SENSE);
  // Перевод АЦП в напряжение (ESP32-S3: 12 бит, 0-3.3В)
  double voltage = (raw / 4095.0) * 3.3 * VOLTAGE_DIVIDER_RATIO;
  return voltage;
}

// Считывание температуры с термистора
double readTemperature() {
  int raw = analogRead(PIN_TEMP_SENSE);
  double voltage = (raw / 4095.0) * 3.3;
  
  // Расчет сопротивления термистора
  double resistance = SERIES_RESISTOR * (3.3 / voltage - 1.0);
  
  // Уравнение Стейнхарта-Харта
  double steinhart;
  steinhart = resistance / TEMP_NOMINAL_RES;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= TEMP_BETA;                      // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMP_NOMINAL + 273.15);  // + (1/To)
  steinhart = 1.0 / steinhart;                 // Инвертировать
  steinhart -= 273.15;                         // В градусы Цельсия
  
  return steinhart;
}

// ==========================================
// НАСТРОЙКА (SETUP)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // 1. Настройка пинов
  pinMode(PIN_RPM_SENSE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_SENSE), rpmISR, FALLING);
  
  // 2. Подключение к Wi-Fi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // 3. Инициализация веб-дашборда
  dashboard.begin(&server, DASHBOARD_HTML_DATA, DASHBOARD_HTML_SIZE);
  
  // Добавляем карточки на дашборд
  StatCard* rpmCard = dashboard.addStatCard("rpm", "Engine RPM", "0", "RPM");
  rpmCard->setVariant(CardVariant::PRIMARY);
  
  StatCard* tempCard = dashboard.addStatCard("temp", "Engine Temp", "0.0", "°C");
  tempCard->setVariant(CardVariant::DANGER); // Температура - важный параметр
  
  StatCard* voltCard = dashboard.addStatCard("voltage", "Battery Voltage", "0.0", "V");
  voltCard->setVariant(CardVariant::SUCCESS);
  
  StatCard* hoursCard = dashboard.addStatCard("hours", "Engine Hours", "0.0", "h");

  server.begin();
  Serial.printf("Dashboard ready at http://%s\n", WiFi.localIP().toString().c_str());

  // 4. Инициализация NMEA 2000
  NMEA2000.SetProductInformation("123456", 100, "Hangkai 30 Gateway", "1.0.0", "1.0.0");
  NMEA2000.SetDeviceInformation(1, 130, 20, 2046);
  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, 22);
  NMEA2000.Open();
  Serial.println("NMEA2000 Gateway started.");
}

// ==========================================
// ОСНОВНОЙ ЦИКЛ (LOOP)
// ==========================================
void loop() {
  // Обязательный вызов для обработки событий дашборда
  dashboard.loop();

  // --- 1. Считывание датчиков (каждые 100 мс) ---
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 100) {
    lastSensorRead = millis();
    
    currentRPM = readRPM();
    currentVoltage = readVoltage();
    currentTemp = readTemperature();
    
    // Учет моточасов (если двигатель работает)
    if (currentRPM > 300) {
      if (!engineRunning) {
        engineStartTime = millis();
        engineRunning = true;
      }
      engineHours += (millis() - engineStartTime) / 3600000.0;
      engineStartTime = millis(); // Сбрасываем таймер для следующего шага
    } else {
      engineRunning = false;
    }
  }

  // --- 2. Отправка данных в NMEA 2000 (каждую секунду) ---
  if (millis() - lastNMEAUpdate > 1000) {
    lastNMEAUpdate = millis();

    // PGN 127488: Engine Parameters, Rapid Update (Обороты)
    tN2kMsg N2kMsgRapid;
    SetN2kEngineParamRapid(N2kMsgRapid, 0, currentRPM, 0);
    NMEA2000.SendMsg(N2kMsgRapid);

    // PGN 127489: Engine Parameters, Dynamic (Температура, напряжение и т.д.)
    tN2kMsg N2kMsgDynamic;
    SetN2kEngineDynamicParam(N2kMsgDynamic, 0, 
                             currentTemp + 273.15, // Температура в Кельвинах
                             -1,                   // Давление масла (нет датчика)
                             currentVoltage,        // Напряжение
                             -1,                   // Часы работы (не в этом PGN)
                             -1,                   // Уровень топлива
                             0,                    // Статус
                             0);                   // Флаги
    NMEA2000.SendMsg(N2kMsgDynamic);
  }

  // --- 3. Обновление данных на веб-дашборде (каждые 500 мс) ---
  if (millis() - lastWebUpdate > 500) {
    lastWebUpdate = millis();
    
    dashboard.updateStatCard("rpm", String(currentRPM, 0));
    dashboard.updateStatCard("temp", String(currentTemp, 1));
    dashboard.updateStatCard("voltage", String(currentVoltage, 2));
    dashboard.updateStatCard("hours", String(engineHours, 1));
  }

  // Обработка входящих сообщений NMEA 2000
  NMEA2000.ParseMessages();
  
  delay(10);
}