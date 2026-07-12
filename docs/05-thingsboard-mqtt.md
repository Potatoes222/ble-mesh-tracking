# ThingsBoard, MQTT, and TLS

Only the gateway talks to ThingsBoard. It publishes telemetry for itself and every sub-device (scan nodes, relay, tags) using the ThingsBoard Gateway API, and subscribes to the RPC topic to receive OTA commands.

<hr>

## Contents

| Section | Topic |
|---|---|
| [I. Role of ThingsBoard](#i-role-of-thingsboard) | Dashboard, zone rule chain, OTA RPC trigger. |
| [II. Broker Setup](#ii-broker-setup) | Docker compose stack and exposed ports. |
| [III. MQTT Topics](#iii-mqtt-topics) | Publish and subscribe topics used by the gateway. |
| [IV. Device Model](#iv-device-model) | TB device profiles for tag, node, gateway. |
| [V. TLS (MQTTS)](#v-tls-mqtts) | Certificate files, embedding, CN verification. |
| [VI. Access Token](#vi-access-token) | Where the token comes from and where it goes. |
| [VII. Reconnect Behaviour](#vii-reconnect-behaviour) | Queueing during broker outages. |

### I. Role of ThingsBoard

ThingsBoard plays three roles.

#### 1. Dashboard

Live view of which tag is in which room, RSSI per scanner, and node health (temperature, VDD, uptime, free heap). Rendered from the telemetry the gateway pushes.

<!--
EVIDENCE: dashboard screenshot with the three widgets (map, RSSI chart, health)
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 1:</em></strong> ThingsBoard dashboard</p>
-->

#### 2. Zone Detection

From version 4 onward, the gateway sends only the raw `{scanner_id, rssi}` per tag. A ThingsBoard Rule Chain picks the strongest scanner and derives the `zone` attribute on the tag device. The gateway still runs a local copy of the same logic, but only for UART debug output (command `2`), not for publishing. This keeps the zone rules editable in the TB UI, without a firmware rebuild.

<!--
EVIDENCE: TB rule chain screenshot for zone detection
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 2:</em></strong> Zone detection rule chain</p>
-->

#### 3. OTA Trigger

A dashboard widget sends an RPC method call. The gateway maps it to an OTA action. See [ota.md](ota.md) for the full flow.

### II. Broker Setup

Root file: [docker-compose.yml](../docker-compose.yml)

Two containers: PostgreSQL for TB storage, and ThingsBoard CE 3.7.0. The following ports are mapped to the host.

| Port | Protocol | Purpose                          |
|------|----------|----------------------------------|
| 8080 | HTTP     | TB Web UI (`http://<host>:8080`) |
| 1883 | MQTT     | plaintext MQTT (test / fallback) |
| 8883 | MQTTS    | MQTT over TLS 1.2 (production)   |
| 7070 | Edge RPC | optional, not used by BMT        |

Start the stack:

```bash
docker compose up -d
```

Default login: `tenant@thingsboard.org` / `tenant`.

### III. MQTT Topics

The gateway uses the ThingsBoard Gateway MQTT API. All topics are relative to the connected client (the gateway).

| Direction | Topic                              | Payload example                                                     | Purpose                                     |
|-----------|------------------------------------|---------------------------------------------------------------------|---------------------------------------------|
| pub       | `v1/devices/me/telemetry`          | `{"status":"ONLINE"}`                                               | gateway self telemetry                      |
| pub       | `v1/devices/me/attributes`         | `{"role":"gateway"}`                                                | gateway self attributes                     |
| pub       | `v1/gateway/connect`               | `{"device":"scan_0x0002","type":"ble_mesh_node"}`                   | register sub-device                         |
| pub       | `v1/gateway/disconnect`            | `{"device":"scan_0x0002"}`                                          | remove sub-device                           |
| pub       | `v1/gateway/attributes`            | `{"scan_0x0002":{"role":"scan"}}`                                   | sub-device attributes                       |
| pub       | `v1/gateway/telemetry`             | `{"tag_0xAB01":[{"scanner_id":"scan_01","rssi":-58}]}`              | tag / node telemetry                        |
| sub       | `v1/devices/me/rpc/request/+`      | `{"method":"ota_scanner"}`                                          | OTA and other server-side calls             |

Sources:

- Topic constants and JSON building: [apps/gateway/components/bmt_thingsboard/bmt_thingsboard.c](../apps/gateway/components/bmt_thingsboard/bmt_thingsboard.c)
- Client init and event handler: [apps/gateway/components/bmt_mqtt/bmt_mqtt.c](../apps/gateway/components/bmt_mqtt/bmt_mqtt.c)

### IV. Device Model

Two device profiles are used. Both are created once in the TB UI; sub-devices are auto-provisioned by the gateway on first `connect`.

| Profile         | Type of device                | Rule chain                                       |
|-----------------|-------------------------------|--------------------------------------------------|
| `ble_tag`       | tags (`tag_0xNNNN`)           | zone detection (strongest RSSI + hysteresis)     |
| `ble_mesh_node` | scan nodes and relay          | default rule chain (telemetry to storage)        |
| default         | gateway itself (`gateway`)    | default                                          |

The gateway sets the profile through the `type` field in the `v1/gateway/connect` payload. The first time it publishes for a tag it also retries `set_role` three times with a 200 ms gap; TB can drop attributes if they arrive before the device is fully created.

<!--
EVIDENCE: TB UI showing the three profiles and one auto-created tag device
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 3:</em></strong> ThingsBoard device profiles</p>
-->

### V. TLS (MQTTS)

The gateway connects to `mqtts://<TB_IP>:8883` and verifies the server certificate using a locally embedded CA.

#### 1. Certificate Files

Root folder: [tls/](../tls/)

| File                             | Purpose                                                              |
|----------------------------------|----------------------------------------------------------------------|
| `ca.key`, `ca.pem`               | self-signed CA (keep the key private)                                |
| `server.key`, `server.pem`       | server key + cert used by TB (SAN = `bmt-tb.local`)                  |
| `server.csr`, `server_ext.cnf`   | CSR + config used to sign the server cert                            |
| `gen_certs.sh`                   | script to regenerate all of the above                                |

The server cert is signed by the CA. The CA public part (`ca.pem`) is copied into the gateway project at [apps/gateway/components/bmt_mqtt/ca.pem](../apps/gateway/components/bmt_mqtt/ca.pem) and embedded into the firmware via `EMBED_TXTFILES` in [apps/gateway/components/bmt_mqtt/CMakeLists.txt](../apps/gateway/components/bmt_mqtt/CMakeLists.txt).

Symbols exposed to code:

```text
extern const uint8_t bmt_ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t bmt_ca_pem_end[]   asm("_binary_ca_pem_end");
```

Used to build `esp_mqtt_client_config_t` in [apps/gateway/components/bmt_mqtt/bmt_mqtt.c](../apps/gateway/components/bmt_mqtt/bmt_mqtt.c).

#### 2. SAN vs IP Verification

TB is reached by IP address (`BMT_TB_IP`), but the server certificate has `bmt-tb.local` in its SubjectAltName.

The MQTT client uses `broker.verification.common_name = BMT_TB_CN` to tell mbedTLS to verify against that name instead of the IP. If the IP changes, no cert change is needed. If the SAN changes, both the cert and `BMT_TB_CN` must move together.

#### 3. Regenerating Certificates

The script does everything in one go:

```bash
cd tls
./gen_certs.sh
```

It rebuilds `ca.*`, `server.*`, and updates the extension file. After running it, copy the new `ca.pem` into `apps/gateway/components/bmt_mqtt/ca.pem` and rebuild the gateway firmware.

<!--
EVIDENCE: TB server config panel with SSL enabled + serial log of successful TLS handshake on the gateway
<table align="center">
  <tr>
    <td align="center">IMAGE_HERE</td>
  </tr>
</table>
<p align="center"><strong><em>Figure 4:</em></strong> TLS handshake log</p>
-->

### VI. Access Token

The gateway authenticates with an access token, not username / password. Get it from the TB UI on the `bmt_gateway` device page (Credentials tab) and paste it into `BMT_TB_GATEWAY_TOKEN` in [apps/gateway/components/bmt_config/bmt_config.h](../apps/gateway/components/bmt_config/bmt_config.h).

Tokens rotate with the device: if the gateway device is deleted and re-created in TB, get a new token.

### VII. Reconnect Behaviour

The MQTT client is configured with `reconnect_timeout_ms = 5000`. On disconnect it retries every 5 seconds forever.

During disconnects:

- The gateway continues to receive mesh messages.
- Publishes are dropped in `bmt_mqtt_publish` (returns `-1`).
- Tag reports enqueued in the MQTT queue keep piling up.
- The worker task drains them once the connection is back, subject to the `BMT_MQTT_QUEUE_SIZE` cap of 64 slots.

Beyond the cap, new tag reports are dropped and counted. The drop counter is visible on the gateway with UART command `3`.
