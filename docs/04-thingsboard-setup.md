# ThingsBoard setup

Run ThingsBoard CE on your own machine with Docker.

## What is in `thingsboard/`

- `docker-compose.yml` — ThingsBoard CE 3.7 and PostgreSQL.
- `rulechain/` — rule chain exports (`ble_tag_zone_detection.json` plus backups).
- `dashboard/indoor_tracking.json` — dashboard, ready to import.
- `tls/` — CA and server certs (both CN and SAN = `bmt-tb.local`; firmware verifies CN).

## Steps

### 1. Install Docker Desktop

Download it from `https://www.docker.com/products/docker-desktop`. Keep it running.

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

### 4. Add two device profiles

Menu Device profiles > `+`. The names must match exactly:

- `ble_tag`
- `ble_mesh_node`

### 5. Add the gateway device and copy the token

Menu Devices > `+ Add device`. Name it `bmt_gateway`. Turn on `Is gateway`. In the Credentials tab, copy the Access Token.

Paste it into `apps/gateway/components/bmt_config/bmt_config.h`:

```
#define BMT_TB_GATEWAY_TOKEN "<paste token here>"
```

### 6. Set the IP

`apps/gateway/components/bmt_config/bmt_config.h` sets `BMT_TB_IP`. Change it if the Docker host is on a different IP.

### 7. Import the rule chain

Menu Rule chains > `+ Import` > pick `thingsboard/rulechain/ble_tag_zone_detection.json`.

Open the `ble_tag` profile. Set the new rule chain as its default.

In the "Apply hysteresis" node, edit `ZONE_MAP`. Pair each scanner MAC with a room name. Read the scanner MACs by pressing `1` on each scanner UART.

### 8. Import the dashboard

Menu Dashboards > `+ Import dashboard` > pick `thingsboard/dashboard/indoor_tracking.json`.

Map the two Entity Aliases:

- `Tag Device` — filter by `Device profile = ble_tag`.
- `All Mesh Devices` — include the `default`, `ble_tag`, and
  `ble_mesh_node` profiles so the entities table shows the gateway and every
  child device.

### 9. Rebuild the gateway

After you set the token in step 5, rebuild and flash `apps/gateway`. The gateway connects over MQTTS on port 8883 and adds sub-devices under the right profile.

## New certs

```
cd thingsboard/tls
bash gen_certs.sh
cp ca.pem ../../apps/gateway/components/bmt_mqtt/ca.pem
```

Rebuild the gateway. `EMBED_TXTFILES` bakes the new `ca.pem` into the firmware.

## Quick check

- Gateway serial log prints `MQTT connected to ThingsBoard`.
- ThingsBoard Devices tab shows `bmt_gateway` online. Sub-devices
  (`bmt_node_<12-hex-MAC>`, `bmt_tag_0x<4-hex-ID>`) show up as scanners,
  relays, and tags come online.
- The Indoor Tracking dashboard updates in real time.
