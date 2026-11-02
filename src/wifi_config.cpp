#include "wifi_config.h"

#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

// ==========================================
// Внутреннее состояние
// ==========================================
static bool          apMode        = false;
static String        currentSsid   = "";
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
// HTML-страница /wifi — стиль совпадает с дашбордом
// ==========================================
static const char WIFI_CONFIG_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Settings — Hangkai 30 Gateway</title>
<style>
:root{
  --bg:#0f172a;
  --bg-card:#1e293b;
  --bg-input:#0b1220;
  --text:#e2e8f0;
  --text-dim:#94a3b8;
  --border:#334155;
  --primary:#8b5cf6;
  --primary-hover:#7c3aed;
  --danger:#ef4444;
  --success:#22c55e;
}
*{box-sizing:border-box}
body{
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
  background:var(--bg);color:var(--text);margin:0;
  padding:20px;padding-bottom:60px;min-height:100vh;
}
.container{max-width:560px;margin:0 auto}
h1{color:var(--primary);font-size:1.5em;margin:0 0 20px}
.back-link{
  display:inline-block;color:var(--primary);text-decoration:none;
  margin-bottom:16px;font-size:0.9em
}
.back-link:hover{text-decoration:underline}
.card{
  background:var(--bg-card);border-radius:12px;padding:20px;
  margin-bottom:16px;box-shadow:0 4px 12px rgba(0,0,0,.25)
}
.card h2{margin:0 0 16px;font-size:1.05em;color:var(--text)}
.status-row{
  display:flex;justify-content:space-between;align-items:center;
  padding:8px 0;border-bottom:1px solid var(--border)
}
.status-row:last-child{border:none}
.status-row .k{color:var(--text-dim);font-size:.92em}
.status-row .v{color:var(--text);font-weight:500}
.mode-badge{
  display:inline-block;padding:2px 10px;border-radius:999px;
  font-size:.8em;font-weight:600
}
.mode-badge.sta{background:rgba(34,197,94,.15);color:var(--success)}
.mode-badge.ap{background:rgba(239,68,68,.15);color:var(--danger)}
label{display:block;margin-bottom:6px;color:var(--text-dim);font-size:.9em}
input{
  width:100%;padding:12px;border-radius:8px;border:1px solid var(--border);
  background:var(--bg-input);color:var(--text);font-size:1em;
  margin-bottom:14px;transition:border-color .15s
}
input:focus{outline:none;border-color:var(--primary)}
button{
  background:var(--primary);color:#fff;border:none;padding:12px 20px;
  border-radius:8px;cursor:pointer;font-size:1em;width:100%;
  font-weight:600;transition:background .15s
}
button:hover{background:var(--primary-hover)}
button.danger{background:var(--danger)}
button.danger:hover{background:#dc2626}
.msg{margin-top:12px;font-size:.92em;min-height:1.2em}
.msg.ok{color:var(--success)}
.msg.err{color:var(--danger)}
code{background:var(--bg-input);padding:2px 6px;border-radius:4px;font-size:.9em;color:var(--primary)}
.hint{color:var(--text-dim);font-size:.9em;margin-top:0;line-height:1.5}
.dash-footer{
  position:fixed;bottom:0;left:0;right:0;
  background:rgba(15,23,42,.95);backdrop-filter:blur(8px);
  border-top:1px solid var(--border);
  padding:8px 16px;display:flex;justify-content:space-between;
  align-items:center;font-size:12px;color:var(--text-dim);
  z-index:9999;font-family:system-ui,sans-serif;flex-wrap:wrap;gap:8px
}
.dash-footer span{color:var(--text)}
.dash-footer .dot{
  display:inline-block;width:8px;height:8px;border-radius:50%;
  background:var(--success);margin-right:6px;animation:pulse 2s infinite
}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
</style>
</head>
<body>
<div class="container">
    <a class="back-link" href="/">← Назад к дашборду</a>
    <h1>⚙ WiFi Settings</h1>

    <div class="card">
        <h2>Состояние</h2>
        <div class="status-row">
            <span class="k">Режим</span>
            <span class="v"><span class="mode-badge %MODE_CLS%">%MODE%</span></span>
        </div>
        <div class="status-row"><span class="k">SSID</span><span class="v">%SSID%</span></div>
        <div class="status-row"><span class="k">IP</span><span class="v">%IP%</span></div>
        <div class="status-row"><span class="k">MAC</span><span class="v">%MAC%</span></div>
        <div class="status-row"><span class="k">Uptime</span><span class="v" id="uptime">—</span></div>
    </div>

    <div class="card">
        <h2>Подключиться к другой сети</h2>
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
        <p class="hint">
            Удаляет сохранённые SSID/пароль из NVS. После перезагрузки
            устройство поднимется в AP-режиме <code>%APSSID%</code> / <code>%APPASS%</code>.
        </p>
        <button class="danger" id="reset">Очистить и перезагрузить</button>
    </div>
</div>

<div class="dash-footer">
    <div><span class="dot"></span>Device: <span>%IP%</span></div>
    <div>MAC: <span>%MAC%</span></div>
</div>

<script>
const msg = document.getElementById('msg');
function show(t,c){ msg.textContent = t; msg.className = 'msg ' + c; }

document.getElementById('f').addEventListener('submit', async e => {
    e.preventDefault();
    show('Сохранение…','');
    try{
        const body = 'ssid=' + encodeURIComponent(document.getElementById('ssid').value)
                   + '&pass=' + encodeURIComponent(document.getElementById('pass').value);
        const r = await fetch('/api/wifi',{
            method:'POST',
            headers:{'Content-Type':'application/x-www-form-urlencoded'},
            body
        });
        if(r.ok){
            show('✓ Сохранено. Перезагрузка…','ok');
            setTimeout(()=>location.href='/',5000);
        } else {
            show('✗ Ошибка: ' + r.status,'err');
        }
    }catch(err){ show('✗ ' + err.message,'err'); }
});

document.getElementById('reset').addEventListener('click', async () => {
    if(!confirm('Удалить сохранённые WiFi-данные и перезагрузить?')) return;
    show('Очистка…','');
    try{
        const r = await fetch('/api/wifi/reset',{method:'POST'});
        if(r.ok){
            show('✓ Очищено. Перезагрузка…','ok');
            setTimeout(()=>location.href='/',5000);
        } else {
            show('✗ Ошибка: ' + r.status,'err');
        }
    }catch(err){ show('✗ ' + err.message,'err'); }
});

// live uptime
function fmt(s){
    const d=Math.floor(s/86400), h=Math.floor((s%86400)/3600),
          m=Math.floor((s%3600)/60), sec=s%60;
    return (d>0 ? d+'d ' : '') +
           (h<10?'0':'')+h + ':' +
           (m<10?'0':'')+m + ':' +
           (sec<10?'0':'')+sec;
}
function upd(){
    fetch('/api/status').then(r=>r.json()).then(d=>{
        document.getElementById('uptime').textContent = fmt(d.uptime);
    }).catch(()=>{});
}
upd(); setInterval(upd, 5000);
</script>
</body>
</html>
)HTML";

// ==========================================
// HTTP-обработчики
// ==========================================
void wifiRegisterWebHandlers(AsyncWebServer& server) {
    // GET /wifi
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
        String html = String(WIFI_CONFIG_HTML);
        bool isAp = wifiIsAPMode();

        html.replace("%MODE%",     isAp ? "AP (настройка)" : "STA (подключено)");
        html.replace("%MODE_CLS%", isAp ? "ap" : "sta");
        html.replace("%SSID%",     wifiCurrentSSID());
        html.replace("%IP%",       wifiLocalIP());
        html.replace("%MAC%",      WiFi.macAddress());
        html.replace("%APSSID%",   WIFI_AP_SSID);
        html.replace("%APPASS%",   WIFI_AP_PASSWORD);

        req->send(200, "text/html; charset=utf-8", html);
    });

    // POST /api/wifi
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

    // POST /api/wifi/reset
    server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        clearCredentials();
        Serial.println("WiFi credentials cleared. Rebooting...");
        req->send(200, "text/plain; charset=utf-8", "ok");
        scheduleReboot(1500);
    });
}