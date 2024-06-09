from LEDStrip import LEDStrip
from screen_replicate import Replicate
from blink import Blink
import time

SERVER_IP = "192.168.0.244"

def main():
    lights = LEDStrip(288, (SERVER_IP, 5005), 0.1)
    effect = Replicate(lights)

    while (True):
        time.sleep(0.01)
        print("Updating")
        effect.animate()

if __name__ == '__main__':
    main()
