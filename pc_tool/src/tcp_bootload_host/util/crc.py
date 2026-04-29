import binascii


def crc32_hex(data: bytes) -> str:
    """Calculate a firmware-compatible CRC32 string.

    Args:
        data: Raw bytes to checksum.

    Returns:
        Eight-character uppercase hexadecimal CRC32 value.
    """
    return f"{binascii.crc32(data) & 0xFFFFFFFF:08X}"
