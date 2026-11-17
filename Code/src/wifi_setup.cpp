#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "config.h"
#include "wifi_setup.h"

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
WebServer server(80);
DNSServer dns;
Preferences preferences;
bool storageReady = false;
bool portalActive = false;
bool connecting = false;
bool submitted = false;
bool connectionQueued = false;
uint32_t attemptStarted = 0;
uint32_t connectedAt = 0;
String pendingSsid;
String pendingPassword;
String message;

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TechPulse Wi-Fi setup</title>
<style>body{font:16px system-ui;max-width:420px;margin:40px auto;padding:20px;background:#f3f6fa;color:#17243b}form{display:grid;gap:12px}input,button{font:inherit;padding:12px;border:1px solid #99a;border-radius:6px}button{background:#1466cc;color:white}label{margin-top:8px}</style>
</head><body><h1>TechPulse Wi-Fi setup</h1>
<p>Enter the name and password of an available 2.4 GHz Wi-Fi network.</p>
<form method="post" action="/connect">
<label for="ssid">Wi-Fi name (SSID)</label>
<input id="ssid" name="ssid" required maxlength="32" autocomplete="off">
<label for="password">Wi-Fi password</label>
<input id="password" name="password" type="password" maxlength="64" autocomplete="off">
<small>Leave the password empty for an open network.</small>
<button>Connect</button></form><p id="status" role="status"></p>
<script>async function update(){try{const r=await fetch('/status');document.getElementById('status').textContent=await r.text()}catch(e){document.getElementById('status').textContent='Setup hotspot disconnected. Rejoin your regular Wi-Fi if connection succeeded.'}}setInterval(update,1500);update();</script>
</body></html>)HTML";

void startAttempt() {
    WiFi.disconnect(false, false);
    WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
    attemptStarted = millis();
    connecting = true;
    connectedAt = 0;
    message = "Connecting... Please wait up to 10 seconds.";
    Serial.println("[WiFi] Connecting (10 second timeout)");
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
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store");
        server.send_P(200, "text/html", PAGE);
    });
    server.on("/status", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/plain", message);
    });
    server.on("/connect", HTTP_POST, []() {
        if (connecting || connectionQueued || connectedAt) {
            server.send(409, "text/plain", "A connection is already in progress. Return to the setup page for status.");
            return;
        }
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
            server.send(400, "text/plain", "Enter a Wi-Fi name of 1-32 bytes and a password of at most 64 bytes.");
            return;
        }
        pendingSsid = ssid;
        pendingPassword = password;
        submitted = true;
        connectionQueued = true;
        message = "Connecting... Please wait up to 10 seconds.";
        server.sendHeader("Location", "/");
        server.send(303, "text/plain", "Connecting. Return to setup for status.");
    });
    server.onNotFound([]() {
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
        server.send(302, "text/plain", "Open Wi-Fi setup");
    });
    server.begin();
    portalActive = true;
    Serial.printf("[WiFi] Join %s, then open http://%s\n",
                  name.c_str(), WiFi.softAPIP().toString().c_str());
}
} // namespace

void beginWifiSetup() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    storageReady = preferences.begin("techpulse-wifi", false);
    if (!storageReady)
        Serial.println("[WiFi] Credential storage unavailable; changes will not survive reboot");
    pendingSsid = WIFI_SSID;
    pendingPassword = WIFI_PASSWORD;
    String saved = storageReady ? preferences.getString("credentials", "") : String();
    if (saved.length() > 1) {
        unsigned int ssidLength = static_cast<uint8_t>(saved[0]);
        if (ssidLength >= 1 && ssidLength <= 32 && saved.length() >= ssidLength + 1 &&
            saved.length() - ssidLength - 1 <= 64) {
            pendingSsid = saved.substring(1, ssidLength + 1);
            pendingPassword = saved.substring(ssidLength + 1);
        }
    }
    startAttempt();
}

void serviceWifiSetup() {
    if (portalActive) {
        dns.processNextRequest();
        server.handleClient();
    }
    if (connectionQueued) {
        connectionQueued = false;
        startAttempt();
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (connecting) {
            connecting = false;
            if (submitted) {
                // Store as one record so interrupted writes cannot mix credentials.
                String credentials = String(static_cast<char>(pendingSsid.length())) +
                                     pendingSsid + pendingPassword;
                bool saved = storageReady &&
                    preferences.putString("credentials", credentials) > 0;
                message = saved ? "Connected! Credentials saved. Setup hotspot closes in 8 seconds."
                                : "Connected, but credentials could not be saved. Setup hotspot closes in 8 seconds.";
                submitted = false;
            } else {
                message = "Connected! Setup hotspot closes in 8 seconds.";
            }
            pendingPassword = "";
            connectedAt = millis();
            Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
        }
        if (portalActive && connectedAt && millis() - connectedAt >= 8000) {
            server.stop();
            dns.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            portalActive = false;
        }
        return;
    }
    connectedAt = 0;
    if (connecting && millis() - attemptStarted >= CONNECT_TIMEOUT_MS) {
        connecting = false;
        WiFi.disconnect(false, false);
        pendingPassword = "";
        submitted = false;
        message = "Connection failed. Check the Wi-Fi name and password, then try again.";
    }
    if (!connecting && !portalActive)
        startPortal();
}
