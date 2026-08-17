message(FATAL_ERROR
    "M13 blocks ESP-IDF's unsigned serial flash targets. "
    "Build and sign the firmware, then use "
    "tools/flash_signed_firmware.py --port PORT --yes."
)
