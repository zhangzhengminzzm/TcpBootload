from pathlib import Path

from tcp_bootload_host.model.dto import OperationResult
from tcp_bootload_host.protocol.bootload_protocol import BootloadProtocol
from tcp_bootload_host.protocol.modbus_client import ModbusClientAdapter


class UpgradeService:
    """Application service that coordinates UI actions and protocol operations."""

    def __init__(self) -> None:
        """Create the Modbus adapter and Bootload protocol facade."""
        self._modbus = ModbusClientAdapter()
        self._bootload = BootloadProtocol(self._modbus)

    @property
    def connected(self) -> bool:
        """Return whether the current device session is connected over TCP."""
        return self._modbus.connected

    def connect(self, ip: str, port: int) -> OperationResult:
        """Connect to a device and return a UI-displayable result."""
        return self._modbus.connect(ip, port)

    def disconnect(self) -> OperationResult:
        """Disconnect from the current device and return a log message."""
        return self._modbus.close()

    def send_boot_command(self) -> OperationResult:
        """Ask the connected App firmware to reset into Bootloader mode."""
        return self._bootload.enter_bootloader()

    def precheck(self, firmware_path: str) -> bool:
        """Return whether the selected firmware path points to an existing file."""
        return Path(firmware_path).exists()
