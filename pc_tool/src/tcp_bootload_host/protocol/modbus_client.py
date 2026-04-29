from pymodbus.client import ModbusTcpClient

from tcp_bootload_host.model.dto import OperationResult, RegisterReadResult


class ModbusClientAdapter:
    """Small synchronous adapter around ``pymodbus`` TCP client operations."""

    def __init__(self) -> None:
        """Initialize an adapter with no active TCP client."""
        self._client: ModbusTcpClient | None = None

    @property
    def connected(self) -> bool:
        """Return whether the underlying TCP socket is currently connected."""
        return bool(self._client and self._client.connected)

    def connect(self, ip: str, port: int, timeout: float = 3.0) -> OperationResult:
        """Open a Modbus TCP connection to the target device.

        Any previous connection is closed before dialing the new endpoint so the UI
        can safely reconnect without leaking sockets.

        Args:
            ip: Device IPv4 address or hostname.
            port: Modbus TCP port, usually 502.
            timeout: Socket timeout in seconds.

        Returns:
            ``OperationResult`` with a log-ready connection message.
        """
        self.close()
        self._client = ModbusTcpClient(ip, port=port, timeout=timeout)

        if self._client.connect():
            return OperationResult(True, f"TCP connected: {ip}:{port}")

        self.close()
        return OperationResult(False, f"TCP connect failed: {ip}:{port}")

    def close(self) -> OperationResult:
        """Close the active Modbus TCP connection if one exists.

        Returns:
            ``OperationResult`` indicating whether a socket was closed or the
            adapter was already disconnected.
        """
        if self._client:
            self._client.close()
            self._client = None
            return OperationResult(True, "TCP disconnected")
        return OperationResult(True, "TCP already disconnected")

    def write_holding_register(
        self,
        address: int,
        value: int,
        device_id: int,
    ) -> OperationResult:
        """Write one Modbus holding register using function code 0x06.

        Args:
            address: Zero-based Modbus protocol address of the holding register.
            value: Unsigned 16-bit value to write.
            device_id: Modbus Unit ID used by the MCU stack.

        Returns:
            ``OperationResult`` containing either the write confirmation or the
            Modbus/transport error text.
        """
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

    def write_holding_registers(
        self,
        address: int,
        values: list[int],
        device_id: int,
    ) -> OperationResult:
        """Write multiple contiguous Modbus holding registers using function 0x10.

        Args:
            address: Zero-based Modbus protocol address of the first register.
            values: Unsigned 16-bit values to write.
            device_id: Modbus Unit ID used by the MCU stack.

        Returns:
            ``OperationResult`` containing either the write confirmation or the
            Modbus/transport error text.
        """
        if not self._client or not self._client.connected:
            return OperationResult(False, "Modbus TCP is not connected")

        try:
            response = self._client.write_registers(
                address,
                values,
                device_id=device_id,
            )
        except Exception as exc:
            return OperationResult(False, f"Write holding registers failed: {exc}")

        if response.isError():
            return OperationResult(False, f"Modbus exception: {response}")

        return OperationResult(
            True,
            f"Wrote {len(values)} holding registers from 0x{address:04X}",
        )

    def read_holding_registers(
        self,
        address: int,
        count: int,
        device_id: int,
    ) -> RegisterReadResult:
        """Read one or more Modbus holding registers using function 0x03.

        Args:
            address: Zero-based Modbus protocol address of the first register.
            count: Number of contiguous holding registers to read.
            device_id: Modbus Unit ID used by the MCU stack.

        Returns:
            ``RegisterReadResult`` containing the register values or error text.
        """
        if not self._client or not self._client.connected:
            return RegisterReadResult(False, "Modbus TCP is not connected", [])

        try:
            response = self._client.read_holding_registers(
                address,
                count=count,
                device_id=device_id,
            )
        except Exception as exc:
            return RegisterReadResult(False, f"Read holding registers failed: {exc}", [])

        if response.isError():
            return RegisterReadResult(False, f"Modbus exception: {response}", [])

        return RegisterReadResult(
            True,
            f"Read {count} holding registers from 0x{address:04X}",
            list(response.registers),
        )
