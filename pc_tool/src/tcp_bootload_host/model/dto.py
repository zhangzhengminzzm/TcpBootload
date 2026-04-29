from dataclasses import dataclass


@dataclass
class FirmwareMeta:
    """Parsed firmware metadata displayed and checked before an upgrade.

    Attributes:
        version: Firmware version string extracted from metadata or filename.
        size: Firmware image size in bytes.
        crc32: Uppercase hexadecimal CRC32 of the full firmware image.
    """

    version: str
    size: int
    crc32: str


@dataclass
class OperationResult:
    """Generic result object returned by service and protocol operations.

    Attributes:
        success: ``True`` when the operation completed as expected.
        message: Human-readable status or error text suitable for the log panel.
    """

    success: bool
    message: str
