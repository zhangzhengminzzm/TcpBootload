from pathlib import Path
from datetime import datetime

from PyQt6.QtCore import QObject, QThread, pyqtSignal
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QFrame,
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QProgressBar,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from tcp_bootload_host.protocol.register_map import (
    BOOT_COMMAND_REGISTER_DISPLAY,
    BOOT_COMMAND_VALUE,
)
from tcp_bootload_host.service.upgrade_service import UpgradeService


class AppBootWorker(QObject):
    """Background worker that sends only the APP-side flag-and-reset command."""

    app_event = pyqtSignal(object)
    done = pyqtSignal(bool, str)

    def __init__(self, service: UpgradeService, ip: str, port: int) -> None:
        """Store APP endpoint parameters captured from the UI thread."""
        super().__init__()
        self._service = service
        self._ip = ip
        self._port = port

    def run(self) -> None:
        """Connect to APP, write BOOT command, and emit thread-safe UI events."""
        try:
            result = self._service.request_app_enter_bootloader(
                ip=self._ip,
                port=self._port,
                event_callback=self.app_event.emit,
            )
            self.done.emit(result.success, result.message)
        except Exception as exc:
            self.done.emit(False, f"APP BOOT worker failed: {exc}")


class UpgradeWorker(QObject):
    """Background worker that runs the Bootloader firmware upgrade workflow."""

    upgrade_event = pyqtSignal(object)
    done = pyqtSignal(bool, str)

    def __init__(
        self,
        service: UpgradeService,
        firmware_path: str,
        ip: str,
        port: int,
    ) -> None:
        """Store Bootloader upgrade parameters captured from the UI thread."""
        super().__init__()
        self._service = service
        self._firmware_path = firmware_path
        self._ip = ip
        self._port = port

    def run(self) -> None:
        """Execute the Bootloader upgrade and emit thread-safe UI events."""
        try:
            result = self._service.upgrade_bootloader_firmware(
                firmware_path=self._firmware_path,
                ip=self._ip,
                port=self._port,
                event_callback=self.upgrade_event.emit,
            )
            self.done.emit(result.success, result.message)
        except Exception as exc:
            self.done.emit(False, f"Upgrade worker failed: {exc}")


class MainWindow(QMainWindow):
    """Main PyQt window for APP reset and Bootloader firmware upgrade."""

    STEP_FILE_CHECK = 0
    STEP_APP_BOOT = 1
    STEP_BOOT_TCP = 2
    STEP_BOOT_READY = 3
    STEP_SESSION = 4
    STEP_ERASE = 5
    STEP_TRANSFER = 6
    STEP_VERIFY = 7
    STEP_ACTIVATE = 8
    STEP_COMPLETE = 9

    STEP_DEFINITIONS = (
        "Firmware file check",
        "APP flag + reset",
        "BOOT Modbus TCP connection",
        "Bootloader ready",
        "Session start",
        "Erase application",
        "Transfer firmware",
        "Verify image",
        "Activate image",
        "Complete",
    )

    STEP_KEY_TO_ROW = {
        "file": STEP_FILE_CHECK,
        "app": STEP_APP_BOOT,
        "tcp": STEP_BOOT_TCP,
        "ready": STEP_BOOT_READY,
        "session": STEP_SESSION,
        "erase": STEP_ERASE,
        "transfer": STEP_TRANSFER,
        "verify": STEP_VERIFY,
        "activate": STEP_ACTIVATE,
        "complete": STEP_COMPLETE,
    }

    def __init__(self) -> None:
        """Create services, build the UI, and initialize button/status state."""
        super().__init__()
        self._app_service = UpgradeService()
        self._boot_service = UpgradeService()
        self._upgrade_running = False
        self._app_thread: QThread | None = None
        self._app_worker: AppBootWorker | None = None
        self._upgrade_thread: QThread | None = None
        self._upgrade_worker: UpgradeWorker | None = None
        self.setWindowTitle("TCP Bootload Host")
        self.resize(1080, 720)
        self._init_ui()
        self._bind_events()
        self._set_app_connected(False)
        self._set_boot_connected(False)
        self._refresh_start_button()

    def _init_ui(self) -> None:
        """Build all widgets used by the host tool."""
        root = QWidget(self)
        layout = QVBoxLayout(root)

        app_row = QHBoxLayout()
        app_row.addWidget(QLabel("APP IP:"))
        self.app_ip_edit = QLineEdit("192.168.1.100")
        app_row.addWidget(self.app_ip_edit)
        app_row.addWidget(QLabel("APP Port:"))
        self.app_port_edit = QLineEdit("502")
        app_row.addWidget(self.app_port_edit)
        self.app_connect_btn = QPushButton("Connect APP")
        app_row.addWidget(self.app_connect_btn)
        self.app_disconnect_btn = QPushButton("Disconnect APP")
        app_row.addWidget(self.app_disconnect_btn)
        self.app_boot_btn = QPushButton("APP Flag + Reset")
        app_row.addWidget(self.app_boot_btn)
        self.app_status_lamp = QFrame()
        self.app_status_lamp.setFixedSize(18, 18)
        app_row.addWidget(self.app_status_lamp)
        self.app_status_label = QLabel("APP TCP disconnected")
        app_row.addWidget(self.app_status_label)
        layout.addLayout(app_row)

        boot_row = QHBoxLayout()
        boot_row.addWidget(QLabel("BOOT IP:"))
        self.boot_ip_edit = QLineEdit("192.168.1.101")
        boot_row.addWidget(self.boot_ip_edit)
        boot_row.addWidget(QLabel("BOOT Port:"))
        self.boot_port_edit = QLineEdit("502")
        boot_row.addWidget(self.boot_port_edit)
        self.boot_connect_btn = QPushButton("Connect BOOT")
        boot_row.addWidget(self.boot_connect_btn)
        self.boot_disconnect_btn = QPushButton("Disconnect BOOT")
        boot_row.addWidget(self.boot_disconnect_btn)
        self.boot_status_lamp = QFrame()
        self.boot_status_lamp.setFixedSize(18, 18)
        boot_row.addWidget(self.boot_status_lamp)
        self.boot_status_label = QLabel("BOOT TCP disconnected")
        boot_row.addWidget(self.boot_status_label)
        layout.addLayout(boot_row)

        fw_row = QHBoxLayout()
        self.file_edit = QLineEdit("")
        self.file_edit.setPlaceholderText("Select firmware file...")
        self.pick_file_btn = QPushButton("Browse")
        self.start_btn = QPushButton("Start BOOT Upgrade")
        fw_row.addWidget(self.file_edit)
        fw_row.addWidget(self.pick_file_btn)
        fw_row.addWidget(self.start_btn)
        layout.addLayout(fw_row)

        progress_row = QHBoxLayout()
        progress_row.addWidget(QLabel("Progress:"))
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        progress_row.addWidget(self.progress_bar)
        layout.addLayout(progress_row)

        layout.addWidget(QLabel("Upgrade Steps:"))
        self.step_table = QTableWidget(len(self.STEP_DEFINITIONS), 4)
        self.step_table.setHorizontalHeaderLabels(("Step", "Status", "Time", "Detail"))
        self.step_table.verticalHeader().setVisible(False)
        self.step_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.step_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.step_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.step_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.step_table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.step_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.step_table)
        self._reset_upgrade_steps()

        log_action_row = QHBoxLayout()
        log_action_row.addStretch()
        self.clear_log_btn = QPushButton("Clear Log")
        log_action_row.addWidget(self.clear_log_btn)
        layout.addLayout(log_action_row)

        self.log_panel = QTextEdit()
        self.log_panel.setReadOnly(True)
        self.log_panel.append("System ready.")
        self.log_panel.append(
            "BOOT command register: "
            f"{BOOT_COMMAND_REGISTER_DISPLAY}, value 0x{BOOT_COMMAND_VALUE:04X}"
        )
        self.log_panel.append("APP flow: write BOOT command only; APP writes Flash flag and resets.")
        self.log_panel.append("BOOT flow: connect BOOT IP and run session/erase/transfer/verify/activate.")
        layout.addWidget(self.log_panel)

        self.setCentralWidget(root)

    def _bind_events(self) -> None:
        """Connect UI signals to their slot handlers."""
        self.app_connect_btn.clicked.connect(self._on_app_connect_clicked)
        self.app_disconnect_btn.clicked.connect(self._on_app_disconnect_clicked)
        self.app_boot_btn.clicked.connect(self._on_app_boot_clicked)
        self.boot_connect_btn.clicked.connect(self._on_boot_connect_clicked)
        self.boot_disconnect_btn.clicked.connect(self._on_boot_disconnect_clicked)
        self.pick_file_btn.clicked.connect(self._on_pick_file_clicked)
        self.start_btn.clicked.connect(self._on_start_clicked)
        self.clear_log_btn.clicked.connect(self._on_clear_log_clicked)
        self.file_edit.textChanged.connect(self._on_firmware_path_changed)
        self.file_edit.textChanged.connect(self._refresh_start_button)

    def _set_status_widgets(
        self,
        lamp: QFrame,
        label: QLabel,
        connected: bool,
        connected_text: str,
        disconnected_text: str,
    ) -> None:
        """Update one TCP status lamp and label."""
        color = "#21a67a" if connected else "#c43d3d"
        lamp.setStyleSheet(
            "QFrame {"
            f"background-color: {color};"
            "border-radius: 9px;"
            "border: 1px solid rgba(0, 0, 0, 80);"
            "}"
        )
        label.setText(connected_text if connected else disconnected_text)

    def _set_app_connected(self, connected: bool) -> None:
        """Update widgets that reflect the APP TCP connection state."""
        self._set_status_widgets(
            self.app_status_lamp,
            self.app_status_label,
            connected,
            "APP TCP connected",
            "APP TCP disconnected",
        )
        self.app_connect_btn.setEnabled((not connected) and (not self._upgrade_running))
        self.app_disconnect_btn.setEnabled(connected and (not self._upgrade_running))
        self.app_boot_btn.setEnabled(not self._upgrade_running)

    def _set_boot_connected(self, connected: bool) -> None:
        """Update widgets that reflect the BOOT TCP connection state."""
        self._set_status_widgets(
            self.boot_status_lamp,
            self.boot_status_label,
            connected,
            "BOOT TCP connected",
            "BOOT TCP disconnected",
        )
        self.boot_connect_btn.setEnabled((not connected) and (not self._upgrade_running))
        self.boot_disconnect_btn.setEnabled(connected and (not self._upgrade_running))
        self._refresh_start_button()

    def _firmware_file_ready(self) -> bool:
        """Return whether the path field contains an existing firmware file."""
        return Path(self.file_edit.text().strip()).is_file()

    def _refresh_start_button(self) -> None:
        """Enable BOOT upgrade only when a valid firmware file is selected."""
        self.start_btn.setEnabled((not self._upgrade_running) and self._firmware_file_ready())

    def _append_log(self, message: str) -> None:
        """Append one status line to the message panel."""
        self.log_panel.append(message)

    def _set_progress(self, value: int) -> None:
        """Set upgrade progress while clamping the value to 0..100."""
        self.progress_bar.setValue(max(0, min(value, 100)))

    def _now_text(self) -> str:
        """Return a millisecond timestamp for step debugging."""
        return datetime.now().strftime("%H:%M:%S.%f")[:-3]

    def _reset_upgrade_steps(self) -> None:
        """Reset the upgrade step table to its initial debug state."""
        for row, step_name in enumerate(self.STEP_DEFINITIONS):
            self.step_table.setItem(row, 0, QTableWidgetItem(step_name))
            self.step_table.setItem(row, 1, QTableWidgetItem("PENDING"))
            self.step_table.setItem(row, 2, QTableWidgetItem("-"))
            self.step_table.setItem(row, 3, QTableWidgetItem(""))

    def _update_step(
        self,
        row: int,
        status: str,
        detail: str = "",
        write_log: bool = True,
    ) -> None:
        """Update one upgrade step row and mirror the same detail to the log."""
        self.step_table.setItem(row, 1, QTableWidgetItem(status))
        self.step_table.setItem(row, 2, QTableWidgetItem(self._now_text()))
        self.step_table.setItem(row, 3, QTableWidgetItem(detail))
        if write_log:
            self._append_log(f"[{self.STEP_DEFINITIONS[row]}] {status}: {detail}")

    def _parse_port(self, edit: QLineEdit, name: str) -> int | None:
        """Parse and validate one TCP port field."""
        try:
            port = int(edit.text().strip())
        except ValueError:
            self._append_log(f"Invalid {name} TCP port.")
            return None

        if not 1 <= port <= 65535:
            self._append_log(f"Invalid {name} TCP port range: {port}")
            return None
        return port

    def _on_app_connect_clicked(self) -> None:
        """Connect to the APP Modbus TCP endpoint."""
        ip = self.app_ip_edit.text().strip()
        port = self._parse_port(self.app_port_edit, "APP")
        if port is None:
            self._set_app_connected(False)
            return

        result = self._app_service.connect(ip, port)
        self._append_log(result.message)
        self._set_app_connected(result.success)
        self._update_step(
            self.STEP_APP_BOOT,
            "INFO" if result.success else "FAIL",
            f"APP endpoint {ip}:{port}: {result.message}",
        )

    def _on_app_disconnect_clicked(self) -> None:
        """Disconnect from the APP endpoint."""
        result = self._app_service.disconnect()
        self._append_log(result.message)
        self._set_app_connected(False)
        self._update_step(self.STEP_APP_BOOT, "INFO", result.message)

    def _on_app_boot_clicked(self) -> None:
        """Start the APP-side flag-and-reset command in a worker thread."""
        ip = self.app_ip_edit.text().strip()
        port = self._parse_port(self.app_port_edit, "APP")
        if port is None:
            return

        self._set_progress(0)
        self._append_log("APP flow started: write Flash flag command and reset only.")
        self._start_app_boot_worker(ip=ip, port=port)

    def _start_app_boot_worker(self, ip: str, port: int) -> None:
        """Create and start the worker thread that sends the APP BOOT command."""
        self._set_upgrade_running(True)

        thread = QThread(self)
        worker = AppBootWorker(service=self._app_service, ip=ip, port=port)
        worker.moveToThread(thread)

        thread.started.connect(worker.run)
        worker.app_event.connect(self._on_upgrade_event)
        worker.done.connect(self._on_app_boot_done)
        worker.done.connect(lambda _success, _message: worker.deleteLater())
        worker.done.connect(lambda _success, _message: thread.quit())
        thread.finished.connect(thread.deleteLater)
        thread.finished.connect(self._cleanup_app_thread)

        self._app_thread = thread
        self._app_worker = worker
        thread.start()

    def _on_boot_connect_clicked(self) -> None:
        """Connect to the BOOT Modbus TCP endpoint."""
        ip = self.boot_ip_edit.text().strip()
        port = self._parse_port(self.boot_port_edit, "BOOT")
        if port is None:
            self._set_boot_connected(False)
            return

        result = self._boot_service.connect(ip, port)
        self._append_log(result.message)
        self._set_boot_connected(result.success)
        self._update_step(
            self.STEP_BOOT_TCP,
            "OK" if result.success else "FAIL",
            f"BOOT endpoint {ip}:{port}: {result.message}",
        )

    def _on_boot_disconnect_clicked(self) -> None:
        """Disconnect from the BOOT endpoint."""
        result = self._boot_service.disconnect()
        self._append_log(result.message)
        self._set_boot_connected(False)
        self._set_progress(0)
        self._update_step(self.STEP_BOOT_TCP, "INFO", result.message)

    def _on_pick_file_clicked(self) -> None:
        """Open a file picker and store the selected firmware path."""
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select firmware",
            "",
            "Firmware (*.bin *.hex);;All files (*.*)",
        )
        if path:
            self.file_edit.setText(path)

    def _on_firmware_path_changed(self) -> None:
        """Update the file-check step whenever the firmware path changes."""
        firmware_path = self.file_edit.text().strip()
        if not firmware_path:
            self._update_step(
                self.STEP_FILE_CHECK,
                "PENDING",
                "No firmware file selected",
                write_log=False,
            )
        elif self._firmware_file_ready():
            file_size = Path(firmware_path).stat().st_size
            self._update_step(
                self.STEP_FILE_CHECK,
                "OK",
                f"{firmware_path} ({file_size} bytes)",
                write_log=False,
            )
        else:
            self._update_step(
                self.STEP_FILE_CHECK,
                "FAIL",
                f"File not found: {firmware_path}",
                write_log=False,
            )

    def _on_start_clicked(self) -> None:
        """Start the Bootloader firmware upgrade workflow in a worker thread."""
        if not self._firmware_file_ready():
            self._append_log("Please select a valid firmware file before upgrading.")
            self._update_step(
                self.STEP_FILE_CHECK,
                "FAIL",
                "Start blocked because firmware file is invalid",
            )
            self._refresh_start_button()
            return

        ip = self.boot_ip_edit.text().strip()
        port = self._parse_port(self.boot_port_edit, "BOOT")
        if port is None:
            return

        self._reset_upgrade_steps()
        self._on_firmware_path_changed()
        self._set_progress(0)
        self._append_log("BOOT upgrade started. APP flag/reset command is not part of this flow.")
        self._append_log("Timing rule: control commands >= 500 ms; firmware packets = 100 ms/packet.")
        self._start_upgrade_worker(
            firmware_path=self.file_edit.text().strip(),
            ip=ip,
            port=port,
        )

    def _start_upgrade_worker(self, firmware_path: str, ip: str, port: int) -> None:
        """Create and start the worker thread that runs the BOOT upgrade sequence."""
        self._set_upgrade_running(True)

        thread = QThread(self)
        worker = UpgradeWorker(
            service=self._boot_service,
            firmware_path=firmware_path,
            ip=ip,
            port=port,
        )
        worker.moveToThread(thread)

        thread.started.connect(worker.run)
        worker.upgrade_event.connect(self._on_upgrade_event)
        worker.done.connect(self._on_upgrade_done)
        worker.done.connect(lambda _success, _message: worker.deleteLater())
        worker.done.connect(lambda _success, _message: thread.quit())
        thread.finished.connect(thread.deleteLater)
        thread.finished.connect(self._cleanup_upgrade_thread)

        self._upgrade_thread = thread
        self._upgrade_worker = worker
        thread.start()

    def _set_upgrade_running(self, running: bool) -> None:
        """Enable or disable controls that must not change during an operation."""
        self._upgrade_running = running
        self.app_ip_edit.setEnabled(not running)
        self.app_port_edit.setEnabled(not running)
        self.boot_ip_edit.setEnabled(not running)
        self.boot_port_edit.setEnabled(not running)
        self.file_edit.setEnabled(not running)
        self.pick_file_btn.setEnabled(not running)
        self._set_app_connected(self._app_service.connected)
        self._set_boot_connected(self._boot_service.connected)
        self._refresh_start_button()

    def _on_upgrade_event(self, event: object) -> None:
        """Apply one worker event to the progress bar, step table, and log."""
        step = getattr(event, "step", "")
        status = getattr(event, "status", "INFO")
        detail = getattr(event, "detail", "")
        progress = getattr(event, "progress", None)

        row = self.STEP_KEY_TO_ROW.get(step)
        if row is not None:
            self._update_step(row, status, detail)

        if progress is not None:
            self._set_progress(progress)

        detail_lower = detail.lower()
        if step == "app":
            if status == "INFO" and "tcp connected" in detail_lower:
                self._set_app_connected(True)
            elif status in {"OK", "FAIL"}:
                self._set_app_connected(self._app_service.connected)
        elif step == "tcp":
            if status == "OK":
                self._set_boot_connected(True)
            elif status == "FAIL":
                self._set_boot_connected(False)
        elif step == "complete" and status == "OK":
            self._set_boot_connected(False)

    def _on_app_boot_done(self, success: bool, message: str) -> None:
        """Handle APP flag/reset command completion and restore controls."""
        self._append_log(message)
        if not success:
            self._update_step(self.STEP_APP_BOOT, "FAIL", message)
        self._set_upgrade_running(False)
        self._set_app_connected(self._app_service.connected)
        self._set_boot_connected(self._boot_service.connected)

    def _on_upgrade_done(self, success: bool, message: str) -> None:
        """Handle BOOT upgrade completion and restore UI controls."""
        self._append_log(message)
        if success:
            self._set_progress(100)
        else:
            self._update_step(self.STEP_COMPLETE, "FAIL", message)
        self._set_upgrade_running(False)
        self._set_app_connected(self._app_service.connected)
        self._set_boot_connected(self._boot_service.connected)

    def _cleanup_app_thread(self) -> None:
        """Drop APP worker references after Qt has stopped the worker thread."""
        self._app_thread = None
        self._app_worker = None

    def _cleanup_upgrade_thread(self) -> None:
        """Drop upgrade thread references after Qt has stopped the worker thread."""
        self._upgrade_thread = None
        self._upgrade_worker = None

    def _on_clear_log_clicked(self) -> None:
        """Clear all text currently displayed in the message panel."""
        self.log_panel.clear()