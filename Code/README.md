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

## Optional legacy tools

`server/` (Python/SQLite/custom dashboard), `compose.yaml` (laptop broker), and `deploy/` remain for reference and are not required by Home Assistant. Their previous usage is preserved in [docs/README_LEGACY.md](docs/README_LEGACY.md); relative paths and commands there refer to the project root. Its collector energy estimate is not part of the new workflow. The original standalone hardware test remains unchanged in `docs/original-hardware-test.cpp` and is not compiled.
