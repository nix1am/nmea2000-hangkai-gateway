#pragma once

#include <Arduino.h>

// ==========================================
// Defaults — используются ТОЛЬКО при первом старте,
// когда в NVS ещё нет сохранённых данных.
// После первого запуска эти значения игнорируются —
// правьте сеть через /wifi в браузере.
// ==========================================
#define WIFI_DEFAULT_SSID     "YOUR_WIFI_SSID"
#define WIFI_DEFAULT_PASSWORD "YOUR_WIFI_PASSWORD"

// Таймаут подключения к STA, мс
#define WIFI_CONNECT_TIMEOUT_MS 20000

// Параметры резервной точки доступа
#define WIFI_AP_SSID     "Hangkai30-Gateway"
#define WIFI_AP_PASSWORD "12345678"

// Ключи NVS
#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define WIFI_NVS_KEY_SSID  "ssid"
#define WIFI_NVS_KEY_PASS  "password"

// Инициализация WiFi:
//  - читает creds из NVS (или пишет туда defaults при первом старте)
//  - пытается подключиться к STA
//  - при неудаче поднимает AP-режим
void wifiInit();

// Регистрирует /wifi, /api/wifi, /api/wifi/reset на сервере.
// Вызывать ДО dashboard.begin(), чтобы маршруты точно не перекрылись.
class AsyncWebServer;
void wifiRegisterWebHandlers(AsyncWebServer& server);

// Проверка отложенной перезагрузки (после сохранения creds).
// Вызывать в loop() каждые несколько мс.
void wifiCheckPendingReboot();

// Состояние
bool   wifiIsStaConnected();
bool   wifiIsAPMode();
String wifiCurrentSSID();
String wifiLocalIP();