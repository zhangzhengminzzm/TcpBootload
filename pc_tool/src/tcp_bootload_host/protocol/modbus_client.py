from pymodbus.client import ModbusTcpClient

from tcp_bootload_host.model.dto import OperationResult


class ModbusClientAdapter:
    def __init__(self) -> None:
        self._client: ModbusTcpClient | None = None

    @property
    def connected(self) -> bool:
        return bool(self._client and self._client.connected)

    def connect(self, ip: str, port: int, timeout: float = 3.0) -> OperationResult:
        self.close()
        self._client = ModbusTcpClient(ip, port=port, timeout=timeout)

        if self._client.connect():
            return OperationResult(True, f"TCP connected: {ip}:{port}")

        self.close()
        return OperationResult(False, f"TCP connect failed: {ip}:{port}")

    def close(self) -> None:
        if self._client:
            self._client.close()
            self._client = None

    def write_holding_register(
        self,
        address: int,
        value: int,
        device_id: int,
    ) -> OperationResult:
        if not self._client or not self._client.connected:
            return OperationResult(False, "Modbus TCP is not connected")

        try:
            response = self._client.write_register(
                address,
                value,
                device_id=device_id,
            )
        except Exception as exc:
            return OperationResult(False, f"Write holding register failed: {exc}")

        if response.isError():
            return OperationResult(False, f"Modbus exception: {response}")

        return OperationResult(
            True,
            f"Wrote holding register 0x{address:04X} = 0x{value:04X}",
        )
