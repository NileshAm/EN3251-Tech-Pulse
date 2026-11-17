# TechPulse Energy Monitor

**HLW8112 → isolated UART → ESP32-C3 Super Mini → Wi-Fi → Raspberry Pi Mosquitto → Home Assistant.**

Start with [README_HOME_ASSISTANT.md](README_HOME_ASSISTANT.md) for Pi setup, credentials, discovery, dashboard, kWh Integral helper and troubleshooting. Docker/Python are not needed on the development laptop.

1. Open this folder in VS Code + PlatformIO.
2. Copy `include/config.example.h` to private `include/config.h`.
3. Supply Pi broker/Wi-Fi credentials and confirmed UART/calibration settings. Keep `HARDWARE_CONFIRMED false` until validated.
4. Build `pio run`, upload `pio run -t upload`, monitor `pio device monitor -b 115200`.

Five sensors are discovered as **TechPulse Energy Monitor**. Hardware telemetry stays at `techpulse/power-01/telemetry`, every two seconds, non-retained. Discovery and status are retained with offline last will and stale-reading expiry.

Simulation is off by default. Explicit `pio run -e esp32-c3-sim -t upload` uses separate `_sim` topics/entities labelled **SIMULATED** and never initializes the meter or switches any load.

Verify physical isolation before USB connection to an energized PCB. GPIOs, baud straps, calibration and real UART/HA operation require physical verification.

## Wi-Fi setup fallback

At boot, the ESP tries the last successfully saved Wi-Fi credentials (or
`WIFI_SSID` / `WIFI_PASSWORD` from `include/config.h` on first use). If it has
not connected within 10 seconds, it opens an unsecured hotspot named
`TechPulse-Setup-XXXX` (the suffix identifies the device).

1. Connect your phone or laptop to that hotspot and stay connected even if it reports no internet.
2. Open **http://192.168.4.1** if the setup page does not open automatically.
3. Enter the available **2.4 GHz** network name and password, then press **Connect**.

Each attempt lasts up to 10 seconds. A failed attempt leaves the setup page
available to retry. Successful credentials are saved in ESP flash for future
boots, and the hotspot closes after 8 seconds. If the saved network later becomes
unavailable, the setup hotspot opens again. Meter sampling and relay protection
continue during setup; MQTT starts once Wi-Fi and the TLS clock are ready.

The setup hotspot is open, so configure it near the device on a trusted site.
Credentials are stored locally on the ESP and are never printed to serial.

## Optional legacy tools

`server/` (Python/SQLite/custom dashboard), `compose.yaml` (laptop broker), and `deploy/` remain for reference and are not required by Home Assistant. Their previous usage is preserved in [docs/README_LEGACY.md](docs/README_LEGACY.md); relative paths and commands there refer to the project root. Its collector energy estimate is not part of the new workflow. The original standalone hardware test remains unchanged in `docs/original-hardware-test.cpp` and is not compiled.
