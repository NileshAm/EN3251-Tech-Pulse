
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <ESP32MQTTClient.h>
#include <esp_idf_version.h>

#include "config.h"
#include "mqtt_ca.h"
#include "wifi_setup.h"

// ======================================
// HARDWARE
// ======================================

HardwareSerial meter(1);
ESP32MQTTClient mqtt;

#define RELAY_PIN 7
#define RELAY_ON HIGH
#define RELAY_OFF LOW
constexpr uint8_t RELAY_LED_PIN = 8; // SuperMini onboard LED, active-low.
constexpr uint8_t RELAY_BUTTON_PIN = 21; // Momentary button to GND.
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t WIFI_RELAY_TIMEOUT_MS = 10000;

bool relayState = false;
const float MAX_CURRENT_A = 1.0f;
const float MAX_VOLTAGE_V = 235.0f;
const uint32_t PROTECTION_SAMPLE_MAX_AGE = 3000;
bool protectionSampleSafe = false;
uint32_t lastProtectionSample = 0;

// ======================================
// DEVICE AND MQTT TOPICS
// ======================================

String macId;
String deviceId;
String baseTopic;
String telemetryTopic;
String availabilityTopic;
String relayCommandTopic;
String relayStateTopic;

String discoveryPrefix = "homeassistant";

const uint32_t SAMPLE_INTERVAL = 2000;

uint32_t lastSample = 0;
uint32_t sequenceNumber = 0;
uint32_t failedReads = 0;

String bootId;

// Flags shared with MQTT callback
volatile bool discoveryRequested = false;
volatile bool relayCommandPending = false;
volatile bool requestedRelayState = false;

// ======================================
// CALIBRATION
// ======================================

const float voltageFactor = VOLTS_PER_COUNT;
const float currentFactor = AMPS_PER_COUNT;
const float powerFactor = WATTS_PER_COUNT;

const float currentOffset = RAW_CURRENT_OFFSET;
const float powerOffset = RAW_POWER_OFFSET;

const bool calibrated =
    voltageFactor > 0 &&
    currentFactor > 0 &&
    powerFactor > 0;

// ======================================
// HLW8112 REGISTER READER
// PRESERVED FROM WORKING FIRMWARE
// ======================================

bool readReg(uint8_t addr,
             uint8_t width,
             uint32_t &value) {

    if (width < 1 || width > 4)
        return false;

    while (meter.available())
        meter.read();

    uint8_t request[] = {0xA5, addr};
    uint8_t reply[5] = {0};

    meter.write(request, 2);
    meter.flush();

    size_t received =
        meter.readBytes(reply, width + 1);

    if (received != size_t(width + 1)) {
        Serial.printf(
            "[HLW] UART timeout: 0x%02X\n",
            addr
        );
        return false;
    }

    uint8_t sum = 0xA5 + addr;
    value = 0;

    for (uint8_t n = 0; n < width; n++) {
        sum += reply[n];
        value = (value << 8) | reply[n];
    }

    if (uint8_t(~sum) != reply[width]) {
        Serial.printf(
            "[HLW] Checksum error: 0x%02X\n",
            addr
        );
        return false;
    }

    return true;
}

// ======================================
// HOME ASSISTANT SENSOR DISCOVERY
// ======================================

bool publishSensor(
    const char* key,
    const char* name,
    const char* unit,
    const char* deviceClass,
    bool diagnostic = false
) {

    String uniqueId = deviceId + "_" + key;

    String configTopic =
        discoveryPrefix + "/sensor/" +
        uniqueId + "/config";

    String json = "{";

    json += "\"name\":\"" + String(name) + "\",";
    json += "\"unique_id\":\"" + uniqueId + "\",";
    json += "\"state_topic\":\"" +
            telemetryTopic + "\",";

    json += "\"value_template\":\"{{ value_json." +
            String(key) + " }}\",";

    json += "\"availability_topic\":\"" +
            availabilityTopic + "\",";

    json += "\"expire_after\":10,";

    if (diagnostic) {
        json += "\"entity_category\":\"diagnostic\",";
    } else {
        json += "\"state_class\":\"measurement\",";

        if (strlen(deviceClass)) {
            json += "\"device_class\":\"" +
                    String(deviceClass) + "\",";
        }

        if (strlen(unit)) {
            json += "\"unit_of_measurement\":\"" +
                    String(unit) + "\",";
        }
    }

    json += "\"device\":{";
    json += "\"identifiers\":[\"" + deviceId + "\"],";
    json += "\"name\":\"TechPulse Energy Monitor\",";
    json += "\"manufacturer\":\"TechPulse\",";
    json += "\"model\":\"ESP32-C3 HLW8112\"";
    json += "}}";

    bool ok = mqtt.publish(
        configTopic.c_str(),
        json.c_str(),
        0,
        true
    );

    Serial.printf(
        "[HA] Sensor %s: %s\n",
        key,
        ok ? "OK" : "FAILED"
    );

    return ok;
}

// ======================================
// RELAY HOME ASSISTANT DISCOVERY
// ======================================

bool publishRelayDiscovery() {

    String uniqueId = deviceId + "_relay";

    String configTopic =
        discoveryPrefix + "/switch/" +
        uniqueId + "/config";

    String json = "{";

    json += "\"name\":\"Relay\",";
    json += "\"unique_id\":\"" + uniqueId + "\",";
    json += "\"state_topic\":\"" +
            relayStateTopic + "\",";
    json += "\"command_topic\":\"" +
            relayCommandTopic + "\",";
    json += "\"availability_topic\":\"" +
            availabilityTopic + "\",";
    json += "\"payload_on\":\"ON\",";
    json += "\"payload_off\":\"OFF\",";
    json += "\"state_on\":\"ON\",";
    json += "\"state_off\":\"OFF\",";

    json += "\"device\":{";
    json += "\"identifiers\":[\"" + deviceId + "\"],";
    json += "\"name\":\"TechPulse Energy Monitor\",";
    json += "\"manufacturer\":\"TechPulse\",";
    json += "\"model\":\"ESP32-C3 HLW8112\"";
    json += "}}";

    return mqtt.publish(
        configTopic.c_str(),
        json.c_str(),
        0,
        true
    );
}

// ======================================
// HOME ASSISTANT DISCOVERY
// ======================================

bool publishProtectionDiscovery(const char* key, const char* name) {
    String uniqueId = deviceId + "_" + key;
    String configTopic = discoveryPrefix + "/binary_sensor/" + uniqueId + "/config";
    String json = "{";
    json += "\"name\":\"" + String(name) + "\",";
    json += "\"unique_id\":\"" + uniqueId + "\",";
    json += "\"device_class\":\"problem\",";
    json += "\"state_topic\":\"" + baseTopic + "/protection\",";
    json += "\"value_template\":\"{{ value_json." + String(key) + " }}\",";
    json += "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",";
    json += "\"availability_topic\":\"" + availabilityTopic + "\",";
    json += "\"expire_after\":10,";
    json += "\"device\":{\"identifiers\":[\"" + deviceId + "\"]}}";
    return mqtt.publish(configTopic.c_str(), json.c_str(), 0, true);
}

void publishDiscovery() {

    if (!mqtt.isConnected())
        return;

    bool ok = true;

    // Physical measurements
    if (calibrated) {
        ok = publishSensor(
            "voltage_v", "Voltage",
            "V", "voltage"
        ) && ok;

        ok = publishSensor(
            "current_a", "Current",
            "A", "current"
        ) && ok;

        ok = publishSensor(
            "power_w", "Active Power",
            "W", "power"
        ) && ok;

        ok = publishSensor(
            "power_factor", "Power Factor",
            "", "power_factor"
        ) && ok;
    }

    ok = publishSensor(
        "frequency_hz", "Frequency",
        "Hz", "frequency"
    ) && ok;

    // Raw data for debugging
    ok = publishSensor(
        "raw_voltage", "Raw Voltage",
        "", "", true
    ) && ok;

    ok = publishSensor(
        "raw_current", "Raw Current",
        "", "", true
    ) && ok;

    ok = publishSensor(
        "raw_power", "Raw Power",
        "", "", true
    ) && ok;

    ok = publishSensor(
        "raw_frequency", "Raw Frequency",
        "", "", true
    ) && ok;

    ok = publishRelayDiscovery() && ok;
    ok = publishProtectionDiscovery("overcurrent", "Overcurrent") && ok;
    ok = publishProtectionDiscovery("overvoltage", "Overvoltage") && ok;

    Serial.println(
        ok ? "[HA] Discovery complete" :
             "[HA] Discovery incomplete"
    );

    if (!ok)
        discoveryRequested = true;
}

// ======================================
// RELAY CONTROL
// ======================================

void publishRelayState() {

    if (!mqtt.isConnected())
        return;

    mqtt.publish(
        relayStateTopic.c_str(),
        relayState ? "ON" : "OFF",
        0,
        true
    );
}

void setRelay(bool on) {

    if (on && (!protectionSampleSafe ||
               millis() - lastProtectionSample > PROTECTION_SAMPLE_MAX_AGE)) {
        Serial.println("[PROTECTION] ON blocked: no recent safe meter reading");
        on = false;
    }

    relayState = on;

    digitalWrite(
        RELAY_PIN,
        relayState ? RELAY_ON : RELAY_OFF
    );

    digitalWrite(RELAY_LED_PIN, relayState ? LOW : HIGH);

    Serial.printf(
        "[RELAY] %s\n",
        relayState ? "ON" : "OFF"
    );

    publishRelayState();
}

// A disconnect trips once per outage. A deliberate button press after the trip
// permits offline operation; reconnecting rearms the timer without turning ON.
bool wifiOutageActive = false;
bool wifiOutageHandled = false;
uint32_t wifiDisconnectedAt = 0;

void serviceRelayWifiSafety() {
    const uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        wifiOutageActive = false;
        wifiOutageHandled = false;
        return;
    }
    if (!wifiOutageActive) {
        wifiOutageActive = true;
        wifiDisconnectedAt = now;
    }
    if (!wifiOutageHandled && now - wifiDisconnectedAt >= WIFI_RELAY_TIMEOUT_MS) {
        wifiOutageHandled = true;
        // Discard any command queued before the timeout.
        relayCommandPending = false;
        if (relayState) {
            Serial.println("[RELAY] Wi-Fi disconnected for 10 seconds; switching OFF");
            setRelay(false);
        }
    }
}

void serviceRelayButton() {
    static bool lastReading = HIGH;
    static bool stableReading = HIGH;
    static uint32_t changedAt = 0;
    const bool reading = digitalRead(RELAY_BUTTON_PIN);
    const uint32_t now = millis();
    if (reading != lastReading) {
        lastReading = reading;
        changedAt = now;
    }
    if (reading != stableReading && now - changedAt >= BUTTON_DEBOUNCE_MS) {
        stableReading = reading;
        if (stableReading == LOW) {
            Serial.println("[RELAY] Button pressed; toggling relay");
            setRelay(!relayState);
        }
    }
}
void publishProtectionFlags(const char* overcurrent, const char* overvoltage) {
    if (!mqtt.isConnected())
        return;

    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"overcurrent\":\"%s\",\"overvoltage\":\"%s\"}",
             overcurrent, overvoltage);
    // Non-retained: HA expires missing readings rather than restoring stale flags.
    mqtt.publish((baseTopic + "/protection").c_str(), payload, 0, false);
}

void rejectProtectionSample() {
    protectionSampleSafe = false;
    if (relayState) {
        Serial.println("[PROTECTION] Meter reading unavailable; switching OFF");
        setRelay(false);
    }
    publishProtectionFlags("None", "None");
}

void checkProtection(float voltage, float current) {
    protectionSampleSafe =
        isfinite(voltageFactor) && voltageFactor > 0 &&
        isfinite(currentFactor) && currentFactor > 0 &&
        isfinite(voltage) && isfinite(current) &&
        voltage >= 0 && voltage <= MAX_VOLTAGE_V &&
        current <= MAX_CURRENT_A;
    lastProtectionSample = millis();

    if (!protectionSampleSafe) {
        Serial.printf("[PROTECTION] Unsafe reading: %.3f V, %.6f A\n",
                      voltage, current);
        if (relayState)
            setRelay(false);
    }
    // A safe reading never automatically restarts a tripped load.
    publishProtectionFlags(
        !isfinite(currentFactor) || currentFactor <= 0 || !isfinite(current)
            ? "None" : (current > MAX_CURRENT_A ? "ON" : "OFF"),
        !isfinite(voltageFactor) || voltageFactor <= 0 ||
        !isfinite(voltage) || voltage < 0
            ? "None" : (voltage > MAX_VOLTAGE_V ? "ON" : "OFF")
    );
}

// ======================================
// MQTT CALLBACKS
// ======================================

void onMqttConnect(
    esp_mqtt_client_handle_t client
) {

    if (!mqtt.isMyTurn(client))
        return;

    Serial.println("[MQTT] CONNECTED!");

    mqtt.subscribe(
        relayCommandTopic.c_str(),
        [](const std::string &payload) {

            if (payload == "ON") {
                requestedRelayState = true;
                relayCommandPending = true;
            } else if (payload == "OFF") {
                requestedRelayState = false;
                relayCommandPending = true;
            }
        }
    );

    mqtt.subscribe(
        "homeassistant/status",
        [](const std::string &payload) {

            if (payload == "online") {
                discoveryRequested = true;
            }
        }
    );

    discoveryRequested = true;
}

// Required MQTT event forwarding
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)

esp_err_t handleMQTT(
    esp_mqtt_event_handle_t event
) {
    mqtt.onEventCallback(event);
    return ESP_OK;
}

#else

void handleMQTT(
    void* args,
    esp_event_base_t base,
    int32_t id,
    void* data
) {
    (void)base;
    (void)id;

    auto* client =
        static_cast<ESP32MQTTClient*>(args);

    client->onEventCallback(
        static_cast<esp_mqtt_event_handle_t>(data)
    );
}

#endif

// ======================================
// READ AND PUBLISH HLW8112
// ======================================

void publishMeasurements() {

    uint32_t u, i, p, f;

    if (!readReg(0x26, 3, u) ||
        !readReg(0x24, 3, i) ||
        !readReg(0x2C, 4, p) ||
        !readReg(0x23, 2, f)) {

        failedReads++;
        Serial.println("[HLW] Read failed");
        rejectProtectionSample();
        return;
    }

    if ((u & 0x800000) ||
        (i & 0x800000)) {

        failedReads++;
        Serial.println("[HLW] Invalid RMS");
        rejectProtectionSample();
        return;
    }

    int32_t rawPower = (int32_t)p;

    float frequencyHz = f ?
        3579545.0f / (8.0f * f) : 0;

    // Apply software calibration
    float voltage = u * voltageFactor;

    float current =
        ((float)i - currentOffset) *
        currentFactor;

    // Local protection runs before telemetry and does not depend on MQTT.
    checkProtection(voltage, current);

    float power =
        ((float)rawPower - powerOffset) *
        powerFactor;

    float pf = 0;

    if (calibrated && voltage > 0 &&
        current > 0) {

        pf = constrain(
            power / (voltage * current),
            -1.0f, 1.0f
        );
    }

    if (!isfinite(frequencyHz) ||
        (calibrated &&
         (!isfinite(voltage) ||
          !isfinite(current) ||
          !isfinite(power) ||
          !isfinite(pf)))) {
        Serial.println("[HLW] Non-finite sample");
        return;
    }

    sequenceNumber++;

    char payload[768];

    int length;

    if (calibrated) {
        length = snprintf(
            payload, sizeof(payload),

            "{"
            "\"device_id\":\"%s\","
            "\"seq\":%lu,"
            "\"raw_voltage\":%lu,"
            "\"raw_current\":%lu,"
            "\"raw_power\":%ld,"
            "\"raw_frequency\":%lu,"
            "\"voltage_v\":%.3f,"
            "\"current_a\":%.6f,"
            "\"power_w\":%.3f,"
            "\"power_factor\":%.4f,"
            "\"frequency_hz\":%.3f,"
            "\"calibrated\":true"
            "}",

            deviceId.c_str(),
            (unsigned long)sequenceNumber,
            (unsigned long)u,
            (unsigned long)i,
            (long)rawPower,
            (unsigned long)f,
            voltage, current, power,
            pf, frequencyHz
        );
    } else {
        length = snprintf(
            payload, sizeof(payload),

            "{"
            "\"device_id\":\"%s\","
            "\"seq\":%lu,"
            "\"raw_voltage\":%lu,"
            "\"raw_current\":%lu,"
            "\"raw_power\":%ld,"
            "\"raw_frequency\":%lu,"
            "\"frequency_hz\":%.3f,"
            "\"calibrated\":false"
            "}",

            deviceId.c_str(),
            (unsigned long)sequenceNumber,
            (unsigned long)u,
            (unsigned long)i,
            (long)rawPower,
            (unsigned long)f,
            frequencyHz
        );
    }

    if (length <= 0 ||
        length >= (int)sizeof(payload))
        return;

    Serial.printf(
        "[HLW] Raw U=%lu I=%lu P=%ld F=%lu\n",
        (unsigned long)u,
        (unsigned long)i,
        (long)rawPower,
        (unsigned long)f
    );

    if (calibrated) {
        Serial.printf(
            "[HLW] %.2f V | %.4f A | %.2f W | "
            "PF %.3f | %.2f Hz\n",
            voltage, current, power,
            pf, frequencyHz
        );
    }

    if (!mqtt.isConnected())
        return;

    bool sent = mqtt.publish(
        telemetryTopic.c_str(),
        payload,
        0,
        false
    );

    Serial.println(
        sent ? "[MQTT] Telemetry queued" :
               "[MQTT] Publish FAILED"
    );
}

// ======================================
// SETUP
// ======================================

void setup() {

    // Set safe outputs before any startup delays or network initialization.
    digitalWrite(RELAY_PIN, RELAY_OFF);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_LED_PIN, HIGH);
    pinMode(RELAY_LED_PIN, OUTPUT);
    pinMode(RELAY_BUTTON_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(1000);

    // Hardware identity
    WiFi.mode(WIFI_STA);

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();

    macId = mac;
    deviceId = "esp32c3_" + mac;

    baseTopic = "esp32/" + mac;

    telemetryTopic = baseTopic + "/telemetry";
    availabilityTopic = baseTopic + "/availability";

    relayCommandTopic = baseTopic + "/relay/set";
    relayStateTopic = baseTopic + "/relay/state";

    bootId = String(esp_random(), HEX);

    // HLW8112 UART
    meter.begin(
        HLW_BAUD,
        SERIAL_8E1,
        HLW_RX_PIN,
        HLW_TX_PIN
    );

    meter.setTimeout(100);

    Serial.println("[HLW] UART initialized");

    // Connect Wi-Fi
    beginWifiSetup();

    // Synchronize clock for TLS validation
    configTime(
        0, 0,
        "pool.ntp.org",
        "time.google.com"
    );

    // Secure MQTT configuration
    mqtt.setCaCert(MQTT_ROOT_CA);

    mqtt.setURI(
        MQTT_URI,
        MQTT_USER,
        MQTT_PASSWORD
    );

    mqtt.setMqttClientName(
        deviceId.c_str()
    );

    mqtt.setKeepAlive(30);
    mqtt.setMaxPacketSize(2048);

    mqtt.enableLastWillMessage(
        availabilityTopic.c_str(),
        "offline",
        true,
        0
    );

    // Start MQTT in loop after Wi-Fi and TLS time are ready.
}

// ======================================
// LOOP
// ======================================

void loop() {

    static uint32_t lastDiscoveryAttempt = 0;
    static bool mqttStarted = false;
    static uint32_t lastMqttStartAttempt = 0;

    serviceWifiSetup();
    serviceRelayWifiSafety();
    if (!mqttStarted && WiFi.status() == WL_CONNECTED &&
        time(nullptr) >= 1700000000 && millis() - lastMqttStartAttempt >= 5000) {
        lastMqttStartAttempt = millis();
        mqttStarted = mqtt.loopStart();
        if (mqttStarted)
            Serial.println("[MQTT] Client started");
    }

    // Read the meter even while MQTT is offline, before accepting ON commands.
    if (millis() - lastSample >= SAMPLE_INTERVAL) {
        lastSample = millis();
        publishMeasurements();
    }

    // Process relay commands outside MQTT callback
    if (relayCommandPending) {
        bool requested = requestedRelayState;
        relayCommandPending = false;
        if (WiFi.status() == WL_CONNECTED)
            setRelay(requested);
    }

    // Local control remains available during Wi-Fi setup and outages.
    serviceRelayButton();

    // Handle reconnection and HA discovery
    if (mqtt.isConnected() && discoveryRequested &&
        millis() - lastDiscoveryAttempt >= 5000) {

        lastDiscoveryAttempt = millis();
        discoveryRequested = false;

        mqtt.publish(
            availabilityTopic.c_str(),
            "online",
            0,
            true
        );

        publishRelayState();
        publishDiscovery();
    }

    delay(10);
}
