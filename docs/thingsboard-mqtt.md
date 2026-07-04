# ThingsBoard, MQTT, and TLS

Only the gateway talks to ThingsBoard. It publishes telemetry for itself and every sub-device (scan nodes, relay, tags) using the ThingsBoard Gateway API, and subscribes to the RPC topic to receive OTA commands.

## What ThingsBoard is used for

Three roles:

1. **Dashboard**. Live view of which tag is in which room, RSSI per scanner, node health (temperature, VDD, uptime, free heap). Rendered from the telemetry the gateway pushes.
2. **Zone detection**. From version 4 onward, the gateway sends only the raw `{scanner_id, rssi}` per tag. A ThingsBoard Rule Chain picks the strongest scanner and derives the `zone` attribute on the tag device. The gateway still runs a local copy of the same logic, but only for UART debug output (command `2`), not for publishing. This keeps the zone rules editable in the TB UI, without a firmware rebuild.
3. **RPC trigger for OTA**. A dashboard widget sends an RPC method call. The gateway maps it to an OTA action. See [ota.md](ota.md).

<!-- EVIDENCE: dashboard screenshot with the three widgets (map, RSSI chart, health) -->
<!-- EVIDENCE: TB rule chain screenshot for zone detection -->

## Broker setup (Docker)

Root file: [docker-compose.yml](../docker-compose.yml)

Two containers: PostgreSQL for TB storage, and ThingsBoard CE 3.7.0. Ports mapped to the host:

| Port | Protocol | Purpose                          |
|------|----------|----------------------------------|
| 8080 | HTTP     | TB Web UI (`http://<host>:8080`) |
| 1883 | MQTT     | plaintext MQTT (test / fallback) |
| 8883 | MQTTS    | MQTT over TLS 1.2 (production)   |
| 7070 | Edge RPC | optional, not used by BMT        |

Start with:

```bash
docker compose up -d
```

Default login is `tenant@thingsboard.org` / `tenant`.

## MQTT topics

The gateway uses the ThingsBoard Gateway MQTT API. All topics are relative to the connected client (the gateway).

| Direction    | Topic                             | Payload            | Purpose                                     |
|--------------|-----------------------------------|--------------------|---------------------------------------------|
| pub          | `v1/devices/me/telemetry`         | `{"status":"ONLINE"}` | gateway self telemetry                    |
| pub          | `v1/devices/me/attributes`        | `{"role":"gateway"}`  | gateway self attributes                   |
| pub          | `v1/gateway/connect`              | `{"device":"scan_0x0002","type":"ble_mesh_node"}` | register sub-device |
| pub          | `v1/gateway/disconnect`           | `{"device":"..."}`  | remove sub-device                           |
| pub          | `v1/gateway/attributes`           | `{"scan_0x0002":{"role":"scan"}}` | sub-device attributes         |
| pub          | `v1/gateway/telemetry`            | `{"tag_0xAB01":[{"scanner_id":"scan_01","rssi":-58}]}` | tag / node telemetry |
| sub          | `v1/devices/me/rpc/request/+`     | `{"method":"ota_scanner"}` | OTA and other server-side calls        |

Topic constants and JSON building: [nodes/gateway/main/bmt_thingsboard.c](../nodes/gateway/main/bmt_thingsboard.c)

Client init and event handler: [nodes/gateway/main/bmt_mqtt.c](../nodes/gateway/main/bmt_mqtt.c)

## Device model in ThingsBoard

Two device profiles are used. Both are created once in the TB UI; sub-devices are auto-provisioned by the gateway on first `connect`.

| Profile         | Type of device                  | Rule chain                              |
|-----------------|---------------------------------|------------------------------------------|
| `ble_tag`       | tags (`tag_0xNNNN`)             | zone detection (strongest RSSI + hysteresis) |
| `ble_mesh_node` | scan nodes and relay            | default rule chain (telemetry to storage) |
| default         | gateway itself (`gateway`)       | default                                  |

The gateway sets the profile through the `type` field in the `v1/gateway/connect` payload. The first time it publishes for a tag it also retries `set_role` three times with a 200 ms gap; TB can drop attributes if they arrive before the device is fully created.

<!-- EVIDENCE: TB UI showing the three profiles + one auto-created tag device -->

## TLS (MQTTS)

The gateway connects to `mqtts://<TB_IP>:8883` and verifies the server certificate using a locally embedded CA.

### Certificate files

Root folder [tls/](../tls/):

| File               | Purpose                             |
|--------------------|--------------------------------------|
| `ca.key`, `ca.pem` | self-signed CA (kept private)        |
| `server.key`, `server.pem` | server key + cert used by TB (SAN = `bmt-tb.local`) |
| `server.csr`, `server_ext.cnf` | CSR + config used to sign the server cert |
| `gen_certs.sh`     | script to regenerate all of the above |

The server cert is signed by the CA. The CA public part (`ca.pem`) is copied into the gateway project at [nodes/gateway/main/ca.pem](../nodes/gateway/main/ca.pem) and embedded into the firmware via `EMBED_TXTFILES` in [nodes/gateway/main/CMakeLists.txt](../nodes/gateway/main/CMakeLists.txt).

Symbols exposed to code:

```
extern const uint8_t bmt_ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t bmt_ca_pem_end[]   asm("_binary_ca_pem_end");
```

Used to build `esp_mqtt_client_config_t` in [nodes/gateway/main/bmt_mqtt.c](../nodes/gateway/main/bmt_mqtt.c).

### SAN and CN verification

TB is reached by IP address (`BMT_TB_IP`), but the server certificate has `bmt-tb.local` in its SubjectAltName. The MQTT client uses `broker.verification.common_name = BMT_TB_CN` to tell mbedTLS to verify against that name instead of the IP. If the IP changes, no cert change is needed. If the SAN changes, both the cert and `BMT_TB_CN` must move together.

### Regenerating certs

The script does everything in one go:

```bash
cd tls
./gen_certs.sh
```

It rebuilds `ca.*`, `server.*`, and updates the extension file. After running it, copy the new `ca.pem` into `nodes/gateway/main/ca.pem` and rebuild the gateway firmware.

<!-- EVIDENCE: TB server config panel showing SSL enabled + serial log of gateway successful TLS handshake -->

## Access token

The gateway authenticates with an access token, not user/password. Get it from the TB UI on the `bmt_gateway` device page (Credentials tab) and paste it into `BMT_TB_GATEWAY_TOKEN` in [nodes/gateway/main/bmt_config.h](../nodes/gateway/main/bmt_config.h). Tokens rotate with the device; if you delete and recreate the gateway device in TB, get a new token.

## Reconnect behavior

The MQTT client is configured with `reconnect_timeout_ms = 5000`. On disconnect it retries every 5 seconds forever. During disconnects, the gateway continues to receive mesh messages, but publishes are dropped in `bmt_mqtt_publish` (returns -1). Tag reports enqueued in the MQTT queue keep piling up, and the worker task drains them once the connection is back — subject to the `BMT_MQTT_QUEUE_SIZE` cap of 64 slots. Beyond that, new tag reports are dropped and counted (visible with UART `3`).
