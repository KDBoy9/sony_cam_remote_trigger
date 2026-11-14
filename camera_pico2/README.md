# Sony A7C II camera controller for Raspberry Pi Pico 2

Pair this firmware with the accompanying Pico W remote. Flash both boards with this release, since it uses version 2 of the LoRa frame format.

## Flash and connect

1. Hold BOOTSEL while plugging the Pico 2 into your computer. Copy `firmware/camera_pico2.uf2` to the mounted BOOTSEL drive.
2. Keep the SX1278 wiring from the working link. Both radios use the same settings in `lora_config.h`.

   | SX1278 signal | Pico GPIO |
   |---|---:|
   | MISO | GP16 |
   | CS/NSS | GP17 |
   | SCK | GP18 |
   | MOSI | GP19 |
   | RESET | GP20 |
   | DIO0 | GP21 |

3. Use the same PIO USB host adapter and power arrangement as in the shutter test. Its USB data lines are D+ on GP0 and D− on GP1. Connect the camera's USB-C cable to that host adapter. The Pico 2's built-in USB port is the CDC log console.
4. Put the Sony in USB PC Remote mode. Use a memory card and select a still-image save destination that includes the camera. For individual shots, use Single Shooting drive mode. Join `KD_PICO_W` on your phone and open `http://192.168.4.1/`. Wait for **Camera ready**.

## Bulb exposure

On the A7C II, select Still, M, and BULB. Use the mechanical shutter, turn Silent Mode off, and turn the built-in BULB Timer off. The Pico 2 sends Sony's bulb start command, starts its local timer when the press is acknowledged, and sends the release at expiry. Use a normal shutter speed again for immediate, delayed, or interval shooting.

A bulb exposure continues if the phone or LoRa link drops. The Pico 2 needs camera USB access to send the release. If USB drops during the exposure, the page reports an unconfirmed release. The firmware attempts to release again after reconnecting and reopening the PTP session.

Camera references  
[Sony bulb shooting](https://helpguide.sony.net/ilc/2360/v1/en/contents/0302C_bulb.html) · [Sony BULB Timer settings](https://helpguide.sony.net/ilc/2360/v1/en/contents/211h_bulb_timer.html) · [libgphoto2 Sony bulb command implementation](https://github.com/gphoto/libgphoto2/blob/662abd148dfd6ecbbacb3ddaa9134d4b357f15e1/camlibs/ptp2/config.c#L10622)

## Build from source

Use an Arm GNU toolchain, CMake, Pico SDK 2.3.1 with its TinyUSB submodule, and a Pico-PIO-USB checkout. The supplied UF2 was built with Arm GCC 14.2.1 and Pico-PIO-USB commit `b186e36`.

```sh
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_PIO_USB_PATH=/path/to/Pico-PIO-USB
cmake --build build --parallel
```

The rebuilt file is `build/camera_pico2.uf2`. The shipped CMake target selects `pico2`.

## Check on hardware

Fire one immediate shot and check that the camera stores the image. Start a five-second interval, observe two shots, and stop it. Try a three-second delayed shot. In BULB, try a short exposure and compare the resulting exposure metadata with the requested duration. The page reports the PTP result and whether a camera image event arrived during the operation. An image event can arrive later, particularly after a long exposure.
