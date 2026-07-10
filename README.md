<div align="center">

![Repo Traffic](https://komarev.com/ghpvc/?username=ble-mesh-tracking&label=Repo+Traffic&color=blue&style=flat-square)

</div>

# BMT - BLE Mesh Tracking

<hr>

## Documentation

| File | Description |
|---|---|
| [README.md](README.md) | Overview, hardware, firmware layout, and quick run steps. |
| [docs/architecture.md](docs/architecture.md) | System layout and how data moves between nodes. |
| [docs/setup.md](docs/setup.md) | Full setup from ESP-IDF install to the dashboard. |
| [docs/setup-thingsboard.md](docs/setup-thingsboard.md) | ThingsBoard install, device profiles, rule chain, dashboard. |
| [docs/operation.md](docs/operation.md) | How each part works at runtime, and what each source file does. |
| [docs/uart.md](docs/uart.md) | UART commands for each node. |
| [docs/thingsboard-mqtt.md](docs/thingsboard-mqtt.md) | MQTT topics, payload format, and RPC. |
| [docs/changelog.md](docs/changelog.md) | Change log. |

## Introduction

BMT tracks tags to rooms indoors.

Tags send BLE beacons. Scanners read the signal strength. A relay passes mesh packets along. The gateway sends raw data to ThingsBoard over MQTTS. ThingsBoard picks the room using a rule chain.

### I. Hardware

| Node | Board | What it does |
|---|---|---|
| Tag | ESP32 | Sends a BLE beacon every 500 ms. |
| Scanner x3 | ESP32 | Reads tag signal strength and sends it over mesh. |
| Relay | ESP32 | Passes mesh packets between far scanners and the gateway. |
| Gateway | ESP32-S3 | Sets up the mesh, forwards data to ThingsBoard, runs OTA. |

**Gateway MCU:**

```text
SoC   : ESP32-S3
Flash : 16 MB
Radio : WiFi + BLE at the same time

Flash Layout
------------
[ 0x000000 - 0x008000 ] Bootloader
[ 0x008000 - 0x00F000 ] Partition table
[ 0x00F000 - 0x010000 ] NVS (mesh keys, node table)
[ 0x010000 - 0x210000 ] App slot 0 (2 MB)
[ 0x210000 - 0x410000 ] App slot 1 (2 MB, OTA)
[ 0x410000 - 0x420000 ] OTA data
```

### II. Firmware Layout

Four apps under `apps/`. Each app is a normal ESP-IDF project. Each module lives in its own component folder under `components/bmt_*/`.

| App | Components | What it does |
|---|---|---|
| **gateway** | 13 (config, types, mesh, node_table, mac_cache, scan_list, mqtt, thingsboard, ota, wifi, watchdog, uart, zone) | Sets up mesh nodes, sends tag data to ThingsBoard, runs OTA, resets the mesh if it goes silent. |
| **scanner** | 9 (config, types, auth, scan_core, tag_table, mesh, scan, ota, uart) | Reads BLE beacons, checks HMAC, tracks up to 20 tags, sends `TAG_STATUS` over mesh. |
| **relay** | 5 (config, types, mesh, ota, uart) | Passes mesh packets at the Network Layer. Also handles `RESET_CMD` and `OTA_TRIGGER`. |
| **tag** | 4 (config, auth, beacon, uart) | Builds and sends the 24-byte BLE beacon with sequence and HMAC-16. |

Some files have the same name across apps (`bmt_types.h`, `bmt_auth.*`, `bmt_uart.*`) but the content is different for each role. Not merged into a shared component yet.

### III. How to Run

1. Start ThingsBoard.

   ```
   cd thingsboard && docker compose up -d
   ```

2. Edit WiFi, ThingsBoard IP, gateway token, and OTA URLs in `apps/*/main/bmt_config.h`.

3. Build and flash each app.

   ```
   cd apps/gateway && idf.py -p COM11 erase-flash flash
   cd apps/scanner && idf.py -p COM19 flash
   cd apps/relay   && idf.py -p COM10 flash
   cd apps/tag     && idf.py -p COM22 flash
   ```

   Built `.bin` files land in `firmware/`. To copy to a different folder: `idf.py -DBMT_OTA_DIR=/some/dir build`.

4. Serve the OTA files.

   ```
   cd firmware && python -m http.server 8080
   ```

5. Start OTA. On the gateway UART, press `u` for scanners and relays, or `g` for the gateway. Also works via ThingsBoard RPC: `ota_scanner`, `ota_relay`, `ota_gateway`.

Full steps are in [docs/setup.md](docs/setup.md).

### IV. Data Flow

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

## Contact & Support

<p style="font-size: 20px;"><strong>Cao Trong Phuoc</strong> - Software Engineer - Embedded Systems</p>

``` Note
Thank you for visiting this repository.
If you have any questions, suggestions, or feedback about this project or firmware development, feel free to contact me directly.
```

<a href="https://github.com/caotrongphuoc">
  <img src="https://img.shields.io/badge/GitHub-caotrongphuoc-181717?style=for-the-badge&logo=github&logoColor=white"/>
</a>

<a href="https://www.linkedin.com/in/cao-trong-phuoc/">
  <img src="https://img.shields.io/badge/LinkedIn-Cao%20Trong%20Phuoc-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white"/>
</a>
