#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${SMOKER_RELEASE_BUILD_DIR:-$repository_root/build-verify}"
unsigned_image="$build_dir/smoker_controller.bin"
signed_image="$repository_root/smoker_controller.bin"
public_key="$repository_root/keys/smoker_ota_signing_public.pem"

command -v idf.py >/dev/null 2>&1 || {
    echo "ESP-IDF v6.0.2 must be activated before signing" >&2
    exit 1
}
[[ "$(idf.py --version)" == "ESP-IDF v6.0.2" ]] || {
    echo "release signing requires exactly ESP-IDF v6.0.2" >&2
    exit 1
}
[[ -f "$unsigned_image" && -f "$build_dir/sdkconfig" ]] || {
    echo "verified unsigned target build is missing from $build_dir" >&2
    exit 1
}
[[ -f "$public_key" ]] || {
    echo "trusted OTA public key is missing: $public_key" >&2
    exit 1
}

python3 "$repository_root/tools/check_effective_sdkconfig.py" \
    "$build_dir/sdkconfig"

if [[ -n "${SMOKER_OTA_SIGNING_KEY_FILE:-}" && -n "${SMOKER_OTA_SIGNING_KEY_B64:-}" ]]; then
    echo "provide exactly one OTA signing-key source" >&2
    exit 1
fi
if [[ -z "${SMOKER_OTA_SIGNING_KEY_FILE:-}" && -z "${SMOKER_OTA_SIGNING_KEY_B64:-}" ]]; then
    echo "set SMOKER_OTA_SIGNING_KEY_FILE or SMOKER_OTA_SIGNING_KEY_B64" >&2
    exit 1
fi

signing_directory="$(mktemp -d "${TMPDIR:-/tmp}/smoker-ota-sign.XXXXXX")"
signing_key="$signing_directory/signing-key.pem"
cleanup() {
    rm -f "$signing_key"
    rmdir "$signing_directory"
}
trap cleanup EXIT
umask 077

if [[ -n "${SMOKER_OTA_SIGNING_KEY_FILE:-}" ]]; then
    [[ -f "$SMOKER_OTA_SIGNING_KEY_FILE" ]] || {
        echo "OTA signing-key file does not exist" >&2
        exit 1
    }
    cp "$SMOKER_OTA_SIGNING_KEY_FILE" "$signing_key"
else
    printf '%s' "$SMOKER_OTA_SIGNING_KEY_B64" | base64 --decode > "$signing_key"
fi
chmod 600 "$signing_key"

rm -f "$signed_image"
idf.py -B "$build_dir" secure-sign-data \
    --keyfile "$signing_key" \
    --output "$signed_image" \
    "$unsigned_image"

# Verification against the repository-owned public key both proves that the
# output is signed and prevents an accidentally replaced CI secret from
# publishing an update which every installed device would reject.
idf.py -B "$build_dir" secure-verify-signature \
    --keyfile "$public_key" \
    "$signed_image"
python3 "$repository_root/tools/check_firmware_size.py" \
    "$signed_image" \
    --partition-size 3145728 \
    --maximum-used-percent 75
chmod 0644 "$signed_image"

echo "Signed release firmware verified against keys/smoker_ota_signing_public.pem"
