# Algorithms

The numeric tricks the firmware and the rule chain use. Each one has a code pointer so you can jump straight to the source.

## 1. Kalman filter for RSSI

**Where:** `apps/scanner/components/bmt_tag_table/bmt_tag_table.c`

Raw BLE RSSI is noisy. Two readings 100 ms apart can differ by 6-8 dBm even when the tag has not moved. A rolling average would work but reacts slowly.

We use a 1D Kalman filter with fixed constants:

```
process noise q = 0.1
measurement noise r = 2.0
```

Update rule per RSSI sample:

```
p = p + q
k = p / (p + r)
x = x + k * (rssi - x)
p = (1 - k) * p
```

`x` is the smoothed RSSI we send over mesh. `p` is the internal uncertainty. `k` is the Kalman gain that decides how much to trust the new sample versus the old estimate.

Tuning: raise `r` to smooth harder (slower to react). Raise `q` to trust new samples more (faster but noisier).

## 2. Distance from RSSI (log-distance path loss)

**Where:** `bmt_tag_table.c`, `calculate_distance()`

BLE ADV includes a `tx_power` field: the RSSI measured at 1 meter. If we know that reference and the current RSSI, we can guess how far the tag is:

```
distance = 10 ^ ((tx_power - rssi_filtered) / (10 * n))
```

`n` is the path-loss exponent. Free space is 2.0. Furnished indoor with walls is around 2.5 to 3.5. We use `BMT_PATH_LOSS_N = 2.5f`.

Values are noisy. Do not use this for anything more precise than "close" vs "across the room".

## 3. Anti-replay via sequence number

**Where:** `bmt_tag_table.c`, `bmt_tag_table_update()`

Each tag ADV packet carries a 1-byte `sequence` field that increments every 500 ms. Scanners keep the last sequence seen per tag.

Rules when a new packet arrives:

- Same sequence as before -> duplicate, count as extra reception, do not reset filter.
- Sequence went backward by 10 or more (`diff <= -10`) OR forward by more than 30 (`diff > BMT_MAX_SEQ_JUMP`) -> looks like tag reboot or replay attack, reset the Kalman filter and log a warning.
- Small backward (1..9) -> late packet from advertising overlap, drop silently.
- Forward but skipped some -> count them as `missed` for loss statistics.

The forward jump limit stops an attacker from capturing an old ADV and replaying it hours later.

## 4. HMAC-16 for beacon authentication

**Where:** `apps/tag/components/bmt_auth/bmt_auth.c` (build), `apps/scanner/components/bmt_auth/bmt_auth.c` (verify)

Tag ADVs include a 2-byte HMAC over the payload. The scanner rejects any ADV with a wrong HMAC. That stops a phone from posing as a tag.

Steps to compute:

1. Build the 24-byte ADV payload with `mac16` set to zero.
2. Compute `HMAC-SHA256(key, payload_without_mac16)`.
3. Take the first 2 bytes of the HMAC output. That is the 16-bit MAC.

Why only 2 bytes? An ADV field is small. A full 32-byte HMAC would not fit. 16 bits is weak against dedicated collision attacks but strong enough against a casual attacker. Combined with anti-replay (see above) it works well in practice.

We use two separate keys, not one:

- Tag key -- signs tag ADVs. Same value on every tag and every scanner (`BMT_TAG_HMAC_KEY[16]` in `bmt_auth.c`).
- OTA beacon key -- signs the gateway's OTA-trigger beacon. Rotates every 24h (see next section).

If one key leaks the other still works.

## 5. Key rotation for OTA beacon

**Where:** `apps/gateway/components/bmt_ota/bmt_ota.c`, `beacon_key_rotate_and_push()`

Every 24 hours the gateway:

1. Generates a fresh random 16-byte key via `esp_fill_random()`.
2. Imports it into PSA. If import fails, aborts and keeps the old key (see the rollback-safe design in the changelog).
3. Persists the new key to NVS.
4. Pushes it to every provisioned scanner via `OTA_KEY_PUSH` opcode over mesh.

Scanners receive the push in `bmt_auth_set_ota_beacon_key()`, import to their own PSA slot, and save to NVS so it survives their reboots.

An attacker who captures a valid OTA beacon and tries to replay it more than 24 hours later gets rejected because the scanner has already moved to a new key.

## 6. Hysteresis for zone assignment

**Where:** ThingsBoard rule chain node `Apply hysteresis` (`thingsboard/rulechain/ble_tag_zone_detection.json`)

Multiple scanners can hear the same tag. The strongest RSSI wins. But if two scanners are close in strength, tiny RSSI wobbles would flip the zone every few seconds. Hysteresis prevents that.

Rule: to switch zone, the new best scanner must beat the current zone's scanner by at least `HYSTERESIS_DBM = 8` dBm.

```
if best_scanner == current_zone_scanner:
    stay
elif (best_rssi - current_zone_rssi) >= HYSTERESIS_DBM:
    switch (subject to debounce, see next)
else:
    stay
```

Tuning: raise to 10 or 12 dBm if you see jitter. Lower to 5 if zones are large and switching feels too slow.

## 7. Leaky-bucket debounce

**Where:** same rule chain node

Even with hysteresis, one strong outlier RSSI can flip zones. To require sustained evidence, we count consecutive readings that all agree on the new zone.

```
DEBOUNCE_COUNT = 2

if new_zone == candidate_zone:
    candidate_count += 1
else:
    candidate_zone = new_zone
    candidate_count = 1

if candidate_count >= DEBOUNCE_COUNT:
    commit switch, reset candidate
```

A single-shot outlier only nudges the counter to 1, then the next report contradicts it and the counter drops. Only if two reports in a row point to the new zone does the switch actually commit.

The "leaky" part: if a report says "keep the current zone" the counter drops by 1 instead of resetting to 0. This tolerates one flap in a two-report window instead of forcing three consecutive agreements.

## 8. Fresh-sample window for zone eval

**Where:** rule chain, `SCANNER_VALID_MS = 10000`

The rule chain keeps the last RSSI from each scanner in server attributes. When evaluating zone, it only considers samples less than 10 seconds old.

Why: a scanner that stopped reporting (dead battery, moved out of range) should not keep influencing the zone. If all samples are stale, the tag is "out_of_range".

## 9. Out-of-range tag timeout

**Where:** `apps/gateway/components/bmt_thingsboard/bmt_thingsboard.c`, `zone_timeout_task`

The rule chain fires only when new telemetry arrives. If a tag disappears completely (dead battery), no more telemetry, no more rule chain runs, dashboard freezes on the last known zone.

The gateway runs a background task every 1 second. For each tracked tag it checks: has any telemetry arrived in the last `BMT_TAG_OUT_OF_RANGE_MS = 10000` ms? If not, it publishes `current_zone = "out_of_range"` to ThingsBoard directly.

## 10. OTA version comparison

**Where:** `apps/gateway/CMakeLists.txt` (build side), `bmt_ota.c` (compare side)

`PROJECT_VER` is computed at build time as the newest mtime of any source file, formatted `YYYYMMDDHHMMSS`. It ends up in the app descriptor of the built `.bin`.

At OTA time we compare two version strings with `strncmp`:

```
if (strncmp(new_desc.version, cur_desc->version, 14) <= 0)
    skip;   // server is not newer
else
    flash;
```

Because the format is fixed-width `YYYYMMDDHHMMSS`, lexicographic compare equals chronological compare. No date parsing required.

## 11. SHA256-skip to avoid pointless flashing

**Where:** same file

Every ESP-IDF `.bin` embeds a SHA256 of the ELF. If server's SHA256 equals node's SHA256, the binaries are identical. Flashing again would just wear the flash for no benefit.

```
if (memcmp(new_desc.app_elf_sha256, cur_desc->app_elf_sha256, 32) == 0)
    skip;
```

This runs before the version compare, so if you rebuild without changing source the OTA becomes a fast HTTP GET plus an instant abort.

## 12. Data watchdog

**Where:** `apps/gateway/components/bmt_watchdog/bmt_watchdog.c`

The watchdog protects against a subtle failure mode: mesh looks up (LEDs, no crashes) but no data is actually flowing.

- After boot: wait 15 seconds for the mesh to stabilize.
- Loop: take a snapshot of `s_mesh_received`, sleep 30 seconds, compare. If unchanged, the mesh is dead.
- Broadcast `RESET_CMD` five times with 1.5 second gap. Each successful send counts.
- If at least one send succeeded: wait 12 seconds for nodes to reset, wipe local state, reboot.
- If all five sends failed: retry the whole loop after 30 seconds (radio might have been busy).

The counter increments on `TAG_STATUS` and on ping ACKs, so "mesh alive" is not tied only to tag traffic. See `bmt_mesh.c:vnd_client_cb`.

## Where to tune

| Constant | File | Effect |
|---|---|---|
| `BMT_PATH_LOSS_N` | scanner `bmt_tag_table.h` | Distance formula slope. |
| Kalman `q`, `r` | scanner `bmt_tag_table.c` | Smoothing aggressiveness. |
| `BMT_MAX_SEQ_JUMP` | scanner `bmt_tag_table.h` | Anti-replay window. |
| `HYSTERESIS_DBM` | rule chain node `Apply hysteresis` | Zone switch guard. |
| `DEBOUNCE_COUNT` | same | Consecutive-agreement requirement. |
| `SCANNER_VALID_MS` | same | Fresh-sample cutoff. |
| `BMT_TAG_OUT_OF_RANGE_MS` | gateway `bmt_zone.h` | Server-side OOR timeout. |
| `BMT_WDG_TIMEOUT_MS` | gateway `bmt_watchdog.c` | Data watchdog window. |

Related runtime behavior is in [08-operation.md](08-operation.md). Protocol side of HMAC-16 keys is in [03-ble-mesh.md](03-ble-mesh.md).
