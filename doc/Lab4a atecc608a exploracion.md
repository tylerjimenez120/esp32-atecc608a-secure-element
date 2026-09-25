# Lab 4a — Exploración del ATECC608A

## 1. Teoría

### ¿Qué es el ATECC608A?

El ATECC608A es un **secure element** (elemento seguro) de Microchip: un chip dedicado de la familia CryptoAuthentication, independiente del microcontrolador principal, diseñado para almacenar y operar claves criptográficas sin que estas salgan nunca del silicio. A diferencia de guardar una clave privada en la flash del ESP32 (como en Lab 2 — Flash Encryption, o en las eFuses de Secure Boot), el ATECC608A nunca expone la clave privada por ningún canal: las operaciones criptográficas (firma, generación de claves) ocurren dentro del chip, y solo entran/salen datos públicos o resultados.

Especificaciones relevantes:
- Interfaz: I2C (dirección por defecto 0x60 en 7 bits / 0xC0 en 8 bits)
- 16 slots configurables de datos/claves
- Motores criptográficos: ECDSA/ECDH sobre NIST P-256, SHA-256, HMAC, AES-128, TRNG certificado NIST SP800-90A/B/C
- Watchdog interno: entre 0.7s y 1.7s, tras el cual el chip vuelve a dormir automáticamente si no recibe comandos

### Wake sequence

El chip pasa la mayor parte del tiempo dormido (bajo consumo, <150nA). Antes de cualquier comunicación real hay que despertarlo:

- **tWLO** (mínimo tiempo con SDA en low para despertar) = **60 µs**
- **tWHI** (espera mínima tras el wake antes de poder mandar comandos) = **1500 µs**

Valores verificados contra el datasheet oficial (*ATECC608A Summary Data Sheet*, DS40001977B, Tabla 2-2, AC Parameters).

El wake no es una transacción I2C normal — no hay ACK de dirección esperado. Se logra manteniendo SDA en low el tiempo suficiente. El truco estándar es mandar un byte `0x00` a la dirección especial `0x00` (general call): a 100kHz, la duración de ese byte en el bus (~90µs) ya cubre tWLO de sobra. El NACK que produce esa transacción es esperado, no un error.

### Protocolo de comandos

El ATECC608A no funciona como un sensor I2C típico (leer/escribir registros directo). Usa un protocolo de comandos propio sobre I2C: cada operación (`Info`, `Read`, `GenKey`, `Sign`, etc.) se empaqueta con Count, Opcode, Param1, Param2, Data y un CRC-16 de verificación. Reimplementar este framing a mano es propenso a errores (el CRC en particular usa un polinomio no estándar) — por eso se usa la librería oficial de Microchip, `cryptoauthlib`, en vez de reconstruirlo desde cero.

### Zona de configuración

El chip guarda sus parámetros de fábrica y configuración en una **zona de memoria EEPROM interna de 128 bytes** (no "registros" en el sentido clásico de periférico — es memoria no volátil reescribible byte a byte). Ahí vive: la dirección I2C configurada, el número de serie único del chip, el estado de lock de cada uno de los 16 slots, y los permisos de lectura/escritura por slot. El acceso a esa memoria siempre pasa por el protocolo de comandos I2C — el chip nunca la expone como memoria mapeada directamente; toda consulta es un comando `Read` que el propio chip resuelve internamente y devuelve por I2C.

## 2. Hardware

- Módulo ATECC608A (comprado junto con CH341A, logic analyzer, CJMCU-2112 y DS3231 vía AliExpress)
- ESP32 (Chip usado en labs anteriores de esta serie)
- Conexión:
  - SDA → GPIO21
  - SCL → GPIO22
  - VCC → 3.3V
  - GND → GND

## 3. Procedimiento

### Paso 1 — Wake manual + bus scan (bajo nivel, sin librería)

Objetivo: entender el mecanismo real de wake antes de esconderlo detrás de una librería.

Implementado con el driver `i2c_master` de ESP-IDF (API nueva, recomendada desde IDF 5.2+):
- `i2c_master_bus_setup()`: inicializa el bus (SDA=21, SCL=22, 100kHz, pull-ups internos).
- `atecc608a_wake_attempt()`: crea un handle **temporal** apuntando a la dirección `0x00`, transmite 1 byte (NACK esperado), destruye el handle, espera 1500µs (tWHI).
- `i2c_bus_scan()`: recorre las 126 direcciones posibles (0x01–0x7E) con `i2c_master_probe()`, reportando cuáles responden con ACK.

Resultado en hardware real:
```
E (303) i2c.master: I2C hardware NACK detected        <- esperado, parte del wake
E (303) i2c.master: I2C transaction unexpected nack detected
I (323) i2c_scanner: Wake attempt sent, waited 1500 us (tWHI)
I (333) i2c_scanner: Scanning I2C bus (addresses 0x01 - 0x7E)...
I (353) i2c_scanner:   -> 0x60  ACK  (expected ATECC608A address)
I (353) i2c_scanner: Scan complete: 1 device(s) found.
```

El ACK en `0x60` es la confirmación **indirecta** de que el wake funcionó — el wake en sí no tiene confirmación directa por protocolo.

Nota de diseño: el scan completo (127 direcciones) es más de lo estrictamente necesario para verificar un solo dispositivo conocido — se mantiene deliberadamente como paso de diagnóstico/documentación (confirma cableado, descarta conflictos de dirección), no por necesidad técnica.

### Paso 2 — Identidad del chip (`Info`) vía cryptoauthlib

Se reemplaza la implementación manual por la librería oficial `espressif/esp-cryptoauthlib` (componente ESP-IDF, instalado con `idf.py add-dependency`). La librería maneja wake, framing y CRC internamente — `atcab_init()` sustituye por completo el setup manual del paso 1.

```c
ATCAIfaceCfg cfg = {
    .iface_type = ATCA_I2C_IFACE,
    .devtype    = ATECC608A,
    .atcai2c = {
        .address = 0xC0,   // ver "Errores y fixes" — no es 0x60
        .bus     = 0,
        .baud    = 100000,
    },
    .wake_delay = 1500,
    .rx_retries = 20,
};
atcab_init(&cfg);
atcab_info(info);   // info[4]
```

Resultado en hardware real:
```
I (305) atecc_info: atcab_init OK - ATECC608A initialized via cryptoauthlib
I (305) atecc_info: Info response: 00 00 60 02
```

Interpretación: `60` identifica la familia ATECC**608**, `02` la revisión de silicio de esta unidad.

### Paso 3 — Lectura completa de la zona de configuración

```c
uint8_t config_data[ATCA_ECC_CONFIG_SIZE];   // 128 bytes
atcab_read_config_zone(config_data);
```

Resultado en hardware real (dump completo, 128 bytes):
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

Campos reconocidos (ver sección 5):
- Offset [004–007]: `00 00 60 02` — coincide exacto con la respuesta de `Info` del paso 2.
- Offset [016]: `C0` — la dirección I2C configurada (0xC0 = 0x60<<1).
- Offset [000–001] y [008–012]: número de serie único del chip (9 bytes repartidos en dos tramos).

## 4. Errores y fixes

### Error 1 — `ets_delay_us` no declarada

```
error: implicit declaration of function 'ets_delay_us'
```
**Causa**: faltaba el include del header que la declara.
**Fix**: agregar `#include "rom/ets_sys.h"`. La función sigue existiendo en IDF 5.5, solo faltaba el include (se descartó reemplazarla por `esp_rom_delay_us` porque `ets_delay_us` sigue siendo válida y es la que se venía usando).

### Error 2 — Dirección I2C: 7 bits vs 8 bits (el más importante)

Al migrar del driver `i2c_master` manual (Paso 1) a `cryptoauthlib` (Paso 2), se reutilizó la dirección de 7 bits (`0x60`) directamente en el campo `.address` de `ATCAIfaceCfg`. Resultado: loop infinito de error.

```
./cryptoauthlib/lib/calib/calib_execution.c:421:ffffffed:rxdata is small buffer
```

**Diagnóstico** (con evidencia, no suposición — inspeccionando el código fuente real del componente ya descargado):
- `atca_iface.h` confirma que el campo correcto es `.address` (no `.slave_address`, que solo existe bajo `ATCA_ENABLE_DEPRECATED`).
- `hal_esp32_i2c.c` reveló la causa real: el HAL hace `ATCA_IFACECFG_VALUE(cfg, atcai2c.address) >> 1` internamente para obtener la dirección real de 7 bits. Es decir, `.address` espera el valor **de 8 bits, pre-desplazado** (`0x60 << 1 = 0xC0`), no el de 7 bits usado directamente con `i2c_master`.
- Pasar `0x60` hacía que el HAL calculara `0x60 >> 1 = 0x30` — hablándole a una dirección equivocada, lo que producía respuesta corrupta (`0xFF`) y el error de "small buffer".

**Fix**: `#define ATECC608A_I2C_ADDR 0xC0` (con el campo `.address` correcto, sin cambiar el nombre).

**Nota de proceso**: se pidió una segunda opinión externa (Gemini) para este error, que acertó en el diagnóstico central (el shift a 8 bits) pero se equivocó en el nombre del campo (sugirió `.slave_address`). La corrección final se validó contra el código fuente real del componente, no contra ninguna de las dos fuentes por separado.

### Error 3 — Pines de `cryptoauthlib` en proyecto nuevo

Cada proyecto nuevo requiere reconfigurar los pines I2C específicos del componente (`CONFIG_ATCA_I2C_SDA_PIN`, `CONFIG_ATCA_I2C_SCL_PIN`), independientes de cualquier configuración hecha en `main.c` — no se heredan de otro proyecto ni de la struct `ATCAIfaceCfg`.

## 5. Conclusiones

- Se verificó en hardware real, en tres capas progresivas: presencia eléctrica en el bus (wake+scan), identidad a nivel de protocolo (`Info`), y contenido completo de configuración (`Read config zone`).
- El uso de `cryptoauthlib` en vez de reimplementar el protocolo a mano es la decisión correcta para cualquier trabajo real de IoT/Edge con este chip — no solo por velocidad de desarrollo, sino porque el CRC y el manejo de errores del protocolo son fáciles de implementar mal, y este es código relacionado a seguridad.
- El error más significativo (dirección 7 bits vs 8 bits) es un recordatorio de que cada capa de abstracción (driver ESP-IDF vs librería del fabricante) puede tener convenciones distintas para el mismo dato — verificar contra el código fuente real, no asumir consistencia entre capas.

## Próximo paso

Lab 4b — eFuse (ESP32) vs Secure Element dedicado (ATECC608A): comparación usando CVE-2019-17391 (extracción de claves de eFuse vía glitching) como evidencia real del lado eFuse, contra la incapacidad estructural del ATECC608A de exportar su clave privada.