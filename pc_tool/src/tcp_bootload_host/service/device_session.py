from dataclasses import dataclass


@dataclass
class DeviceSession:
    ip: str
    port: int = 502
    connected: bool = False

