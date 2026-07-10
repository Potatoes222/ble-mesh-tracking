# ThingsBoard CE — Setup cho BMT (local, MQTTS)

Thư mục này chứa mọi thứ cần để tự host ThingsBoard CE bằng Docker, thay cho
ThingsBoard Cloud. Lấy từ repo gốc `caotrongphuoc/ble-mesh-tracking` (branch
`main`) + cert TLS tự sinh riêng cho máy này.

## Nội dung thư mục

```
docker-compose.yml   — ThingsBoard CE 3.7.0 + PostgreSQL
indoor_tracking.json — dashboard export, sẵn sàng import (5 widget)
tls/                 — CA + server cert TLS (đã sinh, SAN=bmt-tb.local)
docs/thingsboard-mqtt.md — tài liệu gốc, đầy đủ chi tiết MQTT/TLS/device profile
```

## Các bước còn lại (phải làm thủ công trên UI, tôi không tự động hóa được)

### 1. Cài Docker Desktop (nếu chưa có)

Tải tại https://www.docker.com/products/docker-desktop — cài xong khởi động
lại máy nếu được yêu cầu, mở Docker Desktop cho chạy nền.

### 2. Chạy ThingsBoard

```bash
cd g:/Thesis/thingsboard
docker compose up -d
```

Đợi 1-2 phút cho lần đầu (TB tự khởi tạo database). Kiểm tra:

```bash
docker compose ps
```

### 3. Đăng nhập TB UI

Mở `http://localhost:8080`, đăng nhập `tenant@thingsboard.org` / `tenant`.

### 4. Tạo 2 Device Profile (BẮT BUỘC trước khi Gateway kết nối)

Menu **Device profiles** → **+** → tạo lần lượt, **tên phải khớp chính xác**:
- `ble_tag`
- `ble_mesh_node`

(Rule chain để mặc định — firmware đã tự tính `zone` tại chỗ rồi gửi lên,
không cần rule chain xử lý gì thêm.)

### 5. Tạo device Gateway + lấy Access Token

Menu **Devices** → **+ Add device** → đặt tên `bmt_gateway`, bật **Is
gateway**. Sau khi tạo xong, vào device đó → tab **Credentials** → copy
**Access token**.

Dán token này vào `Gateway/main/bmt_config.h`, thay giá trị placeholder:

```c
#define BMT_TB_GATEWAY_TOKEN "<token vừa copy>"
```

### 6. Kiểm tra IP

`Gateway/main/bmt_config.h` đang set `BMT_TB_IP "192.168.2.23"` (trùng máy
chạy OTA server). Nếu máy chạy `docker compose up -d` là máy khác/IP khác,
sửa lại giá trị này cho đúng.

### 7. Import dashboard

Menu **Dashboards** → **+** → **Import dashboard** → chọn file
`indoor_tracking.json`. TB sẽ hỏi map 2 Entity Alias:
- **"Tag Device"** → chọn filter theo **Device profile = ble_tag**
- **"All Mesh Devices"** → chọn filter theo **Device profile = ble_mesh_node**

### 8. Build + flash lại Gateway

Sau khi đã điền token thật (bước 5), build lại `Gateway` và flash — Gateway
sẽ tự kết nối MQTTS (port 8883) và auto-provision sub-device kèm đúng
profile (`type` field, xem `bmt_thingsboard.c`).

## Muốn tạo lại cert (đổi hostname, hết hạn, v.v.)

```bash
cd tls
bash gen_certs.sh
cp ca.pem ../../Gateway/main/ca.pem
```

Rồi build lại Gateway (EMBED_TXTFILES sẽ tự nhúng `ca.pem` mới vào firmware).

## Xác nhận nhanh sau khi chạy

- Serial log Gateway in ra: `MQTTS -> mqtts://<ip>:8883 (verify CN=bmt-tb.local)`
  rồi `MQTT connected to ThingsBoard` — nếu TLS handshake fail sẽ thấy lỗi
  mbedTLS ở đây thay vì "connected".
- TB UI → **Devices**: thấy `bmt_gateway` Online, và các sub-device
  (`bmt_node_0x...`, `bmt_tag_0x...`) tự xuất hiện khi có scanner/relay/tag
  hoạt động.
- Dashboard "Indoor Tracking" hiển thị dữ liệu tag/zone theo thời gian thực.
