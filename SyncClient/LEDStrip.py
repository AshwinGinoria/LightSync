import socket
from typing import List, Tuple

class LEDStrip:
    server: Tuple[str, int] = None
    sock: socket = None
    n_pixels: int = None
    scale: float = None

    def __init__(self, length: int, server: Tuple[str, int], scale = 0.5) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server = server
        self.n_pixels = length
        self.scale = scale

    def update(self, leds: List[List[int]]) -> None:
        self.sock.sendto(self.convert_bytes(leds), self.server)

    def convert_bytes(self, pixels: List[List[int]]) -> List[bytes]:
        flat_list = []
        for pixel in pixels:
            for led in pixel:
                flat_list.append(int(led * self.scale))
        return bytearray(flat_list)
