# BMT — BLE Mesh Tracking System

Hệ thống định vị trong nhà theo phòng (room-level indoor tracking) sử dụng **BLE Mesh**, **ESP32/ESP32-S3** và **ThingsBoard CE** (self-host qua Docker).

> **Branch `update-09-07-2026`** — bản cập nhật lớn: chuyển kiến trúc sang *"Gateway chỉ trung chuyển, thuật toán zone nằm trên ThingsBoard"*, thêm tự phục hồi hoàn toàn sau mất nguồn, và hàng loạt fix ổn định tìm ra qua test phần cứng thật. Chi tiết ở mục [Changelog](#changelog-update-09072026).

---

## Mục lục

1. [Kiến trúc hệ thống](#kiến-trúc-hệ-thống)
2. [Cấu trúc repo](#cấu-trúc-repo)
3. [Hướng dẫn sử dụng từ A → Z](#hướng-dẫn-sử-dụng-từ-a--z)
4. [Cách hoạt động](#cách-hoạt-động)
5. [Mô tả từng file source](#mô-tả-từng-file-source)
6. [Changelog](#changelog-update-09072026)
7. [Lệnh UART](#lệnh-uart-115200)

---

## Kiến trúc hệ thống

```
[Tag BLE Beacon]
      ↓ BLE ADV (HMAC-16 chống giả mạo, key rotate 24h)
[Scanner ESP32 ×3]  ──BLE Mesh──→  [Relay ESP32]  ──BLE Mesh──→  [Gateway ESP32-S3]
 đo RSSI thô                       forward only                   provisioner + WiFi
 KHÔNG tính zone                   (TTL=7, relay ON)              CHỈ trung chuyển dữ liệu thô
                                                                       ↓ MQTTS (TLS)
                                                          [ThingsBoard CE — Docker]
                                                           Rule chain tính zone:
                                                           hysteresis 8dBm + debounce
                                                           leaky-bucket + MAC→room map
                                                                       ↓
                                                              [Dashboard Indoor Tracking]
```

**Nguyên tắc phân tầng (thay đổi lớn nhất so với bản trước):**

- **Gateway là pure data relay** — nhận `{scanner_MAC, tag_id, rssi}` từ mesh và đẩy nguyên vẹn lên ThingsBoard. Không còn tính zone trong firmware.
- **Thuật toán zone chạy trên ThingsBoard** (rule chain `ble_tag_zone_detection`): so RSSI giữa các scanner, hysteresis 8 dBm, debounce 2 lần kiểu leaky-bucket (miss chỉ trừ 1 thay vì reset 0 — chịu nhiễu tốt hơn), ánh xạ `MAC scanner → phòng` chỉnh trực tiếp trên server **không cần flash lại firmware**.
- **Scanner định danh bằng MAC Bluetooth của chính chip** (`esp_read_mac`) — 1 firmware duy nhất flash cho mọi scanner, không set ID thủ công.

### Phần cứng

| Node | Board | Ghi chú |
|---|---|---|
| Tag | ESP32 | Beacon, chạy pin |
| Scanner ×3 | ESP32 | **Tất cả Scanner phải cùng loại board** (OTA dùng chung 1 `.bin`) |
| Relay | ESP32 | Đặt giữa Scanner xa và Gateway |
| Gateway | ESP32-S3 | WiFi + BLE đồng thời, flash 16MB |

---

## Cấu trúc repo

| Thư mục | Nội dung |
|---|---|
| `Gateway/` | Firmware Gateway: provisioner, MQTTS, watchdog, OTA, key rotation |
| `Scanner/` | Firmware Scanner: quét beacon Tag, verify HMAC, gửi TAG_STATUS qua mesh |
| `Relay/` | Firmware Relay: forward mesh (Network Layer), nhận RESET_CMD/OTA |
| `Tag/` | Firmware Tag: phát beacon BLE kèm HMAC-16 |
| `thingsboard/` | Docker compose, rule chain, dashboard, tài liệu, cert TLS |
| `firmware/` | Bản build `.bin` (phục vụ OTA server) |

---

## Hướng dẫn sử dụng từ A → Z

### Bước 0 — Chuẩn bị

- **ESP-IDF v6.0.1** (cài qua ESP-IDF Installation Manager hoặc VS Code extension)
- **Docker Desktop** (chạy ThingsBoard CE)
- Python 3 (làm HTTP server cho OTA)
- Boards: 1× ESP32-S3 (Gateway) + 5× ESP32 (3 Scanner, 1 Relay, 1 Tag)

### Bước 1 — Dựng ThingsBoard

```bash
cd thingsboard
# (tuỳ chọn) sinh bộ cert TLS mới thay bộ dev có sẵn trong tls/:
bash tls/gen_certs.sh
docker compose up -d          # ThingsBoard CE 3.7 + PostgreSQL, MQTTS port 8883
```

Đợi ~2 phút cho TB khởi động xong, vào `http://<IP máy>:8080`, đăng nhập tenant mặc định `tenant@thingsboard.org` / `tenant` (đổi mật khẩu ngay).

Trong TB UI làm theo thứ tự (chi tiết từng màn hình: `thingsboard/SETUP.md` + `thingsboard/docs/thingsboard-mqtt.md`):
1. Tạo 2 **Device Profile** tên chính xác: `ble_tag` và `ble_mesh_node`
2. Tạo device `bmt_gateway` → copy **Access Token**
3. Import **Rule chain** `ble_tag_zone_detection.json` → mở profile `ble_tag`, gán nó làm *Default rule chain*
4. Import **Dashboard** `indoor_tracking.json`
5. Sửa `ZONE_MAP` (MAC scanner → tên phòng) trong node *"Apply hysteresis"* của rule chain theo MAC thật của các scanner (xem MAC bằng lệnh UART `1` trên từng scanner)

### Bước 2 — Cấu hình firmware

Sửa `main/bmt_config.h` của **từng** project:

| Define | Ở đâu | Giá trị |
|---|---|---|
| `BMT_WIFI_SSID` / `BMT_WIFI_PASS` | Gateway, Scanner, Relay | WiFi của bạn |
| `BMT_TB_IP` | Gateway | IP máy chạy Docker |
| `BMT_TB_GATEWAY_TOKEN` | Gateway | Access token copy ở Bước 1 |
| `BMT_OTA_*_URL` | Gateway, Scanner, Relay | `http://<IP>:8080/<Tên>.bin` |

Nếu bạn tự sinh cert mới ở Bước 1: copy `thingsboard/tls/ca.pem` đè vào `Gateway/main/ca.pem`.

### Bước 3 — Build + flash

```bash
# Gateway (lần đầu PHẢI erase vì bảng phân vùng custom):
cd Gateway && idf.py -p COM11 erase-flash flash

# Scanner (flash cùng 1 firmware cho cả 3 board):
cd Scanner && idf.py -p COM19 flash    # lặp lại với COM của 2 board còn lại

cd Relay   && idf.py -p COM10 flash
cd Tag     && idf.py -p COM22 flash
```

### Bước 4 — Vận hành

1. Cắm điện Tag, 3 Scanner, Relay, Gateway (thứ tự không quan trọng)
2. Gateway mặc định ở chế độ **AUTO**: tự phát hiện + provision + cấu hình toàn bộ node — không cần bấm gì
3. Mở serial monitor Gateway (115200), gõ `1` xem bảng node: đủ 4 node `ACTIVE`/`ONLINE` là xong
4. Mở dashboard *Indoor Tracking* trên ThingsBoard — vị trí tag cập nhật realtime

### Bước 5 — OTA (cập nhật firmware từ xa)

```bash
# host thư mục chứa .bin (copy từ build/<Tên>.bin ra):
cd firmware && python -m http.server 8080
```
Trên UART Gateway: `u` = OTA toàn bộ Scanner/Relay qua mesh, `g` = Gateway tự OTA. Hoặc gửi RPC `ota_scanner`/`ota_relay`/`ota_gateway` từ ThingsBoard. Node tự tải, tự flash, tự báo kết quả về dashboard.

### Bước 6 — Kiểm tra tự phục hồi (tuỳ chọn nhưng nên thử)

- **Rút nguồn Gateway → cắm lại**: bảng node + key tự khôi phục từ NVS, dữ liệu tự chạy tiếp, log phải thấy `Node table loaded (4 nodes)` → `Mesh OK`
- **Rút nguồn 1 Scanner → cắm lại**: Gateway in `beacon unprovisioned tro lai — xoa entry cu va provision lai`, node tự vào lại mesh
- **Rút Relay ra luôn**: nếu Scanner xa mất đường về, watchdog Gateway tự reset + re-provision phần còn lại

---

## Cách hoạt động

### 1. Provisioning tự động (zero-touch)
Gateway ở chế độ AUTO tự phát hiện node unprovisioned qua UUID prefix (`SCAN`/`RELAY`), provision bằng **Static OOB authentication**, add AppKey + bind vendor model, rồi push key HMAC beacon hiện hành. NetKey/AppKey sinh ngẫu nhiên (không hardcode) và **được stack lưu NVS** (`CONFIG_BLE_MESH_SETTINGS=y`).

### 2. Luồng dữ liệu tag
Tag phát ADV custom 24 byte (CID Espressif 0x02E5) kèm sequence + HMAC-16 → Scanner verify HMAC, lọc RSSI, anti-replay theo sequence → gửi `TAG_STATUS {tag_id, rssi}` qua mesh → Gateway tra MAC scanner theo địa chỉ nguồn → enqueue MQTT `{scanner_mac, tag_id, rssi}` → rule chain ThingsBoard tính `current_zone` → dashboard.

### 3. Watchdog & tự phục hồi (self-healing)
- **Gateway mất nguồn → cắm lại**: stack khôi phục NetKey/AppKey/danh sách node từ NVS, bảng BMT khôi phục từ NVS riêng → dữ liệu tự chạy lại, không đụng board nào.
- **Watchdog dữ liệu**: sau 15s ổn định, nếu bảng có node mà 30s không có traffic mesh thật (TAG_STATUS *hoặc* ACK ping Relay) → broadcast RESET_CMD ×5 → wipe → tự re-provision. Chốt an toàn: cả 5 lần gửi fail thì **không** wipe.
- **Node mất nguồn → cắm lại**: beacon unprovisioned trở lại, Gateway nhận ra UUID cũ → xoá entry + provision lại.
- **Ping Relay 20s/lần** (`DEFAULT_TTL_GET`): cập nhật ONLINE/OFFLINE + tín hiệu "mesh sống" cho watchdog; ACK chỉ tính khi `error_code == 0`.

### 4. OTA + bảo mật beacon
- OTA Scanner/Relay trigger qua mesh, node tự tải `.bin` từ HTTP server LAN, báo `OTA_RESULT` về Gateway → ThingsBoard.
- Gateway tự kiểm tra firmware mới của chính nó (so SHA256, trùng thì bỏ qua).
- **Key HMAC beacon rotate 24h**: Gateway sinh key random, lưu NVS, push cho toàn bộ Scanner qua mesh. Beacon giả không có key bị loại ngay tại Scanner.

### 5. ThingsBoard
Rule chain `ble_tag_zone_detection` chạy mỗi khi có telemetry tag: đọc trạng thái cũ (server attributes) → chọn scanner có RSSI mạnh nhất trong các mẫu còn tươi (<10s) → nếu muốn đổi zone phải thắng zone hiện tại ≥8 dBm (hysteresis) và giữ được 2 lần liên tiếp kiểu leaky-bucket (debounce) → lưu `current_zone` + `current_rssi`. Đổi vị trí scanner ↔ phòng chỉ cần sửa `ZONE_MAP`.

---

## Mô tả từng file source

### `Gateway/main/` (ESP32-S3 — provisioner + cầu nối MQTT)

| File | Vai trò |
|---|---|
| `main.c` | Entry point — khởi tạo theo thứ tự: NVS (cảnh báo đỏ nếu buộc xoá) → node table → key mesh → key HMAC OTA → MQTT worker → WiFi → MQTT → Bluetooth → mesh → UART → node ping → watchdog → OTA auto-check |
| `bmt_config.h` | Config người dùng: WiFi, IP/token/CN ThingsBoard, URL OTA, tên device profile |
| `bmt_types.h` | Struct + opcode vendor model dùng chung với Scanner/Relay: `TAG_STATUS`, `RESET_CMD`, `OTA_TRIGGER`, `OTA_KEY_PUSH`, `OTA_RESULT` |
| `bmt_mesh.c/.h` | Trái tim Gateway: provisioner (AUTO/MANUAL), task cấu hình node (AppKey add + model bind), callback nhận TAG_STATUS/OTA_RESULT, node tự rejoin khi beacon lại, ping Relay, publish vendor message, wipe, quản lý NetKey/AppKey |
| `bmt_node_table.c/.h` | Bảng node đã provision (addr/UUID/MAC/loại/config_done/online) — save/load NVS namespace `bmt_gw`, mọi lỗi đọc/ghi đều in `esp_err_to_name` |
| `bmt_mac_cache.c/.h` | Cache tạm UUID→MAC lúc quét beacon unprovisioned (trước khi node có entry trong bảng) |
| `bmt_scan_list.c/.h` | Chế độ provision thủ công: UART `s` quét 10s, `p` provision danh sách, `a`/`m` đổi mode |
| `bmt_zone.c/.h` | Tracking tag + đánh giá zone **local** (hysteresis 5dBm) — chỉ còn làm debug/so sánh (log `[debug local]`), zone chính thức tính trên ThingsBoard |
| `bmt_wifi.c/.h` | WiFi STA + tự reconnect khi rớt |
| `bmt_mqtt.c/.h` | MQTT(S) client tới TB, hàng đợi tag report + worker task (không publish trong mesh callback để tránh block BLE host), định tuyến RPC `ota_*` |
| `bmt_thingsboard.c/.h` | Format + publish payload theo chuẩn TB Gateway API (`v1/gateway/connect`, `/telemetry`, `/attributes`): tag report, trạng thái node, kết quả OTA |
| `bmt_ota.c/.h` | 3 nhánh OTA: Gateway tự OTA (`esp_https_ota` + so SHA256), trigger OTA Scanner/Relay qua mesh, OTA-beacon NimBLE + HMAC; kèm **rotate key HMAC 24h** + push key qua mesh |
| `bmt_watchdog.c/.h` | Data watchdog: 15s stabilize → nếu có node mà 30s không có traffic thật → RESET_CMD ×5 → wipe + reboot; không wipe nếu cả 5 lần gửi fail |
| `bmt_uart.c/.h` | Menu UART (bảng lệnh ở cuối README) |
| `ca.pem` | Cert CA để verify server MQTTS (khớp bộ cert trong `thingsboard/tls/`) |

### `Scanner/main/` (ESP32 — mắt của hệ thống)

| File | Vai trò |
|---|---|
| `main.c` | Entry — chỉ gọi `bmt_scan_core_init()` |
| `bmt_scan_core.c/.h` | Orchestrator: init đúng thứ tự NVS → auth → Bluetooth → mesh → GAP scan → UART; giữ `scanner_id` trong NVS (legacy — định danh chính thức giờ theo MAC chip) |
| `bmt_config.h` | WiFi (chỉ dùng khi OTA) + URL firmware |
| `bmt_types.h` | Struct/opcode chung (bản sao đồng bộ với Gateway) |
| `bmt_auth.c/.h` | HMAC-16 (HMAC-SHA256 rút 2 byte, PSA API): verify beacon Tag, verify OTA-beacon, nhận key rotate từ Gateway qua mesh + lưu NVS. 2 key tách biệt — lộ 1 không ảnh hưởng key kia |
| `bmt_scan.c/.h` | GAP scan + radio manager (chia thời gian giữa scan và mesh publish trên 1 radio), parse ADV tag, task check timeout |
| `bmt_tag_table.c/.h` | Bảng tag đang thấy (tối đa 20): lọc RSSI, ước lượng khoảng cách path-loss (n=2.5), **anti-replay** theo sequence (từ chối nhảy seq >30), timeout 5s |
| `bmt_mesh.c/.h` | Node mesh: UUID nhúng MAC chip (`SCAN` + MAC), provisioning Static OOB, gửi TAG_STATUS, nhận RESET_CMD/OTA_TRIGGER/KEY_PUSH; local reset đợi đúng `PROV_RESET_EVT` mới reboot (+ fallback 5s) |
| `bmt_ota.c/.h` | Nhận trigger OTA → bật WiFi → tải `.bin` → `esp_ota` → báo OTA_RESULT về Gateway |
| `bmt_uart.c/.h` | Lệnh `r` (reset mesh), `1` (status kèm MAC), `i` (đổi scanner_id legacy) |
| `Kconfig.projbuild` | Tuỳ chọn menuconfig riêng của project |

### `Relay/main/` (ESP32 — cầu nối vùng xa)

| File | Vai trò |
|---|---|
| `main.c` | Entry — init Bluetooth → mesh → UART |
| `bmt_config.h` | UUID Relay cố định (`RELAY...02`), WiFi + URL OTA |
| `bmt_types.h` | Struct/opcode chung |
| `bmt_mesh.c/.h` | Node mesh với **Relay feature ENABLED** — forward gói mesh ở Network Layer (không cần giải mã nội dung); vẫn có AppKey bind để tự xử lý RESET_CMD/OTA_TRIGGER ở Access Layer; local reset đợi `PROV_RESET_EVT` |
| `bmt_ota.c/.h` | OTA qua trigger mesh (như Scanner) |
| `bmt_uart.c/.h` | Lệnh `r`/`1` (in MAC qua `esp_read_mac` vì UUID Relay không nhúng MAC) + health log 30s/lần |

### `Tag/main/` (ESP32 — beacon đeo người/đồ vật)

| File | Vai trò |
|---|---|
| `main.c` | Entry — `bmt_auth_init()` → `bmt_beacon_start()` → UART |
| `bmt_config.h` | UUID hệ thống, tag_id (minor), loại tag (major: PERSON/ASSET), tx_power calibrate |
| `bmt_auth.c/.h` | Tính HMAC-16 cho từng gói ADV (key Tag riêng, không chung với key OTA-beacon) |
| `bmt_beacon.c/.h` | Payload custom 24B (CID 0x02E5): UUID(16) + major(2) + minor(2) + txpower(1) + **sequence(1)** + **hmac16(2)** — timer 500ms tăng sequence + tính lại HMAC rồi advertise; sequence giúp Scanner đo loss rate + chống replay |
| `bmt_uart.c/.h` | In status: sequence hiện tại, HMAC gói gần nhất, trạng thái advertise |

### `thingsboard/`

| File | Vai trò |
|---|---|
| `docker-compose.yml` | ThingsBoard CE 3.7 + PostgreSQL; mở 8080 (HTTP UI), 8883 (MQTTS), mount cert từ `tls/` |
| `SETUP.md` | Hướng dẫn dựng + cấu hình TB từng bước |
| `tls/gen_certs.sh` | Script sinh CA + cert server (CN `bmt-tb.local` — verify theo CN nên đổi IP không cần làm lại cert); `server_ext.cnf` là config extension đi kèm |
| `tls/ca.key, ca.pem, server.key, server.pem, ...` | Bộ cert/key **dev** đang dùng (bản phát triển — final nên sinh bộ mới) |
| `ble_tag_zone_detection.json` | **Rule chain tính zone** — import vào TB, gán làm default rule chain của profile `ble_tag`. Node quan trọng nhất: *"Apply hysteresis"* (chứa `ZONE_MAP`, `HYSTERESIS_DBM=8`, `DEBOUNCE_COUNT=2`) |
| `ble_tag_zone_detection_metadata_latest.json` | Metadata bản rule chain đang deploy thật (backup đồng bộ sau mỗi lần sửa live) |
| `root_rule_chain_export.json` | Backup Root Rule Chain gốc |
| `rc_verify.json` | Export dùng đối chiếu khi kiểm tra rule chain qua REST API |
| `indoor_tracking.json` | Dashboard theo dõi vị trí tag + trạng thái node |
| `docs/thingsboard-mqtt.md` | Tài liệu MQTT Gateway API: topic, format payload, device profile, RPC |
| `_grindset_gateway_main.c`, `_grindset_scanner_main.c` | Bản `main.c` monolithic của kiến trúc cũ (branch `main`) — giữ để tham khảo/đối chiếu khi viết báo cáo |

---

## Changelog (update 09/07/2026)

So với bản `main` (28/06/2026 — monolithic `main.c`, Gateway tự tính zone, scanner set ID tay):

### Kiến trúc
1. **Gateway = pure relay, thuật toán zone chuyển lên ThingsBoard** (phân tầng: thiết bị thu thập — server xử lý). Rule chain hysteresis + debounce leaky-bucket, chống flapping giữa các phòng, xử lý RSSI stale (fallback giá trị hiển thị cuối thay vì -999).
2. **Định danh scanner bằng MAC** thay cho ID set tay — 1 firmware cho mọi scanner; UART lệnh `1` in MAC từng board để đối chiếu.
3. **ThingsBoard CE self-host qua Docker + MQTTS** (TLS self-signed CA, verify theo CN `bmt-tb.local` — đổi IP không cần tạo lại cert).
4. Code Gateway tách từ 1 file `main.c` thành các module `bmt_*` (xem bảng mô tả file ở trên).

### Độ tin cậy (các bug tìm ra bằng test rút nguồn thật)
5. **Bật `CONFIG_BLE_MESH_SETTINGS=y` cho Gateway** — fix gốc rễ lỗi "rút nguồn Gateway là chết cả hệ": trước đây stack không lưu gì, mỗi lần boot `prov_enable()` tự sinh NetKey ngẫu nhiên MỚI (dòng log "restored from NVS" cũ là in nhầm) → toàn bộ node giữ key cũ thành không liên lạc được (`Failed to find Dst`). Giờ key + danh sách node + devkey + seq đều persist, Gateway mất nguồn xong tự chạy tiếp.
6. **NVS 24K → 64K** + log lỗi chi tiết cho save/load node table + cảnh báo đỏ nếu buộc phải xoá NVS (`NO_FREE_PAGES`) — trước đây các lỗi này bị nuốt im lặng, không thể chẩn đoán.
7. **Node tự rejoin**: UUID đã provision mà beacon unprovisioned trở lại → Gateway xoá entry cũ + provision lại (node rút nguồn không còn kẹt ngoài mesh vĩnh viễn).
8. **Fix race reset node (Scanner/Relay)**: `esp_ble_mesh_node_local_reset()` là hàm async — trước đây delay cố định rồi reboot nên có lúc NVS chưa xoá xong. Giờ đợi đúng `PROV_RESET_EVT` mới reboot, kèm fallback 5s.
9. **Watchdog Gateway**: thêm 15s chờ ổn định sau boot; không wipe nếu cả 5 lần gửi RESET_CMD đều fail; tiêu chí "mesh sống" tính cả ACK ping Relay (không false-trigger reset toàn mesh chỉ vì không có Tag nào ở gần scanner); ACK phải có `error_code == 0` mới được tính.
10. **Fix buffer publish mesh 12 → 20 byte**: trước đây push key HMAC 16 byte fail im lặng (`Too small publication msg size`), làm hỏng toàn bộ tính năng rotate key.
11. Chỉ ping Relay (không ping Scanner) — config client là instance dùng chung, ping nhiều node chồng nhau gây tranh chấp giao dịch làm trạng thái ONLINE/OFFLINE nhấp nháy.

### Bảo mật
12. NetKey/AppKey sinh ngẫu nhiên mỗi lần provision mạng mới (không hardcode trong source).
13. Static OOB authentication khi provisioning — node lạ không biết OOB value không join được dù giả UUID.
14. HMAC-16 trên beacon Tag + **rotate key 24h tự động** push qua mesh; anti-replay theo sequence ở Scanner.
15. MQTTS (TLS) tới ThingsBoard; secret WiFi/token trong repo đã thay bằng placeholder (`YOUR_WIFI_*`, `YOUR_TB_GATEWAY_TOKEN`). Bộ cert/key TLS trong `tls/` là **bộ dev** — khi triển khai thật chạy `tls/gen_certs.sh` sinh bộ mới.

---

## Lệnh UART (115200)

**Gateway**: `1` bảng node · `2` tag/zone · `3` thống kê MQTT/mesh · `s`/`p` scan + provision tay · `a`/`m` đổi AUTO/MANUAL · `u` OTA Scanner/Relay · `g` OTA Gateway · `0` soft reset · `9` full reset (xoá tất cả, re-provision)

**Scanner / Relay**: `1` status (kèm MAC) · `r` reset mesh về unprovisioned
