#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p certs
if [[ -e certs/server.key && -e certs/server.crt ]]; then
    echo 'Certificates already exist; keeping them. Use --cert/--key/--ca for other certificates.'
    exit 0
fi
if [[ -e certs/server.key || -e certs/server.crt ]]; then
    echo 'Incomplete certificate/key pair; use another directory or restore the missing file.' >&2
    exit 1
fi
san='DNS:localhost,IP:127.0.0.1'
if [[ -n "${SERVER_IP:-}" ]]; then san="$san,IP:$SERVER_IP"; fi
umask 077
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 365 \
    -keyout certs/server.key -out certs/server.crt \
    -subj '/CN=quic-testbed' -addext "subjectAltName=$san" \
    -addext 'extendedKeyUsage=serverAuth' \
    -addext 'basicConstraints=critical,CA:TRUE' \
    -addext 'keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign'
chmod 600 certs/server.key
echo "Created testbed certificate: $san"
