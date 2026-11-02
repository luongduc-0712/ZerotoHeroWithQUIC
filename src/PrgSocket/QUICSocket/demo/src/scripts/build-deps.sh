#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version=1.25.0
archive="ngtcp2-$version.tar.xz"
checksum=2a34d2484ba17847a5d11965704e9dd0fac4c6d8efc75ffe1ec7de66d8c6b6fb
prefix="${1:-$PWD/.deps}"
case "$prefix" in /*) ;; *) prefix="$PWD/$prefix" ;; esac
for tool in cc make cmake pkg-config curl tar sha256sum; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool (see README.md)" >&2; exit 1; }
done
pkg-config --atleast-version=3.7.2 gnutls || {
    echo 'Install libgnutls28-dev first (GnuTLS >= 3.7.2 required).' >&2
    exit 1
}
mkdir -p .deps-build
if [[ ! -f ".deps-build/$archive" ]]; then
    curl -fL --retry 2 --max-time 120 \
        "https://github.com/ngtcp2/ngtcp2/releases/download/v$version/$archive" \
        -o ".deps-build/$archive.part"
    mv ".deps-build/$archive.part" ".deps-build/$archive"
fi
printf '%s  %s\n' "$checksum" ".deps-build/$archive" | sha256sum --check
if [[ ! -d ".deps-build/ngtcp2-$version" ]]; then
    tar -xf ".deps-build/$archive" -C .deps-build
fi
cmake -S ".deps-build/ngtcp2-$version" -B .deps-build/build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_LIB_ONLY=ON \
    -DENABLE_GNUTLS=ON -DENABLE_OPENSSL=OFF \
    -DENABLE_SHARED_LIB=ON -DENABLE_STATIC_LIB=OFF -DBUILD_TESTING=OFF
cmake --build .deps-build/build -j "${JOBS:-4}"
cmake --install .deps-build/build
echo "Installed private ngtcp2 $version into $prefix"
