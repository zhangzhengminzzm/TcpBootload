import math
import time
from collections.abc import Callable
from pathlib import Path
from typing import TypeVar

from tcp_bootload_host.model.dto import OperationResult, RegisterReadResult, UpgradeEvent
from tcp_bootload_host.protocol.bootload_protocol import BootloadProtocol
from tcp_bootload_host.protocol.modbus_client import ModbusClientAdapter
from tcp_bootload_host.protocol.register_map import (
    BOOT_DATA_BYTES_PER_PACKET,
    BOOT_STATUS_BUSY,
    BOOT_STATUS_ERROR,
    BOOT_STATUS_IDLE,
    BOOT_STATUS_PACKET_OK,
    BOOT_STATUS_READY,
    BOOT_STATUS_VERIFY_OK,
)


CONTROL_COMMAND_INTERVAL_SECONDS = 0.5
FIRMWARE_PACKET_INTERVAL_SECONDS = 0.1
STATUS_WAIT_TIMEOUT_SECONDS = 8.0
ERASE_WAIT_TIMEOUT_SECONDS = 30.0
VERIFY_WAIT_TIMEOUT_SECONDS = 15.0
DEFAULT_SESSION_ID = 1

_T = TypeVar("_T", OperationResult, RegisterReadResult)
UpgradeEventCallback = Callable[[UpgradeEvent], None]


class UpgradeService:
    """Coordinate one Modbus TCP endpoint used by the host tool.

    One UI owns two instances of this service: one for the APP endpoint and one
    for the BOOT endpoint. Keeping them separate avoids mixing the APP reset
    command with the Bootloader firmware-transfer session.
    """

    def __init__(self) -> None:
        """Create the Modbus adapter and Bootload protocol facade."""
        self._modbus = ModbusClientAdapter()
        self._bootload = BootloadProtocol(self._modbus)
        self._last_control_command_at: float | None = None
        self._last_packet_at: float | None = None

    @property
    def connected(self) -> bool:
        """Return whether the current endpoint is connected over TCP."""
        return self._modbus.connected

    def connect(self, ip: str, port: int) -> OperationResult:
        """Connect to one Modbus TCP endpoint and return a UI-displayable result."""
        return self._run_control_command(lambda: self._modbus.connect(ip, port))

    def disconnect(self) -> OperationResult:
        """Disconnect from the current endpoint and return a log message."""
        self._last_packet_at = None
        return self._modbus.close()

    def send_boot_command(self) -> OperationResult:
        """Write the APP-side BOOT command that asks firmware to reset into Boot."""
        return self._run_control_command(self._bootload.enter_bootloader)

    def request_app_enter_bootloader(
        self,
        ip: str,
        port: int,
        event_callback: UpgradeEventCallback | None = None,
    ) -> OperationResult:
        """Run only the APP-side flow: write Flash flag through APP and reset.

        This method intentionally stops after writing the BOOT command. The APP
        firmware is responsible for writing the Flash boot_flag and resetting the
        MCU. The Bootloader firmware-transfer flow is started separately through
        ``upgrade_bootloader_firmware`` using the BOOT IP/port.
        """
        self._emit(event_callback, "app", "RUNNING", f"Connecting APP Modbus TCP {ip}:{port}.", 5)
        connect_result = self.connect(ip, port)
        if not connect_result.success:
            self._emit(event_callback, "app", "FAIL", connect_result.message, 0)
            return connect_result

        self._emit(event_callback, "app", "INFO", connect_result.message, 8)
        self._emit(
            event_callback,
            "app",
            "RUNNING",
            "Writing APP BOOT command only. APP should write Flash boot_flag and reset.",
            10,
        )

        boot_result = self.send_boot_command()
        self._modbus.close()
        if not boot_result.success:
            self._emit(event_callback, "app", "FAIL", boot_result.message, 0)
            return boot_result

        detail = f"{boot_result.message}; APP should write Flash boot_flag and reset."
        self._emit(event_callback, "app", "OK", detail, 15)
        return OperationResult(True, "APP BOOT request sent. APP should write Flash flag and reset.")

    def precheck(self, firmware_path: str) -> bool:
        """Return whether the selected firmware path points to an existing file."""
        return Path(firmware_path).exists()

    def upgrade_bootloader_firmware(
        self,
        firmware_path: str,
        ip: str,
        port: int,
        event_callback: UpgradeEventCallback | None = None,
    ) -> OperationResult:
        """Run the Bootloader-side firmware upgrade flow.

        Timing rule:
            - APP reset/flag writing is not part of this method.
            - All non-packet Bootloader commands/polls are spaced at least 500 ms.
            - Firmware packet submission is paced at 100 ms per packet.
        """
        firmware_file = Path(firmware_path)
        if not firmware_file.is_file():
            return OperationResult(False, f"Firmware file not found: {firmware_path}")

        firmware = firmware_file.read_bytes()
        image_size = len(firmware)
        if image_size == 0:
            return OperationResult(False, "Firmware file is empty.")

        image_crc = self._bootload.crc32_value(firmware)
        total_packets = max(1, math.ceil(image_size / BOOT_DATA_BYTES_PER_PACKET))
        self._last_packet_at = None

        self._emit(
            event_callback,
            "transfer",
            "INFO",
            "Timing: control commands >= 500 ms; firmware packets = 100 ms/packet.",
        )

        self._emit(event_callback, "tcp", "RUNNING", f"Connecting BOOT Modbus TCP {ip}:{port}.", 5)
        connect_result = self.connect(ip, port)
        if not connect_result.success:
            self._emit(event_callback, "tcp", "FAIL", connect_result.message, 0)
            return connect_result
        self._emit(event_callback, "tcp", "OK", connect_result.message, 10)

        ready_result = self._wait_for_bootloader_ready(event_callback)
        if not ready_result.success:
            return ready_result

        session_result = self._start_session(
            image_size=image_size,
            image_crc=image_crc,
            event_callback=event_callback,
        )
        if not session_result.success:
            return session_result

        erase_result = self._erase_application(event_callback)
        if not erase_result.success:
            return erase_result

        transfer_result = self._transfer_firmware(
            firmware=firmware,
            total_packets=total_packets,
            event_callback=event_callback,
        )
        if not transfer_result.success:
            return transfer_result

        verify_result = self._verify_image(event_callback)
        if not verify_result.success:
            return verify_result

        activate_result = self._activate_image(event_callback)
        if not activate_result.success:
            return activate_result

        self._modbus.close()
        self._emit(event_callback, "complete", "OK", "Upgrade complete. Device is rebooting to APP.", 100)
        return OperationResult(True, "BOOT upgrade complete.")

    def upgrade_firmware(
        self,
        firmware_path: str,
        ip: str,
        port: int,
        event_callback: UpgradeEventCallback | None = None,
    ) -> OperationResult:
        """Backward-compatible wrapper for the Bootloader-side upgrade flow."""
        return self.upgrade_bootloader_firmware(
            firmware_path=firmware_path,
            ip=ip,
            port=port,
            event_callback=event_callback,
        )

    def _wait_for_bootloader_ready(self, event_callback: UpgradeEventCallback | None) -> OperationResult:
        """Wait until the connected BOOT endpoint reports READY."""
        self._emit(event_callback, "ready", "RUNNING", "Reading BOOT_STATUS and waiting for READY.", 15)
        ready_result = self._wait_for_status(
            expected={BOOT_STATUS_READY},
            timeout_seconds=STATUS_WAIT_TIMEOUT_SECONDS,
            step="ready",
            event_callback=event_callback,
        )
        if not ready_result.success:
            return ready_result

        self._emit(event_callback, "ready", "OK", "Bootloader status is READY.", 22)
        return ready_result

    def _start_session(
        self,
        image_size: int,
        image_crc: int,
        event_callback: UpgradeEventCallback | None,
    ) -> OperationResult:
        """Write session metadata and send SESSION_START with 500 ms spacing."""
        self._emit(
            event_callback,
            "session",
            "RUNNING",
            f"Writing session id {DEFAULT_SESSION_ID}.",
            25,
        )
        result = self._run_control_command(lambda: self._bootload.write_session_id(DEFAULT_SESSION_ID))
        if not result.success:
            self._emit(event_callback, "session", "FAIL", result.message, 0)
            return result

        self._emit(
            event_callback,
            "session",
            "INFO",
            f"Writing image metadata: size={image_size}, CRC32=0x{image_crc:08X}.",
            28,
        )
        result = self._run_control_command(lambda: self._bootload.write_image_metadata(image_size, image_crc))
        if not result.success:
            self._emit(event_callback, "session", "FAIL", result.message, 0)
            return result

        result = self._run_control_command(self._bootload.start_session)
        if not result.success:
            self._emit(event_callback, "session", "FAIL", result.message, 0)
            return result

        wait_result = self._wait_for_status(
            expected={BOOT_STATUS_READY},
            timeout_seconds=STATUS_WAIT_TIMEOUT_SECONDS,
            step="session",
            event_callback=event_callback,
        )
        if not wait_result.success:
            return wait_result

        self._emit(event_callback, "session", "OK", "Session is ready.", 35)
        return OperationResult(True, "Session is ready.")

    def _erase_application(self, event_callback: UpgradeEventCallback | None) -> OperationResult:
        """Send ERASE_APP and wait until Bootloader returns READY."""
        self._emit(event_callback, "erase", "RUNNING", "Sending ERASE_APP command.", 38)
        result = self._run_control_command(self._bootload.erase_application)
        if not result.success:
            self._emit(event_callback, "erase", "FAIL", result.message, 0)
            return result

        wait_result = self._wait_for_status(
            expected={BOOT_STATUS_READY},
            timeout_seconds=ERASE_WAIT_TIMEOUT_SECONDS,
            step="erase",
            event_callback=event_callback,
        )
        if not wait_result.success:
            return wait_result

        self._emit(event_callback, "erase", "OK", "Application Flash erase complete.", 45)
        return OperationResult(True, "Application Flash erase complete.")

    def _transfer_firmware(
        self,
        firmware: bytes,
        total_packets: int,
        event_callback: UpgradeEventCallback | None,
    ) -> OperationResult:
        """Send firmware packets using the 100 ms packet pacing rule."""
        self._emit(
            event_callback,
            "transfer",
            "RUNNING",
            f"Sending {total_packets} packets, {BOOT_DATA_BYTES_PER_PACKET} bytes max each.",
            45,
        )

        for packet_index in range(total_packets):
            offset = packet_index * BOOT_DATA_BYTES_PER_PACKET
            packet = firmware[offset : offset + BOOT_DATA_BYTES_PER_PACKET]
            self._pace_packet()

            result = self._bootload.write_packet(packet_index, packet)
            if not result.success:
                self._emit(event_callback, "transfer", "FAIL", result.message, 0)
                return result

            time.sleep(FIRMWARE_PACKET_INTERVAL_SECONDS)
            status_result = self._bootload.read_status()
            if not status_result.success or not status_result.registers:
                detail = status_result.message
                self._emit(event_callback, "transfer", "FAIL", detail, 0)
                return OperationResult(False, detail)

            status = status_result.registers[0]
            if status == BOOT_STATUS_ERROR:
                error_detail = self._read_error_detail(packet_mode=True)
                self._emit(event_callback, "transfer", "FAIL", error_detail, 0)
                return OperationResult(False, error_detail)

            if status != BOOT_STATUS_PACKET_OK:
                detail = f"Unexpected packet status: {self._status_text(status)}"
                self._emit(event_callback, "transfer", "FAIL", detail, 0)
                return OperationResult(False, detail)

            progress = 45 + int(((packet_index + 1) / total_packets) * 40)
            if self._should_report_packet(packet_index, total_packets):
                self._emit(
                    event_callback,
                    "transfer",
                    "RUNNING",
                    f"Packet {packet_index + 1}/{total_packets} OK.",
                    progress,
                )

        self._emit(event_callback, "transfer", "OK", "All firmware packets sent.", 85)
        return OperationResult(True, "All firmware packets sent.")

    def _verify_image(self, event_callback: UpgradeEventCallback | None) -> OperationResult:
        """Send VERIFY_IMAGE and wait for VERIFY_OK using 500 ms spacing."""
        self._emit(event_callback, "verify", "RUNNING", "Sending VERIFY_IMAGE command.", 90)
        result = self._run_control_command(self._bootload.verify_image)
        if not result.success:
            self._emit(event_callback, "verify", "FAIL", result.message, 0)
            return result

        wait_result = self._wait_for_status(
            expected={BOOT_STATUS_VERIFY_OK},
            timeout_seconds=VERIFY_WAIT_TIMEOUT_SECONDS,
            step="verify",
            event_callback=event_callback,
        )
        if not wait_result.success:
            return wait_result

        self._emit(event_callback, "verify", "OK", "Image CRC verify OK.", 95)
        return OperationResult(True, "Image CRC verify OK.")

    def _activate_image(self, event_callback: UpgradeEventCallback | None) -> OperationResult:
        """Send ACTIVATE_IMAGE using the 500 ms control-command pacing rule."""
        self._emit(event_callback, "activate", "RUNNING", "Sending ACTIVATE_IMAGE command.", 98)
        result = self._run_control_command(self._bootload.activate_image)
        if not result.success:
            self._emit(event_callback, "activate", "FAIL", result.message, 0)
            return result

        self._emit(event_callback, "activate", "OK", result.message, 100)
        return result

    def _wait_for_status(
        self,
        expected: set[int],
        timeout_seconds: float,
        step: str,
        event_callback: UpgradeEventCallback | None,
    ) -> OperationResult:
        """Poll BOOT_STATUS at 500 ms or slower until one of expected states appears."""
        deadline = time.monotonic() + timeout_seconds
        last_status = "no status read"

        while time.monotonic() < deadline:
            read_result = self._run_control_command(self._bootload.read_status)
            if read_result.success and read_result.registers:
                status = read_result.registers[0]
                last_status = self._status_text(status)

                if status == BOOT_STATUS_ERROR:
                    detail = self._read_error_detail(packet_mode=False)
                    self._emit(event_callback, step, "FAIL", detail, 0)
                    return OperationResult(False, detail)

                if status in expected:
                    return OperationResult(True, f"Status reached {self._status_text(status)}")

                self._emit(event_callback, step, "INFO", f"Waiting, current status {last_status}.")
            else:
                last_status = read_result.message
                self._emit(event_callback, step, "INFO", f"Status read failed, retrying: {last_status}")

        detail = f"Timeout waiting for status {self._expected_status_text(expected)}; last={last_status}"
        self._emit(event_callback, step, "FAIL", detail, 0)
        return OperationResult(False, detail)

    def _read_error_detail(self, packet_mode: bool) -> str:
        """Read BOOT_ERROR and format it for logs."""
        read_error = self._bootload.read_error() if packet_mode else self._run_control_command(self._bootload.read_error)
        if read_error.success and read_error.registers:
            error_code = read_error.registers[0]
            return f"Bootloader error: 0x{error_code:04X} ({self._bootload.describe_error(error_code)})"
        return f"Bootloader entered ERROR state, but error register read failed: {read_error.message}"

    def _run_control_command(self, operation: Callable[[], _T]) -> _T:
        """Run one non-packet operation after enforcing the 500 ms interval."""
        self._pace_control_command()
        return operation()

    def _pace_control_command(self) -> None:
        """Sleep until the next non-packet command is allowed."""
        now = time.monotonic()
        if self._last_control_command_at is not None:
            remaining = CONTROL_COMMAND_INTERVAL_SECONDS - (now - self._last_control_command_at)
            if remaining > 0:
                time.sleep(remaining)
        self._last_control_command_at = time.monotonic()

    def _pace_packet(self) -> None:
        """Sleep until the next firmware packet is allowed."""
        now = time.monotonic()
        if self._last_packet_at is not None:
            remaining = FIRMWARE_PACKET_INTERVAL_SECONDS - (now - self._last_packet_at)
            if remaining > 0:
                time.sleep(remaining)
        self._last_packet_at = time.monotonic()

    def _emit(
        self,
        callback: UpgradeEventCallback | None,
        step: str,
        status: str,
        detail: str,
        progress: int | None = None,
    ) -> None:
        """Emit a workflow event when the caller supplied a callback."""
        if callback:
            callback(UpgradeEvent(step=step, status=status, detail=detail, progress=progress))

    def _status_text(self, status: int) -> str:
        """Return a readable status string."""
        names = {
            BOOT_STATUS_IDLE: "IDLE",
            BOOT_STATUS_BUSY: "BUSY",
            BOOT_STATUS_READY: "READY",
            BOOT_STATUS_PACKET_OK: "PACKET_OK",
            BOOT_STATUS_VERIFY_OK: "VERIFY_OK",
            BOOT_STATUS_ERROR: "ERROR",
        }
        return f"0x{status:04X} ({names.get(status, 'UNKNOWN')})"

    def _expected_status_text(self, expected: set[int]) -> str:
        """Return a readable string for a set of expected status values."""
        return ", ".join(self._status_text(status) for status in sorted(expected))

    def _should_report_packet(self, packet_index: int, total_packets: int) -> bool:
        """Avoid flooding the UI while still showing useful packet progress."""
        if packet_index == 0 or packet_index == total_packets - 1:
            return True
        report_every = max(1, total_packets // 20)
        return (packet_index + 1) % report_every == 0