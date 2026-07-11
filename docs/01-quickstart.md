# Quickstart

Common flow first, then OS-specific notes. Assumes you have a workstation on the same LAN as the boards you flash.

## What you need

- ESP-IDF v6.0.1.
- Docker (Desktop on Windows, native on Linux).
- Python 3.
- Git.
- 1x ESP32-S3 (gateway) and 5x ESP32 (3 scanners, 1 relay, 1 tag).
- One USB-serial cable per board.

## 1. Clone

```
git clone https://github.com/caotrongphuoc/ble-mesh-tracking.git
cd ble-mesh-tracking
```

## 2. Install ESP-IDF

### Linux

```
mkdir -p ~/esp && cd ~/esp
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32,esp32s3
. ./export.sh
```

Add `. ~/esp/esp-idf/export.sh` to your shell rc to get `idf.py` in every session.

### Windows

Download the ESP-IDF Installer v6.0.1 from `https://dl.espressif.com/dl/esp-idf/` and run it. Pick a short path like `C:\esp`. The installer creates a Start Menu shortcut "ESP-IDF v6.0.1 CMD" that opens a shell with `idf.py` on PATH.

## 3. Start ThingsBoard

Install Docker first. Linux: `sudo apt install docker.io docker-compose-plugin`. Windows: Docker Desktop (needs WSL2 on first run).

Then:

```
cd thingsboard
docker compose up -d
```

Wait 1-2 minutes. Open `http://localhost:8080`, log in with `tenant@thingsboard.org` / `tenant`, change the password.

Device profiles, rule chain, dashboard, and gateway token: see [05-thingsboard-setup.md](05-thingsboard-setup.md). Do these before flashing.

## 4. Set firmware config

Edit `apps/*/components/bmt_config/bmt_config.h`:

| Define | Where | Value |
|---|---|---|
| `BMT_WIFI_SSID` / `BMT_WIFI_PASS` | gateway, scanner, relay | Your WiFi. |
| `BMT_TB_IP` | gateway | Docker host IP. |
| `BMT_TB_GATEWAY_TOKEN` | gateway | Token from step 3. |
| `BMT_OTA_*_URL` | gateway, scanner, relay | `http://<host-ip>:8080/<name>.bin`. |

If you regenerated TLS certs, copy `thingsboard/tls/ca.pem` over `apps/gateway/components/bmt_mqtt/ca.pem`.

## 5. Build and flash

Find the serial ports first: Linux `ls /dev/ttyUSB*`, Windows Device Manager under "Ports (COM & LPT)".

```
# Gateway needs erase-flash the first time (custom partition table)
cd apps/gateway && idf.py -p <port> erase-flash flash
cd apps/scanner && idf.py -p <port> flash
cd apps/relay   && idf.py -p <port> flash
cd apps/tag     && idf.py -p <port> flash
```

Each build copies its `.bin` into `firmware/`. Override with `idf.py -DBMT_OTA_DIR=/some/dir build`.

Linux permission denied on `/dev/ttyUSB*`: `sudo usermod -aG dialout $USER`, then log out and back in.

## 6. Run

1. Power up the tag, scanners, relay, and gateway. Order does not matter.
2. Gateway is in AUTO mode and provisions everything on its own.
3. Open the gateway serial monitor at 115200: `idf.py -p <port> monitor`. Press `1` to see the node table.
4. Open the Indoor Tracking dashboard in ThingsBoard.

Full command list: [09-uart-commands.md](09-uart-commands.md). Test procedures: [10-testing.md](10-testing.md).

## 7. OTA

```
cd firmware && python -m http.server 8080
```

On gateway UART: `u` starts OTA for scanners and relays, `g` for gateway self-update. Full procedure: [11-testing-ota.md](11-testing-ota.md).

## Troubleshooting

- **Gateway stuck at "MQTT connecting..."** — wrong `BMT_TB_IP` or `BMT_TB_GATEWAY_TOKEN`, or firewall blocks port 8883.
- **Scanners never provision** — check gateway is in AUTO mode (press `a`). Move the relay closer if scanners are far.
- **`idf.py flash` "port not found"** on Windows — reboot to pick up new USB drivers, or check Device Manager for a yellow triangle.
- **`docker compose up -d` fails** — on Linux, `sudo systemctl start docker`.
