# Testing guide

Manual verification after a fresh flash. OTA test is separate: [11-testing-ota.md](11-testing-ota.md).

Open serial monitors on every board (`idf.py -p <port> monitor`, 115200 baud, Ctrl+] to exit) and the Indoor Tracking dashboard.

## Test 1: Bring-up (fresh flash)

Goal: gateway provisions every node, telemetry reaches ThingsBoard.

1. Erase and flash the gateway. Boot log ends with `MQTT connected to ThingsBoard`.
2. Power on the relay. Gateway log: `Found unprovisioned [RELAY]` -> `Provision complete addr=0x00xx` -> `[CFG] APP_KEY_ADD ACK` -> `[CFG] MODEL_APP_BIND ACK` -> `[RLY_CFG] Relay 0x00xx fully configured`.
3. Same for each scanner (`[SCAN]` prefix).
4. Power on the tag. Its log: `ADV OK seq=0` and increments every 500 ms.
5. Scanner log shows `Tag 0x0001 | PERSON | RSSI=...`.
6. Gateway log shows `[VND] src=0x00xx MAC=... tag=0x0001 rssi=...`.
7. Press `1` on gateway UART: all nodes `ACTIVE` (scanner) or `ONLINE` (relay).
8. Dashboard: `bmt_gateway` online, sub-devices for each node and each tag.

If any step fails, fix it before moving on.

## Test 2: End-to-end with one tag

Place tag ~1 m from scanner 1. In sync across logs and dashboard:

- Tag: sequence increments.
- Scanner 1: `Tag 0x0001 | ... RSSI=-4x | Filt=-4x.x` every ~1 s.
- Gateway: `[VND] src=0x0002 ... tag=0x0001 rssi=-4x`.
- Dashboard: tag shows in the room mapped to scanner 1.

If tag increments but scanner does not log Tag, HMAC verify likely failed. Check `BMT_TAG_HMAC_KEY` matches byte-for-byte in tag AND scanner `bmt_auth.c`.

## Test 3: Walking test (hysteresis)

Goal: hysteresis and debounce prevent zone flapping.

1. Stand next to scanner 1. Wait for dashboard = room 1.
2. Walk slowly toward scanner 2. RSSIs get close at midpoint.
3. Expect: no per-second zone flip. Switch commits only when new zone beats current by >=8 dBm for 2 telemetry frames in a row.

If it flaps: raise `HYSTERESIS_DBM` (see [04-algorithms.md](04-algorithms.md)).

## Test 4: Out-of-range

1. Tag reporting normally. Remove its battery.
2. Wait 10-15 s.
3. Gateway: `[BMT_TB] Tag 0x0001 OUT OF RANGE`.
4. Dashboard: `current_zone` becomes `out_of_range`.

## Test 5: Node reboot (self-heal)

1. Unplug one scanner for 5 s. Plug back in.
2. Log shows `Already provisioned (restored from NVS)` and traffic resumes within seconds.
3. If NVS was cleared (rare brownout), log shows `Waiting provision...` and gateway re-provisions it (10-20 s).

Same test for the relay.

## Test 6: Gateway reboot (persistence)

1. Yank gateway power for 5 s. Plug back in.
2. Boot log: `Node table loaded (N nodes)`, `NetKey already exists`, `AppKey already exists`.
3. Telemetry resumes within 15-30 s.
4. Press `1` — every previously provisioned node shows with `Config done: YES`.

If log says `NetKey added` (fresh), something wiped NVS. Confirm `CONFIG_BLE_MESH_SETTINGS=y` in `sdkconfig`.

## Test 7: Relay removed (watchdog triggers reset)

Only fires when telemetry actually stops. Do this only if you have a scanner needing the relay path.

1. Confirm normal telemetry via the relay.
2. Unplug the relay.
3. Wait 30-40 s.
4. Gateway: `No mesh data in 30s -- starting reset cycle`, then 5 `RESET_CMD` broadcasts, then `Gateway FULL RESET -- wiping all mesh state`, then reboot.
5. Plug relay back in. Full re-provision cycle takes about a minute.

## Test 8: Multiple tags

1. Flash 3-5 tags with different `BMT_TAG_MINOR`.
2. Every tag shows up in scanner log with different `tag_id`.
3. Every tag on dashboard.
4. Scanner `1` command lists all of them.

Scanner tracks up to `BMT_MAX_TAGS = 20`.

## Test 9: OTA smoke

Full procedure: [11-testing-ota.md](11-testing-ota.md). Quick smoke:

1. `cd firmware && python -m http.server 8080`.
2. On gateway UART: `u`.
3. Scanner and relay reboot within about 90 s each.
4. Gateway logs `Node 0x00xx (...) OTA THANH CONG` for each.

## Regression baseline

Before any release run at minimum: Test 1, Test 3, Test 6, Test 9. Covers the most likely regressions.
