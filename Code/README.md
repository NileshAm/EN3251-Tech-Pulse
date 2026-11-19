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

At boot, the ESP loads one Wi-Fi credential from EEPROM and tries connecting
for up to 20 seconds. If it cannot connect, it opens an unsecured hotspot named
`TechPulse-Setup-XXXX` (the suffix identifies the device).
On upgrade, the previous firmware's primary network is retained as the single
network. On first use, the older saved credential or `WIFI_SSID` / `WIFI_PASSWORD`
from `include/config.h` is used.

1. Connect your phone or laptop to that hotspot and stay connected even if it reports no internet.
2. Open **http://192.168.4.1** if the setup page does not open automatically.
3. Enter the **2.4 GHz** network name and password, then press **Save and connect**.

The credential is saved before connecting, even if the network is currently
unavailable. ESP32's EEPROM library uses persistent flash storage, so the settings
survive power loss. Saving replaces the previous credential. An empty password
means an open network.

Each attempt lasts up to 20 seconds. If it fails, the setup page stays available
to retry. On success, the hotspot closes after 8 seconds. If the connection later
drops, the ESP retries the saved network, then opens the setup hotspot on failure.
Meter sampling and relay protection
continue during setup; MQTT starts once Wi-Fi and the TLS clock are ready.

The setup hotspot is open, so configure it near the device on a trusted site.
Credentials are stored locally on the ESP. Submitted SSIDs and passwords are
printed in plaintext to serial at 115200 baud for debugging, along with save,
connection, timeout and hotspot status.

An external active-high LED on GPIO 0 blinks every 500 ms while disconnected
(including setup hotspot mode), then stays on while connected to Wi-Fi.
After a submission is saved, the ESP restarts and tries that network. If it fails, the setup
hotspot stays available for another submission.

For serial logs, select the ESP's USB COM port (not a **Standard Serial over
Bluetooth link**), set **115200 baud**, start monitoring, then press reset on the
ESP to see boot logs. If no USB serial port appears, reconnect with a USB data
cable and refresh the port list. Close other monitors using the same port.

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
- Saving Wi-Fi credentials shows a confirmation and restarts after 1.5 seconds
  to connect using the saved network. The relay starts OFF after this restart.
## Optional legacy tools

`server/` (Python/SQLite/custom dashboard), `compose.yaml` (laptop broker), and `deploy/` remain for reference and are not required by Home Assistant. Their previous usage is preserved in [docs/README_LEGACY.md](docs/README_LEGACY.md); relative paths and commands there refer to the project root. Its collector energy estimate is not part of the new workflow. The original standalone hardware test remains unchanged in `docs/original-hardware-test.cpp` and is not compiled.
