# ATECC608A — Secure Element Exploration (ESP32 / ESP-IDF)

Progressive exploration of the Microchip ATECC608A secure element on the ESP32, part of the IoT Security Hardening roadmap.

## Structure

Each `labN/` folder is a standalone ESP-IDF project (own `main/`, `CMakeLists.txt`, `sdkconfig`).

- `lab1/` — Manual I2C wake sequence + full bus scan, using ESP-IDF's `i2c_master` driver directly (no external library). Confirms the chip's presence and address (0x60) at the protocol level.
- `lab2/` — Chip identity (`Info` command) and full configuration zone read, using the official `espressif/esp-cryptoauthlib` component instead of reimplementing the ATECC command protocol by hand.
- `lab3/` — (see `doc/` for current status; folder present, content to be documented)

Full write-up (theory, hardware, procedure, real errors and fixes, hardware logs): see `doc/`.

## Hardware

- ATECC608A module
- ESP32
- Wiring: SDA → GPIO21, SCL → GPIO22, VCC → 3.3V, GND → GND

## Building a lab

```
cd labN
idf.py add-dependency "espressif/esp-cryptoauthlib"   # only for labs using cryptoauthlib
idf.py build flash monitor
```

If using `cryptoauthlib`, set its I2C pins after the first build (new project = fresh sdkconfig, pins default to 0):
```
sed -i 's/CONFIG_ATCA_I2C_SDA_PIN=.*/CONFIG_ATCA_I2C_SDA_PIN=21/; s/CONFIG_ATCA_I2C_SCL_PIN=.*/CONFIG_ATCA_I2C_SCL_PIN=22/' sdkconfig
```

## Status

- [x] Lab 4a — ATECC608A exploration (wake/scan, Info, config zone read)
- [ ] Lab 4b — eFuse (ESP32) vs. dedicated Secure Element (ATECC608A), using CVE-2019-17391 as evidence