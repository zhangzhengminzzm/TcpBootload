from pathlib import Path


def read_firmware(path: str) -> bytes:
    return Path(path).read_bytes()

