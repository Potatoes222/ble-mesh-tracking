# Architecture

BMT is a room-level indoor tracking system. It uses BLE Mesh for the wireless layer, ESP32/ESP32-S3 for the nodes, and ThingsBoard CE for the server.

## Data flow

```
[Tag BLE Beacon]
      |  BLE ADV (HMAC-16, key rotates every 24h)
      v
[Scanner ESP32 x3]  --BLE Mesh-->  [Relay ESP32]  --BLE Mesh-->  [Gateway ESP32-S3]
 measures RSSI only                 forwards only                  provisioner + WiFi
                                                                        |  MQTTS (TLS)
                                                                        v
                                                              [ThingsBoard CE (Docker)]
                                                               Rule chain picks a room
                                                                        |
                                                                        v
                                                              [Indoor Tracking dashboard]
```

## Layer rules

- The Gateway only relays data. It does not compute rooms. Zone logic lives in the ThingsBoard rule chain.
- The rule chain uses 8 dBm hysteresis, leaky-bucket debounce, and a `MAC -> room` map. You edit the map in the server, not in firmware.
- Every scanner runs the same firmware. It identifies itself by its own Bluetooth MAC.

## Hardware

| Node       | Board    | Notes                                              |
|------------|----------|----------------------------------------------------|
| Tag        | ESP32    | Battery-powered beacon.                            |
| Scanner x3 | ESP32    | All scanners must be the same board (shared OTA).  |
| Relay      | ESP32    | Placed between far scanners and the gateway.       |
| Gateway    | ESP32-S3 | WiFi + BLE at the same time, 16 MB flash.          |
