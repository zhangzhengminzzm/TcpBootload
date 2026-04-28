from pathlib import Path

from tcp_bootload_host.model.dto import OperationResult
from tcp_bootload_host.protocol.bootload_protocol import BootloadProtocol
from tcp_bootload_host.protocol.modbus_client import ModbusClientAdapter


class UpgradeService:
    def __init__(self) -> None:
        self._modbus = ModbusClientAdapter()
        self._bootload = BootloadProtocol(self._modbus)

    @property
    def connected(self) -> bool:
        return self._modbus.connected

    def connect(self, ip: str, port: int) -> OperationResult:
        return self._modbus.connect(ip, port)

    def close(self) -> None:
        self._modbus.close()

    def send_boot_command(self) -> OperationResult:
        return self._bootload.enter_bootloader()

    def precheck(self, firmware_path: str) -> bool:
        return Path(firmware_path).exists()
