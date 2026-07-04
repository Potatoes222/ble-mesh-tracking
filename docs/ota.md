# OTA (Over-the-Air Update)

Every node except the tag can update its firmware over WiFi. The gateway triggers the update; the target node downloads its own binary from an HTTP server on the LAN.

<hr>

## Contents

| Section | Topic |
|---|---|
| [I. Design](#i-design) | Why HTTP + mesh trigger instead of TB firmware delivery. |
| [II. Trigger Sources](#ii-trigger-sources) | UART, node self-trigger, ThingsBoard RPC. |
| [III. Flow](#iii-flow) | Step-by-step sequence from trigger to reboot. |
| [IV. Partition Table](#iv-partition-table) | Two-slot layout and flash size requirement. |
| [V. Node Type Filter](#v-node-type-filter) | Trigger payload byte meaning. |
| [VI. Configuration](#vi-configuration) | Per-project WiFi and URL settings. |
| [VII. HTTP Server](#vii-http-server) | Python one-liner and firewall notes. |
| [VIII. HTTPS](#viii-https) | Turning on TLS for firmware transport. |
| [IX. Rollback Safety](#ix-rollback-safety) | Behaviour if the new firmware crashes. |
| [X. Troubleshooting](#x-troubleshooting) | Common failures and fixes. |

### I. Design

ThingsBoard is not involved in delivering firmware. It only provides a button (RPC) that the gateway listens for. The firmware binary itself lives on a plain HTTP server on the developer machine.

Reasons for this split:

- MQTT payloads on ThingsBoard CE are capped well below firmware size.
- HTTP serves multi-megabyte files without any TB configuration.
- Adding a new firmware version is a file copy, not a TB upload.

### II. Trigger Sources

Three ways to start OTA.

| Source            | How                                                              |
|-------------------|------------------------------------------------------------------|
| Gateway UART      | `u` then `s` (scanners) or `r` (relays), or `g` (gateway self)    |
| Node UART         | `o` on a scan node or relay for a local test (no gateway needed)  |
| ThingsBoard RPC   | `{"method": "ota_scanner"}`, `"ota_relay"`, or `"ota_gateway"`    |

RPC handler: [nodes/gateway/main/bmt_thingsboard.c](../nodes/gateway/main/bmt_thingsboard.c) - see `bmt_tb_handle_rpc`.

### III. Flow

```text
Gateway
  |  Vendor Model publish OTA_TRIGGER (opcode 0x06, 1-byte payload)
  |  to the unicast address of each target node, retry up to 5 times
  v
Target node
  |  Vendor Model receive callback -> bmt_ota_start()
  |  esp_log_level_set("*", ESP_LOG_NONE) to silence stack chatter
  |  esp_netif + esp_wifi init, connect to BMT_WIFI_SSID
  |  wait up to BMT_OTA_WIFI_TIMEOUT_MS for IP
  v
esp_https_ota()
  |  HTTP GET BMT_OTA_URL
  |  write into inactive OTA partition (ota_0 or ota_1)
  |  verify image header
  v
Success -> otadata switches active slot, esp_restart()
Failure -> WiFi teardown, s_running = false, back to BLE-only
```

The gateway waits `BMT_OTA_INTER_NODE_DELAY_MS` (60 s by default) between two nodes, long enough for one node to finish downloading and reboot before the next one is asked.

Sources:

- Gateway distribute + self-update: [nodes/gateway/main/bmt_ota.c](../nodes/gateway/main/bmt_ota.c)
- Scanner: [nodes/components/bmt_scan_core/src/bmt_ota.c](../nodes/components/bmt_scan_core/src/bmt_ota.c)
- Relay: [nodes/relay_01/main/bmt_ota.c](../nodes/relay_01/main/bmt_ota.c)

<!--
EVIDENCE: full serial log of one OTA cycle on a scanner (trigger, WiFi, download, reboot)
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 1:</em></strong> Scanner OTA serial log</p>
-->

<!--
EVIDENCE: short video showing the "u s" flow on the gateway with one scanner OTA in progress
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 2:</em></strong> End-to-end OTA demo</p>
-->

### IV. Partition Table

All OTA-capable nodes use a custom partition layout with two application slots.

| Partition  | Size       | Purpose                                        |
|------------|------------|------------------------------------------------|
| `nvs`      | `0x6000`   | NVS store                                       |
| `phy_init` | `0x1000`   | PHY calibration                                 |
| `ota_0`    | `0x1C0000` | app slot 0 (~1.75 MB)                          |
| `ota_1`    | `0x1C0000` | app slot 1 (~1.75 MB)                          |
| `otadata`  | `0x2000`   | which slot is active + rollback state          |

At any moment one slot holds the running firmware and the other is free for the next download. On success, `otadata` switches active slot on reboot.

Partition files:

- Gateway: [nodes/gateway/partitions.csv](../nodes/gateway/partitions.csv)
- Scanner: [nodes/scan_node_01/partitions.csv](../nodes/scan_node_01/partitions.csv) (identical for 02/03)
- Relay: [nodes/relay_01/partitions.csv](../nodes/relay_01/partitions.csv)

Two 1.75 MB slots plus 24 KB NVS plus 4 KB PHY plus 8 KB `otadata` means the minimum flash is 4 MB. It is set in each `sdkconfig.defaults` as `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`. The gateway on ESP32-S3 uses 16 MB flash - the same partition layout still fits, the rest is unused.

### V. Node Type Filter

`OTA_TRIGGER` carries one byte.

| Value  | Meaning        |
|--------|----------------|
| `0x01` | scanner only   |
| `0x02` | relay only     |
| `0xFF` | all nodes      |

A scan node ignores the trigger unless the byte is `0x01` or `0xFF`. A relay ignores it unless the byte is `0x02` or `0xFF`. This lets the gateway broadcast without triggering the wrong nodes.

Constants: [nodes/gateway/main/bmt_types.h](../nodes/gateway/main/bmt_types.h) - see `BMT_NODE_TYPE_*`.

### VI. Configuration

Per-project OTA URL and WiFi credentials sit in each project's `bmt_config.h`.

Scan nodes have per-project URLs so the same HTTP server can hold three different binaries side by side.

| Project        | URL                                                  |
|----------------|------------------------------------------------------|
| `scan_node_01` | `http://<PC_IP>:8080/Scanner_01.bin`                 |
| `scan_node_02` | `http://<PC_IP>:8080/Scanner_02.bin`                 |
| `scan_node_03` | `http://<PC_IP>:8080/Scanner_03.bin`                 |
| `relay_01`     | `http://<PC_IP>:8080/Relay.bin`                      |
| `gateway`      | `http://<PC_IP>:8080/Gateway.bin`                    |

Config files:

- [nodes/scan_node_01/main/bmt_config.h](../nodes/scan_node_01/main/bmt_config.h)
- [nodes/relay_01/main/bmt_config.h](../nodes/relay_01/main/bmt_config.h)
- [nodes/gateway/main/bmt_config.h](../nodes/gateway/main/bmt_config.h)

### VII. HTTP Server

Any static file server works. The simplest is Python.

```bash
mkdir -p ~/firmware
cp nodes/gateway/build/gateway.bin           ~/firmware/Gateway.bin
cp nodes/scan_node_01/build/scan_node_01.bin ~/firmware/Scanner_01.bin
cp nodes/scan_node_02/build/scan_node_02.bin ~/firmware/Scanner_02.bin
cp nodes/scan_node_03/build/scan_node_03.bin ~/firmware/Scanner_03.bin
cp nodes/relay_01/build/relay_01.bin         ~/firmware/Relay.bin

cd ~/firmware
python3 -m http.server 8080
```

On Windows, open TCP 8080 in the firewall and set the network to Private. On Linux and macOS this is usually not needed on a home LAN.

<!--
EVIDENCE: HTTP server access log during OTA (200 OK on Scanner_01.bin)
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 3:</em></strong> HTTP server access log</p>
-->

### VIII. HTTPS

`esp_https_ota` verifies TLS if the URL starts with `https://`. The certificate bundle is attached automatically via `esp_crt_bundle_attach`. To move to HTTPS, put the binary behind a TLS reverse proxy on the same host and change the URL. No code change is needed on the node.

### IX. Rollback Safety

If the new firmware crashes before it calls `esp_ota_mark_app_valid_cancel_rollback`, ESP-IDF will reboot into the previous image on the next power cycle. The current code does not call this function explicitly, so any first successful boot is treated as valid.

This is fine for a development setup. For production, add the call after the node has confirmed WiFi, mesh, and MQTT are all working.

<!--
EVIDENCE: forced-crash rollback demo (flash a firmware that panics, observe revert on reboot)
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 4:</em></strong> Rollback demo</p>
-->

### X. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `esp_https_ota FAILED: ESP_ERR_HTTP_CONNECT` | HTTP server not running, firewall closed on 8080, or wrong IP in `BMT_OTA_URL` | Use the LAN IP of the developer machine, not `127.0.0.1`. Check firewall. |
| `Image validation failed`                    | The `.bin` was built for a different target (e.g. ESP32-S3 build on an ESP32 classic scanner) | Rebuild for the correct target and copy the new binary to the HTTP folder. |
| Node reboots into the old firmware repeatedly | The new image panicked before it was marked valid, rollback triggered | Temporarily enable `CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y` and inspect the boot log. |
| OTA never starts even though gateway logs `TRIGGER OK` | The scanner did not receive the mesh message. Its Vendor Model may not be bound to the AppKey | Re-provision, and confirm the scanner log shows `Model AppKey bind done`. |
