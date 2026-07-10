# How it works

## Auto provisioning

The gateway runs in AUTO mode. It finds unprovisioned nodes by their UUID prefix (`SCAN` or `RELAY`) and provisions them with Static OOB authentication. It adds the AppKey, binds the vendor model, and pushes the current HMAC beacon key. NetKey and AppKey are random and stored in NVS.

## Tag data flow

Tags send a 24-byte custom BLE ADV (Espressif CID `0x02E5`) with a sequence number and a 16-bit HMAC.

Scanners check the HMAC, filter RSSI, reject replays, and send `TAG_STATUS {tag_id, rssi}` over mesh.

The gateway looks up the scanner MAC by the source address and forwards `{scanner_mac, tag_id, rssi}` to ThingsBoard.

The rule chain computes `current_zone`. The dashboard shows it.

## Self-healing

- If the gateway loses power, NVS restores the mesh keys and the node table on boot. Traffic resumes with no manual step.
- The data watchdog waits 15 seconds after boot. After that, if the node table is not empty but no real mesh traffic arrives for 30 seconds, it broadcasts `RESET_CMD` five times and re-provisions. If all five sends fail, it does not wipe.
- If a node reboots, its unprovisioned beacon appears again. The gateway drops the old entry and re-provisions it.
- The gateway pings the relay every 20 seconds. Only ACKs with `error_code == 0` count as proof the mesh is alive.

## OTA and beacon security

- Scanner and relay OTA is triggered over mesh. Each node downloads its `.bin` from the LAN HTTP server and reports `OTA_RESULT` back.
- The gateway checks its own firmware by SHA256 and skips OTA if unchanged.
- The HMAC beacon key rotates every 24 hours. The gateway generates a new random key, stores it in NVS, and pushes it to every scanner over mesh. Forged beacons are rejected at the scanner.

## ThingsBoard rule chain

The `ble_tag_zone_detection` rule chain runs on every tag telemetry event:

1. Read the last state from server attributes.
2. Pick the scanner with the strongest RSSI among fresh samples (under 10 seconds old).
3. Switch zone only if the new one beats the current one by at least 8 dBm (hysteresis) and holds for two updates in a row (debounce).
4. Save `current_zone` and `current_rssi`.

To move a scanner to a different room, edit `ZONE_MAP` on the server. No reflash needed.

## Source layout

### `apps/gateway/main/`

- `main.c` — boot sequence: NVS, node table, mesh keys, HMAC OTA key, MQTT worker, WiFi, MQTT, Bluetooth, mesh, UART, ping, watchdog, OTA auto-check.
- `bmt_config.h` — WiFi, ThingsBoard IP/token/CN, OTA URLs, device profile names.
- `bmt_types.h` — vendor-model opcodes and structs shared with scanner and relay.
- `bmt_mesh.c/h` — provisioner, configuration task, mesh callbacks, key management.
- `bmt_node_table.c/h` — provisioned node table (addr, UUID, MAC, type, online), NVS save/load.
- `bmt_mac_cache.c/h` — temporary UUID-to-MAC cache during scan.
- `bmt_scan_list.c/h` — manual provisioning (UART `s`, `p`, `a`, `m`).
- `bmt_zone.c/h` — local zone estimate for debug only.
- `bmt_wifi.c/h` — WiFi STA with auto-reconnect.
- `bmt_mqtt.c/h` — MQTT(S) client, tag report queue, RPC routing.
- `bmt_thingsboard.c/h` — payload formatting for the ThingsBoard Gateway API.
- `bmt_ota.c/h` — gateway self-OTA, mesh-triggered OTA for scanner/relay, HMAC OTA beacon, 24h key rotation.
- `bmt_watchdog.c/h` — data watchdog and full-mesh reset.
- `bmt_uart.c/h` — UART command menu (see [UART commands](uart.md)).
- `ca.pem` — CA cert for the MQTTS server.

### `apps/scanner/main/`

- `main.c` — calls `bmt_scan_core_init()`.
- `bmt_scan_core.c/h` — boot orchestrator; keeps a legacy `scanner_id` in NVS (identity is really the chip MAC now).
- `bmt_config.h` — WiFi (used only for OTA) and firmware URL.
- `bmt_types.h` — vendor-model opcodes and structs.
- `bmt_auth.c/h` — HMAC-16 via PSA API. Two keys: one for tag beacons, one for the OTA beacon. The Gateway can rotate the OTA-beacon key over mesh.
- `bmt_scan.c/h` — BLE GAP scan and radio time-sharing with mesh.
- `bmt_tag_table.c/h` — table of visible tags (up to 20), with anti-replay and 5-second timeout.
- `bmt_mesh.c/h` — mesh node. UUID contains the chip MAC. Handles `RESET_CMD`, `OTA_TRIGGER`, `OTA_KEY_PUSH`.
- `bmt_ota.c/h` — WiFi-based OTA triggered by mesh.
- `bmt_uart.c/h` — UART commands `r`, `1`, `i`.

### `apps/relay/main/`

- `main.c` — inits Bluetooth, mesh, UART.
- `bmt_config.h` — fixed Relay UUID (`RELAY...02`), WiFi, OTA URL.
- `bmt_types.h` — vendor-model opcodes and structs.
- `bmt_mesh.c/h` — mesh node with Relay feature enabled. Forwards at the Network Layer. Still binds AppKey to handle `RESET_CMD` and `OTA_TRIGGER`.
- `bmt_ota.c/h` — same OTA path as the scanner.
- `bmt_uart.c/h` — UART commands `r`, `1`, plus a 30-second health log.

### `apps/tag/main/`

- `main.c` — `bmt_auth_init()`, `bmt_beacon_start()`, UART.
- `bmt_config.h` — UUID, `tag_id`, tag type (`PERSON` or `ASSET`), TX power calibration.
- `bmt_auth.c/h` — computes the 16-bit HMAC for each ADV packet.
- `bmt_beacon.c/h` — 24-byte custom ADV (CID `0x02E5`) with UUID, major, minor, TX power, sequence, and HMAC. Timer runs every 500 ms.
- `bmt_uart.c/h` — status output (sequence, last HMAC, advertising state).

### `thingsboard/`

- `docker-compose.yml` — ThingsBoard CE 3.7 and PostgreSQL. Ports 8080 (UI) and 8883 (MQTTS). Certs mounted from `tls/`.
- `rulechain/` — rule chain exports (main one is `ble_tag_zone_detection.json`).
- `dashboard/indoor_tracking.json` — Indoor Tracking dashboard.
- `tls/` — dev CA and server certs, plus `gen_certs.sh` to make new ones.

### `docs/legacy/`

- `gateway_main.c`, `scanner_main.c` — old monolithic `main.c` files kept for reference.
