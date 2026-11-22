#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
CERTS="$ROOT/docker/certs"
mkdir -p "$CERTS"
chmod 755 "$CERTS"
if [[ -e "$CERTS/ca.pem" || -e "$CERTS/ca.key" || -e "$CERTS/broker.pem" || -e "$CERTS/broker.key" ]]; then
    for file in ca.pem ca.key broker.pem broker.key; do
        [[ -s "$CERTS/$file" ]] || { echo "Incomplete certificate set: $CERTS/$file missing; inspect before replacing." >&2; exit 1; }
    done
    openssl verify -CAfile "$CERTS/ca.pem" -verify_hostname broker.test "$CERTS/broker.pem"
    openssl x509 -in "$CERTS/broker.pem" -checkend 86400 -noout
    chmod 600 "$CERTS/ca.key" "$CERTS/broker.key"
    echo "Reusing existing broker.test certificate."
    exit 0
fi
umask 077
TMP=$(mktemp -d "$CERTS/.generate.XXXXXX")
trap 'rm -rf -- "$TMP"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 3650 \
    -subj '/CN=MQTT QUIC Local Test CA' \
    -addext 'basicConstraints=critical,CA:TRUE,pathlen:0' \
    -addext 'keyUsage=critical,keyCertSign,cRLSign' \
    -keyout "$TMP/ca.key" -out "$TMP/ca.pem" 2>/dev/null
openssl req -new -newkey rsa:2048 -nodes -sha256 \
    -subj '/CN=broker.test' -keyout "$TMP/broker.key" -out "$TMP/broker.csr" 2>/dev/null
cat > "$TMP/broker.ext" <<'EXT'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:broker.test
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid,issuer
EXT
openssl x509 -req -in "$TMP/broker.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" \
    -set_serial "0x$(openssl rand -hex 16)" -days 365 -sha256 \
    -extfile "$TMP/broker.ext" -out "$TMP/broker.pem" 2>/dev/null
for file in ca.pem ca.key broker.pem broker.key; do mv -- "$TMP/$file" "$CERTS/$file"; done
chmod 644 "$CERTS/ca.pem" "$CERTS/broker.pem"
chmod 600 "$CERTS/ca.key" "$CERTS/broker.key"
openssl verify -CAfile "$CERTS/ca.pem" -verify_hostname broker.test "$CERTS/broker.pem"
echo "Generated local CA and broker.test certificate in $CERTS (private keys mode 0600)."
