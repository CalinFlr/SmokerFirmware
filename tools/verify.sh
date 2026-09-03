#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-all}"

case "$mode" in
    all|--host-only|--idf-only)
        ;;
    *)
        echo "usage: tools/verify.sh [--host-only|--idf-only]" >&2
        exit 2
        ;;
esac

activate_local_idf_if_needed() {
    if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1 \
        && { [[ "$mode" == "--host-only" ]] || command -v idf.py >/dev/null 2>&1; }; then
        return
    fi

    local export_script="$repository_root/.tools/esp-idf-v6.0.2/export.sh"
    if [[ -f "$export_script" ]]; then
        export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$repository_root/.tools/espressif}"
        # shellcheck source=/dev/null
        source "$export_script"
    fi
}

run_guardrails() {
    python3 "$repository_root/tools/check_architecture.py"
    python3 "$repository_root/tools/check_traceability.py"
    python3 "$repository_root/tools/check_partitions.py" \
        "$repository_root/partitions.csv"
    python3 "$repository_root/tools/check_release_workflow.py"
}

run_host_validation() {
    command -v cmake >/dev/null 2>&1 || {
        echo "cmake is required for host validation" >&2
        exit 1
    }
    command -v ninja >/dev/null 2>&1 || {
        echo "ninja is required for host validation" >&2
        exit 1
    }

    python3 "$repository_root/tools/check_m12_http_fixture.py"
    python3 "$repository_root/tools/test_release_bundle.py"
    python3 "$repository_root/tools/test_release_workflow.py"

    cmake -S "$repository_root/tests" -B "$repository_root/build-host" -G Ninja
    cmake --build "$repository_root/build-host" --clean-first
    ctest --test-dir "$repository_root/build-host" --output-on-failure
    if "$repository_root/build-host/smoker_v0_tests" invalid-group >/dev/null 2>&1; then
        echo "smoker_v0_tests unexpectedly accepted an unknown group" >&2
        exit 1
    fi
    echo "Unknown host-test group rejection: PASS"

    cmake \
        -S "$repository_root/tests" \
        -B "$repository_root/build-host-sanitize" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build "$repository_root/build-host-sanitize" --clean-first
    ctest --test-dir "$repository_root/build-host-sanitize" --output-on-failure
}

run_idf_validation() {
    command -v idf.py >/dev/null 2>&1 || {
        echo "ESP-IDF v6.0.2 must be activated for target validation" >&2
        exit 1
    }

    local idf_version
    idf_version="$(idf.py --version)"
    if [[ "$idf_version" != "ESP-IDF v6.0.2" ]]; then
        echo "expected ESP-IDF v6.0.2, found: $idf_version" >&2
        exit 1
    fi
    local target_build="${SMOKER_VERIFY_BUILD_DIR:-$repository_root/build-verify}"
    idf.py -C "$repository_root" \
        -B "$target_build" \
        -D "SDKCONFIG=$target_build/sdkconfig" \
        build
    python3 "$repository_root/tools/check_effective_sdkconfig.py" \
        "$target_build/sdkconfig"
    python3 "$repository_root/tools/check_target_compile_commands.py" \
        "$target_build/compile_commands.json"
    local generated_partition_csv
    generated_partition_csv="$(mktemp)"
    python3 \
        "$IDF_PATH/components/partition_table/gen_esp32part.py" \
        --quiet \
        "$target_build/partition_table/partition-table.bin" \
        "$generated_partition_csv"
    python3 "$repository_root/tools/check_partitions.py" \
        "$generated_partition_csv"
    rm -f "$generated_partition_csv"
    python3 "$repository_root/tools/check_firmware_size.py" \
        "$target_build/smoker_controller.bin" \
        --partition-size 3145728 \
        --maximum-used-percent 75

    local flash_target
    local flash_guard_log
    for flash_target in \
        flash \
        app-flash \
        bootloader-flash \
        partition-table-flash \
        otadata-flash; do
        flash_guard_log="$(mktemp)"
        if idf.py -C "$repository_root" \
            -B "$target_build" \
            -p /dev/null \
            "$flash_target" >"$flash_guard_log" 2>&1; then
            echo "unsigned ESP-IDF target unexpectedly succeeded: $flash_target" >&2
            rm -f "$flash_guard_log"
            exit 1
        fi
        if ! grep -Fq "M14 blocks ESP-IDF's unsigned serial flash targets" \
            "$flash_guard_log"; then
            echo "unsigned ESP-IDF target did not fail through the M14 guard: $flash_target" >&2
            cat "$flash_guard_log" >&2
            rm -f "$flash_guard_log"
            exit 1
        fi
        rm -f "$flash_guard_log"
    done
    echo "Unsigned ESP-IDF flash target rejection: PASS"
}

cd "$repository_root"
activate_local_idf_if_needed
run_guardrails

if [[ "$mode" != "--idf-only" ]]; then
    run_host_validation
fi
if [[ "$mode" != "--host-only" ]]; then
    run_idf_validation
fi

echo "M0-M15 verification: PASS ($mode)"
