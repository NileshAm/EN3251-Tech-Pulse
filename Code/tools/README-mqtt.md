# Live MQTT readings

From the project directory:

```powershell
python -m pip install "paho-mqtt>=2,<3"
python tools/mqtt_websocket_reader.py
```

The reader loads `MQTT_URI`, `MQTT_USER`, and `MQTT_PASSWORD` from
`include/config.h`. It uses secure WebSockets with certificate verification and
prints telemetry, protection flags, availability, and relay state. It does not
send relay commands. Retained messages are labelled; they may be old.

To select one device:

```powershell
python tools/mqtt_websocket_reader.py --device aabbccddeeff
```

Press Ctrl+C to stop. Live telemetry normally arrives every two seconds when
the meter and MQTT connection are working. Missing telemetry alone does not
distinguish a meter fault from a network fault.

Protection limits are configured in `include/config.h` using
`PROTECTION_MAX_VOLTAGE_V` (250 V) and `PROTECTION_MAX_CURRENT_A` (2 A).
These provisional limits must fit the verified ratings of the complete circuit
and load. Rebuild and upload firmware to apply changes. Missing, invalid, or
stale meter readings still block relay ON.
