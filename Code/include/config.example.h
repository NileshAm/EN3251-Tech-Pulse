#pragma once
#define PROTECTION_MAX_VOLTAGE_V 250.0f
#define PROTECTION_MAX_CURRENT_A 2.0f
// Copy to config.h and configure from your PCB schematic.
#define WIFI_SSID "Minaya's Galaxy S26 Ultra"
#define WIFI_PASSWORD "Minu1723"
#define MQTT_HOST "192.168.1.100" // Raspberry Pi LAN IP; no http:// or :8123
#define MQTT_PORT 1883
#define MQTT_USER "esp32"
#define MQTT_PASSWORD "CHANGE_ME"
#define DEVICE_ID "power-01"
#define HARDWARE_CONFIRMED false
#define HLW_RX_PIN -1
#define HLW_TX_PIN -1
#define HLW_BAUD 9600
// SI units per raw count; calibrate against reference instruments.
#define VOLTS_PER_COUNT 0.0
#define AMPS_PER_COUNT 0.0
#define WATTS_PER_COUNT 0.0
// Explicit opt-in. Simulation uses power-01_sim and separate HA entities.
#ifndef SIMULATED_TELEMETRY
#define SIMULATED_TELEMETRY 0
#endif
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_STATUS_TOPIC "homeassistant/status"
