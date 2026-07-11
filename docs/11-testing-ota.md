# OTA testing

Verify each OTA path end to end. Assumes bring-up already works (see [10-testing.md](10-testing.md)).

The system has four OTA-related things to test independently:

1. HTTP OTA server serves the correct file.
2. Version compare prevents downgrade.
3. SHA256 check skips identical binaries.
4. Beacon HMAC key rotation works and rejects forgeries.

## Setup

Serve the built binaries:

```
cd firmware
python -m http.server 8080
```

Confirm the URLs in `bmt_config.h` match. From another machine on the LAN:

```
curl -s -o /dev/null -w "%{http_code}\n" http://<host-ip>:8080/Scanner.bin
```

Should print `200`. If `000` or timeout, fix networking or firewall first (see [07-http-tls.md](07-http-tls.md#firewall-notes)).

Open serial monitors on every board.

## Test 1: Gateway self-OTA

**Trigger:** press `g` on gateway UART.

Expected log:

```
[OTA] ===== Gateway self-update =====
[OTA] URL: http://192.168.x.x:8080/Gateway.bin
[OTA] Node   SHA256: <hash-of-current>
[OTA] Server SHA256: <hash-of-server>
```

Then one of three paths.

- **Same SHA256:** `SHA256 match -- node firmware is IDENTICAL to server firmware, skip.` Task exits, no reboot.
- **Same version, different SHA256:** `Server version is NOT newer -- skip, no downgrade.` Task exits.
- **Newer version:** `Server version is NEWER -> flashing firmware...`, then `OTA SUCCESS -- rebooting`, then the gateway reboots into the new firmware.

Verify the gateway came up on the new build: `esp_app_get_description()` prints the version. In the boot log look for the `PROJECT_VER` string that matches what the build produced.

## Test 2: Scanner OTA (broadcast beacon path)

**Trigger:** press `u` on gateway UART.

Gateway path:

```
[OTA] Found 3 SCANNER node(s)
[OTA] Broadcasting NimBLE beacon (15s) -- all 3 scanners OTA simultaneously
[OTA] NimBLE beacon broadcasting (target=0x01, mac16=0x????, 15s)...
```

Every scanner in range should within a few seconds print:

```
[OTA] BLE beacon from Gateway (HMAC OK) -- triggering WiFi OTA!
[OTA] ===== WiFi OTA triggered =====
[OTA] URL: http://192.168.x.x:8080/Scanner.bin
```

If the gateway's beacon HMAC does NOT match what the scanner expects:

```
[OTA] Beacon HMAC mismatch (got 0x???, expect 0x???) -- ignoring, co the la gia mao
```

This is normal after boot before the gateway has pushed its rotated key -- see Test 5.

## Test 3: Relay OTA (unicast mesh path)

**Trigger:** press `u` on gateway UART.

After the scanner beacon window closes the gateway moves to relays:

```
[OTA] -- RELAY 1/1: 0x00xx (Relay_0x00xx) --
[OTA] TRIGGER -> 0x00xx [1/5]: sent
[OTA] TRIGGER -> 0x00xx [2/5]: sent
...
```

The relay receives OTA_TRIGGER over unicast mesh:

```
[OTA] OTA_TRIGGER from 0x0001 -- starting WiFi OTA
```

Then the same OTA flow as scanner (URL, SHA256 check, version compare, flash-or-skip).

Gateway waits `BMT_OTA_NODE_GAP_MS = 90000` ms per relay to allow download + reboot.

## Test 4: OTA result reporting

**Goal:** confirm each node reports back after reboot.

After a successful OTA and reboot, the node's `report_pending_task` (started in `bmt_scan_core.c` / `main.c` relay) fires 5 seconds after boot. It checks NVS for the "pending" flag set by `mark_pending()` before reboot, clears it, and sends `OTA_RESULT` over mesh.

Gateway expects to see:

```
[OTA] ===== Node 0x00xx (Scan_0x00xx) OTA THANH CONG =====
```

If the node's OTA failed, its `ota_wifi_task` sends `OTA_RESULT` with status = 1 in the fail path, and the gateway logs `OTA THAT BAI (status=1)`.

Both results also publish to ThingsBoard as `ota_result: SUCCESS` or `FAILED` attribute on the node's sub-device.

## Test 5: Key rotation

**Goal:** confirm the gateway rotates the OTA beacon HMAC key every 24 h and pushes it to scanners.

The natural test is to wait 24 h. Instead, force an immediate rotation:

1. Temporarily set `BMT_OTA_KEY_ROTATE_INTERVAL_MS` in `apps/gateway/components/bmt_ota/bmt_ota.c` to something like `60000` (60 s). Rebuild and flash the gateway.
2. Wait 60 s after boot.
3. Gateway log: `[SECURITY] OTA-beacon key ROTATED (key_id=...) -- pushing to all scanners...` followed by `Key rotate: pushed to 3 scanner(s)`.
4. Each scanner log: `[SECURITY] OTA_KEY_PUSH nhan tu 0x0001 -- rotate key beacon` followed by `[SECURITY] OTA-beacon key ROTATED`.
5. Trigger OTA (press `u`). Scanners should accept the new beacon.
6. Restore the interval to 24 h and reflash.

If a scanner does NOT receive the push (packet loss), its next boot will still load the old key from NVS. On the next real OTA trigger the scanner will reject the beacon. Push the key again from UART (currently no dedicated command; the next 24 h rotate covers it).

## Test 6: Downgrade protection

**Goal:** confirm the version compare stops us from flashing an older build over a newer one.

1. Note the current firmware version on the gateway. Press `1` on UART or look at the boot banner.
2. Build a fresh firmware (touch any `.c` file). `PROJECT_VER` bumps to now.
3. Copy the new `.bin` to `firmware/`.
4. Press `g`. Gateway sees the new version is newer, flashes it.
5. Now revert: replace `firmware/Gateway.bin` with an older copy (rename the previously saved one).
6. Press `g` again. Expect: `Server version is NOT newer -- skip, no downgrade.`

If it does flash the older `.bin`, someone bypassed the version check (do not do that).

## Test 7: SHA256 skip

**Goal:** confirm running the same OTA twice does not actually reflash.

1. Trigger a successful OTA. Gateway reboots.
2. Immediately trigger the same OTA again (`g`).
3. Expect: `SHA256 match -- node firmware is IDENTICAL to server firmware, skip.`
4. Gateway does NOT reboot.

This test proves the check-then-skip logic. Without it, an OTA auto-check every 3 minutes would burn flash cycles for nothing.

## Test 8: Fault injection

Provoke known failure modes and check the code handles them.

### 8a. HTTP 404

Delete `firmware/Scanner.bin` (or rename it). Trigger scanner OTA.

- Scanner log expects: `[OTA] esp_https_ota_begin FAILED: ESP_ERR_HTTP_CONNECT` or similar.
- Task exits, sends OTA_RESULT status=1.
- Scanner returns to BLE scan mode without a reboot.

Restore the file.

### 8b. WiFi credentials wrong

Temporarily edit `BMT_WIFI_PASS` in the scanner's `bmt_config.h` to an invalid password. Reflash the scanner.

- Trigger OTA on gateway.
- Scanner log expects: `[OTA] WiFi connect timeout` after `BMT_OTA_WIFI_TIMEOUT_MS = 30000` ms.
- Task exits, sends OTA_RESULT status=1.

Fix the password, reflash.

### 8c. Gateway trigger during ongoing OTA

Trigger `u` (scanner OTA). While it is still running, press `u` again.

Expected on gateway:

```
[RPC] OTA already running   -- OR --
OTA already running
```

The atomic CAS on `s_running` (see [04-algorithms.md](04-algorithms.md)) prevents the second call.

### 8d. RPC trigger from ThingsBoard

In ThingsBoard UI, on the `bmt_gateway` device, send RPC:

```
{"method": "ota_scanner", "params": {}}
```

Gateway log: `[RPC] Received: {"method":"ota_scanner",...}` then `[RPC] OTA Scanner triggered`.

Same for `ota_relay` and `ota_gateway`.

If the RPC does nothing, check that the topic filter matches (`v1/devices/me/rpc/request/+`) and that MQTTS is connected.

## What NOT to test

- **Corrupted `.bin` mid-download.** `esp_https_ota_finish()` calls a partition verify. A corrupted download fails the SHA256 check inside `esp_https_ota`, aborts, and rolls back. This is IDF library code, tested by Espressif.
- **Downgrade of the SPI flash contents.** OTA writes to the inactive slot only. The current firmware keeps running until the reboot commits the new slot. If OTA fails mid-flash, the bootloader falls back to the previous slot.
- **Manually setting the OTA data partition.** Do not.

## Fast smoke test

If you only have 5 minutes to verify OTA still works after a code change:

1. `cd firmware && python -m http.server 8080`
2. Rebuild the gateway (`idf.py build`).
3. Copy `apps/gateway/build/Gateway.bin` to `firmware/Gateway.bin` (the build step normally does this via `BMT_OTA_DIR`).
4. On gateway UART, `g`.
5. Expect version compare says NOT newer (because the running binary was just flashed too). Or expect flash + reboot if the running binary is older.

That is enough to confirm the OTA path itself is not broken.
