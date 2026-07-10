# BMT — BLE Mesh Tracking

Room-level indoor tracking using BLE Mesh, ESP32/ESP32-S3, and self-hosted ThingsBoard CE.

Tags send BLE beacons. Scanners read RSSI and forward it over BLE Mesh through a Relay to the Gateway. The Gateway pushes raw data to ThingsBoard over MQTTS. A rule chain in ThingsBoard turns RSSI into rooms.

## Layout

- `apps/gateway/` — provisioner, MQTTS bridge, OTA, watchdog.
- `apps/scanner/` — reads tag beacons, sends RSSI over mesh.
- `apps/relay/` — mesh forwarder.
- `apps/tag/` — beacon sender.
- `thingsboard/` — docker-compose, rule chain, dashboard, TLS certs.
- `firmware/` — built `.bin` files served for OTA.
- `docs/` — documentation.
- `tools/` — helper scripts.

## Quick start

```
# 1. Start ThingsBoard
cd thingsboard && docker compose up -d

# 2. Edit apps/*/main/bmt_config.h (WiFi, TB IP, token)

# 3. Build and flash each app
cd apps/gateway && idf.py -p COM11 erase-flash flash
cd apps/scanner && idf.py -p COM19 flash
cd apps/relay   && idf.py -p COM10 flash
cd apps/tag     && idf.py -p COM22 flash
```

Build all four apps at once: `tools/build-all.sh`. Format all sources: `tools/format.sh`.

## Docs

- [Architecture](docs/architecture.md)
- [Setup](docs/setup.md)
- [ThingsBoard setup](docs/setup-thingsboard.md)
- [How it works](docs/operation.md)
- [UART commands](docs/uart.md)
- [ThingsBoard MQTT API](docs/thingsboard-mqtt.md)
- [Changelog](docs/changelog.md)
