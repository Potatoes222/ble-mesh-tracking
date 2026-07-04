# BLE Mesh

The system uses ESP-IDF BLE Mesh (CID `0x02E5`, vendor model `0x0000`). The gateway acts as provisioner. Scan nodes, the relay, and the gateway itself are mesh nodes.

<hr>

## Contents

| Section | Topic |
|---|---|
| [I. Node Roles](#i-node-roles) | Tag, scanner, relay, gateway responsibilities. |
| [II. Vendor Model Opcodes](#ii-vendor-model-opcodes) | Custom opcodes and their payloads. |
| [III. Keys and Addresses](#iii-keys-and-addresses) | NetKey, AppKey, unicast address plan. |
| [IV. Provisioning](#iv-provisioning) | Manual and auto provisioning modes. |
| [V. Node Table Persistence](#v-node-table-persistence) | NVS storage of provisioned nodes. |
| [VI. Zone Assignment](#vi-zone-assignment) | Scanner ID to room mapping. |
| [VII. Radio Budget](#vii-radio-budget) | Single-radio time-division on scan nodes. |

### I. Node Roles

#### 1. Tag

Not part of the mesh. It only broadcasts a BLE ADV packet (iBeacon layout or a custom 24-byte format with CRC16). Any scan node in range can pick it up.

Source: [nodes/tag_01/main/bmt_beacon.c](../nodes/tag_01/main/bmt_beacon.c)

<!--
EVIDENCE: ADV packet capture from nRF Connect showing the custom BMT payload
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 1:</em></strong> Tag ADV packet capture</p>
-->

#### 2. Scan Node

Roles:

- Passive GAP scanner during the scan phase of the radio cycle.
- Vendor Model server publishing `TAG_STATUS` and `NODE_HEALTH` opcodes.
- Config Server for provisioning.
- Mesh relay enabled so far scanners can hop through it.

UUID prefix `SCAN` (`53:43:41:4E`). Byte 15 of the UUID is the scanner ID (`0x01`, `0x02`, `0x03`). The gateway uses the prefix to tell scan and relay nodes apart during provisioning.

Source: [nodes/components/bmt_scan_core/src/bmt_mesh.c](../nodes/components/bmt_scan_core/src/bmt_mesh.c)

#### 3. Relay

Roles:

- Config Server + Health Server.
- Mesh stack forwards network PDUs (no vendor model publish).
- Vendor Model server on the receive-only side, accepting `RESET_CMD` and `OTA_TRIGGER`.

UUID prefix `RELAY` (`52:45:4C:41:59`). Byte 15 is the relay ID.

Source: [nodes/relay_01/main/bmt_mesh.c](../nodes/relay_01/main/bmt_mesh.c)

#### 4. Gateway

Roles:

- Provisioner (scans for unprovisioned devices and adds them to the network).
- Config Client (sends `APP_KEY_ADD` and `MODEL_APP_BIND` to each new scan node).
- Vendor Model client (receives `TAG_STATUS` and `NODE_HEALTH`; publishes `OTA_TRIGGER` and `RESET_CMD`).
- Config Client is also used for periodic relay ping (health check via `DEFAULT_TTL_GET`).

Source: [nodes/gateway/main/bmt_mesh.c](../nodes/gateway/main/bmt_mesh.c)

### II. Vendor Model Opcodes

Vendor CID `0x02E5`, Model ID `0x0000`.

| Opcode | Name           | Direction          | Payload size | Purpose                                         |
|--------|----------------|--------------------|--------------|-------------------------------------------------|
| `0x00` | `TAG_STATUS`   | scanner -> gateway | 8 B          | one tag report per scan cycle                   |
| `0x03` | `NODE_HEALTH`  | scanner -> gateway | 8 B          | chip temp, VDD, heap, uptime, every 30 s        |
| `0x05` | `RESET_CMD`    | gateway -> node    | 1 B          | remote soft reset                               |
| `0x06` | `OTA_TRIGGER`  | gateway -> node    | 1 B          | tell node to WiFi-OTA (see [ota.md](ota.md))    |

Payloads are kept to 8 bytes so each fits one unsegmented BLE Mesh PDU. This avoids the segmentation ACK loop and its retransmit stalls.

Opcode definitions:

- [nodes/gateway/main/bmt_types.h](../nodes/gateway/main/bmt_types.h)
- [nodes/components/bmt_scan_core/include/bmt_types.h](../nodes/components/bmt_scan_core/include/bmt_types.h)

### III. Keys and Addresses

| Item                       | Value                                                       |
|----------------------------|-------------------------------------------------------------|
| Gateway unicast address    | `0x0001`                                                    |
| Scan / relay unicast start | `0x0002` (assigned by provisioner in order)                 |
| NetKey                     | 16 B fixed, see `g_bmt_net_key` in `bmt_mesh.c` (gateway)   |
| AppKey                     | 16 B fixed, see `g_bmt_app_key` in `bmt_mesh.c` (gateway)   |
| TTL                        | 7 (max hops)                                                |
| Net transmit               | count 7, interval 10 ms (up to 8 transmissions per PDU)     |
| Relay retransmit           | count 7, interval 10 ms                                     |

<!--
EVIDENCE: gateway log showing provision -> APP_KEY_ADD -> MODEL_APP_BIND OK
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 2:</em></strong> Full provisioning + configuration log</p>
-->

### IV. Provisioning

Two modes on the gateway, switched from UART.

#### 1. Manual Mode (default)

1. UART `s` on the gateway. It scans BLE Mesh unprovisioned beacons for 10 s and lists them.
2. UART `p` triggers provisioning for every device in the list that is not already provisioned.

Source: [nodes/gateway/main/bmt_scan_list.c](../nodes/gateway/main/bmt_scan_list.c)

#### 2. Auto Mode

1. UART `a` on the gateway. From that point, any unprovisioned beacon the gateway sees is provisioned immediately.

#### 3. After Provisioning

Both modes end the same way. When a scan node is provisioned, the gateway spawns a background task that sends `APP_KEY_ADD`, then `MODEL_APP_BIND` for the vendor model. When the bind completes, the scanner can publish `TAG_STATUS` to `0x0001`.

Source: [nodes/gateway/main/bmt_mesh.c](../nodes/gateway/main/bmt_mesh.c) - see `scan_config_task`.

<!--
EVIDENCE: serial log showing SCN_CFG APP_KEY_ADD and MODEL_APP_BIND OK for one scanner
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 3:</em></strong> Scan node configuration handshake</p>
-->

### V. Node Table Persistence

The gateway saves its list of provisioned nodes to NVS (namespace `bmt_gw`, key `node_table`) so it survives a reboot without re-provisioning. Wipe with UART `0`.

Source: [nodes/gateway/main/bmt_node_table.c](../nodes/gateway/main/bmt_node_table.c)

### VI. Zone Assignment

Zone name is a function of scanner ID, hard-coded in [nodes/gateway/main/bmt_zone.c](../nodes/gateway/main/bmt_zone.c) - see `bmt_zone_name`.

| Scanner ID | Zone name      |
|------------|----------------|
| `0x01`     | `bedroom_1`    |
| `0x02`     | `bedroom_2`    |
| `0x03`     | `toilet`       |
| `0xFF`     | `out_of_range` |

To move rooms around, either change the physical placement of the scanner with a given ID, or edit the table in `bmt_zone_name`.

### VII. Radio Budget

Each ESP32 has one BLE radio. A scan node cannot GAP-scan and publish over mesh at the same time. The scanner divides time between the two phases; see [docs/algorithms.md](algorithms.md) for the exact schedule.
