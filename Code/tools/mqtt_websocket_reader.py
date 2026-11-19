"""Read live telemetry/protection via WSS using the local firmware config.

Install: python -m pip install "paho-mqtt>=2,<3"
Run: python tools/mqtt_websocket_reader.py
"""

import argparse
import json
import re
import ssl
from datetime import datetime
from pathlib import Path
from urllib.parse import urlsplit


def read_config(path):
    text = path.read_text(encoding="utf-8")
    values = {}
    for name in ("MQTT_URI", "MQTT_USER", "MQTT_PASSWORD"):
        match = re.search(r'^\s*#define\s+' + name + r'\s+"([^"\r\n]*)"', text, re.M)
        if not match:
            raise ValueError(f"Missing string definition: {name} in {path}")
        values[name] = match.group(1)
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).resolve().parents[1] / "include" / "config.h")
    parser.add_argument("--device", default="+", help="Device MAC without colons, or + for all devices")
    args = parser.parse_args()
    if not re.fullmatch(r"\+|[0-9a-fA-F]{12}", args.device):
        parser.error("--device must be a 12-digit MAC without colons, or +")
    try:
        config = read_config(args.config)
        uri = urlsplit(config["MQTT_URI"])
        if uri.scheme != "wss" or not uri.hostname:
            raise ValueError("MQTT_URI must be a wss:// URL")
        port = uri.port or 443
    except (OSError, ValueError) as error:
        parser.error(str(error))

    import paho.mqtt.client as mqtt

    topics = [(f"esp32/{args.device.lower()}/{suffix}", 0) for suffix in
              ("telemetry", "protection", "availability", "relay/state")]

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"MQTT connection rejected: {reason_code}", flush=True)
            return
        result, _ = client.subscribe(topics)
        print(f"Connected. Subscribe request: {mqtt.error_string(result)}", flush=True)

    def on_subscribe(client, userdata, mid, reason_codes, properties):
        print("Subscription results: " + ", ".join(str(code) for code in reason_codes), flush=True)

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        print(f"Disconnected: {reason_code}", flush=True)

    def on_message(client, userdata, message):
        payload = message.payload.decode("utf-8", errors="replace")
        try:
            payload = json.dumps(json.loads(payload), ensure_ascii=False)
        except ValueError:
            pass
        retained = " [retained]" if message.retain else ""
        print(f"{datetime.now().astimezone().isoformat(timespec='seconds')} {message.topic}{retained}: {payload}", flush=True)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
    client.username_pw_set(config["MQTT_USER"], config["MQTT_PASSWORD"])
    client.ws_set_options(path=(uri.path or "/") + ("?" + uri.query if uri.query else ""))
    client.tls_set_context(ssl.create_default_context())
    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    print(f"Connecting to {uri.hostname}:{port}; Ctrl+C to stop.", flush=True)
    try:
        client.connect_async(uri.hostname, port, keepalive=30)
        client.loop_forever(retry_first_connection=True)
    except KeyboardInterrupt:
        print("Stopped.")
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
