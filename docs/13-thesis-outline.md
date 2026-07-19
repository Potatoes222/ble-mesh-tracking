# Outline khoá luận

Outline cho báo cáo khoá luận tốt nghiệp dựa trên project này, theo template ĐHKHTN VNU-HCM Khoa Điện tử - Viễn thông (https://github.com/anhnguyen-kelly23/Thesis_Template_latex).

Skeleton LaTeX đã export sẵn dưới thư mục `thesis/` ở repo root.

## Đề tài đề xuất

Hệ thống định vị trong nhà theo phòng dựa trên BLE Mesh và ESP32, tích hợp thu thập – xử lý dữ liệu qua ThingsBoard.

EN: *Room-level Indoor Positioning System based on BLE Mesh with ESP32, integrated with ThingsBoard for data pipeline*.

## TÓM TẮT (`Appendix/tomtat.tex`)

- **Background**: Định vị trong nhà không thể dùng GPS. Nhu cầu theo dõi người/tài sản trong bệnh viện, nhà máy, nhà dưỡng lão đang tăng. BLE beacon rẻ, phổ biến nhưng single-scanner phủ sóng hẹp.
- **Purpose**: Xây dựng hệ thống định vị theo phòng dùng nhiều scanner nối với nhau qua BLE Mesh, thu thập RSSI, xử lý ở tầng server để chống nhiễu.
- **Method**: Bốn firmware Tag/Scanner/Relay/Gateway trên ESP32/ESP32-S3, giao thức BLE Mesh vendor model, cầu nối MQTTS lên ThingsBoard CE (Docker), rule chain hysteresis 8 dBm + debounce leaky-bucket 2 lần xác nhận, HMAC-16 chống giả mạo, khóa Tag dẫn xuất theo epoch 1h, khóa OTA-beacon xoay 24h, OTA qua HTTPS.
- **Results**: Source code hiện đã triển khai pipeline end-to-end, lọc RSSI, quyết định zone phía server, bảo mật beacon, watchdog tự phục hồi và OTA. Các chỉ số thực nghiệm như thời gian chuyển phòng, flapping và độ chính xác cần được đo theo các bài test ở Chương 5 trước khi đưa ra kết luận định lượng.
- **Evaluation**: Kiến trúc phân tầng (edge–gateway–server) tách xử lý ra khỏi firmware, dễ tinh chỉnh không cần re-flash.

**Từ khoá**: BLE Mesh, RSSI, indoor tracking, ESP32, ThingsBoard, HMAC, Kalman filter.

## CHƯƠNG 1 — GIỚI THIỆU

### 1.1 Bối cảnh và động lực thực hiện
- Nhu cầu theo dõi vị trí trong nhà (bệnh viện, nhà dưỡng lão, kho, nhà máy).
- Hạn chế của GPS trong nhà. Các giải pháp thay thế: WiFi RTT, UWB, BLE, camera.
- Vì sao chọn BLE: giá rẻ, pin lâu, phổ biến trong smartphone.
- Vì sao BLE Mesh thay vì BLE point-to-point: vùng phủ mesh, forwarding, self-healing.

### 1.2 Mục tiêu nghiên cứu
- Xây dựng đầy đủ 4 loại node embedded (Tag/Scanner/Relay/Gateway).
- Tích hợp pipeline server-side để xử lý RSSI, quyết định zone (không xử lý trên firmware).
- Bảo mật lớp beacon (chống giả mạo) và lớp truyền (TLS).
- Cơ chế cập nhật firmware từ xa (OTA) cho toàn bộ node.

### 1.3 Phạm vi
- Định vị mức phòng (room-level), không phải toạ độ (x,y).
- Trong nhà, 3-5 phòng, môi trường thí nghiệm.
- 1-N tag di động, 3 scanner cố định, 1 relay, 1 gateway.

### 1.4 Các công cụ và board mạch phát triển
- Framework ESP-IDF v6.0.1.
- Board ESP32-S3 (Gateway/Tag/Relay) và ESP32 (Scanner), theo `CONFIG_IDF_TARGET` hiện tại của từng project.
- ThingsBoard CE 3.7 + PostgreSQL trên Docker.
- Công cụ phụ trợ: VS Code + ESP-IDF extension, clang-format, Git.

### 1.5 Cấu trúc khoá luận
(Bullet 6 chương theo template.)

## CHƯƠNG 2 — TỔNG QUAN NGHIÊN CỨU

### 2.1 Các phương pháp định vị trong nhà
- Fingerprinting (WiFi RSSI database).
- BLE beacon với 1 scanner (iBeacon, Eddystone).
- UWB (DecaWave, Apple U1).
- Hybrid.

### 2.2 So sánh giải pháp thương mại
- Estimote, Kontakt.io, Aruba Meridian.
- Ưu / nhược, chi phí, năng lực.

### 2.3 Các nghiên cứu học thuật liên quan
- Bài báo về Kalman filter cho BLE RSSI.
- Path-loss model calibration.
- BLE Mesh cho IoT (Bluetooth SIG whitepaper).

### 2.4 Khoảng trống nghiên cứu
- Ít giải pháp mở nguồn tự host end-to-end.
- Bảo mật beacon thường bị bỏ qua.
- OTA hàng loạt hiếm được đề cập.

## CHƯƠNG 3 — CƠ SỞ LÝ THUYẾT

### 3.1 BLE Advertising và RSSI
- Cấu trúc gói ADV, manufacturer specific data.
- RSSI là gì, path-loss theo khoảng cách.

### 3.2 BLE Mesh
- 3.2.1 Provisioner và Node.
- 3.2.2 NetKey, AppKey, Access Layer, Network Layer.
- 3.2.3 Provisioning + Static OOB authentication.
- 3.2.4 Vendor model + opcode.
- 3.2.5 Relay feature.

### 3.3 Xử lý tín hiệu RSSI
- 3.3.1 Kalman filter 1D (q, r, gain k).
- 3.3.2 Path-loss log-distance model (n).
- 3.3.3 Anti-replay theo sequence number.

### 3.4 Quyết định zone
- 3.4.1 Chọn scanner mạnh nhất trong cửa sổ tươi.
- 3.4.2 Hysteresis 8 dBm.
- 3.4.3 Leaky-bucket debounce với 2 lần xác nhận; logic hiện nằm trong `ble_tag_zone_detection_metadata_latest.json`.

### 3.5 Bảo mật
- 3.5.1 HMAC-SHA256 và HMAC-16 truncated.
- 3.5.2 Hai cơ chế khóa độc lập: Tag derive khóa theo epoch cục bộ 1h; Gateway xoay khóa HMAC của OTA-beacon mỗi 24h và push cho Scanner qua mesh.
- 3.5.3 MQTTS/TLS + CN verify.

### 3.6 OTA
- 3.6.1 esp_https_ota, dual-slot partition.
- 3.6.2 SHA256 skip, version compare YYYYMMDDHHMMSS.
- 3.6.3 Trigger: mesh broadcast + WiFi OTA.

### 3.7 Giao thức ThingsBoard Gateway API
- 3.7.1 Kiến trúc Gateway/Sub-device.
- 3.7.2 Topic: `v1/gateway/telemetry`, `v1/gateway/attributes`, `v1/devices/me/rpc/request`.
- 3.7.3 Rule chain engine.

## CHƯƠNG 4 — TRIỂN KHAI HỆ THỐNG

### 4.1 Kiến trúc tổng thể
- Sơ đồ 4 tầng: Tag → Scanner → Relay → Gateway → ThingsBoard.
- Nguyên tắc: firmware chỉ chuyển dữ liệu thô, xử lý ở server.

### 4.2 Firmware Tag
- 4.2.1 Cấu trúc ADV 24 byte (UUID + major + minor + tx_power + sequence + HMAC-16).
- 4.2.2 HMAC-SHA256 qua PSA Crypto API.
- 4.2.3 Timer 500 ms + sequence++.

### 4.3 Firmware Scanner
- 4.3.1 Radio time-sharing GAP scan / mesh publish.
- 4.3.2 Tag table (tối đa 20 tag) + Kalman filter.
- 4.3.3 Anti-replay + timeout 5 s.
- 4.3.4 Gửi TAG_STATUS qua vendor model.

### 4.4 Firmware Relay
- 4.4.1 Relay feature enabled ở Network Layer.
- 4.4.2 Vẫn bind AppKey để xử lý RESET_CMD và OTA_TRIGGER.

### 4.5 Firmware Gateway
- 4.5.1 Provisioner AUTO mode + UUID prefix detection.
- 4.5.2 Config task chờ ACK APP_KEY_ADD + MODEL_APP_BIND.
- 4.5.3 Node table persist NVS.
- 4.5.4 MQTT worker + hàng đợi tag report (tránh block BLE host).
- 4.5.5 Watchdog: 15s stabilize + 30s traffic timeout + wipe/re-provision.
- 4.5.6 Key rotation 24h.

### 4.6 Server-side ThingsBoard
- 4.6.1 Docker compose + PostgreSQL.
- 4.6.2 Device profile `ble_tag`, `ble_mesh_node`.
- 4.6.3 Rule chain `ble_tag_zone_detection` (hysteresis 8 dBm + debounce leaky-bucket 2 lần trong bản metadata latest; cần đồng bộ lại file export portable trước khi import mới).
- 4.6.4 Rule chain `ble_mesh_node_ota` (persist ota_result).
- 4.6.5 Dashboard Indoor Tracking (6 widget).

### 4.7 Bảo mật end-to-end
- 4.7.1 Static OOB provisioning.
- 4.7.2 HMAC-16 cho Tag beacon với khóa epoch 1h; HMAC-16 cho OTA-beacon với khóa xoay 24h.
- 4.7.3 MQTTS self-signed CA + CN verify.

### 4.8 OTA
- 4.8.1 Nginx HTTPS server trên Docker, port 8443, dùng CA/CN verification.
- 4.8.2 Beacon broadcast cho scanner (NimBLE + HMAC).
- 4.8.3 Unicast mesh cho relay.
- 4.8.4 Auto-check định kỳ 3 phút.

### 4.9 Cấu trúc source code
- 4.9.1 `apps/{gateway,scanner,relay,tag}` — ESP-IDF projects.
- 4.9.2 `components/bmt_ota` — shared component.
- 4.9.3 `thingsboard/` — server assets.

## CHƯƠNG 5 — KẾT QUẢ THỰC NGHIỆM

### 5.1 Thiết lập thí nghiệm
- 5.1.1 Bố trí phòng (bản vẽ mặt bằng, vị trí scanner).
- 5.1.2 Danh sách phần cứng.
- 5.1.3 Cấu hình phần mềm và tham số hyperparameters.

### 5.2 Test 1: Bring-up
- Thứ tự provisioning, log gateway.
- Thời gian đến khi tag đầu tiên có dữ liệu trên dashboard.

### 5.3 Test 2: Độ chính xác định vị theo phòng
- Đi bộ theo route định trước, so sánh zone dashboard với ground truth.
- Bảng confusion matrix.
- Accuracy tổng.

### 5.4 Test 3: Chống nhiễu (walking test hysteresis)
- Đo tỉ lệ flap khi tag đứng gần biên 2 phòng.
- So sánh HYSTERESIS_DBM = 5 / 8 / 12.

### 5.5 Test 4: Self-healing
- Rút nguồn gateway, đo thời gian phục hồi.
- Rút nguồn scanner riêng, đo thời gian re-provision.
- Rút relay, đo watchdog fire time.

### 5.6 Test 5: OTA
- Thời gian OTA 1 node (broadcast vs unicast).
- Verify SHA256 skip, downgrade protection.
- Test fault injection: HTTP 404, WiFi drop.

### 5.7 Test 6: Bảo mật beacon
- Dùng phone giả beacon (nRF Connect), verify scanner reject.
- Test replay attack.
- Đo tỉ lệ false-negative HMAC.

### 5.8 Đánh giá tổng
- 5.8.1 Ưu điểm.
- 5.8.2 Hạn chế (chỉ level phòng, không toạ độ; RSSI biến động).
- 5.8.3 So sánh với giải pháp thương mại.

## CHƯƠNG 6 — KẾT LUẬN

### 6.1 Tóm tắt đóng góp
- Hệ thống end-to-end mã nguồn mở.
- Kiến trúc phân tầng: firmware relay, server xử lý.
- Bảo mật ở cả 2 lớp: beacon HMAC + MQTTS.
- OTA cho toàn hệ thống.

### 6.2 Hạn chế
- Chỉ định vị theo phòng.
- Chỉ chạy trong LAN, chưa xử lý đa site.
- Chưa test scale > 10 tag.

### 6.3 Hướng phát triển
- 6.3.1 Định vị toạ độ (x,y) bằng trilateration + Kalman 2D.
- 6.3.2 Học máy để hiệu chỉnh path-loss theo môi trường.
- 6.3.3 Fleet management cho hàng trăm gateway.
- 6.3.4 Chống nhiễu WiFi (2.4 GHz coexistence).

## Phụ lục đề xuất

- **Phụ lục A**: Cấu trúc gói ADV chi tiết + code C.
- **Phụ lục B**: Rule chain node scripts (đầy đủ JS).
- **Phụ lục C**: Log serial mẫu (bring-up, OTA success, OTA fail).
- **Phụ lục D**: Bản vẽ mặt bằng thí nghiệm.
- **Phụ lục E**: Bảng tham số hyperparameter tuning.
