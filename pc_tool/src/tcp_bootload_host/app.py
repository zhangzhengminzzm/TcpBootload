import sys

from PyQt6.QtWidgets import QApplication

from tcp_bootload_host.ui.main_window import MainWindow


def run() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()

