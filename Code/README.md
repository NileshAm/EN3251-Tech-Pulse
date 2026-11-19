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

On first boot with this firmware revision, `WIFI_SSID` and `WIFI_PASSWORD` from
`include/config.h` are saved to EEPROM, replacing the older-format record.
Later boots use the saved EEPROM credential; setup-page changes survive power loss.
Editing config.h after initialization does not overwrite an existing saved credential.

At startup, and after an established connection drops, the ESP tries the saved
network for **10 seconds in station-only mode**. If it fails, it switches to
**AP-only mode**, starts `TechPulse-Setup-XXXX`, and serves the setup page.
It stays in setup mode without background station retries so the page stays available.

1. Join `TechPulse-Setup-XXXX` and stay connected despite the no-internet warning.
2. Open **http://192.168.4.1** explicitly (HTTP, not HTTPS).
3. Enter the **2.4 GHz** SSID and password, then select **Save and connect**.

The credential is committed to EEPROM before connecting. After 1.5 seconds to
allow the HTTP response to arrive, the setup hotspot closes and the ESP tries
station-only mode for 10 seconds. On failure the setup hotspot returns; rejoin it
to submit credentials again. Resetting also retries the saved network.
AP and station modes never run simultaneously. The setup web server is available
only in fallback AP mode, not after joining the regular network.

Meter sampling, relay protection and the local button continue during setup.
MQTT starts after station Wi-Fi and TLS time are ready. GPIO 0 blinks while
not connected to the regular Wi-Fi and stays lit once connected.

The setup hotspot is unsecured. Credentials are stored locally and submitted
credentials are printed in plaintext to serial as in the existing debug workflow.
Serial logs at 115200 baud show the attempted SSID, connection status and IP.
Verify isolation before connecting USB to an energized PCB.

## Relay controls

- Relay output: GPIO 7, active-high. Every power-up/reset starts OFF.
- Button: momentary switch between GPIO 21 and GND, using the internal pull-up.
  Each debounced press toggles the relay; holding it does not repeat.
- SuperMini onboard LED: GPIO 8, active-low. Lit means relay ON; dark means OFF.
  The external GPIO 0 LED continues to show Wi-Fi status.
- Home Assistant MQTT ON/OFF commands also control the relay while connected.
- After 10 continuous seconds without Wi-Fi, the relay switches OFF once per
  outage. A new button press allows local operation while still offline.
  Reconnecting rearms the timeout and leaves the relay in its current state;
  it never automatically turns the relay ON.
- Existing meter protections apply to both button and MQTT ON requests: a recent
  safe reading is required. Unsafe or unavailable readings switch the relay OFF.
- Saving Wi-Fi credentials shows a confirmation, then switches to station-only
  mode after 1.5 seconds without restarting. Existing relay protections still apply.
## Optional legacy tools

`server/` (Python/SQLite/custom dashboard), `compose.yaml` (laptop broker), and `deploy/` remain for reference and are not required by Home Assistant. Their previous usage is preserved in [docs/README_LEGACY.md](docs/README_LEGACY.md); relative paths and commands there refer to the project root. Its collector energy estimate is not part of the new workflow. The original standalone hardware test remains unchanged in `docs/original-hardware-test.cpp` and is not compiled.
