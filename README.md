# Camera Doorbell

Firmware PlatformIO pentru placa AI-Thinker ESP32-CAM cu OV2640 și PSRAM.

Stadiul curent implementează primele două etape ale proiectului:

- flux HTTP MJPEG;
- snapshot JPEG prin HTTP;
- microSD în mod SDMMC 1-bit;
- salvare asincronă a fotografiilor pe microSD;
- nume UTC după sincronizarea NTP și nume fallback înainte de sincronizare;
- pagină web și endpoint de stare;
- configurare Wi-Fi și microSD salvată persistent în NVS;
- browser web read-only pentru directoare, fotografii și filme de pe microSD;
- GPIO13 rezervat pentru controlul direct al emițătorului RF433.

Sketch-urile și materialele istorice din `old_src/` sunt păstrate ca referință.
PlatformIO compilează numai sursele din `src/`.

## Pini auxiliari

| Funcție | GPIO |
|---|---:|
| microSD DATA0 | 2 |
| microSD CLK | 14 |
| microSD CMD | 15 |
| LED flash | 4 |
| Buton activ HIGH | 12 |
| FS1000A DATA | 13 |

GPIO12 trebuie menținut LOW în timpul pornirii; folosește un rezistor extern de pull-down
de 10 kΩ. Nu conecta semnale de 5 V la GPIO-urile
ESP32. Emițătorul poate fi alimentat la 5 V, dar masa sa trebuie să fie comună cu masa
plăcii, iar intrarea DATA este comandată la 3,3 V de GPIO13.

## Configurare

```bash
cp include/secrets.example.hpp include/secrets.hpp
```

Editează `include/secrets.hpp` și completează rețeaua Wi-Fi. Fișierul este ignorat de Git.
Dacă nu există sau rețeaua nu poate fi accesată, placa pornește access point-ul
`CameraDoorbell-XXXXXX`, folosind parola fallback din configurație. La prima pornire,
deschide `http://192.168.4.1/config` și setează rețeaua direct din browser. Valorile
salvate din interfața web au prioritate față de `secrets.hpp` și sunt păstrate în NVS.
Parolele nu sunt stocate pe card și nu sunt returnate de API.

Pagina de configurare permite și:

- activarea/dezactivarea microSD;
- alegerea directoarelor de fotografii și filme;
- stabilirea spațiului liber minim sub care nu se mai salvează fotografii.

Salvarea configurației repornește automat placa. Browserul microSD afișează data
salvării din metadata FAT (sau o deduce din numele fotografiilor create de firmware),
este intenționat read-only în această etapă și permite previzualizarea sau descărcarea.

## Build și upload

```bash
pio run
pio run --target upload
pio device monitor
```

Pentru programarea unui modul fără programator USB integrat, GPIO0 trebuie legat la GND
în timpul resetării și uploadului. După upload, deconectează GPIO0 de la GND și resetează
placa.

## Endpoint-uri

| Metodă | URL | Rol |
|---|---|---|
| GET | `/` | interfața web |
| GET | `/capture` | JPEG curent, fără salvare |
| POST | `/api/capture` | pune o fotografie în coada de salvare |
| GET | `/api/status` | stare JSON pentru cameră, SD, rețea și memorie |
| GET | `/photos/latest` | ultima fotografie salvată |
| GET | `/config` | pagina de configurare Wi-Fi și stocare |
| GET/POST | `/api/config` | citire/salvare configurare (parolele nu sunt returnate) |
| GET | `/files` | browser pentru conținutul microSD |
| GET | `/api/files?path=/` | listarea JSON a unui director |
| GET | `/media?path=/photos/...jpg` | previzualizare inline |
| GET | `/download?path=/photos/...jpg` | descărcare fișier |
| GET | `http://IP:81/stream` | flux MJPEG pentru browser/motionEye/Frigate |

Implicit, fotografiile sunt stocate în `/photos/YYYY/MM/DD/`. Până la sincronizarea NTP
sunt stocate în `/photos/unsynced/`. Directorul rădăcină poate fi schimbat din pagina
de configurare.
