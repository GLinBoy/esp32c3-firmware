#!/usr/bin/env python3
"""Publish a retained OTA notification to the `devices/ota` MQTT topic.

The firmware subscribes to this topic on boot and on every (re)connect, so a
retained message acts both as an immediate notification and as the boot-time
fallback check.

Usage:
    python tools/publish_ota.py <version> <url> <sha256> [options]

Example:
    python tools/publish_ota.py 1.0.0-rc2 \
        https://github.com/<owner>/esp32c3-firmware/releases/download/v1.0.0-rc2/firmware.bin \
        "$(cat firmware.sha256)"
"""

from __future__ import annotations

import argparse
import json
import sys

import paho.mqtt.client as mqtt

OTA_TOPIC = "devices/ota"
DEFAULT_BROKER = "test.mosquitto.org"
DEFAULT_PORT = 1883


def make_client() -> "mqtt.Client":
    # paho-mqtt >= 2.0 requires an explicit callback API version.
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
    except AttributeError:
        return mqtt.Client()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="target firmware version, e.g. 1.0.0-rc2")
    parser.add_argument("url", help="HTTPS URL of the firmware.bin asset")
    parser.add_argument("sha256", help="expected SHA-256 of firmware.bin")
    parser.add_argument("--broker", default=DEFAULT_BROKER, help="MQTT broker host")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="MQTT broker port")
    parser.add_argument("--qos", type=int, default=1, choices=(0, 1, 2))
    parser.add_argument(
        "--no-retain",
        action="store_true",
        help="publish without the retain flag (default: retained)",
    )
    args = parser.parse_args()

    payload = {"version": args.version, "url": args.url, "sha256": args.sha256}
    retain = not args.no_retain

    client = make_client()
    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()
    info = client.publish(OTA_TOPIC, json.dumps(payload), qos=args.qos, retain=retain)
    info.wait_for_publish()
    client.loop_stop()
    client.disconnect()

    print(
        f"Published OTA notification to {args.broker}:{args.port} "
        f"topic '{OTA_TOPIC}' (retain={retain}, qos={args.qos}):"
    )
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
