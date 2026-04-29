from dataclasses import dataclass


@dataclass
class DeviceSession:
    """Connection state snapshot for a target Modbus TCP device.

    Attributes:
        ip: Target device IP address.
        port: Target TCP port, defaulting to the Modbus TCP standard port.
        connected: Last known connection state.
    """

    ip: str
    port: int = 502
    connected: bool = False
