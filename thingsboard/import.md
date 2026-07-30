# IMPORT.md — Triển khai hệ thống BMT từ đầu (code → ThingsBoard → chạy thật)

Guide này dành cho việc **dựng toàn bộ hệ trên một máy mới** (hoặc dựng lại từ số 0): lấy code → dựng server → import cấu hình → flash firmware → kiểm tra đầu-cuối. Mỗi bước đều có phần **"Tại sao"** giải thích bản chất để không làm theo kiểu mù.

> Vận hành hằng ngày (bật/tắt ThingsBoard) xem `Thingboards.md`. Chi tiết MQTT API xem `../docs/05-thingsboard-mqtt.md`. Bắt đầu nhanh bản tiếng Anh: `../docs/00-quickstart.md`.

---

## Phần 0 — Bức tranh tổng thể

Luồng dữ liệu khi chạy xong:

```
Tag ──ADV──→ Scanner ──mesh──→ Relay ──mesh──→ Gateway ──MQTTS──→ ThingsBoard ──→ Dashboard
```

Việc triển khai đi ngược từ phải sang trái: **dựng server trước** (để có token cho Gateway), **flash Gateway sau cùng các node**. Checklist tổng:

- [ ] Máy: Docker Desktop + Git (+ ESP-IDF v6.0.1 nếu build firmware)
- [ ] Clone code branch `update-12-07-2026`
- [ ] ThingsBoard chạy (`docker compose up -d`)
- [ ] 2 Device Profile + 1 device gateway + token
- [ ] Import rule chain + gán default + import dashboard
- [ ] Sửa `bmt_config.h` (WiFi, IP, token) → build → flash 6 board
- [ ] Sửa `ZONE_MAP` theo MAC scanner thật
- [ ] Kiểm tra đầu-cuối

---

## Phần 1 — Chuẩn bị máy

Cài theo thứ tự:

1. **Docker Desktop** — [docker.com](https://www.docker.com/products/docker-desktop/). Sau khi cài, vào Settings → General → tick *"Start Docker Desktop when you sign in"* (để server tự sống khi khởi động máy).
2. **Git** — [git-scm.com](https://git-scm.com/)
3. **ESP-IDF v6.0.1** — chỉ cần nếu máy này sẽ build/flash firmware. Cài qua [ESP-IDF Installation Manager](https://dl.espressif.com/dl/esp-idf/) hoặc VS Code extension "ESP-IDF". *(Nếu chỉ chạy server thì bỏ qua.)*
4. Driver USB-Serial cho board (CH340/CP210x) nếu Windows chưa nhận cổng COM.

**Tại sao Docker?** ThingsBoard CE là ứng dụng Java + PostgreSQL — cài tay rất lằng nhằng. Docker gói cả 2 thành 2 container, một lệnh là chạy, xoá cũng sạch.

---

## Phần 2 — Lấy code

```bash
git clone -b update-12-07-2026 https://github.com/Potatoes222/Grindset.git
cd Grindset
```

Trong repo đã có sẵn **mọi thứ** cần cho phần server: `thingsboard/docker-compose.yml`, bộ cert TLS dev (`thingsboard/tls/`), file JSON import (`thingsboard/rulechain/`, `thingsboard/dashboard/`), tài liệu (`docs/`). Firmware nguồn nằm ở `apps/{gateway, scanner, relay, tag}` — mỗi module là 1 ESP-IDF component trong `components/` của từng app, riêng OTA của scanner/relay dùng chung `components/bmt_ota` ở gốc repo.

**Tại sao dùng branch này?** Branch `main` là kiến trúc cũ (Gateway tự tính zone, monolithic). Branch `update-12-07-2026` là bản hiện hành: zone tính trên ThingsBoard, tự phục hồi sau mất nguồn, cấu trúc component chuẩn ESP-IDF.

---

## Phần 3 — Dựng ThingsBoard

```bash
cd thingsboard
docker compose up -d
```

Đợi ~2 phút (lần đầu lâu hơn vì phải tải image + khởi tạo database). Kiểm tra:

```bash
docker compose ps          # 3 container "running": thingsboard, tb-postgres, bmt-ota-server
docker compose logs -f tb  # thấy "Started ThingsboardServerApplication" là xong
```

Container thứ 3 `bmt-ota-server` (nginx, cổng **8081**) tự phục vụ file firmware `.bin` từ thư mục `firmware/` của repo — **không cần** tự tay chạy `python -m http.server` mỗi lần OTA nữa. Build xong firmware mới chỉ việc copy `.bin` vào `firmware/` là các node tải được.

Vào **http://localhost:8080**, đăng nhập tenant mặc định:
- User: `tenant@thingsboard.org`
- Pass: `tenant` → **đổi mật khẩu ngay** (góc phải trên → Profile)

**Tại sao có thư mục `tls/`?** Gateway kết nối MQTT qua **TLS (port 8883)** để mã hoá dữ liệu + xác thực server. Docker compose mount cert từ `tls/` vào container. Bộ cert trong repo là bộ dev dùng được ngay; điểm thiết kế đáng chú ý: cert verify theo **Common Name `bmt-tb.local`** chứ không theo IP — nên **đổi máy/đổi IP không cần làm lại cert**. Nếu muốn bộ cert riêng: `bash tls/gen_certs.sh` rồi copy `ca.pem` mới đè vào `apps/gateway/components/bmt_mqtt/ca.pem` (vì Gateway nhúng CA để verify server).

---

## Phần 4 — Cấu hình ThingsBoard (phần "import" chính)

Làm **đúng thứ tự** — bước sau phụ thuộc bước trước.

### 4.1. Tạo 2 Device Profile

**Profiles → Device profiles → ＋** tạo 2 profile, tên phải **chính xác từng ký tự**:

| Tên | Dành cho |
|---|---|
| `ble_tag` | Các tag được track |
| `ble_mesh_node` | Scanner/Relay (trạng thái online/offline) |

**Tại sao phải tạo trước, tại sao tên phải khớp?** Gateway không cần bạn tạo device cho từng tag/node — nó **tự khai báo** chúng qua MQTT Gateway API (`v1/gateway/connect`) kèm field `"type": "ble_tag"` / `"ble_mesh_node"` (định nghĩa trong `apps/gateway/components/bmt_config/bmt_config.h` → `BMT_PROFILE_TAG/NODE`). ThingsBoard tra profile **theo tên** để gán — tên lệch một ký tự là device rơi vào profile `default`, rule chain zone sẽ không chạy cho nó.

### 4.2. Tạo device Gateway + lấy token

**Entities → Devices → ＋ Add new device**:
- Name: `bmt_gateway`
- Tick ô **"Is gateway"**
- Tạo xong → mở device → tab **Details → Copy access token**

**Tại sao?** Đây là "tài khoản" duy nhất firmware cần: Gateway ESP32 xác thực bằng token này, mọi tag/node con đi qua nó (kiến trúc gateway của ThingsBoard). Token này sẽ dán vào firmware ở Phần 5.

### 4.3. Import rule chain tính zone

**Rule chains → ⬆ (Import rule chain)** → chọn `thingsboard/rulechain/ble_tag_zone_detection.json`.
(Tuỳ chọn: import thêm `rulechain/ble_mesh_node.json` — theo dõi kết quả OTA của Scanner/Relay trên dashboard, gán làm default rule chain cho profile `ble_mesh_node`.)

Mở rule chain vừa import xem thử — nó gồm 7 node, luồng chính:

```
[Fetch tag state]──→[Apply hysteresis]──→[Build attrs payload]──→[Save attributes]
 (đọc trạng thái cũ    (THUẬT TOÁN zone)        └──→[Build TS payload]──→[Save timeseries]
  từ server attrs)
```

**Tại sao cần rule chain riêng?** Đây chính là "bộ não" thay cho code tính zone trong firmware cũ. Node **"Apply hysteresis"** chứa toàn bộ thuật toán (script TBEL): chọn scanner RSSI mạnh nhất trong các mẫu <10s, muốn đổi phòng phải thắng ≥8 dBm (hysteresis — chống nhảy phòng khi 2 scanner xấp xỉ) và giữ được 2 lần liên tiếp kiểu leaky-bucket (debounce — chịu nhiễu đơn lẻ).

### 4.4. Gán rule chain làm default cho profile `ble_tag`

**Profiles → Device profiles → `ble_tag` → ✏ → Rule chain** → chọn `ble_tag_zone_detection` → Save.

**Tại sao bước này bắt buộc?** Import xong rule chain chỉ "nằm đó". Telemetry của device đi vào rule chain nào là do **profile của device** quyết định. Không gán = dữ liệu tag chỉ chạy qua Root chain (lưu thô) mà không bao giờ được tính zone. Đây là bước dễ quên nhất.

### 4.5. Import dashboard

**Dashboards → ⬆ Import dashboard** → chọn `thingsboard/dashboard/indoor_tracking.json`.

### 4.6. Sửa ZONE_MAP theo MAC scanner thật

Mở rule chain → double-click node **"Apply hysteresis"** → tìm đoạn đầu script:

```js
var ZONE_MAP = {
    "9a01c6842178": "room_1",
    "12a60986e694": "room_2",
    "86eab91ad6b8": "room_3"
};
```

Thay key bằng MAC **của scanner bạn** (chữ thường, không dấu `:`), value là tên phòng muốn hiển thị. Lấy MAC: cắm scanner, mở serial 115200, gõ `1` → dòng `MAC`. Sửa xong bấm ✓ → **Save rule chain** (nút ✓ đỏ góc phải).

**Tại sao map ở đây mà không ở firmware?** Đây là lợi ích chính của kiến trúc mới: dời scanner sang phòng khác chỉ cần sửa 1 dòng trên web — hệ vẫn chạy, không flash lại gì cả.

---

## Phần 5 — Cấu hình firmware

Sửa `components/bmt_config/bmt_config.h` của từng app (trong `apps/gateway`, `apps/scanner`, `apps/relay`, `apps/tag`):

| Define | Project | Ghi gì vào |
|---|---|---|
| `BMT_WIFI_SSID` / `BMT_WIFI_PASS` | Gateway, Scanner, Relay | WiFi 2.4GHz (ESP32 không bắt được 5GHz) |
| `BMT_TB_IP` | Gateway | **IP LAN của máy chạy Docker** (xem bằng `ipconfig` — ví dụ `192.168.1.50`) |
| `BMT_TB_GATEWAY_TOKEN` | Gateway | Token copy ở bước 4.2 |
| `BMT_OTA_SCANNER_URL` v.v. | Gateway, Scanner, Relay | `http://<IP đó>:8081/<Tên>.bin` — cổng **8081** = server OTA tự động trong Docker (nginx, service `ota-fileserver`), KHÔNG dùng 8080 vì đó là Web UI của ThingsBoard |

**Tại sao IP phải sửa mà cert thì không?** IP nướng cứng trong firmware (thiết bị nhúng không có DNS nội bộ tin cậy), nên đổi máy server = build + flash lại Gateway. Cert thì verify theo CN nên giữ nguyên được. Tag không có WiFi — không phải sửa gì.

---

## Phần 6 — Build + flash

Mở terminal ESP-IDF (ESP-IDF PowerShell / `export.bat`), flash từng project theo cổng COM tương ứng:

```bash
cd apps/gateway && idf.py -p COM11 erase-flash flash   # LẦN ĐẦU bắt buộc erase-flash
cd ../scanner   && idf.py -p COM19 flash               # flash CÙNG firmware cho cả 3 scanner
cd ../relay     && idf.py -p COM10 flash
cd ../tag       && idf.py -p COM22 flash
```

**Tại sao Gateway phải `erase-flash` lần đầu?** Gateway dùng bảng phân vùng custom (NVS 64KB thay vì 24KB mặc định — chứa key mesh + bảng node). Flash board đang có layout khác mà không erase = NVS cũ nằm sai chỗ, dữ liệu rác. Các lần flash sau (cùng layout) thì không cần erase — và **không nên** erase, vì sẽ mất key mesh khiến cả mesh phải provision lại.

**Tại sao 3 scanner chung 1 firmware?** Scanner tự lấy MAC chip làm định danh (UUID mesh nhúng MAC), không có gì hardcode theo board — nên 1 file `.bin` flash cho cả 3, và OTA sau này cũng broadcast 1 lần là cả 3 cùng nhận.

---

## Phần 7 — Chạy + kiểm tra đầu-cuối

Cắm điện tất cả board (thứ tự không quan trọng), rồi kiểm theo chuỗi — **hỏng khúc nào dừng khúc đó**:

1. **Serial Gateway** (115200): thấy `WiFi connected` → `MQTT connected to ThingsBoard`
   - Nếu `esp-tls: select() timeout`: ThingsBoard chưa chạy hoặc `BMT_TB_IP` sai
2. Gateway AUTO provision: lần lượt `Provision complete addr=0x000X` cho 4 node, gõ `1` → cả 4 `ACTIVE`/`ONLINE`, có dòng `Node table saved to NVS (4 nodes)`
3. Thấy dòng `[VND] src=... MAC=... tag=0x0001 rssi=...` chạy đều → mesh + tag OK
4. Trên ThingsBoard **Entities → Devices**: tự xuất hiện `bmt_tag_0x0001`, `bmt_node_0x000X` (gateway tự khai báo — đúng như giải thích ở 4.1)
5. Mở device tag → **Attributes** → thấy `current_zone` đổi khi mang tag qua phòng khác
6. Mở dashboard *Indoor Tracking* → vị trí hiển thị realtime

**Test tự phục hồi (nên làm để tin hệ):** rút nguồn Gateway 10s → cắm lại → log phải ra `Node table loaded (4 nodes)` → `NVS nodes detected — watching 30s` → `Mesh OK` và dữ liệu tự chạy tiếp, không đụng board nào.

---

## Phần 8 — Lỗi thường gặp khi triển khai mới

| Triệu chứng | Nguyên nhân hay gặp | Xử lý |
|---|---|---|
| Gateway `MQTT disconnected` liên tục | TB chưa chạy / sai `BMT_TB_IP` / sai token | Check `docker compose ps`, ping IP, xem lại token |
| Device tag nằm profile `default` | Tạo profile sau khi gateway đã connect, hoặc sai tên profile | Xoá device đó trên TB → gateway tự tạo lại đúng profile |
| Tag có telemetry nhưng không có `current_zone` | Quên bước 4.4 (gán default rule chain) | Gán rồi đợi gói telemetry kế tiếp |
| Zone luôn là 1 phòng / không đổi | `ZONE_MAP` sai MAC (nhớ: chữ thường, không `:`) | Đối chiếu MAC bằng lệnh `1` trên scanner |
| Node provision xong lại `Failed to find Dst` sau reboot | Flash Gateway đè mà không erase lần đầu (layout NVS lệch) | `idf.py erase-flash flash` + `r` các node |
| Web 8080 không lên | TB đang boot / Docker chưa chạy | `docker compose logs -f tb` xem tiến độ |

---

## Phần 9 — (Tuỳ chọn) mang theo dữ liệu lịch sử

2 file JSON chỉ mang **cấu hình logic**, không mang **dữ liệu** (telemetry, lịch sử vị trí). Dữ liệu nằm trong volume PostgreSQL của Docker. Muốn chuyển cả dữ liệu sang máy mới:

```bash
# Máy cũ — backup volume ra file:
docker run --rm -v thingsboard_postgres-data:/data -v ${PWD}:/backup alpine tar czf /backup/tb-data.tar.gz -C /data .

# Máy mới — dựng compose xong, TẮT stack rồi restore:
docker compose down
docker run --rm -v thingsboard_postgres-data:/data -v ${PWD}:/backup alpine sh -c "rm -rf /data/* && tar xzf /backup/tb-data.tar.gz -C /data"
docker compose up -d
```

(Tên volume xem bằng `docker volume ls`.) Với demo khoá luận thường không cần — hệ mới ghi dữ liệu mới từ đầu.
