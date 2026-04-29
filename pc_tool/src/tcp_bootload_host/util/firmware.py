from pathlib import Path


def read_firmware(path: str) -> bytes:
    """Read a firmware image file into memory.

    Args:
        path: File path selected in the UI.

    Returns:
        Raw firmware bytes.
    """
    return Path(path).read_bytes()
