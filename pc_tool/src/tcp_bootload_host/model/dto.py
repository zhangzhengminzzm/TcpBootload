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


@dataclass
class RegisterReadResult:
    """Result returned after reading one or more Modbus holding registers.

    Attributes:
        success: ``True`` when the read completed without Modbus/transport error.
        message: Human-readable detail suitable for the log panel.
        registers: Register values returned by the device. Empty on failure.
    """

    success: bool
    message: str
    registers: list[int]


@dataclass
class UpgradeEvent:
    """One progress/debug event emitted by the upgrade workflow.

    Attributes:
        step: Stable step key used by the UI table.
        status: Step state such as RUNNING, OK, FAIL, or INFO.
        detail: Detailed text displayed in the step table and log panel.
        progress: Optional 0..100 progress value.
    """

    step: str
    status: str
    detail: str
    progress: int | None = None
