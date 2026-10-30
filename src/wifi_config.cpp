#include "wifi_config.h"

#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

// ==========================================
// Внутреннее состояние
// ==========================================
static bool          apMode       = false;
static String        currentSsid  = "";
static bool          rebootPending = false;
static unsigned long rebootAt      = 0;

// ==========================================
// NVS
// ==========================================
static bool loadCredentials(String& ssid, String& password) {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, true);
    ssid     = prefs.getString(WIFI_NVS_KEY_SSID, "");
    password = prefs.getString(WIFI_NVS_KEY_PASS, "");
    prefs.end();
    return ssid.length() > 0;
}

static bool saveCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(WIFI_NVS_NAMESPACE, false)) return false;
    prefs.putString(WIFI_NVS_KEY_SSID, ssid);
    prefs.putString(WIFI_NVS_KEY_PASS, password);
    prefs.end();
    return true;
}

static void clearCredentials() {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}

// ==========================================
// AP-режим
// ==========================================
static void startAPMode() {
    apMode = true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

    Serial.println();
    Serial.println("===========================================");
    Serial.println(" AP MODE (WiFi configuration)");
    Serial.printf ("  SSID: %s\n", WIFI_AP_SSID);
    Serial.printf ("  PWD:  %s\n", WIFI_AP_PASSWORD);
    Serial.printf ("  URL:  http://%s/wifi\n", WiFi.softAPIP().toString().c_str());
    Serial.println("===========================================");
}

// ==========================================
// Инициализация
// ==========================================
void wifiInit() {
    String ssid, pass;
    bool hasCreds = loadCredentials(ssid, pass);

    if (!hasCreds) {
        // Первый запуск — пишем дефолт из wifi_config.h в NVS
        ssid = WIFI_DEFAULT_SSID;
        pass = WIFI_DEFAULT_PASSWORD;
        if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID") {
            saveCredentials(ssid, pass);
            hasCreds = true;
            Serial.println("First boot: default WiFi credentials saved to NVS.");
        } else {
            Serial.println("No credentials in NVS and no defaults defined.");
        }
    }

    if (hasCreds) {
        Serial.printf("Connecting to WiFi \"%s\"", ssid.c_str());
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("\nWiFi STA TIMEOUT.");
                break;
            }
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            currentSsid = ssid;
            Serial.println();
            Serial.printf("WiFi connected. IP: %s\n",
                          WiFi.localIP().toString().c_str());
            return;
        }
    }

    startAPMode();
}

// ==========================================
// Состояние
// ==========================================
bool   wifiIsStaConnected() { return !apMode && WiFi.status() == WL_CONNECTED; }
bool   wifiIsAPMode()       { return apMode; }
String wifiCurrentSSID()    { return apMode ? String(WIFI_AP_SSID) : currentSsid; }
String wifiLocalIP()        { return apMode ? WiFi.softAPIP().toString()
                                            : WiFi.localIP().toString(); }

// ==========================================
// Отложенная перезагрузка
// ==========================================
static void scheduleReboot(unsigned long delayMs) {
    rebootPending = true;
    rebootAt      = millis() + delayMs;
}

void wifiCheckPendingReboot() {
    if (rebootPending && millis() >= rebootAt) {
        Serial.println("Rebooting...");
        ESP.restart();
    }
}

// ==========================================
// HTML-страница /wifi
// ==========================================
static const char WIFI_CONFIG_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Config — Hangkai 30</title>
<style>
 body{font-family:system-ui,-apple-system,sans-serif;background:#1a1a2e;color:#eee;margin:0 auto;padding:20px;max-width:520px}
 h1{color:#a855f7;font-size:1.4em;margin:0 0 14px}
 h2{color:#a855f7;font-size:1.05em;margin:0 0 14px}
 .card{background:#16213e;border-radius:10px;padding:20px;margin-bottom:15px;box-shadow:0 2px 8px rgba(0,0,0,.3)}
 label{display:block;margin-bottom:6px;color:#aaa;font-size:.9em}
 input{width:100%;padding:10px;border-radius:4px;border:1px solid #333;background:#0f0f23;color:#eee;box-sizing:border-box;margin-bottom:14px;font-size:1em}
 input:focus{outline:none;border-color:#a855f7}
 button{background:#a855f7;color:#fff;border:none;padding:12px 20px;border-radius:4px;cursor:pointer;font-size:1em;width:100%;font-weight:600}
 button:hover{background:#9333ea}
 button.danger{background:#dc2626}
 button.danger:hover{background:#b91c1c}
 .info{background:#16213e;border-radius:10px;padding:12px 16px;font-size:.88em;color:#aaa;margin-bottom:15px}
 .info strong{color:#eee}
 .msg{margin-top:12px;font-size:.92em}
 .ok{color:#22c55e}
 .err{color:#ef4444}
</style>
</head>
<body>
<h1>🔧 WiFi Config</h1>

<div class="info">
  <div>Режим: <strong>%MODE%</strong></div>
  <div>SSID: <strong>%SSID%</strong></div>
  <div>IP: <strong>%IP%</strong></div>
</div>

<div class="card">
  <h2>Подключиться к сети</h2>
  <form id="f">
    <label>SSID</label>
    <input id="ssid" name="ssid" placeholder="MyHomeNetwork" required maxlength="32" autocomplete="off">
    <label>Пароль</label>
    <input id="pass" name="pass" type="password" placeholder="password" maxlength="64" autocomplete="off">
    <button type="submit">Сохранить и перезагрузить</button>
  </form>
  <div id="msg" class="msg"></div>
</div>

<div class="card">
  <h2>Сброс</h2>
  <p style="color:#aaa;font-size:.9em;margin-top:0">
    Удаляет сохранённые SSID/пароль. После перезагрузки устройство поднимется в AP-режиме
    <code>%APSSID%</code> / <code>%APPASS%</code>.
  </p>
  <button class="danger" id="reset">Очистить и перезагрузить</button>
</div>

<script>
const msg = document.getElementById('msg');
function show(text, cls){ msg.textContent = text; msg.className = 'msg ' + cls; }

document.getElementById('f').addEventListener('submit', async e => {
  e.preventDefault();
  show('Сохранение…', '');
  try {
    const body = 'ssid=' + encodeURIComponent(document.getElementById('ssid').value)
               + '&pass=' + encodeURIComponent(document.getElementById('pass').value);
    const r = await fetch('/api/wifi', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body
    });
    if (r.ok) {
      show('✓ Сохранено. Перезагрузка…', 'ok');
      setTimeout(() => location.href = '/', 5000);
    } else {
      show('✗ Ошибка: ' + r.status + ' ' + await r.text(), 'err');
    }
  } catch (err) {
    show('✗ ' + err.message, 'err');
  }
});

document.getElementById('reset').addEventListener('click', async () => {
  if (!confirm('Удалить сохранённые WiFi-данные и перезагрузить?')) return;
  show('Очистка…', '');
  try {
    const r = await fetch('/api/wifi/reset', {method:'POST'});
    if (r.ok) {
      show('✓ Очищено. Перезагрузка…', 'ok');
      setTimeout(() => location.href = '/', 5000);
    } else {
      show('✗ Ошибка: ' + r.status, 'err');
    }
  } catch (err) {
    show('✗ ' + err.message, 'err');
  }
});
</script>
</body>
</html>
)HTML";

// ==========================================
// HTTP-обработчики
// ==========================================
void wifiRegisterWebHandlers(AsyncWebServer& server) {
    // GET /wifi — страница конфигурации
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
        String html = String(WIFI_CONFIG_HTML);
        html.replace("%MODE%",  wifiIsAPMode() ? "AP (настройка)" : "STA (подключено)");
        html.replace("%SSID%",  wifiCurrentSSID());
        html.replace("%IP%",    wifiLocalIP());
        html.replace("%APSSID%", WIFI_AP_SSID);
        html.replace("%APPASS%", WIFI_AP_PASSWORD);
        req->send(200, "text/html; charset=utf-8", html);
    });

    // POST /api/wifi — сохранить новые creds
    server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid", true)) {
            req->send(400, "text/plain; charset=utf-8", "missing ssid");
            return;
        }
        String ssid = req->getParam("ssid", true)->value();
        String pass = req->hasParam("pass", true)
                        ? req->getParam("pass", true)->value()
                        : String("");

        if (ssid.length() == 0 || ssid.length() > 32) {
            req->send(400, "text/plain; charset=utf-8", "invalid ssid");
            return;
        }
        if (!saveCredentials(ssid, pass)) {
            req->send(500, "text/plain; charset=utf-8", "nvs write failed");
            return;
        }

        Serial.printf("WiFi credentials updated. SSID=\"%s\". Rebooting...\n",
                      ssid.c_str());
        req->send(200, "text/plain; charset=utf-8", "ok");
        scheduleReboot(1500);
    });

    // POST /api/wifi/reset — очистить NVS
    server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        clearCredentials();
        Serial.println("WiFi credentials cleared. Rebooting...");
        req->send(200, "text/plain; charset=utf-8", "ok");
        scheduleReboot(1500);
    });
}