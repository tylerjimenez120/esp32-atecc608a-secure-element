/*
 * Lab 4a - ATECC608A exploration (step 2)
 * Reads the chip's Info (revision) using the official cryptoauthlib,
 * instead of reimplementing the I2C command protocol by hand.
 *
 * cryptoauthlib drives I2C internally (wake, framing, CRC, retries),
 * so the manual bus setup / wake attempt from the previous step is no
 * longer needed here - it's superseded by atcab_init().
 *
 * CONFIG STRUCT FIELD NAMES: verified against the real local header
 * (atca_iface.h) - .address, .bus, .baud under .atcai2c are correct for
 * this component (ATCA_ENABLE_DEPRECATED is not active, so it's NOT
 * .slave_address).
 *
 * ADDRESS FORMAT: verified against hal_esp32_i2c.c - the HAL does
 * `ATCA_IFACECFG_VALUE(cfg, atcai2c.address) >> 1` internally to get the
 * real 7-bit I2C address. That means .address must be given here in
 * 8-bit/pre-shifted form (0x60 << 1 = 0xC0), NOT the raw 7-bit address
 * we used directly with the i2c_master driver in the previous step.
 * Passing 0x60 here made the HAL compute 0x60 >> 1 = 0x30, talking to
 * the wrong device address entirely - which is what caused the garbage
 * response (0xFF) and the "rxdata is small buffer" error.
 */

#include <stdio.h>
#include "esp_log.h"
#include "cryptoauthlib.h"

static const char *TAG = "atecc_info";

#define ATECC608A_I2C_ADDR   0xC0   /* 0x60 << 1 - see note above */
#define ATECC608A_I2C_BUS    0

void app_main(void)
{
    /* ATCAIfaceCfg describes to cryptoauthlib HOW to reach the device:
     * which physical interface (I2C), which exact part (ATECC608A), and
     * the bus-level parameters its internal HAL needs to open the port. */
    ATCAIfaceCfg cfg = {
        .iface_type = ATCA_I2C_IFACE,   /* use I2C (not SWI/SPI) as the physical link */
        .devtype    = ATECC608A,        /* tells the lib which command set/timing to use for this specific chip */
        .atcai2c = {
            .address    = ATECC608A_I2C_ADDR,  /* 0xC0: pre-shifted 8-bit address (see header note) */
            .bus        = ATECC608A_I2C_BUS,   /* which ESP32 I2C controller (0 or 1) */
            .baud       = 100000,              /* bus clock speed, same 100kHz as before */
        },
        .wake_delay = 1500,   /* tWHI in us - lib waits this long internally after waking the chip, same value we verified earlier */
        .rx_retries = 20,     /* how many times the lib retries a read before giving up on a communication error */
    };

    /* atcab_init() does everything our manual wake_attempt()+bus_setup()
     * did before, but internally: opens the I2C port with the params
     * above, sends the wake pulse, waits wake_delay, and leaves the
     * device ready to accept commands. This single call replaces the
     * whole previous step. */
    ATCA_STATUS status = atcab_init(&cfg);
    if (status != ATCA_SUCCESS) {
        /* init can fail for the same reasons the old wake/scan could:
         * bad wiring, wrong address, chip not present, wrong config */
        ESP_LOGE(TAG, "atcab_init failed, status=0x%02X", status);
        return;
    }
    ESP_LOGI(TAG, "atcab_init OK - ATECC608A initialized via cryptoauthlib");

    /* atcab_info() builds and sends the actual "Info" command packet
     * (Count/Opcode/Param1/Param2/CRC framing, all handled internally),
     * and copies the chip's 4-byte reply into info[]. This is real
     * protocol-level communication, not just an I2C address ACK like
     * the bus scan in the previous step. */
    uint8_t info[4] = {0};
    status = atcab_info(info);
    if (status != ATCA_SUCCESS) {
        /* a failure here (as opposed to atcab_init failing) means the
         * chip is reachable but rejected/mishandled this specific
         * command - a different class of problem than a wiring/address
         * issue */
        ESP_LOGE(TAG, "atcab_info failed, status=0x%02X", status);
        return;
    }

    /* info[2] identifies the chip family (0x60 = ATECC608 series),
     * info[3] is this unit's silicon revision */
    ESP_LOGI(TAG, "Info response: %02X %02X %02X %02X",
             info[0], info[1], info[2], info[3]);
}

/*
                    +---------------------+
                    |      app_main()     |
                    +----------+----------+
                               |
                               v
              +--------------------------------+
              |   ATCAIfaceCfg cfg = {...}     |
              |   iface=I2C, addr=0xC0, bus=0,   |
              |   baud=100kHz, wake_delay=1500   |
              +----------------+-----------------+
                               |
                               v
              +--------------------------------+
              |          atcab_init(&cfg)      |
              |  - Init internal I2C HAL       |
              |  - Wake up the chip            |
              |  - status != OK? -> log error  |
              |                      & return  |
              +----------------+-----------------+
                               | OK
                               v
              +--------------------------------+
              |         atcab_info(info)       |
              |  - Send Info command (Revision)|
              |  - Read 4-byte response        |
              |  - status != OK? -> log error  |
              |                      & return  |
              +----------------+-----------------+
                               | OK
                               v
                    +---------------------+
                    | Log: info[0..3]     |
                    | "00 00 60 02"       |
                    | (Chip Identity)     |
                    +---------------------+
*/