#include "relay_config.h"

#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ==========================================
// Внутренние данные
// ==========================================
static const uint8_t RELAY_PINS[RELAY_COUNT] = {
    RELAY_PIN_1, RELAY_PIN_2, RELAY_PIN_3, RELAY_PIN_4
};

static const char* RELAY_NAMES[RELAY_COUNT] = {
    "Relay 1", "Relay 2", "Relay 3", "Relay 4"
};

static bool relayState[RELAY_COUNT] = { false, false, false, false };

// ==========================================
// Запись состояния на физический пин
// ==========================================
static void writePin(uint8_t idx, bool on) {
    if (idx >= RELAY_COUNT) return;
#if RELAY_ACTIVE_LOW
    digitalWrite(RELAY_PINS[idx], on ? LOW : HIGH);
#else
    digitalWrite(RELAY_PINS[idx], on ? HIGH : LOW);
#endif
}

// ==========================================
// NVS
// ==========================================
static void relaySave() {
    Preferences prefs;
    if (!prefs.begin(RELAY_NVS_NAMESPACE, false)) return;
    uint8_t mask = 0;
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        if (relayState[i]) mask |= (1 << i);
    }
    prefs.putUChar("mask", mask);
    prefs.end();
}

static void relayLoad() {
    Preferences prefs;
    if (!prefs.begin(RELAY_NVS_NAMESPACE, true)) return;
    uint8_t mask = prefs.getUChar("mask", 0);
    prefs.end();
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        relayState[i] = (mask >> i) & 0x01;
        writePin(i, relayState[i]);
    }
    Serial.printf("Relay states restored from NVS: 0x%02X\n", mask);
}

// ==========================================
// Публичный API
// ==========================================
void relayInit() {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        relayState[i] = false;
        writePin(i, false);            // на старте всё выключено
    }
    relayLoad();                       // затем восстанавливаем из NVS
    Serial.printf("Relay module initialized on GPIO %d,%d,%d,%d\n",
                  RELAY_PIN_1, RELAY_PIN_2, RELAY_PIN_3, RELAY_PIN_4);
}

void relaySet(uint8_t idx, bool on) {
    if (idx >= RELAY_COUNT) return;
    relayState[idx] = on;
    writePin(idx, on);
    relaySave();
    Serial.printf("Relay %u -> %s\n", idx + 1, on ? "ON" : "OFF");
}

bool relayGet(uint8_t idx) {
    if (idx >= RELAY_COUNT) return false;
    return relayState[idx];
}

void relaySetAll(bool on) {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        relayState[i] = on;
        writePin(i, on);
    }
    relaySave();
    Serial.printf("All relays -> %s\n", on ? "ON" : "OFF");
}

const char* relayName(uint8_t idx) {
    if (idx >= RELAY_COUNT) return "?";
    return RELAY_NAMES[idx];
}

// ==========================================
// HTML-страница /relay
// ==========================================
static const char RELAY_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relay Control — Hangkai 30</title>
<style>
 body{font-family:system-ui,-apple-system,sans-serif;background:#1a1a2e;color:#eee;margin:0 auto;padding:20px;max-width:560px}
 h1{color:#a855f7;font-size:1.4em;margin:0 0 14px}
 .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:15px}
 .relay{background:#16213e;border-radius:10px;padding:16px;box-shadow:0 2px 8px rgba(0,0,0,.3);text-align:center}
 .relay .name{color:#aaa;font-size:.85em;margin-bottom:10px;text-transform:uppercase;letter-spacing:.5px}
 .relay .sw{display:inline-block;width:60px;height:32px;background:#0f0f23;border-radius:16px;position:relative;cursor:pointer;transition:background .2s;border:1px solid #333}
 .relay .sw.on{background:#a855f7}
 .relay .sw::after{content:'';position:absolute;top:3px;left:3px;width:24px;height:24px;background:#666;border-radius:50%;transition:transform .2s,background .2s}
 .relay .sw.on::after{transform:translateX(28px);background:#fff}
 .controls{display:flex;gap:10px;margin-bottom:15px}
 .controls button{flex:1;padding:12px;border:none;border-radius:6px;cursor:pointer;font-size:.95em;font-weight:600}
 .btn-on{background:#22c55e;color:#fff}  .btn-on:hover{background:#16a34a}
 .btn-off{background:#dc2626;color:#fff} .btn-off:hover{background:#b91c1c}
 .status{background:#16213e;border-radius:10px;padding:12px 16px;font-size:.88em;color:#aaa;margin-bottom:15px;display:flex;justify-content:space-between;align-items:center}
 .status strong{color:#eee}
 .nav{color:#a855f7;font-size:.9em;text-decoration:none}
 .nav:hover{text-decoration:underline}
</style>
</head>
<body>
<div class="status">
  <span><strong id="summary">Loading…</strong></span>
  <a href="/" class="nav">← Dashboard</a>
</div>

<div class="grid" id="grid"></div>

<div class="controls">
  <button class="btn-on"  onclick="setAll(1)">All ON</button>
  <button class="btn-off" onclick="setAll(0)">All OFF</button>
</div>

<script>
const COUNT = 4;
let state = [false,false,false,false];
let names = ['Relay 1','Relay 2','Relay 3','Relay 4'];

function render() {
  const grid = document.getElementById('grid');
  grid.innerHTML = '';
  for (let i = 0; i < COUNT; i++) {
    const div = document.createElement('div');
    div.className = 'relay';
    div.innerHTML = '<div class="name">'+names[i]+'</div>'
                  + '<div class="sw '+(state[i]?'on':'')+'" data-idx="'+i+'"></div>';
    grid.appendChild(div);
  }
  document.querySelectorAll('.sw').forEach(el => {
    el.addEventListener('click', () => {
      const idx = parseInt(el.dataset.idx);
      toggle(idx, !state[idx]);
    });
  });
  const onCount = state.filter(x => x).length;
  document.getElementById('summary').textContent = 'Active: ' + onCount + ' / ' + COUNT;
}

async function refresh() {
  try {
    const r = await fetch('/api/relay');
    const arr = await r.json();
    arr.forEach(it => { state[it.idx] = it.state; names[it.idx] = it.name; });
    render();
  } catch(e) { console.error(e); }
}

async function toggle(idx, on) {
  state[idx] = on;
  render();
  try {
    await fetch('/api/relay', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'idx='+idx+'&state='+(on?1:0)
    });
  } catch(e) { console.error(e); refresh(); }
}

async function setAll(on) {
  state = state.map(() => on);
  render();
  try {
    await fetch('/api/relay/all', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'state='+(on?1:0)
    });
  } catch(e) { console.error(e); refresh(); }
}

refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)HTML";

// ==========================================
// HTTP-обработчики
// ==========================================
void relayRegisterWebHandlers(AsyncWebServer& server) {
    // GET /relay — страница управления
    server.on("/relay", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html; charset=utf-8", RELAY_HTML);
    });

    // GET /api/relay — состояние всех реле (JSON)
    server.on("/api/relay", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["idx"]   = i;
            o["name"]  = RELAY_NAMES[i];
            o["state"] = relayState[i];
        }
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json; charset=utf-8", out);
    });

    // POST /api/relay (form: idx=0..3 & state=0|1)
    server.on("/api/relay", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("idx", true) || !req->hasParam("state", true)) {
            req->send(400, "text/plain; charset=utf-8", "idx & state required");
            return;
        }
        int idx   = req->getParam("idx",   true)->value().toInt();
        int state = req->getParam("state", true)->value().toInt();
        if (idx < 0 || idx >= RELAY_COUNT) {
            req->send(400, "text/plain; charset=utf-8", "bad idx");
            return;
        }
        relaySet((uint8_t)idx, state != 0);
        req->send(200, "text/plain; charset=utf-8", "ok");
    });

    // POST /api/relay/all (form: state=0|1)
    server.on("/api/relay/all", HTTP_POST, [](AsyncWebServerRequest* req) {
        int state = 0;
        if (req->hasParam("state", true)) {
            state = req->getParam("state", true)->value().toInt();
        }
        relaySetAll(state != 0);
        req->send(200, "text/plain; charset=utf-8", "ok");
    });
}