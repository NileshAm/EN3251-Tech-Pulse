import csv
import json
from datetime import datetime
from pathlib import Path

import paho.mqtt.client as mqtt

BROKER = "broker.hivemq.com"
TOPIC = "techpulse/techpulse-minay-a7c92f/calibration"

FILE = Path("hlw8112_calibration_withoutload.csv")

FIELDS = [
    "received_at",
    "test_label",
    "boot_id",
    "seq",
    "timestamp_ms",
    "raw_voltage",
    "raw_current",
    "raw_power",
    "raw_frequency",
    "frequency_hz",
    "failed_reads",
    "calibrated"
]

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        client.subscribe(TOPIC)
        print("Subscribed:", TOPIC)
    else:
        print("MQTT connection failed:", reason_code)

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())

        row = {
            "received_at": datetime.now().astimezone().isoformat(),
            **{key: data.get(key, "") for key in FIELDS[1:]}
        }

        with FILE.open("a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIELDS)
            writer.writerow(row)

        print(
            row["test_label"],
            row["seq"],
            row["raw_voltage"],
            row["raw_current"],
            row["raw_power"]
        )

    except (ValueError, UnicodeDecodeError) as error:
        print("Invalid message:", error)

if not FILE.exists() or FILE.stat().st_size == 0:
    with FILE.open("w", newline="") as f:
        csv.DictWriter(f, fieldnames=FIELDS).writeheader()

client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2
)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, 1883, 60)

print("Logging HLW8112 readings to:", FILE.resolve())

try:
    client.loop_forever()
except KeyboardInterrupt:
    client.disconnect()
    print("Logging stopped")
