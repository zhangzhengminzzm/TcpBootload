import binascii

from tcp_bootload_host.model.dto import OperationResult, RegisterReadResult
from tcp_bootload_host.protocol.modbus_client import ModbusClientAdapter
from tcp_bootload_host.protocol.register_map import (
    BOOT_COMMAND_REGISTER,
    BOOT_COMMAND_REGISTER_DISPLAY,
    BOOT_COMMAND_VALUE,
    BOOT_CMD_ACTIVATE_IMAGE,
    BOOT_CMD_ERASE_APP,
    BOOT_CMD_SESSION_START,
    BOOT_CMD_TRANSFER_PACKET,
    BOOT_CMD_VERIFY_IMAGE,
    BOOT_DATA_START_REGISTER,
    BOOT_ERROR_REGISTER,
    BOOT_ERROR_TEXT,
    BOOT_IMAGE_SIZE_H_REGISTER,
    BOOT_PACKET_INDEX_H_REGISTER,
    BOOT_SESSION_ID_REGISTER,
    BOOT_STATUS_ERROR,
    BOOT_STATUS_REGISTER,
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

    @staticmethod
    def _u32_to_regs(value: int) -> tuple[int, int]:
        """Split an unsigned 32-bit value into high/low 16-bit registers."""
        return ((value >> 16) & 0xFFFF, value & 0xFFFF)

    @staticmethod
    def _packet_to_registers(data: bytes) -> list[int]:
        """Pack bytes into big-endian 16-bit Modbus register values."""
        values: list[int] = []
        for index in range(0, len(data), 2):
            high = data[index]
            low = data[index + 1] if index + 1 < len(data) else 0
            values.append((high << 8) | low)
        return values

    @staticmethod
    def crc32_value(data: bytes) -> int:
        """Calculate the standard CRC32 value used by the MCU Bootload code."""
        return binascii.crc32(data) & 0xFFFFFFFF

    def _write_command(self, command: int, name: str) -> OperationResult:
        """Write one Bootload command word to BOOT_COMMAND."""
        result = self._modbus.write_holding_register(
            address=BOOT_COMMAND_REGISTER,
            value=command,
            device_id=MODBUS_DEVICE_ID,
        )
        if result.success:
            return OperationResult(
                True,
                f"{name} command sent: {BOOT_COMMAND_REGISTER_DISPLAY} = 0x{command:04X}",
            )
        return result

    def enter_bootloader(self) -> OperationResult:
        """Send the App-to-Bootloader command through the configured holding register.

        The command is written to ``BOOT_COMMAND_REGISTER`` with value
        ``BOOT_COMMAND_VALUE``. On the MCU this request normally causes a reset, so
        future transfer stages should reconnect before continuing with the session.

        Returns:
            ``OperationResult`` describing whether the write request was accepted by
            the Modbus client layer.
        """
        return self._write_command(BOOT_COMMAND_VALUE, "BOOT")

    def write_session_id(self, session_id: int) -> OperationResult:
        """Write the upgrade session id register."""
        return self._modbus.write_holding_register(
            address=BOOT_SESSION_ID_REGISTER,
            value=session_id & 0xFFFF,
            device_id=MODBUS_DEVICE_ID,
        )

    def write_image_metadata(self, image_size: int, image_crc: int) -> OperationResult:
        """Write image size and image CRC registers."""
        size_h, size_l = self._u32_to_regs(image_size)
        crc_h, crc_l = self._u32_to_regs(image_crc)
        return self._modbus.write_holding_registers(
            address=BOOT_IMAGE_SIZE_H_REGISTER,
            values=[size_h, size_l, crc_h, crc_l],
            device_id=MODBUS_DEVICE_ID,
        )

    def start_session(self) -> OperationResult:
        """Send SESSION_START command after metadata registers are written."""
        return self._write_command(BOOT_CMD_SESSION_START, "SESSION_START")

    def erase_application(self) -> OperationResult:
        """Send ERASE_APP command to erase the application Flash partition."""
        return self._write_command(BOOT_CMD_ERASE_APP, "ERASE_APP")

    def write_packet(self, packet_index: int, data: bytes) -> OperationResult:
        """Write one firmware packet and submit it with TRANSFER_PACKET command."""
        packet_crc = self.crc32_value(data)
        index_h, index_l = self._u32_to_regs(packet_index)
        crc_h, crc_l = self._u32_to_regs(packet_crc)

        meta_result = self._modbus.write_holding_registers(
            address=BOOT_PACKET_INDEX_H_REGISTER,
            values=[index_h, index_l, len(data), crc_h, crc_l],
            device_id=MODBUS_DEVICE_ID,
        )
        if not meta_result.success:
            return meta_result

        data_result = self._modbus.write_holding_registers(
            address=BOOT_DATA_START_REGISTER,
            values=self._packet_to_registers(data),
            device_id=MODBUS_DEVICE_ID,
        )
        if not data_result.success:
            return data_result

        command_result = self._write_command(BOOT_CMD_TRANSFER_PACKET, "TRANSFER_PACKET")
        if not command_result.success:
            return command_result

        return OperationResult(
            True,
            f"Packet {packet_index} sent: {len(data)} bytes, CRC32=0x{packet_crc:08X}",
        )

    def verify_image(self) -> OperationResult:
        """Send VERIFY_IMAGE command after all firmware packets are written."""
        return self._write_command(BOOT_CMD_VERIFY_IMAGE, "VERIFY_IMAGE")

    def activate_image(self) -> OperationResult:
        """Send ACTIVATE_IMAGE command to boot the verified application."""
        return self._write_command(BOOT_CMD_ACTIVATE_IMAGE, "ACTIVATE_IMAGE")

    def read_status(self) -> RegisterReadResult:
        """Read the Bootload status register."""
        return self._modbus.read_holding_registers(
            address=BOOT_STATUS_REGISTER,
            count=1,
            device_id=MODBUS_DEVICE_ID,
        )

    def read_error(self) -> RegisterReadResult:
        """Read the Bootload error register."""
        return self._modbus.read_holding_registers(
            address=BOOT_ERROR_REGISTER,
            count=1,
            device_id=MODBUS_DEVICE_ID,
        )

    def describe_error(self, error_code: int) -> str:
        """Return a readable Bootload error name."""
        return BOOT_ERROR_TEXT.get(error_code, f"UNKNOWN_0x{error_code:04X}")

    def status_is_error(self, status: int) -> bool:
        """Return whether a status word represents the Bootload ERROR state."""
        return status == BOOT_STATUS_ERROR
