# Testing guide

How to verify the system actually works after a fresh flash. OTA has its own guide: [11-testing-ota.md](11-testing-ota.md).

The tests here are manual, mostly by watching serial logs and the ThingsBoard dashboard. There is no automated test suite.

## Setup for testing

Open serial monitors on every board at 115200 baud:

```
# In one terminal per board
cd apps/gateway && idf.py -p /dev/ttyUSB0 monitor
cd apps/scanner && idf.py -p /dev/ttyUSB1 monitor
cd apps/relay   && idf.py -p /dev/ttyUSB2 monitor
cd apps/tag     && idf.py -p /dev/ttyUSB3 monitor
```

Also open the ThingsBoard UI on `http://<host-ip>:8080` and go to the Indoor Tracking dashboard.

Press Ctrl+] to exit any `idf.py monitor` session.

## Test 1: Bring-up (fresh flash)

**Goal:** confirm the gateway can provision every node and the tag data reaches ThingsBoard.

Steps:

1. Erase and flash the gateway. Power it on and watch the serial log.
   - Expect: `NVS init OK`, `NetKey added`, `AppKey added`, `Mesh init OK`, `MQTTS -> mqtts://...`, `MQTT connected to ThingsBoard`.
2. Power on the relay. Its log shows `Waiting provision...`.
3. On the gateway, expect within a few seconds: `Found unprovisioned [RELAY]`, then `Provision complete addr=0x00xx type=RELAY`, then `[CFG] APP_KEY_ADD ACK from 0x00xx (err=0)`, then `[CFG] MODEL_APP_BIND ACK from 0x00xx (err=0)`, then `[RLY_CFG] Relay 0x00xx fully configured`.
4. Power on the scanners one by one. Same flow but with `[SCAN]` prefix and `SCN_CFG`.
5. Power on the tag. Its log shows `ADV OK seq=0` and increments every 500 ms.
6. On any scanner, watch for `Tag 0x0001 | PERSON | RSSI=-52dBm ...` lines.
7. On the gateway, watch for `[VND] src=0x00xx MAC=... tag=0x0001 rssi=-52 (mesh_recv=1)`.
8. Press `1` on gateway UART. All nodes should show `ACTIVE` (scanner) or `ONLINE` (relay).
9. Open ThingsBoard dashboard. Look for a device named `bmt_gateway` online, plus sub-devices for each node and each tag.

If any step fails, do not skip ahead. Fix it before continuing.

## Test 2: End-to-end data flow with one tag

**Goal:** confirm every layer is passing data.

Put the tag ~1 meter from scanner 1. Watch the four logs and the dashboard at the same time.

- **Tag log:** sequence increments every 500 ms.
- **Scanner 1 log:** `Tag 0x0001 | ... RSSI=-4x dBm | Filt=-4x.x` every ~1 second.
- **Gateway log:** `[VND] src=0x0002 MAC=... tag=0x0001 rssi=-4x` and `[BMT_TB] TB [bmt_tag_0001] scanner=... rssi=-4x`.
- **Dashboard:** the tag icon shows up in the room mapped to scanner 1. `current_rssi` matches the last log value.

If the tag log increments but the scanner does not print a Tag line, the HMAC verify might be failing. Check that `BMT_TAG_HMAC_KEY` in `apps/tag/components/bmt_auth/bmt_auth.c` and `apps/scanner/components/bmt_auth/bmt_auth.c` are byte-identical.

## Test 3: Walking test (zone hysteresis)

**Goal:** confirm hysteresis and debounce actually prevent zone flapping.

1. Stand next to scanner 1 with the tag. Wait for the dashboard to show room 1.
2. Walk slowly toward scanner 2. As you cross the midpoint, the RSSI to both scanners becomes similar.
3. Expect: the dashboard does not flip zones every second. It waits until you are clearly closer to scanner 2 (RSSI difference >= 8 dBm, held for 2 consecutive telemetry frames), then commits the switch.

If the zone flaps back and forth: raise `HYSTERESIS_DBM` from 8 to 10 or 12 in the rule chain node "Apply hysteresis". See [04-algorithms.md](04-algorithms.md#6-hysteresis-for-zone-assignment).

## Test 4: Out-of-range

**Goal:** confirm the timeout task publishes `out_of_range` when a tag disappears.

1. With one tag reporting normally, remove its battery.
2. Wait 10-15 seconds.
3. Gateway log expects `[BMT_TB] Tag 0x0001 OUT OF RANGE`.
4. Dashboard: the tag's `current_zone` attribute becomes `out_of_range`.

Rule chain also handles OOR (see [04-algorithms.md](04-algorithms.md#8-fresh-sample-window-for-zone-eval)), but the gateway task is the safety net when telemetry stops completely.

## Test 5: Node reboot (self-heal)

**Goal:** confirm a node that reboots rejoins the mesh without manual re-provision.

1. Unplug one scanner. Wait 5 seconds. Plug it back in.
2. Scanner boots. Log shows `Already provisioned (restored from NVS)`. Within a few seconds it should start sending Tag reports again.
3. If instead the scanner's log shows `Waiting provision...`, its NVS was cleared (perhaps a hardware brownout). The gateway will detect the unprovisioned beacon and re-provision it. This takes 10-20 seconds.
4. Gateway log expects one of:
   - `[VND] src=0x00xx ...` from the recovered scanner (no re-prov needed), or
   - `Node 0x00xx (Scan_0x...) beacon unprovisioned tro lai...` followed by a fresh provision + config cycle.

Repeat for the relay.

## Test 6: Gateway reboot (persistence)

**Goal:** confirm the gateway restores its state from NVS after power loss.

1. Unplug the gateway (yank the cable, not a soft reset).
2. Wait 5 seconds. Plug back in.
3. Watch the serial log. Expect: `NVS init OK`, `Node table loaded (N nodes)`, `NetKey already exists in stack (SETTINGS restore/auto-create)`, `AppKey already exists in stack`.
4. Within 15-30 seconds telemetry should resume: scanners send TAG_STATUS, gateway forwards to ThingsBoard.
5. Press `1` on gateway UART. Node table should show every previously provisioned node, with `Config done: YES`.

If instead the log says `NetKey added` (fresh), something wiped NVS. Confirm `CONFIG_BLE_MESH_SETTINGS=y` in `sdkconfig`.

## Test 7: Relay removed (mesh path failure)

**Goal:** confirm the watchdog triggers a full mesh reset when data stops flowing.

1. Place one scanner far from the gateway, needing the relay to reach.
2. Confirm normal telemetry from that scanner.
3. Unplug the relay.
4. Wait 30-40 seconds.
5. Gateway log expects: `No mesh data in 30s -- starting reset cycle`, then five `RESET_CMD` broadcasts, then `Gateway FULL RESET -- wiping all mesh state...`, then reboot.
6. After the gateway reboots, plug the relay back in. Everything re-provisions from scratch. Takes about a minute for all nodes to come back.

Note: if the far scanner was still reachable directly (short indoor distances), the watchdog will NOT fire. That is the correct behavior. It only fires when telemetry actually stops.

## Test 8: Multiple tags

**Goal:** confirm the system scales to more than one tag.

1. Flash 3-5 tags with different `BMT_TAG_MINOR` values.
2. Power them all up in the same room.
3. Every tag should show up in the scanner log with different `tag_id`.
4. Every tag should appear on the dashboard.
5. Scanner tag table (press `1` on scanner UART) should list all of them.

Scanner keeps up to `BMT_MAX_TAGS = 20` simultaneously.

## Test 9: OTA smoke test

Full OTA test is in [11-testing-ota.md](11-testing-ota.md). For a quick smoke test:

1. Start the HTTP server: `cd firmware && python -m http.server 8080`.
2. On gateway UART, press `u`.
3. Every scanner and relay should download and reboot within about 90 seconds each (sequential).
4. Confirm the gateway sees `[OTA] ===== Node 0x00xx (...) OTA THANH CONG =====` for each.

## What NOT to test manually

The following are covered by design and do not need manual verification:

- **HMAC-16 rejection of forged tag ADVs.** The scanner already rejects packets it cannot verify. If you want to check, use `nRF Connect` on a phone to broadcast the same manufacturer data but with a wrong MAC field. The scanner will not log the tag.
- **NVS erase on `NO_FREE_PAGES`.** Very rare unless you flash dozens of times without erase.
- **Kalman filter output.** Trust the math. If RSSI looks noisy in the log's `Filt=` column, raise `r` in `bmt_tag_table.c`.

## Regression baseline

Before any release run at minimum:

- Test 1 (bring-up)
- Test 3 (walking)
- Test 6 (gateway persistence)
- Test 9 smoke (OTA)

That covers the paths most likely to regress across firmware changes.
