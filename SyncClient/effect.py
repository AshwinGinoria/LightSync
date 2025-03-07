from LEDStrip import LEDStrip
import time

class Effect:
    lights: LEDStrip = None
    interval: float = None
    is_running: bool = False

    def __init__(self, lights: LEDStrip, interval: float = 60):
        self.lights = lights
        self.interval = interval

    def animate(self):
        self.lights.fill((0, 0, 0))

    def stop(self):
        self.is_running = False

    def run(self):
        self.is_running = True
        while (self.is_running):
            self.animate()
            time.sleep(self.interval)