#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <EEPROM.h>
#include "config.h"
#include "wifi_setup.h"

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint8_t WIFI_LED_PIN = 0;
constexpr uint32_t LED_BLINK_MS = 500;
bool ledOn = false;
uint32_t lastLedToggle = 0;
int lastWifiStatus = -1;
WebServer server(80);
DNSServer dns;
Preferences preferences;
bool storageReady = false;
bool portalActive = false;
bool routesRegistered = false;
bool connecting = false;
bool connectionQueued = false;
bool wasConnected = false;
struct Network { char ssid[33]; char password[65]; };
struct Settings { uint32_t magic; Network network; };
constexpr uint32_t SETTINGS_MAGIC = 0x54505731;
// Keep the earlier allocation size so upgrading does not truncate its record.
constexpr size_t EEPROM_BYTES = 200;
Settings settings = {};
uint32_t attemptStarted = 0;
uint32_t connectedAt = 0;
String pendingSsid;
String pendingPassword;
String message;

const char *statusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "idle";
        case WL_NO_SSID_AVAIL: return "SSID unavailable";
        case WL_SCAN_COMPLETED: return "scan complete";
        case WL_CONNECTED: return "connected";
        case WL_CONNECT_FAILED: return "connection failed";
        case WL_CONNECTION_LOST: return "connection lost";
        case WL_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

void serviceWifiIndicator(wl_status_t status) {
    if (static_cast<int>(status) != lastWifiStatus) {
        Serial.printf("[WiFi] Status: %s (%d)\n", statusName(status), static_cast<int>(status));
        lastWifiStatus = status;
    }
    if (status == WL_CONNECTED) {
        ledOn = true;
        lastLedToggle = millis();
    } else if (millis() - lastLedToggle >= LED_BLINK_MS) {
        ledOn = !ledOn;
        lastLedToggle = millis();
    }
    digitalWrite(WIFI_LED_PIN, ledOn ? HIGH : LOW);
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TechPulse Wi-Fi setup</title>
<style>body{font:16px system-ui;max-width:420px;margin:40px auto;padding:20px;background:#f3f6fa;color:#17243b}form{display:grid;gap:12px}input,button{font:inherit;padding:12px;border:1px solid #99a;border-radius:6px}button{background:#1466cc;color:white}label{margin-top:8px}</style>
</head><body><h1>TechPulse Wi-Fi setup</h1>
<p>Enter one 2.4 GHz Wi-Fi network. It is saved to EEPROM and loaded automatically at startup.</p>
<form method="post" action="/connect">
<label for="ssid">Wi-Fi name (SSID)</label>
<input id="ssid" name="ssid" required maxlength="32" autocomplete="off">
<label for="password">Wi-Fi password</label>
<input id="password" name="password" type="password" maxlength="64" autocomplete="off">
<small>Leave the password empty for an open network.</small>
<button>Save and connect</button></form><p id="status" role="status"></p>
<script>async function update(){try{const r=await fetch('/status');document.getElementById('status').textContent=await r.text()}catch(e){document.getElementById('status').textContent='Setup hotspot disconnected. Rejoin your regular Wi-Fi if connection succeeded.'}}setInterval(update,1500);update();</script>
</body></html>)HTML";

void startAttempt() {
    pendingSsid = settings.network.ssid;
    pendingPassword = settings.network.password;
    WiFi.disconnect(false, false);
    WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
    attemptStarted = millis();
    connecting = true;
    connectedAt = 0;
    message = "Connecting to saved network (up to 20 seconds)...";
    Serial.printf("[WiFi] Trying SSID: %s (20 second timeout)\n", pendingSsid.c_str());
}

bool saveSettings(const Settings &next) {
    if (!storageReady) return false;
    EEPROM.put(0, next);
    return EEPROM.commit();
}

bool validNetwork(const Network &network) {
    return memchr(network.ssid, '\0', sizeof(network.ssid)) &&
           memchr(network.password, '\0', sizeof(network.password));
}

void startSequence() {
    wasConnected = false;
    connectedAt = 0;
    if (settings.network.ssid[0]) startAttempt();
    else message = "No saved network. Enter Wi-Fi credentials below.";
}

void startPortal() {
    WiFi.mode(WIFI_AP_STA);
    String name = "TechPulse-Setup-" + WiFi.macAddress().substring(12);
    name.replace(":", "");
    if (!WiFi.softAP(name.c_str())) {
        Serial.println("[WiFi] Could not start setup hotspot; retrying next loop");
        return;
    }
    dns.start(53, "*", WiFi.softAPIP());
    if (!routesRegistered) {
        server.on("/", HTTP_GET, []() {
            server.sendHeader("Cache-Control", "no-store");
            server.send_P(200, "text/html", PAGE);
        });
        server.on("/status", HTTP_GET, []() {
            server.sendHeader("Cache-Control", "no-store");
            server.send(200, "text/plain", message);
        });
        server.on("/connect", HTTP_POST, []() {
            if (connecting || connectionQueued || wasConnected) {
                server.send(409, "text/plain", "A connection is already in progress. Return to the setup page for status.");
                return;
            }
            String ssid = server.arg("ssid");
            String password = server.arg("password");
            // Plaintext credential logging explicitly requested for serial debugging.
            Serial.printf("[WiFi] Received SSID: %s\n[WiFi] Received password: %s\n",
                          ssid.c_str(), password.c_str());
            if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64 ||
                strlen(ssid.c_str()) != ssid.length() || strlen(password.c_str()) != password.length()) {
                Serial.println("[WiFi] Invalid credentials; setup remains available");
                server.send(400, "text/plain", "Use an SSID of 1-32 bytes and a password of at most 64 bytes, without null characters.");
                return;
            }
            Settings next = {};
            next.magic = SETTINGS_MAGIC;
            ssid.toCharArray(next.network.ssid, sizeof(next.network.ssid));
            password.toCharArray(next.network.password, sizeof(next.network.password));
            if (!saveSettings(next)) {
                Serial.println("[WiFi] EEPROM save failed; setup remains available");
                server.send(500, "text/plain", "Could not save credentials to EEPROM. Please retry.");
                return;
            }
            settings = next;
            Serial.println("[WiFi] Credentials saved to EEPROM; connection attempts queued");
            connectionQueued = true;
            message = "Credentials saved to EEPROM. Connecting...";
            server.sendHeader("Location", "/");
            server.send(303, "text/plain", "Connecting. Return to setup for status.");
        });
        server.onNotFound([]() {
            server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
            server.send(302, "text/plain", "Open Wi-Fi setup");
        });
        routesRegistered = true;
    }
    server.begin();
    portalActive = true;
    Serial.printf("[WiFi] Join %s, then open http://%s\n",
                  name.c_str(), WiFi.softAPIP().toString().c_str());
}
} // namespace

void beginWifiSetup() {
    digitalWrite(WIFI_LED_PIN, LOW);
    pinMode(WIFI_LED_PIN, OUTPUT);
    ledOn = false;
    lastLedToggle = millis();
    WiFi.persistent(false);
    // Application owns retry order, including after a connection loss.
    WiFi.setAutoReconnect(false);
    storageReady = EEPROM.begin(EEPROM_BYTES);
    if (!storageReady)
        Serial.println("[WiFi] Credential storage unavailable; changes will not survive reboot");
    if (storageReady) EEPROM.get(0, settings);
    // The earlier two-network record starts with its primary network at the same offset.
    if ((settings.magic == SETTINGS_MAGIC || settings.magic == 0x54505732) &&
        validNetwork(settings.network) && settings.network.ssid[0]) {
        if (settings.magic != SETTINGS_MAGIC) {
            settings.magic = SETTINGS_MAGIC;
            if (!saveSettings(settings)) Serial.println("[WiFi] EEPROM migration save failed");
        }
        Serial.printf("[WiFi] Loaded SSID from EEPROM: %s\n", settings.network.ssid);
        startSequence();
        return;
    }
    settings = {};
    settings.magic = SETTINGS_MAGIC;
    pendingSsid = WIFI_SSID;
    pendingPassword = WIFI_PASSWORD;
    // Migrate the network saved by the previous single-network firmware.
    bool legacyReady = preferences.begin("techpulse-wifi", true);
    String saved = legacyReady ? preferences.getString("credentials", "") : String();
    if (legacyReady) preferences.end();
    if (saved.length() > 1) {
        unsigned int ssidLength = static_cast<uint8_t>(saved[0]);
        if (ssidLength >= 1 && ssidLength <= 32 && saved.length() >= ssidLength + 1 &&
            saved.length() - ssidLength - 1 <= 64) {
            pendingSsid = saved.substring(1, ssidLength + 1);
            pendingPassword = saved.substring(ssidLength + 1);
        }
    }
    if (pendingSsid.length() <= 32 && pendingPassword.length() <= 64) {
        pendingSsid.toCharArray(settings.network.ssid, sizeof(settings.network.ssid));
        pendingPassword.toCharArray(settings.network.password, sizeof(settings.network.password));
    }
    if (!saveSettings(settings)) Serial.println("[WiFi] Initial settings could not be saved");
    startSequence();
}

void serviceWifiSetup() {
    serviceWifiIndicator(WiFi.status());
    if (portalActive) {
        dns.processNextRequest();
        server.handleClient();
    }
    if (connectionQueued) {
        connectionQueued = false;
        startSequence();
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (connecting) {
            connecting = false;
            wasConnected = true;
            message = "Connected! Setup hotspot closes in 8 seconds.";
            pendingPassword = "";
            connectedAt = millis();
            Serial.printf("[WiFi] Connected to SSID: %s; IP: %s\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        }
        if (portalActive && wasConnected && millis() - connectedAt >= 8000) {
            server.stop();
            dns.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            portalActive = false;
            Serial.println("[WiFi] Setup hotspot stopped; station connected");
        }
        return;
    }
    if (wasConnected) {
        Serial.println("[WiFi] Connection lost; retrying saved network");
        startSequence();
    }
    if (connecting && millis() - attemptStarted >= CONNECT_TIMEOUT_MS) {
        Serial.printf("[WiFi] Connection timed out: %s\n", statusName(WiFi.status()));
        connecting = false;
        WiFi.disconnect(false, false);
        pendingPassword = "";
        message = "Connection failed. Check the Wi-Fi name and password, then save and try again.";
        Serial.println("[WiFi] Saved network failed; setup hotspot available for another submission");
    }
    if (!connecting && !portalActive)
        startPortal();
}
