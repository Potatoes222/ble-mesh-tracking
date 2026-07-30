# Hướng dẫn mở ThingsBoard (dùng hằng ngày)

> File này là guide vận hành nhanh. Cài đặt lần đầu từ số 0 (tạo profile, import rule chain, dashboard...) xem `thingsboard/SETUP.md`.

---

## Cách 1 — Mở qua app Docker Desktop (dễ nhất, đang dùng)

1. Mở app **Docker Desktop** (Start Menu gõ "Docker Desktop", hoặc icon ngoài desktop)
   - Đường dẫn thật trên máy này: `C:\Users\ADMIN\AppData\Local\Programs\DockerDesktop\Docker Desktop.exe`
2. Đợi icon cá voi ở góc taskbar hết chạy loading (~30–60s)
3. **Không cần làm gì thêm** — 2 container `thingsboard` + `tb-postgres` tự khởi động lại theo restart policy
4. Đợi thêm ~1–2 phút cho ThingsBoard boot xong → vào **http://localhost:8080**

Trong app Docker Desktop, tab **Containers** phải thấy 2 dòng màu xanh:
- `thingsboard` — Running
- `tb-postgres` — Running

Nếu container không tự chạy (hiếm): bấm nút ▶ (Start) ngay trên dòng đó trong app.

## Cách 2 — Mở qua terminal (PowerShell / CMD)

```powershell
# Bật Docker Desktop nếu chưa chạy:
Start-Process "C:\Users\ADMIN\AppData\Local\Programs\DockerDesktop\Docker Desktop.exe"

# Đợi daemon sẵn sàng (lệnh này hết báo lỗi là được):
docker info

# Bật ThingsBoard (chỉ cần khi container không tự chạy):
cd g:\Thesis\thingsboard
docker compose up -d
```

---

## Địa chỉ truy cập

| Cái gì | Địa chỉ |
|---|---|
| Web UI (dashboard) | http://localhost:8080 — máy khác trong LAN: http://192.168.2.23:8080 |
| MQTTS (Gateway ESP32 kết nối vào) | `192.168.2.23:8883` (TLS) |
| Đăng nhập tenant | `tenant@thingsboard.org` |

Gateway ESP32 **tự reconnect** khi ThingsBoard sống lại — không cần reset board.

---

## Lệnh hay dùng (chạy trong `g:\Thesis\thingsboard`)

```powershell
docker compose ps           # xem trạng thái
docker compose logs -f tb   # xem log ThingsBoard realtime (Ctrl+C thoát)
docker compose stop         # tắt tạm (GIỮ dữ liệu)
docker compose start        # bật lại
docker compose restart      # khởi động lại (khi TB đơ)
```

> ⚠️ **TUYỆT ĐỐI không chạy `docker compose down -v`** — cờ `-v` xoá volume = mất sạch database (device, rule chain, dashboard, toàn bộ telemetry). `down` không có `-v` thì an toàn.

---

## Lỗi thường gặp

| Triệu chứng | Nguyên nhân | Xử lý |
|---|---|---|
| Serial Gateway báo `esp-tls: select() timeout` + `MQTT disconnected` | ThingsBoard đang tắt (Docker chưa chạy) | Mở Docker Desktop, đợi 2 phút — Gateway tự nối lại |
| `docker: failed to connect to the docker API...` | Docker Desktop chưa bật | Mở app Docker Desktop trước, rồi mới gõ lệnh docker |
| Web 8080 quay mãi không lên | TB đang boot (nhất là sau khi bật máy) | Đợi 1–2 phút, xem tiến độ: `docker compose logs -f tb` |
| Dashboard không có dữ liệu mới nhưng web vẫn vào được | Gateway mất WiFi/MQTT hoặc mesh chết | Xem serial Gateway: gõ `3` (thống kê MQTT/mesh), `1` (bảng node) |

---

## Mẹo: tự động hoàn toàn

Docker Desktop → ⚙️ **Settings → General → tick "Start Docker Desktop when you sign in"**

Từ đó: bật máy → Docker tự chạy → ThingsBoard tự sống → Gateway tự kết nối. Không phải mở gì bằng tay nữa.
