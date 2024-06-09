import socket
from typing import List, Tuple

class LEDStrip:
    server: Tuple[str, str] = None
    sock: socket = None
    n_pixels: int = None

    def __init__(self, length: int, server: Tuple[str, str]) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server = server
        self.n_pixels = length

    def update(self, leds: List[List[int]]) -> None:
        self.sock.sendto(LEDStrip.convert_bytes(leds), self.server)

    def fill(self, color):
        leds = [color] * self.n_pixels
        self.update_strip(leds)

    @staticmethod
    def convert_bytes(pixels: List[List[int]]) -> List[bytes]:
        flat_list = []
        for pixel in pixels:
            for led in pixel:
                flat_list.append(led)
        return bytearray(flat_list)

    def animate():
        pass
