# Firmware release workflow

`private_key.pem` is a generated development key and is ignored by Git. Back it
up outside the source tree. For production, generate a new key once:

```powershell
python tools/release_firmware.py keygen --force
```

Rebuild and reflash the Bootloader whenever the public key changes. Package the
current application with:

```powershell
python tools/release_firmware.py release `
  --version 1.0.0 --version-code 10000 `
  --base-url https://firmware.example.com/stable
```

Upload the versioned `.bin` first. Replace `manifest.json` last and configure
the web server to send `Cache-Control: no-cache` for the manifest. The binary
may use immutable long-term caching.

Verify all hashes, signatures, metadata and Intel HEX checksums before flashing
or publishing:

```powershell
python tools/verify_release.py
```
