from tcp_bootload_host.model.dto import OperationResult
from tcp_bootload_host.protocol.modbus_client import ModbusClientAdapter
from tcp_bootload_host.protocol.register_map import (
    BOOT_COMMAND_REGISTER,
    BOOT_COMMAND_REGISTER_DISPLAY,
    BOOT_COMMAND_VALUE,
    MODBUS_DEVICE_ID,
)


class BootloadProtocol:
    """High-level Bootload command wrapper built on top of Modbus TCP."""

    def __init__(self, modbus_client: ModbusClientAdapter) -> None:
        """Store the Modbus adapter used to send Bootload holding-register commands.

        Args:
            modbus_client: Connected Modbus TCP adapter. Ownership stays with the
                service layer so UI disconnects and reconnects can reuse it safely.
        """
        self._modbus = modbus_client

    def enter_bootloader(self) -> OperationResult:
        """Send the App-to-Bootloader command through the configured holding register.

        The command is written to ``BOOT_COMMAND_REGISTER`` with value
        ``BOOT_COMMAND_VALUE``. On the MCU this request normally causes a reset, so
        future transfer stages should reconnect before continuing with the session.

        Returns:
            ``OperationResult`` describing whether the write request was accepted by
            the Modbus client layer.
        """
        result = self._modbus.write_holding_register(
            address=BOOT_COMMAND_REGISTER,
            value=BOOT_COMMAND_VALUE,
            device_id=MODBUS_DEVICE_ID,
        )
        if result.success:
            return OperationResult(
                True,
                "BOOT command sent: "
                f"{BOOT_COMMAND_REGISTER_DISPLAY} = 0x{BOOT_COMMAND_VALUE:04X}",
            )
        return result
