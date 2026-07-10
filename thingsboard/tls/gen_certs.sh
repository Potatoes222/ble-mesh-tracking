#!/usr/bin/env bash
# ============================================================================
# BMT MQTTS certificate generator
# ----------------------------------------------------------------------------
# Tao:
#   ca.pem / ca.key       - Certificate Authority (self-signed)
#   server.pem / server.key - Server cert cho ThingsBoard, SAN = HOSTNAME
#
# ca.pem     -> nhung vao ESP32 (main/ca.pem)
# server.*   -> cho ThingsBoard Docker
#
# CHAY:  bash gen_certs.sh
# Yeu cau: openssl (Git Bash tren Windows co san)
# ============================================================================

set -e

# Tat MSYS path conversion tren Git Bash Windows.
# Neu khong, /C=VN/... bi bien thanh C:/Program Files/Git/C=VN/...
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

# ---- CONFIG: doi hostname o day neu muon ----
HOSTNAME="bmt-tb.local"
DAYS=3650              # 10 nam, khoi lo het han giua ky thesis
# ----------------------------------------------

echo "=== BMT MQTTS cert generator ==="
echo "Hostname (SAN): $HOSTNAME"
echo ""

# 1. CA private key
openssl genrsa -out ca.key 2048

# 2. CA self-signed cert
openssl req -new -x509 -days $DAYS -key ca.key -out ca.pem \
    -subj "//C=VN/O=BMT/CN=BMT-Root-CA"

echo "[1/4] CA created: ca.pem"

# 3. Server private key
openssl genrsa -out server.key 2048

# 4. Server CSR
openssl req -new -key server.key -out server.csr \
    -subj "//C=VN/O=BMT/CN=$HOSTNAME"

echo "[2/4] Server key + CSR created"

# 5. SAN extension file (QUAN TRONG: ESP32 verify theo SAN nay)
cat > server_ext.cnf <<EOF
subjectAltName = DNS:$HOSTNAME
extendedKeyUsage = serverAuth
EOF

# 6. Ky server cert bang CA
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key \
    -CAcreateserial -out server.pem -days $DAYS \
    -extfile server_ext.cnf

echo "[3/4] Server cert signed by CA (SAN=DNS:$HOSTNAME)"

# 7. Verify chuoi ky
openssl verify -CAfile ca.pem server.pem

echo "[4/4] Verification OK"
echo ""
echo "=== DONE ==="
echo "Files:"
echo "  ca.pem      -> ESP32 (copy vao main/ca.pem)"
echo "  server.pem  -> ThingsBoard"
echo "  server.key  -> ThingsBoard"
echo ""
echo "Xem SAN de kiem tra:"
openssl x509 -in server.pem -noout -text | grep -A1 "Subject Alternative Name"
