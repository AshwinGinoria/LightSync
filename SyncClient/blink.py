from LEDStrip import LEDStrip

BLACK = [0, 0, 0]
RED = [255, 0, 0]
GREEN = [0, 255, 0]
BLUE = [0, 0, 255]
WHITE = [255, 255, 255]

class Blink:
    lights: LEDStrip = None
    n_pixels: int = None
    state: bool = None

    def __init__(self, lights: LEDStrip):
        self.lights = lights
        self.state = False
        self.n_pixels = lights.n_pixels

    def fill(self, color):
        leds = [color] * self.n_pixels
        self.lights.update(leds)
    
    def animate(self):
        if (self.state == True):
            self.fill(GREEN)
            self.state = False
        else :
            self.fill(BLACK)
            self.state = True
