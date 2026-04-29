from pathlib import Path

from PyQt6.QtWidgets import (
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QProgressBar,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from tcp_bootload_host.protocol.register_map import (
    BOOT_COMMAND_REGISTER_DISPLAY,
    BOOT_COMMAND_VALUE,
)
from tcp_bootload_host.service.upgrade_service import UpgradeService


class MainWindow(QMainWindow):
    """Main PyQt window for connecting to the device and starting Bootload."""

    def __init__(self) -> None:
        """Create services, build the UI, and initialize button/status state."""
        super().__init__()
        self._service = UpgradeService()
        self.setWindowTitle("TCP Bootload Host")
        self.resize(980, 680)
        self._init_ui()
        self._bind_events()
        self._set_connected(False)
        self._refresh_start_button()

    def _init_ui(self) -> None:
        """Build all widgets used by the host tool.

        The layout contains connection controls, firmware selection, upgrade
        progress, log actions, and the message panel. Business logic is bound in
        ``_bind_events`` so construction stays focused on widget creation.
        """
        root = QWidget(self)
        layout = QVBoxLayout(root)

        conn_row = QHBoxLayout()
        conn_row.addWidget(QLabel("IP:"))
        self.ip_edit = QLineEdit("192.168.1.100")
        conn_row.addWidget(self.ip_edit)
        conn_row.addWidget(QLabel("Port:"))
        self.port_edit = QLineEdit("502")
        conn_row.addWidget(self.port_edit)
        self.connect_btn = QPushButton("Connect")
        conn_row.addWidget(self.connect_btn)
        self.disconnect_btn = QPushButton("Disconnect")
        conn_row.addWidget(self.disconnect_btn)
        self.status_lamp = QFrame()
        self.status_lamp.setFixedSize(18, 18)
        conn_row.addWidget(self.status_lamp)
        self.status_label = QLabel("TCP disconnected")
        conn_row.addWidget(self.status_label)
        layout.addLayout(conn_row)

        fw_row = QHBoxLayout()
        self.file_edit = QLineEdit("")
        self.file_edit.setPlaceholderText("Select firmware file...")
        self.pick_file_btn = QPushButton("Browse")
        self.start_btn = QPushButton("Start Upgrade")
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
        layout.addWidget(self.log_panel)

        self.setCentralWidget(root)

    def _bind_events(self) -> None:
        """Connect UI signals to their slot handlers."""
        self.connect_btn.clicked.connect(self._on_connect_clicked)
        self.disconnect_btn.clicked.connect(self._on_disconnect_clicked)
        self.pick_file_btn.clicked.connect(self._on_pick_file_clicked)
        self.start_btn.clicked.connect(self._on_start_clicked)
        self.clear_log_btn.clicked.connect(self._on_clear_log_clicked)
        self.file_edit.textChanged.connect(self._refresh_start_button)

    def _set_connected(self, connected: bool) -> None:
        """Update widgets that reflect the current TCP connection state.

        Args:
            connected: ``True`` when the Modbus TCP socket is open.
        """
        color = "#21a67a" if connected else "#c43d3d"
        self.status_lamp.setStyleSheet(
            "QFrame {"
            f"background-color: {color};"
            "border-radius: 9px;"
            "border: 1px solid rgba(0, 0, 0, 80);"
            "}"
        )
        self.status_label.setText("TCP connected" if connected else "TCP disconnected")
        self.connect_btn.setEnabled(not connected)
        self.disconnect_btn.setEnabled(connected)
        self._refresh_start_button()

    def _firmware_file_ready(self) -> bool:
        """Return whether the path field contains an existing firmware file."""
        return Path(self.file_edit.text().strip()).is_file()

    def _refresh_start_button(self) -> None:
        """Enable Start Upgrade only when TCP is connected and a file is selected."""
        self.start_btn.setEnabled(
            self._service.connected and self._firmware_file_ready()
        )

    def _append_log(self, message: str) -> None:
        """Append one status line to the message panel."""
        self.log_panel.append(message)

    def _set_progress(self, value: int) -> None:
        """Set upgrade progress while clamping the value to 0..100."""
        self.progress_bar.setValue(max(0, min(value, 100)))

    def _on_connect_clicked(self) -> None:
        """Handle the Connect button click and open the Modbus TCP session."""
        ip = self.ip_edit.text().strip()
        try:
            port = int(self.port_edit.text().strip())
        except ValueError:
            self._append_log("Invalid TCP port.")
            self._set_connected(False)
            return

        result = self._service.connect(ip, port)
        self._append_log(result.message)
        self._set_connected(result.success)

    def _on_disconnect_clicked(self) -> None:
        """Handle the Disconnect button click and close the TCP session."""
        result = self._service.disconnect()
        self._append_log(result.message)
        self._set_connected(False)
        self._set_progress(0)

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

    def _on_start_clicked(self) -> None:
        """Start the current upgrade stage after validating the selected file.

        At this stage the host sends only the App-to-Bootloader command. Once full
        packet transfer is implemented, this slot should hand work to a background
        worker and update ``progress_bar`` from packet acknowledgements.
        """
        if not self._firmware_file_ready():
            self._append_log("Please select a valid firmware file before upgrading.")
            self._refresh_start_button()
            return

        self._set_progress(0)
        self._append_log("Upgrade started.")
        result = self._service.send_boot_command()
        self._append_log(result.message)
        if result.success:
            self._set_progress(100)
            self._append_log("Upgrade command stage complete.")
        else:
            self._set_progress(0)
            self._set_connected(self._service.connected)

    def _on_clear_log_clicked(self) -> None:
        """Clear all text currently displayed in the message panel."""
        self.log_panel.clear()
