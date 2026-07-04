<div align="center">

![Repo Traffic](https://komarev.com/ghpvc/?username=ble-mesh-tracking&label=Repo+Traffic&color=blue&style=flat-square)

</div>

# BLE Mesh Tracking - Indoor Zone Tracking on ESP32

<!--
EVIDENCE: banner image (system overview or floor plan photo, PNG or GIF)
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 1:</em></strong> System banner</p>
-->

<hr>

## Documentation

| File | Description |
|---|---|
| [README.md](README.md) | Project overview, hardware, repo layout, quick start. |
| [docs/mesh.md](docs/mesh.md) | Node roles, BLE Mesh vendor model, opcodes, provisioning modes. |
| [docs/ota.md](docs/ota.md) | WiFi OTA flow, partition layout, HTTP server, trigger sources. |
| [docs/thingsboard-mqtt.md](docs/thingsboard-mqtt.md) | MQTT topics, TLS setup, ThingsBoard device profiles and rule chains. |
| [docs/algorithms.md](docs/algorithms.md) | CRC16, Kalman RSSI filter, path loss, zone hysteresis, time-division radio. |

## Introduction

BLE Mesh Tracking is a room-level indoor tracking system built on ESP32 and BLE Mesh. A BLE tag (an iPhone iBeacon or an ESP32 running the tag firmware) is picked up by three scan nodes placed around the home. Each scan node filters RSSI, then forwards a small report over BLE Mesh to a gateway. The gateway connects to WiFi and pushes each report to ThingsBoard over MQTT. The output is which room a tag is in, not coordinates.

Core concepts exercised while building BMT:

- **BLE Mesh vendor model:** custom opcodes for tag reports, node health, OTA trigger, and remote reset.
- **Radio scheduling:** time-division of a single BLE radio between GAP scan and mesh publish.
- **Signal processing:** 1-D Kalman filter, log-distance path loss, zone hysteresis.
- **OTA over WiFi:** mesh-triggered self-update using `esp_https_ota` and a two-slot partition table.
- **MQTT bridge with TLS:** the gateway acts as a ThingsBoard multi-device gateway.

### I. Hardware

| Node    | Board                     | BLE stack | Note                             |
|---------|---------------------------|-----------|----------------------------------|
| Gateway | ESP32-S3 DevKitC N16R8    | NimBLE    | WiFi + provisioner, TX +20 dBm   |
| Scanner | ESP32 WROOM-32            | Bluedroid | 3 units, one per zone            |
| Relay   | ESP32 WROOM-32            | Bluedroid | Optional mesh forwarder          |
| Tag     | ESP32 WROOM-32 or iPhone  | -         | iBeacon or custom UUID `AB00...` |

<!--
EVIDENCE: photo of the assembled hardware, all four nodes side by side
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 2:</em></strong> BMT hardware set</p>
-->

### II. System Overview

```text
Tag (iPhone iBeacon / ESP32)
  |  BLE ADV
  v
Scan node 0x01 / 0x02 / 0x03 ----BLE Mesh---- Relay (optional forwarder)
  |                                                |
  '----------------- BLE Mesh --------------------'
                        |
                        v
                     Gateway (ESP32-S3 + WiFi)
                        |  MQTTS (ThingsBoard Gateway API)
                        v
                     ThingsBoard (Docker)
```

Only the gateway has WiFi. Scan nodes reach the gateway through the mesh. A far scanner can hop through the relay when the direct link to the gateway is too weak.

<!--
EVIDENCE: floor plan showing scanner positions, gateway location, and covered zones
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 3:</em></strong> Deployment floor plan</p>
-->

### III. Repo Structure

```text
nodes/
  gateway/            provisioner + MQTT bridge + OTA orchestrator
  scan_node_01..03/   one project per scanner, IDs 0x01 / 0x02 / 0x03
  relay_01/           BLE Mesh forwarder + WiFi OTA
  tag_01/             ESP32 tag beacon (optional)
  components/
    bmt_scan_core/    shared component used by all three scan nodes
docs/
  mesh.md             mesh roles, models, provisioning
  ota.md              OTA flow and configuration
  thingsboard-mqtt.md MQTT topics, TLS, ThingsBoard usage
  algorithms.md       CRC16, Kalman, path loss, zone hysteresis
docker-compose.yml    ThingsBoard CE + PostgreSQL
tls/                  server cert and CA for MQTTS
```

ESP-IDF version: **v6.0**. Each project under `nodes/` is a standalone build.

### IV. Quick Start

**1. Start ThingsBoard:**

```bash
docker compose up -d
```

Open `http://<host_ip>:8080` (default login `tenant@thingsboard.org` / `tenant`).
Create the gateway device, enable "Is gateway", copy the access token.

**2. Configure secrets** in `nodes/gateway/main/bmt_config.h` (WiFi SSID / password, TB IP, gateway token).

**3. Build and flash the gateway:**

```bash
cd nodes/gateway
idf.py set-target esp32s3
idf.py -p /dev/ttyUSB0 flash monitor
```

**4. Build and flash each scan node** (one at a time, waiting for `=== READY ===` between each):

```bash
cd nodes/scan_node_01
idf.py set-target esp32
idf.py -p /dev/ttyUSB0 flash monitor
```

**5. Provision:** on the gateway UART, press `a` for auto-provision, then wait until all nodes appear under `1` (list nodes).

<!--
EVIDENCE: gateway serial log after successful provisioning of all nodes
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 4:</em></strong> Gateway serial log after provisioning</p>
-->

### V. Serial Commands

**Gateway:**

| Key | Action |
|-----|--------|
| `1` | List provisioned nodes |
| `2` | List tracked tags and zones |
| `3` | Show MQTT queue statistics |
| `s` | Scan for unprovisioned beacons (10 s) |
| `p` | Provision the current scan list |
| `a` | Switch to auto-provision mode |
| `u` | OTA a group: `s` = all scanners, `r` = all relays |
| `g` | OTA the gateway itself |
| `0` | Wipe NVS and reboot |

**Scanner / Relay:**

| Key | Action |
|-----|--------|
| `1` | Print status |
| `r` | Reset mesh provisioning and reboot |
| `o` | Trigger local WiFi OTA (for test) |

### VI. Stack

ESP-IDF v6.0, BLE Mesh vendor model (CID `0x02E5`), NimBLE on the gateway, Bluedroid on scan and relay nodes, MQTTS with TLS 1.2, ThingsBoard CE 3.7.0.

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
