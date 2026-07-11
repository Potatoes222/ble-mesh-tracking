# BLE Mesh in this project

Just the parts of BLE Mesh we actually use. For the full spec see the Bluetooth Mesh Profile 1.0.1 document.

## Why BLE Mesh instead of raw BLE

Raw BLE is point-to-point. Every scanner would need direct radio contact with the gateway. In a real building that fails when a wall blocks the path.

BLE Mesh adds a Network Layer that forwards packets across nodes. A "far" scanner can send its data through a relay that sits between it and the gateway. The gateway itself does not need line-of-sight to every scanner.

## Roles

BLE Mesh defines several device types. This project uses two.

- **Provisioner** — the gateway. It finds unprovisioned nodes, gives them keys, assigns their address, and binds their models to an AppKey. There is only one provisioner in the network.
- **Node** — every other device (scanners, relay, tag receivers). A node holds keys, listens for messages, and can also forward.

We do not use Friend and Low Power nodes. All nodes stay awake.

## Keys

Two keys matter for our traffic.

- **NetKey** — encrypts the Network Layer. Every node in the mesh must have the same NetKey. If NetKeys do not match, packets do not decrypt and the mesh silently drops them.
- **AppKey** — encrypts the Access Layer, one level up from Network. The gateway binds an AppKey to our vendor model. Any node that wants to send or receive vendor messages must also have that AppKey.

Both keys are random per network. The gateway generates them once at first boot (`bmt_mesh_generate_keys_if_needed()`), the stack saves them into NVS (because `CONFIG_BLE_MESH_SETTINGS=y`), and they survive across reboots.

Nodes receive NetKey during provisioning. They receive AppKey via a config message called `APP_KEY_ADD` sent after provisioning.

## Addresses

Every provisioned node gets a 16-bit unicast address. The gateway uses `0x0001`. Nodes start at `0x0002` and go up.

We use these address types:

- **Unicast** — for talking to one specific node.
- **All-nodes group `0xFFFF`** — for broadcasts like `RESET_CMD`. Every node in the mesh receives it.

## Provisioning

Unprovisioned nodes advertise themselves with a `Provisioning Beacon` that carries a 16-byte UUID. Our nodes put a label in that UUID so the gateway can tell them apart:

- Scanner UUID starts with ASCII `SCAN` followed by the chip's Bluetooth MAC.
- Relay UUID starts with ASCII `RELAY`.

The gateway sees the beacon, checks the UUID prefix, and adds the node to its provisioning queue.

Then it runs the standard flow:

1. Send `Provisioning Invite`.
2. Exchange public keys and derive a session key.
3. Authenticate with Static OOB (see below).
4. Send `Provisioning Data` (NetKey + primary address).
5. Node emits `PROV_COMPLETE_EVT`.

The gateway now knows the node's unicast address. It saves it in the node table (`bmt_node_table_save()`).

## Static OOB authentication

Without authentication, anyone advertising the right UUID could join the mesh. That is a real attack: a rogue device impersonates a scanner and starts forging RSSI reports.

Static OOB fixes this. Provisioner and node both know a 16-byte secret before they meet:

```c
static const uint8_t BMT_MESH_STATIC_OOB_VAL[16] = { 0x8E, 0x2F, ... };
```

The provisioner passes it to `esp_ble_mesh_provisioner_set_static_oob_value()`. The node passes it as `static_val` in `esp_ble_mesh_prov_t`.

During authentication both sides derive a check number from that secret. If a rogue device does not have the same 16 bytes hard-coded, the check fails and provisioning aborts.

If you fork this project for a real deployment, change these bytes.

## Vendor model

Bluetooth SIG defines standard models (light on/off, sensor status, etc.). We do not need those. We define one vendor model that carries our own opcodes.

Vendor model ID: `0x0000` under Company ID `0x02E5` (Espressif).

Opcodes we use:

| Opcode | Payload | Direction | Meaning |
|---|---|---|---|
| `TAG_STATUS` | `bmt_tag_report_t` | scanner -> gateway | RSSI report for one tag. |
| `RESET_CMD` | 1 byte | gateway -> all | Wipe local state and reboot. |
| `OTA_TRIGGER` | 1 byte target | gateway -> node | Node should start WiFi OTA. |
| `OTA_RESULT` | `bmt_ota_result_t` | node -> gateway | Node reports success or failure. |
| `OTA_KEY_PUSH` | 16 bytes | gateway -> scanner | New HMAC beacon key. |

These are declared in each app's `bmt_types.h` and must match byte-for-byte across gateway/scanner/relay/tag.

## Config Client and Config Server

To send AppKeys and bind models, the mesh spec uses two extra models:

- **Config Server** runs on every node. It processes `APP_KEY_ADD`, `MODEL_APP_BIND`, `DEFAULT_TTL_GET` and returns status ACKs.
- **Config Client** runs on the provisioner. It sends those requests.

Our `bmt_mesh.c` in the gateway calls `esp_ble_mesh_config_client_set_state()` twice per node during configuration:

1. `APP_KEY_ADD` — hands the AppKey to the node.
2. `MODEL_APP_BIND` — tells the node "use this AppKey for our vendor model".

Only after both ACKs come back does the gateway set `n->config_done = true`. Without the second ACK the node cannot decrypt our vendor messages.

## Publishing and TTL

To send a vendor message the code fills the model's publication struct and calls `esp_ble_mesh_model_publish()`:

```c
s_vnd_models[0].pub->publish_addr = dst;   // unicast or 0xFFFF
s_vnd_models[0].pub->app_idx      = s_app_key_idx;
s_vnd_models[0].pub->ttl          = 7;
esp_ble_mesh_model_publish(&s_vnd_models[0], opcode, len, data, ROLE_PROVISIONER);
```

TTL 7 means the packet can be relayed up to 7 hops before it dies. In our small mesh (3 scanners + 1 relay + gateway) 7 is more than enough.

## Relay feature

A node with the `.relay = ESP_BLE_MESH_RELAY_ENABLED` config forwards mesh packets at the Network Layer. It does not need the AppKey to forward — it only needs the NetKey, which every node has.

The dedicated relay node (`apps/relay`) is the main forwarder. Scanners also have Relay enabled, so a scanner that hears a packet another scanner missed will forward it too. This gives the mesh a second chance and improves reliability.

## GATT Proxy

`.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED` lets a smartphone with an nRF Mesh style app connect over GATT and inject mesh messages. We do not use this in production but keeping it enabled makes debugging easier.

## Persistence: `CONFIG_BLE_MESH_SETTINGS=y`

The mesh stack has its own NVS storage for NetKey, AppKey, devkey, sequence numbers, and the provisioner's node list. When this Kconfig option is on, the stack writes to NVS every time state changes.

We enable this on the gateway. Without it, every reboot generates a fresh random NetKey and the whole mesh stops working. See [08-operation.md](08-operation.md) for the bug story.

Sequence numbers matter too. A mesh node signs each packet with an incrementing seq. If the gateway reboots and forgets its seq, other nodes reject its packets as replays. Persistence fixes that too.

## What we do not implement

- **Friend and Low Power** — battery saving mode. We do not need it because all nodes are wall-powered except the tag, and the tag does not participate in mesh (it just advertises).
- **Health Server / Health Client** — periodic self-test. Left out to save flash.
- **Proxy Client** — connecting over GATT to another node. We use the direct advertising bearer.
- **Provisioning over GATT** — we use advertising bearer only.

See [08-operation.md](08-operation.md) for how these pieces work at runtime, and [04-algorithms.md](04-algorithms.md) for the math that runs on top of mesh data.
