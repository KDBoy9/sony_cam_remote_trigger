# Handheld camera remote for Raspberry Pi Pico W

Flash this alongside the accompanying Pico 2 camera firmware. Both boards use the same version 2 LoRa framing and the same radio settings.

1. Hold BOOTSEL while plugging the Pico W into your computer. Copy `firmware/remote_pico_w.uf2` to the mounted BOOTSEL drive.
2. Keep the existing SX1278 wiring. MISO, CS, SCK, MOSI, RESET, and DIO0 use GP16, GP17, GP18, GP19, GP20, and GP21 respectively.
3. Join `KD_PICO_W` and open `http://192.168.4.1/` in your browser. The access point is open by default, as in the link test. To enable WPA2, set `AP_PASSWORD` in `app_config.h` and rebuild.
4. Wait for **Camera ready** before using the controls.

| Control | Behavior |
|---|---|
| Fire shutter | Requests one shot immediately and displays the PTP result. |
| Start interval / Stop interval | Fires the first shot immediately, then repeats at the entered start-to-start interval. The page shows the latest result, attempt count, and next-shot countdown. Stop finishes any shot already in progress. If the camera takes longer than the interval, the next shot starts when the previous sequence finishes. |
| Start countdown | Waits the entered number of seconds, then fires once. |
| Start bulb | Starts an exposure and counts down to release. Set the camera to BULB with its built-in BULB Timer off before using this control. |

Timed controls accept whole seconds from 1 to 65,535. The Pico 2 owns the timers. An interval keeps running if you close the page or lose the radio link. Reconnect and use **Stop interval** to end it. A USB failure ends the active run and appears on the page when LoRa status is available.

The page distinguishes a LoRa acknowledgement from a completed camera operation. If command delivery is uncertain, wait for a fresh camera status and check the camera before sending the same command again. The attempt count lets you see how many shots ran if radio updates were missed.

## Build from source

Use an Arm GNU toolchain, CMake, and Pico SDK 2.3.1 with its TinyUSB, CYW43, and lwIP submodules.

```sh
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build --parallel
```

The rebuilt file is `build/remote_pico_w.uf2`. The shipped CMake target selects `pico_w`. The two projects each include a copy of the shared radio protocol. Apply changes to both copies if you modify the over-the-air format or radio settings.
