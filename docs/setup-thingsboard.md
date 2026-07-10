# ThingsBoard setup

Self-host ThingsBoard CE with Docker instead of using the cloud.

## `thingsboard/` layout

- `docker-compose.yml` — ThingsBoard CE 3.7 and PostgreSQL.
- `rulechain/` — rule chain exports (`ble_tag_zone_detection.json` plus backups).
- `dashboard/indoor_tracking.json` — dashboard ready to import.
- `tls/` — CA and server certs (SAN `bmt-tb.local`).

## Steps

### 1. Install Docker Desktop

Download from `https://www.docker.com/products/docker-desktop`. Keep it running in the background.

### 2. Start ThingsBoard

```
cd thingsboard
docker compose up -d
```

Wait 1-2 minutes on the first run. Check status:

```
docker compose ps
```

### 3. Log in

Open `http://localhost:8080`. Log in with `tenant@thingsboard.org` / `tenant`.

### 4. Create two device profiles

Menu Device profiles > `+`. Names must match exactly:

- `ble_tag`
- `ble_mesh_node`

### 5. Create the gateway device and copy the token

Menu Devices > `+ Add device`. Name it `bmt_gateway`. Enable `Is gateway`. In the Credentials tab, copy the Access Token.

Paste it into `apps/gateway/main/bmt_config.h`:

```
#define BMT_TB_GATEWAY_TOKEN "<paste token here>"
```

### 6. Check the IP

`apps/gateway/main/bmt_config.h` defines `BMT_TB_IP`. Update it if the Docker host has a different IP.

### 7. Import the rule chain

Menu Rule chains > `+ Import` > pick `thingsboard/rulechain/ble_tag_zone_detection.json`.

Open the `ble_tag` profile and set the imported rule chain as its default.

In the "Apply hysteresis" node, edit `ZONE_MAP`: pair each scanner MAC with a room name. Read scanner MACs by sending UART `1` to each scanner.

### 8. Import the dashboard

Menu Dashboards > `+ Import dashboard` > pick `thingsboard/dashboard/indoor_tracking.json`.

Map the two Entity Aliases:

- `Tag Device` — filter by `Device profile = ble_tag`.
- `All Mesh Devices` — filter by `Device profile = ble_mesh_node`.

### 9. Rebuild the gateway

After you set the real token in step 5, rebuild and flash `apps/gateway`. It will connect over MQTTS on port 8883 and auto-register sub-devices with the right profile.

## New certs

```
cd thingsboard/tls
bash gen_certs.sh
cp ca.pem ../../apps/gateway/main/ca.pem
```

Rebuild the gateway. `EMBED_TXTFILES` bundles the new `ca.pem` into the firmware.

## Quick check

- Gateway serial log shows `MQTT connected to ThingsBoard`.
- ThingsBoard Devices tab shows `bmt_gateway` online, plus sub-devices (`bmt_node_0x...`, `bmt_tag_0x...`) as scanners, relays, and tags come online.
- The Indoor Tracking dashboard updates in real time.
