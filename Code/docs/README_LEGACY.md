# Tech Pulse · IoT power monitor

ESP32-C3 + HLW8112 → UART through galvanic isolator → Wi-Fi/MQTT → Python collector + SQLite → browser analytics.

## Run the dashboard now

Requires Python 3.11+. Demo has no third-party dependencies:

```powershell
python server/app.py --demo
```

Open http://127.0.0.1:8000. The simulator writes a labelled `demo-meter` sample every two seconds. It never pretends to be a physical device. Stop with Ctrl+C. Data persists in `server/telemetry.sqlite3`.

The dashboard offers meter selection, 1h/24h/7d history, active power, RMS voltage/current, frequency, calculated power factor, peak and sample-average power (API), estimated import energy, observation coverage, stale status and CSV export. All UI assets are local; no CDN required.

## Connect MQTT

Use an existing authenticated MQTT broker, or run Mosquitto with Docker Desktop. Create passwords interactively (use the same ESP32 password in firmware):

```powershell
docker run --rm -it -v "${PWD}/deploy:/config" eclipse-mosquitto:2 mosquitto_passwd -c /config/passwords esp32
docker run --rm -it -v "${PWD}/deploy:/config" eclipse-mosquitto:2 mosquitto_passwd /config/passwords collector
docker compose up -d
python -m pip install -r server/requirements.txt
$env:MQTT_HOST="127.0.0.1"
$env:MQTT_USER="collector"
$env:MQTT_PASSWORD="your-collector-password"
python server/app.py
```

The firmware uses the computer's **LAN IP**, not 127.0.0.1. PC and ESP32 must reach the same broker. Allow inbound TCP 1883 on the private network. The supplied ACL permits `power-01`; update it if changing the device ID. Broker reconnection is automatic. Retained telemetry is ignored to avoid treating an old reading as current.

Optional collector variables: `MQTT_PORT`, `MQTT_TLS=1`, `MQTT_CA` (CA file). The included firmware and Docker broker use plaintext MQTT on a trusted LAN. Internet hosting needs firmware TLS using WiFiClientSecure, broker TLS, and an authenticated HTTPS reverse proxy for the dashboard. The supplied HTTP server binds loopback and has no user accounts; this codebase is a local project baseline, not a public multi-user service.

## Configure the PCB

1. Copy `include/config.example.h` to `include/config.h` (ignored by Git).
2. Enter Wi-Fi, broker LAN IP, and MQTT credentials.
3. Confirm the **actual GPIO numbers** wired to the isolator's ESP32-side TX/RX. ESP RX receives isolator TX; ESP TX drives isolator RX. Set `HLW_RX_PIN` and `HLW_TX_PIN`. Do not infer GPIOs solely from the board's TX/RX labels.
4. Confirm the HLW8112 SPIEN is strapped for UART and determine baud from SCLK/SCSN: 9600, 19200 or 38400. Serial framing is 8 data bits, even parity, 1 stop bit. The isolator must support that baud and signal direction.
5. Calibrate `VOLTS_PER_COUNT`, `AMPS_PER_COUNT`, `WATTS_PER_COUNT` using known reference measurements divided by their raw register counts. Current orientation determines the power sign. The driver reads voltage 0x26, current A 0x24, signed power A 0x2C, and frequency 0x23; it rejects UART checksum failures and invalid RMS samples. Use a debugger or temporary serial logging after `readReg` to inspect counts.
6. Set `HARDWARE_CONFIRMED true` after configuration. Build with `pio run`; flash with `pio run -t upload`; inspect with `pio device monitor`.

Hardware is disabled by default. No relay pins are driven. The original blinking/button/relay test is preserved in `docs/original-hardware-test.cpp` outside the firmware source directory.

The existing `esp32-c3-devkitm-1` PlatformIO target is retained and USB CDC enabled for the Super Mini. Confirm the board's flash/USB configuration during first upload. This initial driver uses channel A and the chip's default AC measurement configuration (SYSCON default enables U and A); it does not reset the chip, overwrite calibration, enable channel B or read the hardware energy counters. If your board requires a different PGA or startup register setup, configure and validate it against the schematic before use. Frequency assumes the nominal 3.579545 MHz clock.

The PCB and live measurement accuracy cannot be verified in software. Confirm isolation and signal voltage compatibility before attaching USB or instruments to an energized mains assembly.

## Telemetry contract

Topic: `techpulse/<device_id>/telemetry`, JSON, non-retained. Firmware publishes every 2 seconds at QoS 0. No offline buffering is claimed; connectivity gaps remain visible.

```json
{"device_id":"power-01","boot_id":"a04f13","seq":1,"voltage_v":230.2,"current_a":1.25,"power_w":270.0,"power_factor":0.9383,"frequency_hz":50.01,"simulated":false}
```

`boot_id` changes per boot; sequence numbers deduplicate repeated MQTT messages. The collector validates types, finite numbers, ranges, identity and topic consistency. Timestamps are UTC Unix seconds assigned on receipt; device clock sync is unnecessary. Analytics use receipt time, so delivery delays affect estimates.

Energy uses trapezoidal integration of positive power, excluding gaps >10 seconds, reboots, and real/demo boundaries. It is **observed import energy**, not billing-grade consumption or a total across outages. Negative power is shown but doesn't subtract from import energy. Coverage states how much time was integrated. History queries are limited to seven days and chart responses to 600 averaged points; CSV retains full resolution. SQLite history is retained indefinitely; back up the DB and establish retention before long-running deployments.

## API and checks

- `GET /api/devices`: latest sample for each meter.
- `GET /api/readings?device=power-01&hours=24`: summary and chart points.
- `GET /api/export?device=power-01&hours=24`: CSV.

```powershell
python -m unittest discover -s server/tests -v
node --check server/static/app.js
pio run
```

Optional `--host`, `--port`, `--db` server flags. `--host 0.0.0.0` enables LAN viewing; restrict network access appropriately.

Protocol reference: [Hiliwei HLW8110/HLW8112 datasheet rev 1.01](https://datasheet.lcsc.com/datasheet/pdf/7618fcc29341bc35e74ce1d001211dc8.pdf?productCode=C970140), sections 9 and 12.2. UART pins, baud straps, sensor/divider ratios, calibration and physical PCB testing still require your hardware details.
