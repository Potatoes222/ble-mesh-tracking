# HTTP OTA server and TLS

Two protocols outside the mesh: HTTP for OTA `.bin` transfer, TLS for MQTT.

## HTTP OTA server

### Serve the built binaries

Every build copies its `.bin` to `firmware/`. Serve from there:

```
cd firmware
python -m http.server 8080
```

Expected files:

- `http://<host-ip>:8080/Gateway.bin`
- `http://<host-ip>:8080/Scanner.bin`
- `http://<host-ip>:8080/Relay.bin`

The URLs in `bmt_config.h` (`BMT_OTA_*_URL`) must match.

### Why plain HTTP, not HTTPS

The `.bin` has its own protection: SHA256 in the app descriptor (verified after download), version compare stops downgrade, and the HMAC beacon means an attacker cannot even trigger OTA. A fake `.bin` fails the SHA256 check. HTTPS on the OTA server would just add setup pain without a real gain.

If you want HTTPS anyway, change `esp_http_client_config_t` to use TLS. Not covered here.

### Firewall

Port 8080 must be reachable from the LAN.

- Linux: `sudo ufw allow 8080/tcp`.
- Windows: allow Python through Defender when it prompts.

Test from another machine:

```
curl -s -o /dev/null -w "%{http_code}\n" http://<host-ip>:8080/Gateway.bin
```

Should print `200`.

## TLS for MQTT

MQTT to ThingsBoard uses TLS on port 8883. Protects the gateway token and prevents someone on the network from injecting fake telemetry.

### Cert layout under `thingsboard/tls/`

| File | Role |
|---|---|
| `ca.key` | CA private key. Never leaves the machine. |
| `ca.pem` | CA cert. Gateway trusts this to verify the server. |
| `server.key` | Server private key. |
| `server.pem` | Server cert signed by CA. Presented in TLS handshake. |
| `server.csr` | Intermediate signing request. Regenerated each run. |
| `server_ext.cnf` | OpenSSL extension file with SAN. |
| `gen_certs.sh` | Regenerates everything. |

Gateway embeds `ca.pem` via `EMBED_TXTFILES "ca.pem"` in `apps/gateway/components/bmt_mqtt/CMakeLists.txt`.

### CN verification (not full SAN)

`bmt_mqtt.c` sets the client to verify the server's Common Name against `BMT_TB_CN = "bmt-tb.local"`. Not SAN. That means:

- Server cert must have CN = `bmt-tb.local`.
- IP of the server does not matter — you can change it without regenerating certs.

Why CN and not SAN: the gateway has no DNS resolution, only IP. CN mode fits that constraint.

### Regenerate certs (production deploy)

```
cd thingsboard/tls
bash gen_certs.sh
cp ca.pem ../../apps/gateway/components/bmt_mqtt/ca.pem
```

Rebuild and flash the gateway. Then restart the broker:

```
cd thingsboard
docker compose restart
```

### Debug a failing handshake

Watch gateway serial log at boot:

- `MQTT connected to ThingsBoard` — good.
- `mbedtls: X509 - Certificate verification failed` — `ca.pem` in firmware does not match server's cert. Most common cause: regenerated certs but forgot to reflash gateway.
- `mbedtls: X509 - The CRT/CRL/CSR verification failed` — CN mismatch. Check `BMT_TB_CN` against actual server cert CN.

Rule out server side from another machine:

```
openssl s_client -connect <host-ip>:8883 -showcerts
```

If CN and issuer look right there, the problem is on the gateway side (wrong embedded `ca.pem` or wrong `BMT_TB_CN`).

Related: [04-thingsboard-setup.md](04-thingsboard-setup.md), [05-thingsboard-mqtt.md](05-thingsboard-mqtt.md), [10-testing-ota.md](10-testing-ota.md).
