# BLE Mesh Tracking (BMT)

Indoor zone-level tracking on ESP32. A BLE tag is picked up by scan nodes, forwarded over BLE Mesh to a gateway, and pushed to ThingsBoard over MQTT. The output is which room a tag is in, not coordinates.

<!-- EVIDENCE: overall system photo or floorplan -->

## Data flow

```
Tag (iPhone iBeacon / ESP32)
  |  BLE ADV
  v
Scan node 0x01 / 0x02 / 0x03  ----BLE Mesh----> Relay (optional forwarder)
  |                                                   |
  '------------------ BLE Mesh -----------------------'
                        |
                        v
                     Gateway (ESP32-S3 + WiFi)
                        |  MQTTS (ThingsBoard Gateway API)
                        v
                     ThingsBoard (Docker)
```

Only the gateway has WiFi. Scan nodes reach the gateway via mesh; a far node can hop through the relay.

## Hardware

| Node    | Board                       | Note                              |
|---------|-----------------------------|-----------------------------------|
| Gateway | ESP32-S3 DevKitC N16R8      | NimBLE + WiFi, +20 dBm TX         |
| Scanner | ESP32 WROOM-32              | Bluedroid, 3 units, one per zone  |
| Relay   | ESP32 WROOM-32              | Bluedroid, optional               |
| Tag     | ESP32 WROOM-32 or iPhone    | iBeacon or custom UUID (`AB0000...`) |

## Repo layout

```
nodes/
  gateway/            provisioner + MQTT bridge + OTA orchestrator
  scan_node_01..03/   one project per scanner (ID differs)
  relay_01/           BLE Mesh forwarder + WiFi OTA
  tag_01/             ESP32 tag beacon (optional)
  components/
    bmt_scan_core/    shared component used by all scan nodes
docs/
  mesh.md             mesh roles, models, provisioning
  ota.md              OTA flow and configuration
  thingsboard-mqtt.md MQTT topics, TLS, TB usage
  algorithms.md       CRC16, Kalman, path loss, zone hysteresis
docker-compose.yml    ThingsBoard CE + PostgreSQL
tls/                  server cert + CA for MQTTS
```

## Quick start

Requirements: ESP-IDF v6.0, Docker, one ESP32-S3 board + several ESP32 boards.

```bash
# 1. Start ThingsBoard
docker compose up -d

# 2. Configure secrets in nodes/gateway/main/bmt_config.h
#    (WiFi SSID/pass, TB IP, TB gateway token)

# 3. Build and flash gateway
cd nodes/gateway
idf.py set-target esp32s3
idf.py -p /dev/ttyUSB0 flash monitor

# 4. Build and flash each scan node
cd ../scan_node_01
idf.py set-target esp32
idf.py -p /dev/ttyUSB0 flash monitor
```

See [docs/mesh.md](docs/mesh.md) for provisioning steps after flashing.

## Documentation

- [docs/mesh.md](docs/mesh.md) — node roles, BLE Mesh models, provisioning modes
- [docs/ota.md](docs/ota.md) — WiFi OTA flow, partition layout, HTTP server setup
- [docs/thingsboard-mqtt.md](docs/thingsboard-mqtt.md) — MQTT topics, TLS setup, TB device profiles and rule chains
- [docs/algorithms.md](docs/algorithms.md) — CRC16, Kalman filter, path loss, zone hysteresis, time-division radio

## Serial commands (summary)

Gateway UART: `1` list nodes, `2` list tracked tags, `3` MQTT queue stats, `s` scan, `p` provision, `u` OTA scanner/relay, `g` OTA gateway, `0` wipe NVS.
Scanner / relay UART: `1` status, `r` reset mesh, `o` test OTA.

## Stack

ESP-IDF v6.0, BLE Mesh vendor model (CID 0x02E5), NimBLE on gateway, Bluedroid on scan and relay nodes, MQTTS with TLS 1.2, ThingsBoard CE 3.7.0.

## Contact

Cao Trong Phuoc — https://github.com/caotrongphuoc
