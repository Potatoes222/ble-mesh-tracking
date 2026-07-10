# Setup

## 0. What you need

- ESP-IDF v6.0.1 (via the ESP-IDF Installation Manager or the VS Code extension).
- Docker Desktop for ThingsBoard.
- Python 3 for the OTA HTTP server.
- 1x ESP32-S3 for the gateway, 5x ESP32 for 3 scanners, 1 relay, 1 tag.

## 1. Start ThingsBoard

```
cd thingsboard
bash tls/gen_certs.sh    # optional: fresh dev certs
docker compose up -d
```

Open `http://<host-ip>:8080`. Log in with `tenant@thingsboard.org` / `tenant`. Change the password. Full ThingsBoard steps are in [setup-thingsboard.md](setup-thingsboard.md).

## 2. Set firmware config

Edit `apps/*/components/bmt_config/bmt_config.h`:

| Define | Where | Value |
|---|---|---|
| `BMT_WIFI_SSID` / `BMT_WIFI_PASS` | gateway, scanner, relay | Your WiFi. |
| `BMT_TB_IP` | gateway | Docker host IP. |
| `BMT_TB_GATEWAY_TOKEN` | gateway | Access token from step 1. |
| `BMT_OTA_*_URL` | gateway, scanner, relay | `http://<host>:8080/<name>.bin`. |

If you made new certs, copy `thingsboard/tls/ca.pem` over `apps/gateway/components/bmt_mqtt/ca.pem`.

## 3. Build and flash

```
# Gateway needs erase on first flash (custom partition table)
cd apps/gateway && idf.py -p COM11 erase-flash flash

# Same firmware for all three scanners
cd apps/scanner && idf.py -p COM19 flash

cd apps/relay   && idf.py -p COM10 flash
cd apps/tag     && idf.py -p COM22 flash
```

Each build copies its `.bin` to `firmware/`. To send it somewhere else:

```
idf.py -DBMT_OTA_DIR=/some/dir build
```

## 4. Run

1. Power up the tag, scanners, relay, and gateway. Any order works.
2. The gateway is in AUTO mode. It finds and provisions every node.
3. Open the gateway serial monitor at 115200 baud. Press `1` to see the node table.
4. Open the Indoor Tracking dashboard in ThingsBoard.

## 5. OTA

```
cd firmware && python -m http.server 8080
```

On the gateway UART, press `u` to OTA scanners and relays. Press `g` to OTA the gateway. You can also send `ota_scanner`, `ota_relay`, or `ota_gateway` as RPCs from ThingsBoard.

## 6. Failure tests

- Unplug the gateway and plug it back in. Keys and the node table come back from NVS. Data starts flowing again.
- Unplug one scanner and plug it back in. The gateway re-provisions it.
- Unplug the relay. If a far scanner loses its path, the gateway watchdog resets the mesh and re-provisions the rest.
