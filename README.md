# LightSync

Client and Server to run custom LED lights

## Things to do

- [ ] LED Server
  - [x] Add PicoLED
  - [ ] Wifi communication
  - [ ] Restful API Server for simple patterns
  - [ ] Restful API for full control
    - [ ] Implementation
    - [ ] Give identity to controller
- [ ] Controller Client
  - [ ] Wifi communication with LED Server
  - [ ] Control simple patterns
  - [ ] Sync box implementation
  - [ ] Add audio processing to sync box
  - [ ] transfer to HDMI ?

## Client

```
g++ -std=c++14 -o client client.cpp 
./client
```