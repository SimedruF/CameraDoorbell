# Camera Doorbell

PlatformIO firmware for the AI-Thinker ESP32-CAM board with an OV2640 camera and PSRAM.

The current implementation provides:

- an HTTP MJPEG stream;
- JPEG snapshots over HTTP;
- a microSD card operating in 1-bit SDMMC mode;
- asynchronous photo storage on the microSD card;
- asynchronous MJPEG-in-AVI recording on the microSD card;
- UTC filenames after NTP synchronization and fallback filenames before synchronization;
- a web interface and status endpoint;
- persistent Wi-Fi and microSD configuration stored in NVS;
- a read-only web browser for directories, photos, and videos on the microSD card;
- optional HLK-LD2410C integration over UART, with automatic photos and AVI recordings
  on presence events;
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
through `/api/status`. On a rising presence event, it can automatically enqueue a photo,
start an AVI recording, or do both. The trigger target type and cooldown period are
selectable from `/config`.

## AVI/MJPEG recorder

The recorder runs inside the storage task and copies frames from the camera service. It
does not access the OV2640 driver directly, so the web stream, snapshots, photos, and
recording all share the same latest JPEG frame safely. A recording can be started and
stopped from the main web page or started automatically by an HLK-LD2410C presence event.

The output is a standard AVI file with an MJPEG video stream, `00dc` frame chunks, and an
`idx1` seek index. It contains video only; there is no audio track. While recording, data
is written to an `.avi.tmp` file. The frame count, timing, RIFF sizes, and seek index are
finalized before the file is atomically renamed to `.avi`, so a failed or empty recording
is not exposed as a completed video.

The configurable recorder limits are 1–20 FPS, 1–600 seconds, and 8–1024 MB. The actual
recording rate is capped by the configured camera rate and may be lower at large frame
sizes. Recording also stops before consuming the configured minimum free-space reserve.
Browsing, downloading, or previewing card files is temporarily rejected while recording
to avoid starving SDMMC writes. New photo-save requests are also rejected during that
interval, although snapshots and the live stream remain available. Many browsers cannot
play MJPEG-in-AVI inline; download the file and use VLC or `ffplay` when the browser
preview does not start.

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
- a minimum free-space threshold protected by photo and video storage;
- AVI/MJPEG enable, frame rate, duration, and maximum file-size controls;
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
| POST | `/api/record/start` | Start an AVI/MJPEG recording asynchronously |
| POST | `/api/record/stop` | Stop and finalize the active recording asynchronously |
| GET | `/api/status` | JSON status for camera, radar, storage, network, and memory |
| GET | `/photos/latest` | Most recently saved photo |
| GET | `/videos/latest` | Most recently completed AVI recording |
| GET | `/config` | Wi-Fi, storage, camera, and radar configuration page |
| GET/POST | `/api/config` | Read/save configuration; passwords are never returned |
| GET | `/files` | microSD content browser |
| GET | `/api/files?path=/` | JSON directory listing |
| GET | `/media?path=/photos/...jpg` | Inline file preview |
| GET | `/download?path=/photos/...jpg` | File download |
| GET | `http://IP:81/stream` | MJPEG stream for browsers, motionEye, or Frigate |

By default, photos are stored under `/photos/YYYY/MM/DD/`. Until NTP synchronization,
they are stored under `/photos/unsynced/`. The root directory can be changed from the
configuration page. Videos follow the same layout under `/videos/YYYY/MM/DD/` and
`/videos/unsynced/`.

After downloading a recording, its container and stream can be inspected on a computer:

```bash
ffprobe -hide_banner recording.avi
```
