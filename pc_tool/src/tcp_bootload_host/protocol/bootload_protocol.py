from tcp_bootload_host.model.dto import OperationResult
from tcp_bootload_host.protocol.modbus_client import ModbusClientAdapter
from tcp_bootload_host.protocol.register_map import (
    BOOT_COMMAND_REGISTER,
    BOOT_COMMAND_REGISTER_DISPLAY,
    BOOT_COMMAND_VALUE,
    MODBUS_DEVICE_ID,
)


class BootloadProtocol:
    def __init__(self, modbus_client: ModbusClientAdapter) -> None:
        self._modbus = modbus_client

    def enter_bootloader(self) -> OperationResult:
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
