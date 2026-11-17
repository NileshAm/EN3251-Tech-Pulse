# TechPulse: Raspberry Pi + Home Assistant

**HLW8112 → isolated UART → ESP32-C3 → Wi-Fi → Pi Mosquitto → Home Assistant.** Windows is only for development. No laptop Docker, Python collector, SQLite or custom web dashboard is required.

## Windows: open, build, upload

Install VS Code and its PlatformIO IDE extension. Open the directory containing `platformio.ini`; let PlatformIO install the tools. In the PlatformIO terminal:

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

The existing `esp32-c3-devkitm-1` target, PubSubClient 2.8 and native USB CDC flags are retained for this project's Super Mini. Use a USB data cable; add `--upload-port COMx` or monitor `--port COMx` if necessary. Follow your board's BOOT/RESET procedure for initial upload. No connected device is required to build.

## Pi, broker and MQTT integration

1. Write Home Assistant OS for your Pi using Raspberry Pi Imager, following the [official installation guide](https://www.home-assistant.io/installation/raspberrypi/). Boot on your LAN; open `http://homeassistant.local:8123` and complete onboarding.
2. In **Settings → Apps** (Add-ons in older releases), install the official **Mosquitto broker**. Enable start on boot and watchdog, then start it.
3. Create a dedicated non-admin user, e.g. `techpulse_esp`, in **Settings → People → Users**. Enable Advanced Mode in your profile if Users is hidden. Choose a strong password; avoid reserved names `homeassistant` and `addons`. Follow the [official Mosquitto guide](https://github.com/home-assistant/apps/blob/master/mosquitto/DOCS.md).
4. In **Settings → Devices & services**, configure discovered MQTT, or select **Add integration → MQTT** and set it up with the official app. Keep discovery enabled with prefix `homeassistant` and birth topic `homeassistant/status`.
5. Find the Pi's IPv4 address in **Settings → System → Network** or the router's DHCP list. Reserve the address if possible. ESP32 connects to this address on **1883**, not the laptop, localhost or HA web port 8123. Use reachable 2.4 GHz Wi-Fi without guest/client isolation.

## Private firmware configuration

Copy `include/config.example.h` to `include/config.h` (already Git-ignored).

| Setting | Supply |
| --- | --- |
| `WIFI_SSID`, `WIFI_PASSWORD` | Local 2.4 GHz Wi-Fi credentials |
| `MQTT_HOST`, `MQTT_PORT` | Raspberry Pi LAN IPv4 address, `1883` |
| `MQTT_USER`, `MQTT_PASSWORD` | Dedicated broker credentials |
| `DEVICE_ID` | Keep `power-01` |
| `HLW_RX_PIN`, `HLW_TX_PIN` | Confirmed GPIOs on ESP side of isolator |
| `HLW_BAUD` | Confirm chip baud straps; example 9600 is a placeholder |
| `VOLTS_PER_COUNT`, `AMPS_PER_COUNT`, `WATTS_PER_COUNT` | Positive SI units per raw register count from reference calibration |
| `HARDWARE_CONFIRMED` | Keep `false` until hardware settings are validated |
| `SIMULATED_TELEMETRY` | Disabled (`0`) by default |

ESP RX receives isolator TX; ESP TX drives isolator RX. Framing is 8E1. Supply the schematic, voltage-divider values and current sensor/shunt/gain details before calibration. No UART assignments or chip setup are inferred. Default hardware settings deliberately prevent measurements.

The existing driver reads RMS U `0x26`, RMS IA `0x24`, signed PowerA `0x2C` and frequency `0x23`; it checks UART checksum and RMS validity, scales with the existing calibration constants, and computes signed PF=W/(V×A), clamped to [-1,1]. Frequency retains the nominal 3.579545 MHz clock assumption. No chip reset, new register initialization or hardware energy-counter reads are added. Samples remain two seconds apart.

## Verify reception and discovery

Open Serial Monitor. Logs show Wi-Fi success/IP or retry, MQTT success/failure code, discovery publishing, valid readings, UART timeout/checksum/invalid readings and telemetry publish failures. No passwords are logged. Retry scheduling is timer driven; MQTT/TCP handshakes can briefly block with two-second timeouts, and each UART read has a 100 ms timeout.

In MQTT integration **Configure → Listen to a topic**, listen to `techpulse/power-01/#`. Expect JSON telemetry every two seconds after hardware is confirmed, with `simulated:false`. Status uses retained `online`/`offline` at `techpulse/power-01/status`, including a broker last will. Telemetry is never retained.

Open **Settings → Devices & services → MQTT → Devices → TechPulse Energy Monitor**. Five sensors share a stable device ID: Voltage, Current, Active Power, Power Factor, Frequency. Each is `measurement` with the appropriate class and V/A/W/no unit/Hz. Retained discovery topics are `homeassistant/sensor/techpulse_power-01_<field>/config`. IDs do not change per boot. Discovery republishes on MQTT reconnect and HA's online birth message; subsequent regular samples provide fresh states. Ten-second expiry prevents a connected ESP with a failed meter from leaving measurements indefinitely available.

Create/edit a dashboard and add an Entities card for these five sensors; add Gauge/History graph cards as desired. Select actual entities from the device page rather than assuming generated entity IDs. Sources: [MQTT discovery](https://www.home-assistant.io/integrations/mqtt/), [MQTT sensor options](https://www.home-assistant.io/integrations/sensor.mqtt/), [sensor classes](https://www.home-assistant.io/integrations/sensor/).

## kWh using one Integral helper

Once real positive load power is verified, select **Settings → Devices & services → Helpers → Create helper → Integral**. Input: the actual Active Power sensor in W. Name: `TechPulse Energy`. Method: **Left** for step-like appliance loads; metric prefix: **k**; integration time: **hours**; precision: **4**; max sub-interval: **10 seconds**; device class: **energy**, if offered. Result: kWh.

Verify in Developer Tools → States: `device_class: energy`, `unit_of_measurement: kWh`, state class `total` (or `total_increasing` where applicable). Add this helper to Energy dashboard individual-device consumption; use grid consumption only if measuring the entire grid feed. See [Integral documentation](https://www.home-assistant.io/integrations/integration/) and [Energy FAQ](https://www.home-assistant.io/docs/energy/faq/).

This is sampled energy, not billing-grade or hardware-counter energy. Missing samples/outages cannot be recovered; availability detection takes time and boundary intervals can be inaccurate. HA downtime loses samples too. Verify disconnect/reconnect behavior before relying on totals. Negative power integrates as signed net energy: confirm current orientation for consumption-only use. Do not also run the legacy collector's energy calculation for this path, and never feed simulation into the real Energy dashboard.

## Explicit simulation without mains

Use safe USB power only, with mains disconnected. Configure Wi-Fi/Pi credentials and keep `HARDWARE_CONFIRMED false`:

```powershell
pio run -e esp32-c3-sim
pio run -e esp32-c3-sim -t upload
pio device monitor -b 115200
```

This explicit compile-time mode publishes fixed synthetic readings with `simulated:true` under `techpulse/power-01_sim/telemetry`, separate status/discovery IDs, and **TechPulse Energy Monitor (SIMULATED)**. It initializes neither meter UART nor relay/LED/button GPIO. Return to hardware with `pio run -t upload` (default environment). To remove test discovery, publish empty retained payloads to its five `homeassistant/sensor/techpulse_power-01_sim_<field>/config` topics; otherwise retained configs survive reboot.

## Hardware reference and safety

`docs/original-hardware-test.cpp` remains verbatim, outside compiled sources. Its actual settings are GPIO8 LED blink 500 ms, GPIO0 indicator flash 100 ms, debounced GPIO21 button, **GPIO7 relay every 5000 ms**. This differs from the supplied GPIO3/one-second description; confirm the schematic before changing the reference.

GPIO8 is a boot strapping pin. GPIO21 is native UART0 TX and can conflict with the old button circuit if used for the meter. GPIO18/19 are native USB and must remain available for CDC. GPIO0 and GPIO3 are not boot straps, but PCB connections still need checking. [Espressif C3 pin documentation](https://documentation.espressif.com/esp32-c3_datasheet_en.html). Full UART conflict review awaits your schematic. The firmware has no relay control/enable path and never drives the old test outputs.

**Verify physical isolation of the supply, isolator and all signal paths before connecting a USB-powered computer or instrument to the mains-connected PCB.** A UART isolator alone does not establish whole-board safety. Port 1883 is plaintext for the trusted local demo network; do not expose it to the internet.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Wi-Fi fails | SSID/password, 2.4 GHz, router isolation and Serial status |
| MQTT fails | Pi IP, app running, port 1883 and credentials; -2/-4 often network/timeout, 4/5 authentication/authorization |
| Discovered but unavailable | Hardware gate/calibration and recent readings; status means MQTT connectivity, not meter health |
| UART timeout | GPIOs, crossed TX/RX, isolator direction/power, UART straps, baud and 8E1 |
| Checksum failure | Baud/parity, noise, level compatibility and isolator speed; bad samples are discarded |
| Wrong/negative readings | Reference calibration, orientation and chip gain/setup; inspect raw registers with suitable instruments |
| Telemetry but no discovery | Discovery enabled, correct prefix/birth topic, discovery write permissions, HA logs and config-topic messages |
| Old/test entities persist | Stable IDs; remove obsolete retained configs, not just UI entities |
| No data without a meter | Expected with hardware disabled; simulation is explicit and uses `_sim` |

## Checks and legacy code

Build both modes using `pio run` and `pio run -e esp32-c3-sim`. Physical upload, UART accuracy, HA onboarding, power-loss availability and restart recovery require the actual Pi/ESP setup.

`server/`, `compose.yaml` and `deploy/` are optional legacy tools. They are not used in normal HA operation; no changes to their old ACL or collector settings are necessary. The old custom-dashboard guide is preserved in `docs/README_LEGACY.md`.
