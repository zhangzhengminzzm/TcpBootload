from pathlib import Path

from PyQt6.QtWidgets import (
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
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
    def __init__(self) -> None:
        super().__init__()
        self._service = UpgradeService()
        self.setWindowTitle("TCP Bootload Host")
        self.resize(980, 680)
        self._init_ui()
        self._bind_events()
        self._set_connected(False)
        self._refresh_start_button()

    def _init_ui(self) -> None:
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
        self.connect_btn.clicked.connect(self._on_connect_clicked)
        self.pick_file_btn.clicked.connect(self._on_pick_file_clicked)
        self.start_btn.clicked.connect(self._on_start_clicked)
        self.file_edit.textChanged.connect(self._refresh_start_button)

    def _set_connected(self, connected: bool) -> None:
        color = "#21a67a" if connected else "#c43d3d"
        self.status_lamp.setStyleSheet(
            "QFrame {"
            f"background-color: {color};"
            "border-radius: 9px;"
            "border: 1px solid rgba(0, 0, 0, 80);"
            "}"
        )
        self.status_label.setText("TCP connected" if connected else "TCP disconnected")
        self._refresh_start_button()

    def _firmware_file_ready(self) -> bool:
        return Path(self.file_edit.text().strip()).is_file()

    def _refresh_start_button(self) -> None:
        self.start_btn.setEnabled(
            self._service.connected and self._firmware_file_ready()
        )

    def _append_log(self, message: str) -> None:
        self.log_panel.append(message)

    def _on_connect_clicked(self) -> None:
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

    def _on_pick_file_clicked(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select firmware",
            "",
            "Firmware (*.bin *.hex);;All files (*.*)",
        )
        if path:
            self.file_edit.setText(path)

    def _on_start_clicked(self) -> None:
        if not self._firmware_file_ready():
            self._append_log("Please select a valid firmware file before upgrading.")
            self._refresh_start_button()
            return

        result = self._service.send_boot_command()
        self._append_log(result.message)
        if not result.success:
            self._set_connected(self._service.connected)
