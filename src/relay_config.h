#pragma once

#include <Arduino.h>

// ==========================================
// 4-канальный релейный модуль
// ==========================================
// Пины выбраны так, чтобы НЕ конфликтовать с:
//   - CAN (5/4), ADC (1/2/3), RGB LED (48),
//   - USB (19/20), UART0 (43/44),
//   - flash/PSRAM (26-32).
// Проверьте на своей плате, если используется
// неофициальный клон ESP32-S3-DevKitC-1.
// ==========================================
#define RELAY_COUNT   4
#define RELAY_PIN_1   10
#define RELAY_PIN_2   11
#define RELAY_PIN_3   12
#define RELAY_PIN_4   13

// Большинство модулей реле (SRD-05VDC-SL-C, JQC-3FF-S-Z и др.)
// срабатывают при LOW на входе. Если ваш — наоборот, поставьте false.
#define RELAY_ACTIVE_LOW  true

// NVS-раздел для сохранения состояний
#define RELAY_NVS_NAMESPACE "relay_cfg"

// Инициализация GPIO и восстановление состояний из NVS.
// Вызывать в setup() ДО server.begin().
void relayInit();

// Управление
void relaySet(uint8_t idx, bool on);
bool relayGet(uint8_t idx);
void relaySetAll(bool on);
const char* relayName(uint8_t idx);

// HTTP: /relay (страница), /api/relay (GET/POST), /api/relay/all (POST)
class AsyncWebServer;
void relayRegisterWebHandlers(AsyncWebServer& server);