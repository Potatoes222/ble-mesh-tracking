<div align="center">

![Repo Traffic](https://komarev.com/ghpvc/?username=ble-mesh-tracking&label=Repo+Traffic&color=blue&style=flat-square)

</div>

# BLE Mesh Tracking

<hr>

Room-level indoor tracking on ESP32 / ESP32-S3 with BLE Mesh and a self-hosted ThingsBoard CE.

Tags send BLE beacons. Scanners read signal strength. A relay forwards mesh packets. The gateway pushes raw data to ThingsBoard over MQTTS. A ThingsBoard rule chain turns RSSI into a room name.

New to the project? Start with [docs/00-quickstart.md](docs/00-quickstart.md).

## Documentation

| File | Description |
|---|---|
| [docs/00-quickstart.md](docs/00-quickstart.md) | Clone, install ESP-IDF, run Docker, build and flash, verify. Linux and Windows. |
| [docs/01-architecture.md](docs/01-architecture.md) | System layout and how data moves between nodes. |
| [docs/02-ble-mesh.md](docs/02-ble-mesh.md) | BLE Mesh parts used in this project. |
| [docs/03-algorithms.md](docs/03-algorithms.md) | Kalman filter, HMAC, hysteresis, OTA compare, watchdog. |
| [docs/04-thingsboard-setup.md](docs/04-thingsboard-setup.md) | ThingsBoard install, device profiles, rule chain, dashboard. |
| [docs/05-thingsboard-mqtt.md](docs/05-thingsboard-mqtt.md) | MQTT topics, payload format, and RPC. |
| [docs/06-http-tls.md](docs/06-http-tls.md) | HTTP OTA server and TLS cert flow. |
| [docs/07-operation.md](docs/07-operation.md) | Runtime behavior and per-file source description. |
| [docs/08-uart-commands.md](docs/08-uart-commands.md) | UART commands for each node. |
| [docs/09-testing.md](docs/09-testing.md) | 9 manual tests and regression baseline. |
| [docs/10-testing-ota.md](docs/10-testing-ota.md) | End-to-end OTA testing and fault injection. |
| [docs/11-checklist.md](docs/11-checklist.md) | Pre-commit, pre-release, deployment checklists. |
| [docs/99-changelog.md](docs/99-changelog.md) | Change log. |

## Hardware

| Node | Board | What it does |
|---|---|---|
| Tag | ESP32 | Sends a BLE beacon every 500 ms. |
| Scanner x3 | ESP32 | Reads tag RSSI and sends `TAG_STATUS` over mesh. |
| Relay | ESP32 | Forwards mesh packets between far scanners and the gateway. |
| Gateway | ESP32-S3 | Provisions mesh, forwards data to ThingsBoard, runs OTA. |

## Firmware layout

Four apps under `apps/`. Each is a standard ESP-IDF project with modules under `components/bmt_*/`.

| App | Modules | What it does |
|---|---|---|
| **gateway** | 13 (config, types, mesh, node_table, mac_cache, scan_list, mqtt, thingsboard, ota, wifi, watchdog, uart, zone) | Provisions, bridges to ThingsBoard, runs OTA, resets mesh if silent. |
| **scanner** | 9 (config, types, auth, scan_core, tag_table, mesh, scan, ota, uart) | Reads BLE beacons, verifies HMAC, sends `TAG_STATUS`. |
| **relay** | 5 (config, types, mesh, ota, uart) | Forwards at the Network Layer. Handles `RESET_CMD` and `OTA_TRIGGER`. |
| **tag** | 4 (config, auth, beacon, uart) | Sends a 24-byte BLE beacon with sequence and HMAC-16. |

Shared code (relay and scanner OTA) lives at repo root under `components/bmt_ota/`.

## Data flow

```text
[Tag BLE Beacon]
      |  BLE ADV (HMAC-16, key rotates every 24h)
      v
[Scanner ESP32 x3]  --BLE Mesh-->  [Relay ESP32]  --BLE Mesh-->  [Gateway ESP32-S3]
 reads RSSI                         forwards only                  provisioner + WiFi
                                                                        |  MQTTS
                                                                        v
                                                              [ThingsBoard CE]
                                                               Rule chain picks a room
                                                                        |
                                                                        v
                                                              [Indoor Tracking dashboard]
```

## Contact

<p><strong>Cao Trong Phuoc</strong> - Software Engineer - Embedded Systems</p>

<a href="https://github.com/caotrongphuoc">
  <img src="https://img.shields.io/badge/GitHub-caotrongphuoc-181717?style=for-the-badge&logo=github&logoColor=white"/>
</a>

<a href="https://www.linkedin.com/in/cao-trong-phuoc/">
  <img src="https://img.shields.io/badge/LinkedIn-Cao%20Trong%20Phuoc-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white"/>
</a>
