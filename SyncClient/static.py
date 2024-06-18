from LEDStrip import LEDStrip
from effect import Effect

class Static(Effect):
    color = None
    def __init__(self, lights: LEDStrip, color = (0, 255, 255)):
        super().__init__(lights)
        self.color = color
    
    def animate(self):
        self.lights.fill(self.color)
