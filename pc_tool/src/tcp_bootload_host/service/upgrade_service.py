from pathlib import Path


class UpgradeService:
    def precheck(self, firmware_path: str) -> bool:
        return Path(firmware_path).exists()

