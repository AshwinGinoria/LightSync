from LEDStrip import LEDStrip
from PIL import ImageGrab, Image
from typing import List, Tuple
import numpy as np

class Replicate:
    lights: LEDStrip = None
    n_pixels: int = None
    state: bool = None
    size: Tuple[int, int] = None

    def __init__(self, lights: LEDStrip):
        self.lights = lights
        self.state = False
        self.n_pixels = lights.n_pixels
        size = (int(self.n_pixels * (16/50)), int(self.n_pixels * (9/50)))
        self.size = size
        if (size[0] + size[1] < self.n_pixels / 2):
            self.size = (size[0] + 1, size[1])

    def _crop_border(self, image):
        return image
    
    def calc_lights(self, image) -> List[List[int]]:
        image = self._crop_border(image)

        image = image.resize(self.size).convert('RGB')
        array = np.array(image.getdata(), dtype=np.ubyte)

        array = np.resize(array, (self.size[1], self.size[0], 3))
        # Image.fromarray(array).show()
        print(array.shape)

        leds = []
        leds.extend(array[0])
        leds.extend(array[:,0])
        leds.extend(array[-1])
        leds.extend(array[:,-1])

        return leds


    def get_ss(self):
        return ImageGrab.grab(bbox=None)


    def animate(self):
        ss = self.get_ss()
        leds = self.calc_lights(ss)
        self.lights.update(leds)
