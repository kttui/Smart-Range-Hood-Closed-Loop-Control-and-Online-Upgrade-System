#!/usr/bin/env python3
"""Verify a generated OTA release without using its private key."""

import argparse
import base64
import binascii
import hashlib
import json
import re
import struct
from pathlib import Path

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

from release_firmware import canonical_manifest


def load_public_key(header: Path):
    text = header.read_text(encoding="ascii")
    values = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", text))
    if len(values) != 64:
        raise ValueError(f"expected 64 public-key bytes in {header}, found {len(values)}")
    numbers = ec.EllipticCurvePublicNumbers(
        int.from_bytes(values[:32], "big"),
        int.from_bytes(values[32:], "big"),
        ec.SECP256R1(),
    )
    return numbers.public_key()


def verify_ihex(path: Path) -> tuple[int, int]:
    upper = 0
    lowest = 0xFFFFFFFF
    highest = 0
    eof_count = 0
    for line_number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_number}: invalid Intel HEX record")
        record = bytes.fromhex(line[1:])
        if sum(record) & 0xFF:
            raise ValueError(f"{path}:{line_number}: checksum mismatch")
        length = record[0]
        address = int.from_bytes(record[1:3], "big")
        record_type = record[3]
        if length + 5 != len(record):
            raise ValueError(f"{path}:{line_number}: record length mismatch")
        if record_type == 4:
            upper = int.from_bytes(record[4:6], "big") << 16
        elif record_type == 0:
            lowest = min(lowest, upper + address)
            highest = max(highest, upper + address + length)
        elif record_type == 1:
            eof_count += 1
    if eof_count != 1:
        raise ValueError(f"{path}: expected exactly one EOF record")
    return lowest, highest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", default="Release/firmware")
    parser.add_argument("--public-header", default="Common/ota_public_key.h")
    args = parser.parse_args()

    release = Path(args.release)
    manifest = json.loads((release / "manifest.json").read_text(encoding="utf-8"))
    firmware = (release / f"stm32-{manifest['version']}.bin").read_bytes()
    digest = hashlib.sha256(firmware).hexdigest()
    if len(firmware) != manifest["size"] or digest != manifest["sha256"]:
        raise ValueError("firmware size or SHA-256 does not match manifest")

    signature = base64.b64decode(manifest["signature"], validate=True)
    if len(signature) != 64:
        raise ValueError("manifest signature is not raw 64-byte r||s")
    r_value = int.from_bytes(signature[:32], "big")
    s_value = int.from_bytes(signature[32:], "big")
    load_public_key(Path(args.public_header)).verify(
        utils.encode_dss_signature(r_value, s_value),
        canonical_manifest(manifest["version_code"], manifest["size"], manifest["sha256"]),
        ec.ECDSA(hashes.SHA256()),
    )

    metadata = (release / "factory_metadata.bin").read_bytes()
    if len(metadata) != 128:
        raise ValueError("factory metadata must be 128 bytes")
    stored_crc = struct.unpack_from("<I", metadata, 120)[0]
    if stored_crc != (binascii.crc32(metadata[:120]) & 0xFFFFFFFF):
        raise ValueError("factory metadata CRC mismatch")
    fields = struct.unpack_from("<6I", metadata)
    if fields[4] != manifest["version_code"] or fields[5] != manifest["size"]:
        raise ValueError("factory metadata version or size mismatch")
    if metadata[24:56].hex() != manifest["sha256"] or metadata[56:120] != signature:
        raise ValueError("factory metadata hash or signature mismatch")

    metadata_b_clear = (release / "factory_metadata_b_clear.bin").read_bytes()
    if len(metadata_b_clear) != 128 or any(metadata_b_clear):
        raise ValueError("factory metadata B clear marker must be 128 zero bytes")

    bundle = release / "factory_bundle.hex"
    if bundle.exists():
        lowest, highest = verify_ihex(bundle)
        if lowest != 0x08000000 or highest > 0x08040000:
            raise ValueError(f"factory bundle outside STM32 flash: {lowest:#x}..{highest:#x}")
        print(f"factory bundle range: {lowest:#010x}..{highest:#010x}")
    print(f"release verified: version={manifest['version']} code={manifest['version_code']} size={len(firmware)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
