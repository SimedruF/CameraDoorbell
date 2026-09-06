# Camera Doorbell

PlatformIO firmware for the AI-Thinker ESP32-CAM board with an OV2640 camera and PSRAM.

The current implementation provides:

- an HTTP MJPEG stream;
- JPEG snapshots over HTTP;
- a microSD card operating in 1-bit SDMMC mode;
- asynchronous photo storage on the microSD card;
- UTC filenames after NTP synchronization and fallback filenames before synchronization;
- a web interface and status endpoint;
- persistent Wi-Fi and microSD configuration stored in NVS;
- a read-only web browser for directories, photos, and videos on the microSD card;
- optional HLK-LD2410C integration over UART, with automatic photos on presence events;
- GPIO13 reserved for direct control of the RF433 transmitter.

The historical sketches and supporting material in `old_src/` are retained for reference.
PlatformIO compiles only the sources from `src/`.

## Auxiliary pins

| Function | GPIO |
|---|---:|
| microSD DATA0 | 2 |
| microSD CLK | 14 |
| microSD CMD | 15 |
| Flash LED | 4 |
| Active-HIGH doorbell button | 12 |
| FS1000A DATA | 13 |
| HLK-LD2410C UART_TX | 3 (UART RX) |

GPIO12 must remain LOW during startup; use an external 10 kΩ pull-down resistor. Do not
connect 5 V signals to the ESP32 GPIO pins. The transmitter may be powered from 5 V, but
it must share a common ground with the board, and its DATA input is driven at 3.3 V by
GPIO13.

## HLK-LD2410C presence radar

Radar integration is disabled by default and can be enabled from `/config` after the
hardware has been installed. Use the following connections for RX-only operation:

| HLK-LD2410C | ESP32-CAM |
|---|---|
| VCC | 5 V from a stable power supply |
| GND | Common GND |
| UART_TX | GPIO3 |
| UART_RX | Not connected |
| OUT | Not connected |

GPIO3 is also the UART input used for programming. Always disconnect the
`UART_TX → GPIO3` wire during upload. If the board remains attached to an ESP32-CAM-MB,
the USB converter's TX output may conflict with the radar. Configure the radar distances
and sensitivities over Bluetooth with HLKRadarTool.

The radar task runs on core 0, reads the stream at 256000 baud, and publishes the
connection state, presence state, moving/stationary targets, distances, and energy values
through `/api/status`. On a rising presence event, it can automatically enqueue a photo
for the storage task. The trigger target type and cooldown period are selectable from
`/config`.

## Configuration

```bash
cp include/secrets.example.hpp include/secrets.hpp
```

Edit `include/secrets.hpp` and enter the Wi-Fi network credentials. This file is ignored
by Git. If it is missing or the network cannot be reached, the board starts the
`CameraDoorbell-XXXXXX` access point using the fallback password from the configuration.
On first boot, open `http://192.168.4.1/config` and configure the network from a browser.
Values saved through the web interface take precedence over `secrets.hpp` and are stored
in NVS. Passwords are not stored on the card and are never returned by the API.

The configuration page also provides:

- microSD enable/disable control;
- configurable photo and video directories;
- a minimum free-space threshold below which photos are no longer stored;
- Manual/day, Streaming, Photo, and Night profiles;
- resolutions from QQVGA to UXGA, JPEG quality, and 1–30 FPS;
- OV2640 brightness, contrast, saturation, and effects;
- automatic or manual white balance, exposure, and gain;
- pixel, gamma, lens, and downsize corrections;
- horizontal mirror, vertical flip, test bars, and PWM flash LED intensity.

Camera settings are applied after restart to avoid reconfiguring the sensor while the
streaming and storage tasks are using a frame. Saving the configuration automatically
restarts the board. The microSD browser displays the saved date from FAT metadata, or
infers it from filenames created by the firmware. It is intentionally read-only at this
stage and supports file previews and downloads.

## Build and upload

```bash
pio run
pio run --target upload
pio device monitor
```

To program a module without an integrated USB programmer, connect GPIO0 to GND during
reset and upload. After the upload, disconnect GPIO0 from GND and reset the board.

## Endpoints

| Method | URL | Purpose |
|---|---|---|
| GET | `/` | Web interface |
| GET | `/capture` | Current JPEG without saving it |
| POST | `/api/capture` | Enqueue a photo for storage |
| GET | `/api/status` | JSON status for camera, radar, storage, network, and memory |
| GET | `/photos/latest` | Most recently saved photo |
| GET | `/config` | Wi-Fi, storage, camera, and radar configuration page |
| GET/POST | `/api/config` | Read/save configuration; passwords are never returned |
| GET | `/files` | microSD content browser |
| GET | `/api/files?path=/` | JSON directory listing |
| GET | `/media?path=/photos/...jpg` | Inline file preview |
| GET | `/download?path=/photos/...jpg` | File download |
| GET | `http://IP:81/stream` | MJPEG stream for browsers, motionEye, or Frigate |

By default, photos are stored under `/photos/YYYY/MM/DD/`. Until NTP synchronization,
they are stored under `/photos/unsynced/`. The root directory can be changed from the
configuration page.
