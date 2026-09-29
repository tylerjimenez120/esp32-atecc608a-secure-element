# Lab 4b — eFuse (ESP32) vs. Dedicated Secure Element (ATECC608A)

## 1. Framing

This lab is not a benchmark of equivalent operations — it's a comparison of **security guarantee levels**. The ESP32 protects its Flash Encryption key with a single configuration bit (`RD_DIS`) in its eFuses; the ATECC608A refuses access to its private key at the protocol level, with no bit to disable. This is eFuse-based security vs. dedicated hardware-based security — a different tier, not the same kind of protection with different performance.

## 2. Evidence — ESP32 side (eFuse)

Same command (`espefuse.py summary`), same chip family, different outcome depending on configuration:

### Protected chip (Secure Boot V2 + Flash Encryption active, `RD_DIS=1`)

```
RD_DIS (BLOCK0)          Disable reading from BlOCK1-3        = 1 R/- (0x1)
FLASH_CRYPT_CNT (BLOCK0) Flash encryption enabled              = 1 R/W (0b0000001)
MAC (BLOCK0)              = 70:4b:ca:47:3d:78 (CRC 0xf6 OK) R/W
ABS_DONE_1 (BLOCK0)       Secure boot V2 enabled                = True R/W (0b1)
SECURE_VERSION (BLOCK3)  Secure version for anti-rollback       = 1 R/W (0x00000001)
BLOCK1 (BLOCK1)          Flash encryption key
   = ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? -/-
BLOCK2 (BLOCK2)          Security boot key
   = e8 bb 28 11 db bb ec 3c 34 67 19 95 a7 a6 5b 5d b1 68 9f 04 47 14 48 66 78 ad 00 a0 88 d1 eb c8 R/-
```

`BLOCK1` (the Flash Encryption AES key) is **masked** — unreadable by software, even with physical bus access, as long as `RD_DIS=1` holds. `BLOCK2` (the Secure Boot public key hash) is always visible — it's not a secret, it's a public hash by design.

### Chip without that bit set (`RD_DIS=0`, earlier/test state)

Same command, same `BLOCK1` field — but with `RD_DIS=0`, the field reads out in cleartext (real AES key bytes visible via software, no exploit needed). The entire difference between the two chips is a single configuration bit.

**Key point**: `RD_DIS` is a **configuration-level** protection, not a structural one. There is a documented CVE (CVE-2019-17391) that bypasses it via voltage glitching (`VDD_CPU`/`VDD_RTC`) during a ~500µs early-boot window, before `RD_DIS` takes effect — extracting `BLOCK1`/`BLOCK2` even when the bit is correctly programmed. Requires physical access; affects pre-V3 chip revisions (mitigated in ESP32-D0WD-V3 / ESP32-S2).

## 3. Evidence — ATECC608A side (secure element)

### `GenKey` attempt before locking the zones (factory state)

```
calib_genkey_base - execution failed
atcab_genkey failed: 0xFFFFFFF4   (ATCA_EXECUTION_ERROR)
```

The chip **refuses the operation outright** — it won't generate any key until both the Config Zone and the Data Zone are locked (a mandatory, irreversible step by design).

### After locking (one-time, permanent):

```
Config zone locked
Data zone locked
Public key (X||Y, 64 bytes):
20 f4 5c 73 ea ea b0 6d d8 15 c3 dd 61 b2 a3 68
91 c5 c1 24 15 4f 13 8c 2c 5d d6 c4 bd ef 04 20
f9 0f 21 0f e6 5a 88 8e f5 3f 5d 6b 56 0d d5 5f
a5 62 d0 d6 74 29 77 f1 ae d5 cb 04 7e df 3a c4
Signature (R||S, 64 bytes):
ba b0 10 cb 48 ed 92 d1 b0 f7 a3 43 b9 35 df 02
82 14 c6 b5 6e 56 c0 fe 22 bc a5 3f 7e e6 67 93
47 bc 25 6a 81 34 3e a2 9d a2 2d b6 0f ba f0 93
f2 db a3 54 52 b0 c3 a4 d5 cc d6 f8 a4 dc d3 e7
```

### Attempt to read the private key slot directly

```
calib_read_zone - execution failed
status: 0xFFFFFFF4   (ATCA_EXECUTION_ERROR)
```

Rejected — **no exception, no flag that allows it**. Unlike `RD_DIS`, there is no chip configuration state in which this read succeeds; the `Read` command on an ECC private key slot never exposes the key in cleartext, by construction of the protocol itself.

## 4. Comparison table

| Aspect | ESP32 (eFuse, Flash Encryption) | ATECC608A |
|---|---|---|
| Protection mechanism | Configuration bit (`RD_DIS`) | Structural protocol-level refusal, no bit to disable |
| Can it be bypassed? | Yes — CVE-2019-17391 (voltage glitching, ~500µs boot window) | No public CVE of this kind; would require compromising the secure element's own silicon |
| Key scope | One key protects the entire flash | Independent keys per slot (16 slots) |
| Key rotation | Fixed for life (eFuse, write-once) | Regenerable via `GenKey`, unless the slot is individually locked |
| Where the crypto operation happens | ESP32 CPU (software) | Inside the chip itself (dedicated hardware) |

## 5. Conclusion

This isn't a "which signs faster" benchmark — it's a difference in **guarantees**. The ESP32's eFuse protection depends on a configuration bit having been set correctly and on no physical attack window existing; the ATECC608A doesn't depend on any configuration to deny access to the private key — it's a property of the protocol itself, verified here with real hardware evidence on both sides.

## Next step

None pending in the embedded security roadmap for the ATECC608A — this lab closes the eFuse vs. dedicated secure element comparison.
