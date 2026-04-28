from dataclasses import dataclass


@dataclass
class FirmwareMeta:
    version: str
    size: int
    crc32: str

