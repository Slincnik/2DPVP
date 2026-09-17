#!/usr/bin/env bash
set -euo pipefail

version="2.5.7"
arch="$(dpkg --print-architecture)"
case "$arch" in
  amd64|arm64|armhf) ;;
  *) echo "Unsupported Debian architecture: $arch" >&2; exit 1 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
destination="$root/.deps/msquic"
base="https://packages.microsoft.com/repos/microsoft-debian-trixie-prod/pool/main/libm/libmsquic"
source="https://raw.githubusercontent.com/microsoft/msquic/v${version}/src/inc"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$destination/include"
curl -fsSL "$base/libmsquic_${version}_${arch}.deb" -o "$tmp/libmsquic.deb"
dpkg-deb -x "$tmp/libmsquic.deb" "$destination"
(
  cd "$tmp"
  apt-get download libxdp1 >/dev/null
  dpkg-deb -x libxdp1_*.deb "$destination"
)
curl -fsSL "$source/msquic.h" -o "$destination/include/msquic.h"
curl -fsSL "$source/msquic_posix.h" -o "$destination/include/msquic_posix.h"
curl -fsSL "$source/quic_sal_stub.h" -o "$destination/include/quic_sal_stub.h"

echo "MsQuic ${version} installed locally in $destination"
