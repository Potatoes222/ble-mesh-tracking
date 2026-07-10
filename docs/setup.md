# Setup

## 0. Requirements

- ESP-IDF v6.0.1 (via the ESP-IDF Installation Manager or the VS Code extension).
- Docker Desktop (for ThingsBoard CE).
- Python 3 (for the OTA HTTP server).
- 1x ESP32-S3 (Gateway) and 5x ESP32 (3 scanners, 1 relay, 1 tag).

## 1. Start ThingsBoard

```
cd thingsboard
bash tls/gen_certs.sh    # optional: new dev certs
docker compose up -d
```

Open `http://<host-ip>:8080` and log in with `tenant@thingsboard.org` / `tenant`. Change the password. Full ThingsBoard steps are in [setup-thingsboard.md](setup-thingsboard.md).

## 2. Configure firmware

Edit `apps/*/main/bmt_config.h`:

| Define                            | Where                    | Value                          |
|-----------------------------------|--------------------------|--------------------------------|
| `BMT_WIFI_SSID` / `BMT_WIFI_PASS` | gateway, scanner, relay  | Your WiFi.                     |
| `BMT_TB_IP`                       | gateway                  | Docker host IP.                |
| `BMT_TB_GATEWAY_TOKEN`            | gateway                  | Access token from step 1.      |
| `BMT_OTA_*_URL`                   | gateway, scanner, relay  | `http://<host>:8080/<name>.bin`. |

If you generated new certs, copy `thingsboard/tls/ca.pem` over `apps/gateway/main/ca.pem`.

## 3. Build and flash

```
# Gateway needs erase on first flash (custom partition table)
cd apps/gateway && idf.py -p COM11 erase-flash flash

# Same firmware for all three scanners
cd apps/scanner && idf.py -p COM19 flash

cd apps/relay   && idf.py -p COM10 flash
cd apps/tag     && idf.py -p COM22 flash
```

After each build, the `.bin` is copied to `firmware/`. To copy elsewhere:

```
idf.py -DBMT_OTA_DIR=/some/other/dir build
```

## 4. Run

1. Power up the tag, scanners, relay, and gateway. Order does not matter.
2. The gateway is in AUTO mode. It finds and provisions every node on its own.
3. Open the gateway serial monitor at 115200 baud. Type `1` to see the node table.
4. Open the Indoor Tracking dashboard in ThingsBoard.

## 5. OTA

```
cd firmware && python -m http.server 8080
```

On the gateway UART: `u` triggers OTA for scanners and relays, `g` triggers OTA for the gateway. You can also send `ota_scanner`, `ota_relay`, or `ota_gateway` RPCs from ThingsBoard.

## 6. Failure tests

- Unplug the gateway and plug it back in. Keys and the node table load from NVS. Data resumes on its own.
- Unplug one scanner and plug it back in. The gateway re-provisions it.
- Unplug the relay. If a far scanner loses its path, the gateway watchdog resets and re-provisions the rest.
