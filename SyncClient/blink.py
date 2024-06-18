from LEDStrip import LEDStrip

BLACK = [0, 0, 0]
RED = [255, 0, 0]
GREEN = [0, 255, 0]
BLUE = [0, 0, 255]
WHITE = [255, 255, 255]

class Blink:
    lights: LEDStrip = None
    state: bool = None

    def __init__(self, lights: LEDStrip):
        super().__init__(lights, 1.0)

        self.state = False
    
    def animate(self):
        if (self.state == True):
            self.lights.fill(GREEN)
            self.state = False
        else :
            self.lights.fill(BLACK)
            self.state = True
