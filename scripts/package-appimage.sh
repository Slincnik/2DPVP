#!/usr/bin/env bash
set -euo pipefail

linuxdeploy_version="1-alpha-20251107-1"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gateway_url="${GATEWAY_URL:-}"

if [[ -z "$gateway_url" ]]; then
  echo "GATEWAY_URL is required, for example:" >&2
  echo "  GATEWAY_URL=http://192.168.1.10:8080 make appimage" >&2
  exit 1
fi
if [[ "$gateway_url" != http://* && "$gateway_url" != https://* ]]; then
  echo "GATEWAY_URL must start with http:// or https://" >&2
  exit 1
fi
if [[ "$gateway_url" == *"'"* || "$gateway_url" =~ [[:space:]] ]]; then
  echo "GATEWAY_URL must not contain quotes or whitespace" >&2
  exit 1
fi
if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "AppImage packaging currently supports x86_64 only" >&2
  exit 1
fi

if [[ ! -f "$root/.deps/msquic/include/msquic.h" ]]; then
  echo "MsQuic is missing; run 'make client' first" >&2
  exit 1
fi

build_dir="$root/client/build-appimage"
app_dir="$build_dir/AppDir"
dist_dir="$root/dist"
tool_dir="$root/.deps/linuxdeploy"
linuxdeploy="$tool_dir/linuxdeploy-x86_64.AppImage"
output="$dist_dir/PvPDuel-x86_64.AppImage"

mkdir -p "$tool_dir" "$dist_dir"
if [[ ! -f "$linuxdeploy" ]] || ! echo "$linuxdeploy_sha256  $linuxdeploy" | sha256sum --check --status; then
  rm -f "$linuxdeploy"
  curl -fL "https://github.com/linuxdeploy/linuxdeploy/releases/download/${linuxdeploy_version}/linuxdeploy-x86_64.AppImage" -o "$linuxdeploy"
  echo "$linuxdeploy_sha256  $linuxdeploy" | sha256sum --check --status || {
    echo "linuxdeploy checksum verification failed" >&2
    rm -f "$linuxdeploy"
    exit 1
  }
  chmod +x "$linuxdeploy"
fi

make -C "$root" proto
cmake -S "$root/client" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPVP_DUEL_REQUIRE_SECURE_STORAGE=ON
cmake --build "$build_dir" --target pvp_duel_client --parallel

rm -rf "$app_dir" "$output"
mkdir -p "$app_dir/usr/bin"
cat >"$app_dir/usr/bin/pvp-duel-launcher" <<EOF
#!/bin/sh
if [ -z "\${GATEWAY_URL:-}" ]; then
  GATEWAY_URL='$gateway_url'
fi
export GATEWAY_URL
appdir="\${APPDIR:-\$(CDPATH= cd -- "\$(dirname -- "\$0")/../.." && pwd)}"
exec "\$appdir/usr/bin/pvp_duel_client" "\$@"
EOF
chmod +x "$app_dir/usr/bin/pvp-duel-launcher"

ARCH=x86_64 LDAI_OUTPUT="$output" "$linuxdeploy" --appimage-extract-and-run \
  --appdir "$app_dir" \
  --executable "$build_dir/pvp_duel_client" \
  --desktop-file "$root/client/packaging/pvp-duel.desktop" \
  --icon-file "$root/client/packaging/pvp-duel.svg" \
  --output appimage

chmod +x "$output"
echo "Created $output"
echo "Embedded default Gateway: $gateway_url"
echo "Override at runtime with: GATEWAY_URL=http://host:8080 $output"
