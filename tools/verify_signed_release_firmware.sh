#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${SMOKER_RELEASE_BUILD_DIR:-$repository_root/build-verify}"
signed_image="${1:-$repository_root/smoker_controller.bin}"
public_key="$repository_root/keys/smoker_ota_signing_public.pem"

command -v idf.py >/dev/null 2>&1 || {
    echo "ESP-IDF v6.0.2 must be activated before signature verification" >&2
    exit 1
}
[[ "$(idf.py --version)" == "ESP-IDF v6.0.2" ]] || {
    echo "release signature verification requires exactly ESP-IDF v6.0.2" >&2
    exit 1
}
[[ -f "$signed_image" ]] || {
    echo "signed release image is missing: $signed_image" >&2
    exit 1
}
[[ -f "$public_key" ]] || {
    echo "trusted OTA public key is missing: $public_key" >&2
    exit 1
}

idf.py -B "$build_dir" secure-verify-signature \
    --keyfile "$public_key" \
    "$signed_image"
python3 "$repository_root/tools/check_firmware_size.py" \
    "$signed_image" \
    --partition-size 3145728 \
    --maximum-used-percent 75

echo "Signed firmware verified against keys/smoker_ota_signing_public.pem"
