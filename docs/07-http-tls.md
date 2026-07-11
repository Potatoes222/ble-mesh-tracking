# HTTP OTA server and TLS

Two protocols outside the mesh:

- **HTTP** carries the OTA `.bin` from your workstation to each node.
- **TLS** protects MQTT between the gateway and ThingsBoard.

## HTTP OTA server

### Serve the built binaries

Every `idf.py build` copies the `.bin` to the repo's `firmware/` folder. Run a plain HTTP server from that folder:

```
cd firmware
python -m http.server 8080
```

Files expected at these paths:

- `http://<host-ip>:8080/Gateway.bin`
- `http://<host-ip>:8080/Scanner.bin`
- `http://<host-ip>:8080/Relay.bin`

The URLs in `bmt_config.h` (`BMT_OTA_GATEWAY_URL`, `BMT_OTA_SCANNER_URL`, `BMT_OTA_RELAY_URL`) must match.

### Why plain HTTP, not HTTPS

The OTA .bin already has its own protection:

- SHA256 baked into the app descriptor. `esp_https_ota` verifies it against the running slot before accepting.
- Version compare with a monotonic `YYYYMMDDHHMMSS` timestamp prevents downgrade.
- Anti-forgery on the mesh trigger: the HMAC-signed OTA beacon means a random attacker cannot make a node start OTA at all.

An attacker on the LAN can host a fake `.bin`, but the SHA256 check catches it because the fake `.bin` will not match the gateway's expectation. So plain HTTP saves a lot of setup pain (no cert on the OTA server) without opening a real attack vector.

If you want HTTPS anyway: swap the URL, add the CA to `bmt_config.h`, and change `esp_http_client_config_t` to use TLS. Not covered here.

### Firewall notes

Port 8080 must be reachable from the LAN. On Linux:

```
sudo ufw allow 8080/tcp
```

On Windows the first time you run `python -m http.server 8080`, Windows Defender pops a dialog asking to allow Python through the firewall. Say yes.

Check reachability from another machine on the LAN:

```
curl http://<host-ip>:8080/Gateway.bin -o /tmp/x.bin
```

If it fails, the OTA will also fail. Fix the firewall first.

### Alternatives to python http.server

Any static file server works.

- `busybox httpd -f -p 8080`
- `caddy file-server --listen :8080`
- `npx http-server -p 8080`

Do not use nginx with fancy features. Plain, no caching, no redirect.

## TLS for MQTT

The MQTT connection to ThingsBoard uses TLS on port 8883. This protects the gateway token (which acts as a password) and prevents someone on the network from injecting fake telemetry.

### Certificate layout

Everything lives under `thingsboard/tls/`:

| File | What it is |
|---|---|
| `ca.key` | Private key of our own Certificate Authority. Never leaves the machine. |
| `ca.pem` | Public certificate of our CA. Gateway trusts this to verify the server. |
| `server.key` | Private key for the MQTT server. |
| `server.pem` | Server cert signed by our CA. Presented during TLS handshake. |
| `server.csr` | Intermediate signing request. Regenerated on each run. |
| `server_ext.cnf` | OpenSSL extension file with the SAN. |
| `ca.srl` | Serial number tracker for the CA. |
| `gen_certs.sh` | Script that regenerates everything above. |

The gateway embeds `ca.pem` into its firmware at compile time via `EMBED_TXTFILES "ca.pem"` in `apps/gateway/components/bmt_mqtt/CMakeLists.txt`.

### CN verification (not full SAN)

`bmt_mqtt.c` configures the MQTT client to verify the server's Common Name against `BMT_TB_CN = "bmt-tb.local"`.

That means the check is:

```
does server cert's CN equal "bmt-tb.local"?
```

Not:

```
does server cert's SAN cover this specific hostname or IP?
```

Why CN and not SAN: our gateway does not know its own hostname or IP resolution rules. Modern TLS libraries prefer SAN but ESP-IDF MQTT lets us pick CN mode for the exact case where the client cannot do DNS.

The upshot: **you can change the ThingsBoard IP and keep the same certs**. Nothing in the certs is tied to a specific IP. The gateway just checks that whoever answered claims to be `bmt-tb.local` in the CN field.

### Regenerate the certs

The certs in `tls/` are development ones. For production, regenerate:

```
cd thingsboard/tls
bash gen_certs.sh
```

The script does, roughly:

1. Create a new CA if one does not exist.
2. Create a new server key.
3. Generate a CSR with the extension file (CN = `bmt-tb.local`, SAN = same).
4. Sign the CSR with the CA.
5. Print the fingerprints.

Then copy the new `ca.pem` into the gateway source so the firmware trusts it:

```
cp ca.pem ../../apps/gateway/components/bmt_mqtt/ca.pem
```

Rebuild and flash the gateway. `EMBED_TXTFILES` picks up the new `ca.pem` at compile time.

### Restart the broker

ThingsBoard reads the cert files on start. After regenerating:

```
cd thingsboard
docker compose restart
```

Give it a minute to come back up.

### Debug a failing TLS handshake

Watch the gateway serial log during boot. Look for one of:

- `MQTT connected to ThingsBoard` -> everything works.
- `mbedtls: SSL - The peer notified us that the connection is going to be closed` -> server rejected our handshake. Usually the wrong TLS version or cipher suite. Not typical for our stack.
- `mbedtls: X509 - Certificate verification failed` -> the gateway does not trust the server cert. Either `ca.pem` in firmware does not match `ca.pem` in `tls/`, or the server presented a completely different cert.
- `mbedtls: X509 - The CRT/CRL/CSR verification failed` -> CN mismatch. Check `BMT_TB_CN` in `bmt_config.h` matches whatever CN the server cert actually has.

The most common cause: rebuilt certs, restarted the broker, but forgot to also update the gateway's `ca.pem` and reflash. Symptoms are `X509 - Certificate verification failed` every time.

To rule out certs entirely, run `openssl s_client` from another machine:

```
openssl s_client -connect <host-ip>:8883 -showcerts
```

If this returns the right CN and the right issuer, the server side is fine. Then the problem is on the gateway (wrong `ca.pem` embedded, or wrong `BMT_TB_CN`).

## Related docs

- [05-thingsboard-setup.md](05-thingsboard-setup.md) — ThingsBoard install and device profile setup.
- [06-thingsboard-mqtt.md](06-thingsboard-mqtt.md) — MQTT topics and payload format.
- [11-testing-ota.md](11-testing-ota.md) — end-to-end OTA test procedure.
