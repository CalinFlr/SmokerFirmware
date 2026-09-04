#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir_input="${SMOKER_RELEASE_BUILD_DIR:-$repository_root/build-verify}"
signed_image_input="${1:-$repository_root/smoker_controller.bin}"
expected_unsigned_input="${2:-}"
public_key="$repository_root/keys/smoker_ota_signing_public.pem"

command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required for release path canonicalization" >&2
    exit 1
}
command -v idf.py >/dev/null 2>&1 || {
    echo "ESP-IDF v6.0.2 must be activated before signature verification" >&2
    exit 1
}
[[ "$(idf.py --version)" == "ESP-IDF v6.0.2" ]] || {
    echo "release signature verification requires exactly ESP-IDF v6.0.2" >&2
    exit 1
}
[[ ! -L "$signed_image_input" ]] || {
    echo "signed release image must not be a symlink: $signed_image_input" >&2
    exit 1
}
[[ -e "$signed_image_input" ]] || {
    echo "signed release image is missing: $signed_image_input" >&2
    exit 1
}
[[ -f "$signed_image_input" ]] || {
    echo "signed release image is not a regular file: $signed_image_input" >&2
    exit 1
}
[[ -f "$public_key" ]] || {
    echo "trusted OTA public key is missing: $public_key" >&2
    exit 1
}
[[ -n "$expected_unsigned_input" ]] || {
    echo "expected independent unsigned image path is required" >&2
    exit 2
}
[[ ! -L "$expected_unsigned_input" ]] || {
    echo "independent unsigned image must not be a symlink: $expected_unsigned_input" >&2
    exit 1
}
[[ -e "$expected_unsigned_input" ]] || {
    echo "independent unsigned image is missing: $expected_unsigned_input" >&2
    exit 1
}
[[ -f "$expected_unsigned_input" ]] || {
    echo "independent unsigned image is not a regular file: $expected_unsigned_input" >&2
    exit 1
}

if ! signed_image="$(python3 - "$signed_image_input" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
if path.is_symlink() or not path.is_file():
    raise SystemExit(1)
print(path.resolve(strict=True))
PY
)"; then
    echo "failed to canonicalize signed release image: $signed_image_input" >&2
    exit 1
fi

if ! expected_unsigned="$(python3 - "$expected_unsigned_input" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
if path.is_symlink() or not path.is_file():
    raise SystemExit(1)
print(path.resolve(strict=True))
PY
)"; then
    echo "failed to canonicalize independent unsigned image: $expected_unsigned_input" >&2
    exit 1
fi

if ! build_dir="$(python3 - "$build_dir_input" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
if path.exists() and not path.is_dir():
    raise SystemExit(1)
print(path.resolve(strict=False))
PY
)"; then
    echo "failed to canonicalize release build directory: $build_dir_input" >&2
    exit 1
fi

idf.py -B "$build_dir" secure-verify-signature \
    --keyfile "$public_key" \
    "$signed_image"
python3 "$repository_root/tools/check_signed_release_payload.py" \
    "$signed_image" \
    "$expected_unsigned"
python3 "$repository_root/tools/check_firmware_size.py" \
    "$signed_image" \
    --partition-size 3145728 \
    --maximum-used-percent 75

echo "Signed firmware verified against keys/smoker_ota_signing_public.pem"
