from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("TCP Bootload Host")
        self.resize(980, 680)
        self._init_ui()

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
        layout.addWidget(self.log_panel)

        self.setCentralWidget(root)

