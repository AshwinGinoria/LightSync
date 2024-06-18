from LEDStrip import LEDStrip
import time

class Effect:
    lights: LEDStrip = None
    interval: float = None

    def __init__(self, lights: LEDStrip, interval: float = 60):
        self.lights = lights
        self.interval = interval

    def animate(self):
        self.lights.fill((0, 0, 0))

    def run(self):
        while (True):
            self.animate()
            time.sleep(self.interval)