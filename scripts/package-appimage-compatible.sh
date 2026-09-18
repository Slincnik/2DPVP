#!/usr/bin/env bash
set -euo pipefail

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
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker with BuildKit/buildx is required for the compatible AppImage build" >&2
  exit 1
fi

mkdir -p "$root/dist"
rm -f "$root/dist/PvPDuel-x86_64.AppImage"

cd "$root"
docker buildx build \
  --file client/packaging/Dockerfile.appimage \
  --build-arg "GATEWAY_URL=$gateway_url" \
  --build-arg "PVP_DUEL_VERSION=${PVP_DUEL_VERSION:-0.0.0-dev}" \
  --build-arg "PVP_DUEL_UPDATE_MANIFEST_URL=${PVP_DUEL_UPDATE_MANIFEST_URL:-}" \
  --output "type=local,dest=$root/dist" \
  .

chmod +x "$root/dist/PvPDuel-x86_64.AppImage"
echo "Created Ubuntu 22.04-compatible $root/dist/PvPDuel-x86_64.AppImage"
echo "Embedded default Gateway: $gateway_url"
