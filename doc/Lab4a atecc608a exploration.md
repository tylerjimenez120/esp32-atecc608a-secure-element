# Lab 4a — ATECC608A Exploration

## 1. Theory

### What is the ATECC608A?

The ATECC608A is a **secure element** by Microchip: a dedicated chip from the CryptoAuthentication family, independent from the main microcontroller, designed to store and operate cryptographic keys without those keys ever leaving the silicon. Unlike storing a private key in the ESP32's flash (as in Lab 2 — Flash Encryption, or in Secure Boot's eFuses), the ATECC608A never exposes the private key through any channel: cryptographic operations (signing, key generation) happen inside the chip, and only public data or results go in/out.

Relevant specs:
- Interface: I2C (default address 0x60 in 7-bit / 0xC0 in 8-bit)
- 16 configurable data/key slots
- Crypto engines: ECDSA/ECDH over NIST P-256, SHA-256, HMAC, AES-128, NIST SP800-90A/B/C certified TRNG
- Internal watchdog: 0.7s to 1.7s, after which the chip automatically goes back to sleep if it receives no commands

### Wake sequence

The chip spends most of its time asleep (low power, <150nA). Before any real communication it must be woken up:

- **tWLO** (minimum SDA-low time to wake) = **60 µs**
- **tWHI** (minimum wait after wake before commands can be sent) = **1500 µs**

Values verified against the official datasheet (*ATECC608A Summary Data Sheet*, DS40001977B, Table 2-2, AC Parameters).

The wake is not a normal I2C transaction — no address ACK is expected. It's achieved by holding SDA low long enough. The standard trick is sending a `0x00` byte to the special address `0x00` (general call): at 100kHz, that byte's duration on the bus (~90µs) already comfortably covers tWLO. The resulting NACK is expected, not an error.

### Command protocol

The ATECC608A doesn't work like a typical I2C sensor (direct register read/write). It uses its own command protocol over I2C: every operation (`Info`, `Read`, `GenKey`, `Sign`, etc.) is packaged with Count, Opcode, Param1, Param2, Data, and a verification CRC-16. Reimplementing this framing by hand is error-prone (the CRC in particular uses a non-standard polynomial) — hence using Microchip's official library, `cryptoauthlib`, instead of rebuilding it from scratch.

### Configuration zone

The chip stores its factory and configuration parameters in a **128-byte internal EEPROM memory zone** (not "registers" in the classic peripheral sense — it's non-volatile memory, rewritable byte by byte). It holds: the configured I2C address, the chip's unique serial number, the lock status of each of the 16 slots, and per-slot read/write permissions. Access to that memory always goes through the I2C command protocol — the chip never exposes it as directly mapped memory; every query is a `Read` command that the chip itself resolves internally and returns over I2C.

## 2. Hardware

- ATECC608A module (bought together with a CH341A, logic analyzer, CJMCU-2112, and DS3231 via AliExpress)
- ESP32 (same chip used in earlier labs of this series)
- Wiring:
  - SDA → GPIO21
  - SCL → GPIO22
  - VCC → 3.3V
  - GND → GND

## 3. Procedure

### Step 1 — Manual wake + bus scan (low level, no library)

Goal: understand the real wake mechanism before hiding it behind a library.

Implemented with ESP-IDF's `i2c_master` driver (new API, recommended since IDF 5.2+):
- `i2c_master_bus_setup()`: initializes the bus (SDA=21, SCL=22, 100kHz, internal pull-ups).
- `atecc608a_wake_attempt()`: creates a **temporary** handle pointing at address `0x00`, transmits 1 byte (NACK expected), destroys the handle, waits 1500µs (tWHI).
- `i2c_bus_scan()`: sweeps the 126 possible addresses (0x01–0x7E) with `i2c_master_probe()`, reporting which ones ACK.

Real hardware result:
```
E (303) i2c.master: I2C hardware NACK detected        <- expected, part of the wake
E (303) i2c.master: I2C transaction unexpected nack detected
I (323) i2c_scanner: Wake attempt sent, waited 1500 us (tWHI)
I (333) i2c_scanner: Scanning I2C bus (addresses 0x01 - 0x7E)...
I (353) i2c_scanner:   -> 0x60  ACK  (expected ATECC608A address)
I (353) i2c_scanner: Scan complete: 1 device(s) found.
```

The ACK at `0x60` is the **indirect** confirmation that the wake worked — the wake itself has no direct protocol-level confirmation.

Design note: the full scan (127 addresses) is more than strictly necessary to verify one known device — kept deliberately as a diagnostic/documentation step (confirms wiring, rules out address conflicts), not out of technical need.

### Step 2 — Chip identity (`Info`) via cryptoauthlib

The manual implementation is replaced with the official `espressif/esp-cryptoauthlib` library (an ESP-IDF component, installed with `idf.py add-dependency`). The library handles wake, framing, and CRC internally — `atcab_init()` fully replaces the manual setup from step 1.

```c
ATCAIfaceCfg cfg = {
    .iface_type = ATCA_I2C_IFACE,
    .devtype    = ATECC608A,
    .atcai2c = {
        .address = 0xC0,   // see "Errors and fixes" - not 0x60
        .bus     = 0,
        .baud    = 100000,
    },
    .wake_delay = 1500,
    .rx_retries = 20,
};
atcab_init(&cfg);
atcab_info(info);   // info[4]
```

Real hardware result:
```
I (305) atecc_info: atcab_init OK - ATECC608A initialized via cryptoauthlib
I (305) atecc_info: Info response: 00 00 60 02
```

Interpretation: `60` identifies the ATECC**608** family, `02` is this unit's silicon revision.

### Step 3 — Full configuration zone read

```c
uint8_t config_data[ATCA_ECC_CONFIG_SIZE];   // 128 bytes
atcab_read_config_zone(config_data);
```

Real hardware result (full 128-byte dump):
```
[000] 01 23 B0 EC 00 00 60 02 A6 58 6E 30 EE C1 61 00
[016] C0 00 00 00 83 20 87 20 8F 20 C4 8F 8F 8F 8F 8F
[032] 9F 8F AF 8F 00 00 00 00 00 00 00 00 00 00 00 00
[048] 00 00 AF 8F FF FF FF FF 00 00 00 00 FF FF FF FF
[064] 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[080] 00 00 00 00 00 00 55 55 FF FF 00 00 00 00 00 00
[096] 33 00 33 00 33 00 1C 00 1C 00 1C 00 1C 00 1C 00
[112] 3C 00 3C 00 3C 00 3C 00 3C 00 3C 00 3C 00 1C 00
```

Recognized fields (see section 5):
- Offset [004–007]: `00 00 60 02` — exact match with the `Info` response from step 2.
- Offset [016]: `C0` — the configured I2C address (0xC0 = 0x60<<1).
- Offset [000–001] and [008–012]: the chip's unique serial number (9 bytes split across two ranges).

## 4. Errors and fixes

### Error 1 — `ets_delay_us` not declared

```
error: implicit declaration of function 'ets_delay_us'
```
**Cause**: missing the header include that declares it.
**Fix**: add `#include "rom/ets_sys.h"`. The function still exists in IDF 5.5, it was just missing the include (replacing it with `esp_rom_delay_us` was ruled out since `ets_delay_us` remains valid and was already in use).

### Error 2 — I2C address: 7-bit vs 8-bit (the most significant one)

When migrating from the manual `i2c_master` driver (Step 1) to `cryptoauthlib` (Step 2), the 7-bit address (`0x60`) was reused directly in the `.address` field of `ATCAIfaceCfg`. Result: infinite error loop.

```
./cryptoauthlib/lib/calib/calib_execution.c:421:ffffffed:rxdata is small buffer
```

**Diagnosis** (with evidence, not guesswork — inspecting the component's real, already-downloaded source code):
- `atca_iface.h` confirmed the correct field is `.address` (not `.slave_address`, which only exists under `ATCA_ENABLE_DEPRECATED`).
- `hal_esp32_i2c.c` revealed the real cause: the HAL internally does `ATCA_IFACECFG_VALUE(cfg, atcai2c.address) >> 1` to get the real 7-bit address. That means `.address` expects the **8-bit, pre-shifted** value (`0x60 << 1 = 0xC0`), not the 7-bit one used directly with `i2c_master`.
- Passing `0x60` made the HAL compute `0x60 >> 1 = 0x30` — talking to the wrong address entirely, producing a corrupted response (`0xFF`) and the "small buffer" error.

**Fix**: `#define ATECC608A_I2C_ADDR 0xC0` (keeping the correct `.address` field name unchanged).

**Process note**: a second, external opinion (Gemini) was sought for this error, which got the core diagnosis right (the 8-bit shift) but got the field name wrong (suggested `.slave_address`). The final fix was validated against the component's real source code, not taken from either source at face value.

### Error 3 — `cryptoauthlib` pins on a new project

Every new project requires reconfiguring the component's specific I2C pins (`CONFIG_ATCA_I2C_SDA_PIN`, `CONFIG_ATCA_I2C_SCL_PIN`), independent of anything set in `main.c` — they are not inherited from another project or from the `ATCAIfaceCfg` struct.

## 5. Conclusions

- Verified on real hardware across three progressive layers: electrical presence on the bus (wake+scan), protocol-level identity (`Info`), and full configuration contents (`Read config zone`).
- Using `cryptoauthlib` instead of reimplementing the protocol by hand is the right call for any real IoT/Edge work with this chip — not just for development speed, but because the protocol's CRC and error handling are easy to get wrong, and this is security-related code.
- The most significant error (7-bit vs 8-bit address) is a reminder that each abstraction layer (ESP-IDF driver vs. vendor library) can use different conventions for the same value — verify against the real source code rather than assuming consistency across layers.

## Next step

Lab 4b — eFuse (ESP32) vs. dedicated Secure Element (ATECC608A): a comparison using CVE-2019-17391 (eFuse key extraction via glitching) as real-world evidence on the eFuse side, against the ATECC608A's structural inability to export its private key.