import sys

from PyQt6.QtWidgets import QApplication

from tcp_bootload_host.ui.main_window import MainWindow


def run() -> int:
    """Create the Qt application, show the main window, and enter the event loop.

    Returns:
        The Qt application exit code. The caller can pass it to ``sys.exit`` if
        the launcher needs the process return value.
    """
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()
