import socket
import time
import json

SERVER_IP = "192.168.0.244"
UDP_PORT = 5005
TCP_PORT = 80
LED_LENGTH = 100

BLACK = (0, 0, 0)
RED = (255, 0, 0)
YELLOW = (255, 150, 0)
GREEN = (0, 255, 0)
CYAN = (0, 255, 255)
BLUE = (0, 0, 255)
PURPLE = (180, 0, 255)
WHITE = (255, 255, 255)
COLORS = (BLACK, RED, YELLOW, GREEN, CYAN, BLUE, PURPLE, WHITE)

udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

def send_udp(cmd: bytes) -> None:
    udp_sock.sendto(cmd, (SERVER_IP, UDP_PORT))

def tcp_connect():
    tcp_sock.connect((SERVER_IP, TCP_PORT))

def send_tcp(cmd: bytes) -> None:
    tcp_sock.send(cmd)

def send_cmd(cmd: bytes) -> None:
    print("Sending ", len(cmd))
    send_tcp(cmd)

tcp_connect()

send_cmd(b"Hello World")

def set_color(color):
    leds = [color] * LED_LENGTH
    send_cmd(json.dumps(leds).encode('utf-8'))

def test_blink():
    on = True
    while True:
        try: 
            if (on):
                set_color(WHITE)
                on = False
            else:
                set_color(BLACK)
                on = True
            
            time.sleep(0.05)
        except KeyboardInterrupt as e:
            print("End")
            break

    set_color(BLACK)
    
test_blink()