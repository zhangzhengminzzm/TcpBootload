from pathlib import Path
import sys


if __package__:
    from .app import run
else:
    # Support running this file directly:
    # python E:\...\pc_tool\src\tcp_bootload_host\main.py
    src_root = Path(__file__).resolve().parents[1]
    if str(src_root) not in sys.path:
        sys.path.insert(0, str(src_root))
    from tcp_bootload_host.app import run


if __name__ == "__main__":
    run()
