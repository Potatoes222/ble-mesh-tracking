# Quickstart

Get the whole system running on a clean machine. Common flow first, then OS-specific notes.

## What you need

- 1x ESP32-S3 (for Gateway) and 5x ESP32 (for 3 scanners, 1 relay, 1 tag).
- A USB-to-Serial cable per board.
- ESP-IDF v6.0.1.
- Docker (Desktop on Windows, native on Linux).
- Python 3 for the OTA HTTP server.
- Git.

## 1. Clone the repo

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

Add `. ~/esp/esp-idf/export.sh` to your shell rc file if you want it in every session.

### Windows

Use the official ESP-IDF Installer:

1. Download from `https://dl.espressif.com/dl/esp-idf/`, pick the v6.0.1 offline installer.
2. Run it. Pick a short install path like `C:\esp` to avoid long-path issues.
3. It creates a shortcut "ESP-IDF v6.0.1 CMD" on the Start menu. Open that shortcut to get a shell with `idf.py` on PATH.

VS Code users: install the "Espressif IDF" extension. Point it at the ESP-IDF folder the installer set up.

## 3. Start ThingsBoard

### Linux

```
sudo apt install docker.io docker-compose-plugin
sudo systemctl start docker
cd thingsboard
docker compose up -d
```

### Windows

1. Install Docker Desktop from `https://www.docker.com/products/docker-desktop`.
2. Start Docker Desktop and let it finish setting up WSL2 if it asks.
3. In PowerShell:

```
cd thingsboard
docker compose up -d
```

### Both

Wait 1-2 minutes for ThingsBoard to boot. Check status:

```
docker compose ps
```

Open `http://localhost:8080`. Log in with `tenant@thingsboard.org` / `tenant`. Change the password.

Full ThingsBoard steps (device profiles, rule chain, dashboard import, token copy) are in [05-thingsboard-setup.md](05-thingsboard-setup.md). Do those before flashing the gateway.

## 4. Set firmware config

Edit `apps/*/components/bmt_config/bmt_config.h`:

| Define | In which app | Value |
|---|---|---|
| `BMT_WIFI_SSID` / `BMT_WIFI_PASS` | gateway, scanner, relay | Your WiFi credentials. |
| `BMT_TB_IP` | gateway | Docker host IP. |
| `BMT_TB_GATEWAY_TOKEN` | gateway | Access token from ThingsBoard setup. |
| `BMT_OTA_*_URL` | gateway, scanner, relay | `http://<host-ip>:8080/<name>.bin`. |

If you generated new TLS certs, copy `thingsboard/tls/ca.pem` over `apps/gateway/components/bmt_mqtt/ca.pem`.

## 5. Build and flash

### Linux

```
# Gateway needs erase on first flash (custom partition table)
cd apps/gateway && idf.py -p /dev/ttyUSB0 erase-flash flash

# Same firmware for all three scanners
cd apps/scanner && idf.py -p /dev/ttyUSB1 flash

cd apps/relay && idf.py -p /dev/ttyUSB2 flash
cd apps/tag   && idf.py -p /dev/ttyUSB3 flash
```

Find the port with `ls /dev/ttyUSB*` after plugging in the board. If permission denied, add yourself to the `dialout` group: `sudo usermod -aG dialout $USER`, then log out and back in.

### Windows

```
cd apps/gateway
idf.py -p COM11 erase-flash flash

cd apps/scanner
idf.py -p COM19 flash

cd apps/relay
idf.py -p COM10 flash

cd apps/tag
idf.py -p COM22 flash
```

Find COM ports in Device Manager under "Ports (COM & LPT)".

### Both

Each build copies its `.bin` into `firmware/` for the OTA server to serve. Override the target folder:

```
idf.py -DBMT_OTA_DIR=/some/dir build
```

## 6. Run

1. Power up the tag, scanners, relay, and gateway. Any order works.
2. The gateway is in AUTO mode. It finds and provisions every node on its own.
3. Open the gateway serial monitor at 115200 baud (`idf.py -p <port> monitor`).
4. Press `1` on the gateway UART. You should see all four nodes as `ACTIVE` or `ONLINE`.
5. Open the Indoor Tracking dashboard in ThingsBoard. Tags should show up as they report.

Full UART command list: [09-uart-commands.md](09-uart-commands.md).

## 7. OTA (optional)

Serve the built binaries:

```
cd firmware
python -m http.server 8080
```

On the gateway UART: press `u` for scanner+relay OTA, `g` for gateway self-update. See [11-testing-ota.md](11-testing-ota.md) for the full OTA test procedure.

## Troubleshooting

- **Gateway serial log stops at "MQTT connecting..."** — check `BMT_TB_IP` and `BMT_TB_GATEWAY_TOKEN` in `bmt_config.h`. Also check firewall on the Docker host.
- **Scanners never provision** — check gateway is in AUTO mode (`a` on UART), and check radio distance. Relay first if scanners are far from gateway.
- **`idf.py flash` fails "port not found"** — the ESP-IDF Installer on Windows sometimes needs a reboot to pick up new USB drivers.
- **`docker compose up -d` fails** — on Linux, make sure the `docker` service is running: `sudo systemctl start docker`.

More runtime behavior in [08-operation.md](08-operation.md). Failure and recovery tests in [10-testing.md](10-testing.md).
