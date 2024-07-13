from LEDStrip import LEDStrip
from screen_replicate import Replicate
from blink import Blink
from static import Static
from enum import Enum

SERVER_IP = "192.168.0.244"
class Mode(Enum):
    STATIC = 1
    REPLICATE = 2
    BLINK = 3

def main(mode):
    lights = LEDStrip(288, (SERVER_IP, 5005), 0.1)
    effect = None

    if (mode == Mode.REPLICATE):
        effect = Replicate(lights)
    elif (mode == Mode.STATIC):
        effect = Static(lights, (39,15,0))
    elif (mode == Mode.BLINK):
        effect = Blink(lights)

    try:
        effect.run()
    except KeyboardInterrupt:
        lights.fill((0, 0, 0))
        # pass

if __name__ == '__main__':
    main(Mode.REPLICATE)
