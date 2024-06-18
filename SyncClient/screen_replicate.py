from LEDStrip import LEDStrip
from PIL import ImageGrab, Image
from typing import List
import numpy as np

from effect import Effect

class Replicate(Effect):
    offset: int = 0
    _height: int = 0
    _width: int = 0
    dead_leds = 0

    def __init__(self, lights: LEDStrip, offset = 0):
        super().__init__(lights, 0.02)
        self.offset = offset

        # initialize scale
        W, H = self.get_ss().size
        scale_factor = (self.lights.n_pixels + 4 - self.dead_leds) / (2 * (H + W))

        self._height = round(H * scale_factor)
        self._width = round(W * scale_factor)

    def isEmpty(self, arr: List) -> bool:
        return np.sum(arr) == 0

    def _cut_border(self, image):
        h = len(image)
        B = 6
        
        for i in range(B, 0, -1):
            image[i - 1] = image[i]
            image[h - i] = image[h - i - 1]

        return image

    def calc_lights(self, image) -> List[List[int]]:
        image = image.resize((self._width, self._height), Image.BICUBIC).convert('RGB')

        array = np.array(image.getdata(), dtype=np.ubyte)
        array = np.resize(array, (self._height, self._width, 3))
        # array = self._cut_border(array)

        leds = []

        # Left
        leds.extend(np.flip(array[:,0], 0))
        # Top
        leds.extend(array[0])
        # Right
        leds.extend(array[:,-1])
        # Bottom
        leds.extend(np.flip(array[-1], 0))

        return leds

    def get_ss(self):
        return ImageGrab.grab(bbox=None)

    def animate(self):
        ss = self.get_ss()
        leds = self.calc_lights(ss)
        self.lights.update(leds[self.offset:] + leds[:self.offset])

