# Algorithms and Logic

This document lists the small algorithms and heuristics used across BMT: what they compute, why they are needed, and where in the source they live. Formulas are given in one line; runnable code is in the source files linked next to each section.

<hr>

## Contents

| Section | Topic |
|---|---|
| [I. CRC-16 CCITT](#i-crc-16-ccitt) | Integrity check on the tag ADV payload. |
| [II. Kalman Filter (1-D)](#ii-kalman-filter-1-d) | Per-tag RSSI smoothing on the scan node. |
| [III. Path Loss Distance Model](#iii-path-loss-distance-model) | Log-distance conversion from RSSI to metres. |
| [IV. Sequence Loss Rate](#iv-sequence-loss-rate) | Packet loss tracking from a 1-byte sequence. |
| [V. Zone Detection with Hysteresis](#v-zone-detection-with-hysteresis) | Picking a room from multiple RSSI streams. |
| [VI. Time-Division Radio](#vi-time-division-radio) | Sharing one BLE radio between scan and mesh publish. |
| [VII. Mesh Retransmit Strategy](#vii-mesh-retransmit-strategy) | net_transmit and relay_retransmit values. |

### I. CRC-16 CCITT

Used on the tag side to protect the 24-byte ADV payload, and on the scan side to reject corrupted packets before they reach the tag table.

Parameters:

| Item      | Value            |
|-----------|------------------|
| Polynomial | `0x1021`         |
| Initial value | `0xFFFF`      |
| Input reflected | no           |
| Output reflected | no          |
| XOR out   | `0x0000`         |
| Covered bytes | first 22 B of the payload (`uuid + major + minor + tx_power + sequence`) |

Verification on the scanner drops the packet on mismatch (see `parse_adv`) and prints `CRC fail` at debug level.

Sources:

- Tag side (compute + insert): [nodes/tag_01/main/bmt_beacon.c](../nodes/tag_01/main/bmt_beacon.c) - see `crc16_ccitt` and `build_adv_data`.
- Scan side (compute + verify): [nodes/components/bmt_scan_core/src/bmt_crc.c](../nodes/components/bmt_scan_core/src/bmt_crc.c) and [nodes/components/bmt_scan_core/src/bmt_scan.c](../nodes/components/bmt_scan_core/src/bmt_scan.c) - see `parse_adv`.

<table align="center">
  <tr>
    <td align="center"><!-- EVIDENCE: serial log showing a corrupted packet rejected with "CRC fail" --></td>
  </tr>
</table>
<p align="center"><strong><em>Figure 1:</em></strong> CRC rejection log</p>

### II. Kalman Filter (1-D)

Raw RSSI is noisy. Averaging over N samples introduces lag; the 1-D Kalman filter reacts faster to real movement while still smoothing measurement jitter. One filter instance per active tag on each scanner.

State and update:

```text
predict:  p = p + q
gain:     k = p / (p + r)
update:   x = x + k * (rssi - x)
covariance: p = (1 - k) * p
```

Values used:

| Item | Value  | Meaning                              |
|------|--------|--------------------------------------|
| `q`  | `0.1`  | process noise (how fast x can change) |
| `r`  | `2.0`  | measurement noise (dBm variance)      |
| `x` initial | first RSSI seen | filtered RSSI              |
| `p` initial | `1.0`   | initial covariance                  |

Why Kalman here and not a moving average: the 1-D form has one state and one gain, is cheap enough to run on every scanned packet, and the gain `k` self-adapts to noise. A moving average of the same "smoothing strength" would lag by half the window.

Source: [nodes/components/bmt_scan_core/src/bmt_tag_table.c](../nodes/components/bmt_scan_core/src/bmt_tag_table.c) - see `kalman_init` and `kalman_update`.

<table align="center">
  <tr>
    <td align="center"><!-- EVIDENCE: plot of raw RSSI vs Kalman-filtered RSSI over ~60 seconds while walking through the covered area --></td>
  </tr>
</table>
<p align="center"><strong><em>Figure 2:</em></strong> Raw vs filtered RSSI</p>

### III. Path Loss Distance Model

Converts filtered RSSI into an approximate distance in metres for display purposes only (the zone decision does not use distance).

Formula:

```text
d = 10 ^ ((tx_power - rssi_filtered) / (10 * n))
```

with `n = 2.5` (`BMT_PATH_LOSS_N`) chosen for typical indoor conditions with walls. `tx_power` is the "Measured Power" reported in the tag's ADV packet (RSSI at 1 m during calibration), not the radio TX power.

The published `distance_dm` field is `int16_t` in decimetres (`= d * 10`) so it fits in one byte pair inside the 8-byte tag report.

Source: [nodes/components/bmt_scan_core/src/bmt_tag_table.c](../nodes/components/bmt_scan_core/src/bmt_tag_table.c) - see `calc_distance`.

<table align="center">
  <tr>
    <td align="center"><!-- EVIDENCE: table or plot of measured distance vs model distance at 1, 2, 3, 4, 5 metres --></td>
  </tr>
</table>
<p align="center"><strong><em>Figure 3:</em></strong> Measured vs modelled distance</p>

### IV. Sequence Loss Rate

The tag ADV payload includes a 1-byte sequence number that increments every ~500 ms and wraps at 255. The scan node uses the delta between the current and last-seen sequence to count missed packets per tag and compute a loss percentage.

Rules used in `apply_update`:

- If the sequence is the same as the last one seen, treat it as a re-scan (RSSI update but no gap).
- If the delta is small and negative (`-1..-9`), treat it as an out-of-order arrival and skip.
- Otherwise the missed count is `delta - 1` (wrap-safe formula for the negative case).
- Missed counts above 200 are ignored (tag was probably out of range for a while).

Loss percentage in the published report:

```text
loss_pct = total_missed * 100 / (total_received + total_missed)
```

Source: [nodes/components/bmt_scan_core/src/bmt_tag_table.c](../nodes/components/bmt_scan_core/src/bmt_tag_table.c) - see `apply_update`.

For iPhone tags (Apple CID `0x004C`) there is no sequence, so `sequence = 0` and `loss_pct` stays at 0.

### V. Zone Detection with Hysteresis

The gateway maintains, per tag, the latest RSSI seen from each scanner and a timestamp for each. The zone is the scanner ID whose RSSI is the freshest and highest.

Freshness window: `BMT_SCANNER_VALID_MS = 3500 ms`. An RSSI older than that is treated as expired for the current decision.

Hysteresis rule: to switch from the current zone to a new best scanner, the new scanner's RSSI must beat the current one by at least `BMT_ZONE_HYSTERESIS_DBM = 5 dBm`. Otherwise the current zone stays.

Out of range: if no report for the tag arrives from any scanner for `BMT_TAG_OUT_OF_RANGE_MS = 10000 ms`, the local zone resets to `BMT_ZONE_UNKNOWN = 0xFF`.

Note: since v4, the gateway does not publish the zone. It publishes only `{scanner_id, rssi}` and lets the ThingsBoard rule chain compute the zone. The local logic above is used only for the UART debug view (command `2`).

Source: [nodes/gateway/main/bmt_zone.c](../nodes/gateway/main/bmt_zone.c) - see `evaluate` and `timeout_task`.

<table align="center">
  <tr>
    <td align="center"><!-- EVIDENCE: gateway UART log of a zone change (bedroom_1 -> toilet -> out_of_range) --></td>
  </tr>
</table>
<p align="center"><strong><em>Figure 4:</em></strong> Zone transitions log</p>

### VI. Time-Division Radio

Each ESP32 has one BLE radio. It cannot GAP-scan (to hear tags) and publish over BLE Mesh at the same time. The scan node splits time between the two.

Cycle length: `1500 ms`.

| Phase          | Duration                              | What runs                           |
|----------------|---------------------------------------|-------------------------------------|
| GAP scan       | `BMT_GAP_SCAN_DURATION_MS = 1200 ms`  | passive scan, filter, Kalman update |
| Mesh publish   | `BMT_MESH_PUBLISH_DURATION_MS = 300 ms` | one `TAG_STATUS` per active tag    |

Phase offset per scanner:

```text
offset_ms = (scanner_id - 1) * 500
```

With three scan nodes at IDs 1 / 2 / 3, their publish windows never overlap. This spreads mesh traffic in time and reduces PDU collisions at the gateway.

Source: [nodes/components/bmt_scan_core/src/bmt_scan.c](../nodes/components/bmt_scan_core/src/bmt_scan.c) - see `radio_task`.

<table align="center">
  <tr>
    <td align="center"><!-- EVIDENCE: timing diagram of the three scanners' scan / publish phases across ~4 seconds --></td>
  </tr>
</table>
<p align="center"><strong><em>Figure 5:</em></strong> Three-scanner time-division schedule</p>

### VII. Mesh Retransmit Strategy

The vendor model publish path uses aggressive retransmit values to trade a bit of air time for higher packet reliability across the mesh.

| Parameter          | Value           | Meaning                                              |
|--------------------|-----------------|------------------------------------------------------|
| `net_transmit`     | count 7, 10 ms  | up to 8 transmissions of a PDU, 10 ms apart          |
| `relay_retransmit` | count 7, 10 ms  | up to 8 forwards of another node's PDU               |
| `TTL`              | 7               | max mesh hops                                        |

The payloads themselves are kept small (8 bytes each) so a single PDU is never segmented; segmentation would introduce an ACK loop with its own timeouts. Combined with the retransmit values above, a `TAG_STATUS` message is very likely to reach the gateway even with two of the eight transmissions lost.

Source: [nodes/gateway/main/bmt_mesh.c](../nodes/gateway/main/bmt_mesh.c) and [nodes/components/bmt_scan_core/src/bmt_mesh.c](../nodes/components/bmt_scan_core/src/bmt_mesh.c) - see `s_cfg_server`.
