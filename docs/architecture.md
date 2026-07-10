# Architecture

BMT is a room-level indoor tracking system. It uses BLE Mesh for the radio, ESP32 and ESP32-S3 for the nodes, and ThingsBoard CE as the server.

## Data flow

```
[Tag BLE Beacon]
      |  BLE ADV (HMAC-16, key rotates every 24h)
      v
[Scanner ESP32 x3]  --BLE Mesh-->  [Relay ESP32]  --BLE Mesh-->  [Gateway ESP32-S3]
 reads RSSI                         forwards only                  provisioner + WiFi
                                                                        |  MQTTS (TLS)
                                                                        v
                                                              [ThingsBoard CE (Docker)]
                                                               Rule chain picks a room
                                                                        |
                                                                        v
                                                              [Indoor Tracking dashboard]
```

## Rules

- The gateway only forwards data. It does not pick a room. Room logic runs in the ThingsBoard rule chain.
- The rule chain uses 8 dBm hysteresis, leaky-bucket debounce, and a `MAC -> room` map. You edit the map on the server, not in firmware.
- Every scanner runs the same firmware. Each one uses its own Bluetooth MAC as its ID.

## Hardware

| Node | Board | Notes |
|---|---|---|
| Tag | ESP32 | Battery-powered beacon. |
| Scanner x3 | ESP32 | All scanners use the same board so they share one OTA build. |
| Relay | ESP32 | Sits between far scanners and the gateway. |
| Gateway | ESP32-S3 | Runs WiFi and BLE at the same time. 16 MB flash. |
