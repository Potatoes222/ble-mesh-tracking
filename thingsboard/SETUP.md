# ThingsBoard CE setup for BMT (self-hosted, MQTTS)

Local README for the `thingsboard/` directory. Full step-by-step guide with troubleshooting is in [../docs/04-thingsboard-setup.md](../docs/04-thingsboard-setup.md).

## Folder contents

```
docker-compose.yml  — ThingsBoard CE 3.7 + PostgreSQL
dashboard/          — dashboard export, ready to import (6 widgets, includes OTA panel)
rulechain/          — rule chain exports (zone hysteresis + OTA attribute persist)
tls/                — CA + server cert (dev bundle, SAN = bmt-tb.local)
```

## Manual UI steps

### 1. Install Docker

Docker Desktop on Windows / macOS. Native `docker` + `docker-compose-plugin` on Linux.

### 2. Start ThingsBoard

```
cd thingsboard
docker compose up -d
```

Wait 1-2 minutes on the first run (database init). Check with `docker compose ps`.

### 3. Log in

Open `http://localhost:8080`, log in as `tenant@thingsboard.org` / `tenant`, change the password.

### 4. Create two device profiles

Menu Device profiles > `+`. Names must match exactly:

- `ble_tag`
- `ble_mesh_node`

### 5. Import rule chains

Menu Rule chains > `+ Import`:

- `rulechain/ble_tag_zone_detection.json` — set as default rule chain for the `ble_tag` profile.
- `rulechain/ble_mesh_node.json` — set as default rule chain for the `ble_mesh_node` profile (persists `ota_last_result` and `ota_last_time` on each node).

### 6. Create the gateway device and copy the token

Menu Devices > `+ Add device`. Name it `bmt_gateway`, enable `Is gateway`. In the Credentials tab, copy the Access Token.

Paste it into `apps/gateway/components/bmt_config/bmt_config.h`:

```c
#define BMT_TB_GATEWAY_TOKEN "<paste token here>"
```

Also update `BMT_TB_IP` in the same file to the IP of the machine running Docker.

### 7. Import the dashboard

Menu Dashboards > `+ Import dashboard` > pick `dashboard/indoor_tracking.json`.

Map the entity aliases:

- `Tag Device` — filter by `Device profile = ble_tag`.
- `All Mesh Devices` — filter by `Device profile` includes `ble_tag` and `ble_mesh_node`.
- `Mesh Nodes` — filter by `Device profile = ble_mesh_node` (used by the OTA table).

### 8. Rebuild and flash the gateway

After step 6, rebuild `apps/gateway` and flash. The gateway connects over MQTTS on port 8883 and auto-registers sub-devices under the right profile.

## Regenerate certs

```
cd tls
bash gen_certs.sh
cp ca.pem ../../apps/gateway/components/bmt_mqtt/ca.pem
```

Rebuild the gateway. `EMBED_TXTFILES` bundles the new `ca.pem` into the firmware.

## Quick check after everything is up

- Gateway serial log prints `MQTTS -> mqtts://<ip>:8883 (verify CN=bmt-tb.local)` then `MQTT connected to ThingsBoard`. TLS handshake errors show up here as mbedTLS messages.
- TB UI Devices tab shows `bmt_gateway` online. Sub-devices (`bmt_node_0x...`, `bmt_tag_0x...`) appear as scanners, relays, and tags come online.
- Indoor Tracking dashboard updates in real time. The OTA Status table lists each mesh node with online state and last OTA result.
