# OTA signing public key

`smoker_ota_signing_public.pem` is the public half of the M13 RSA-3072 OTA
signing identity. Its SHA-256 SubjectPublicKeyInfo fingerprint is:

```text
195ef507154fe0bc1b8726f7310f174afcc6f385b13e5e84535eb7bcabfd63e7
```

This public key is intentionally versioned. It cannot create a valid firmware
signature. `tools/sign_release_firmware.sh` verifies every release image
against it, which also makes a wrong or unexpectedly replaced GitHub secret
fail before publication.

ESP-IDF's signed-update-without-Secure-Boot mode takes the trusted public-key
digest from the signature block of the currently running signed application.
Consequently the first full serial M13 installation and every OTA release must
be signed by the matching private key. The private key is maintainer-only,
ignored under `local-secrets/`, backed up separately, and supplied to the
release job only through the tag-restricted `firmware-release` environment
secret.
