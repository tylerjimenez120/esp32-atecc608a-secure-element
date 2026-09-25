/*
 * Lab 4a - ATECC608A exploration (step 3)
 * Reads the full Configuration Zone (128 bytes on ECC devices) via
 * cryptoauthlib. Read-only, does not lock or modify anything - safe to
 * run as many times as needed while still in the discovery phase.
 *
 * Reuses the same ATCAIfaceCfg from the previous step (atcab_init is
 * still what wakes the chip and readies it for commands).
 */

#include <stdio.h>
#include "esp_log.h"
#include "cryptoauthlib.h"

static const char *TAG = "atecc_config";

#define ATECC608A_I2C_ADDR   0xC0   /* 0x60 << 1 - verified in the previous step */
#define ATECC608A_I2C_BUS    0

void app_main(void)
{
    ATCAIfaceCfg cfg = {
        .iface_type = ATCA_I2C_IFACE,
        .devtype    = ATECC608A,
        .atcai2c = {
            .address    = ATECC608A_I2C_ADDR,
            .bus        = ATECC608A_I2C_BUS,
            .baud       = 100000,
        },
        .wake_delay = 1500,
        .rx_retries = 20,
    };

    ATCA_STATUS status = atcab_init(&cfg);
    if (status != ATCA_SUCCESS) {
        ESP_LOGE(TAG, "atcab_init failed, status=0x%02X", status);
        return;
    }
    ESP_LOGI(TAG, "atcab_init OK");

    /* ATCA_ECC_CONFIG_SIZE = 128 bytes: the full config zone for ECC
     * devices (ATECC508A/608A/608B). atcab_read_config_zone() reads it
     * in the internal word-size chunks the chip requires and assembles
     * it here in one contiguous buffer. */
    uint8_t config_data[ATCA_ECC_CONFIG_SIZE] = {0};
    status = atcab_read_config_zone(config_data);
    if (status != ATCA_SUCCESS) {
        ESP_LOGE(TAG, "atcab_read_config_zone failed, status=0x%02X", status);
        return;
    }

    ESP_LOGI(TAG, "Config zone (%d bytes):", ATCA_ECC_CONFIG_SIZE);
    /* dump 16 bytes per line, offset prefix, for readability in the log */
    for (int i = 0; i < ATCA_ECC_CONFIG_SIZE; i += 16) {
        ESP_LOGI(TAG, "  [%03d] %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 i,
                 config_data[i+0],  config_data[i+1],  config_data[i+2],  config_data[i+3],
                 config_data[i+4],  config_data[i+5],  config_data[i+6],  config_data[i+7],
                 config_data[i+8],  config_data[i+9],  config_data[i+10], config_data[i+11],
                 config_data[i+12], config_data[i+13], config_data[i+14], config_data[i+15]);
    }
}

/*
                    +---------------------+
                    |      app_main()     |
                    +----------+----------+
                               |
                               v
              +--------------------------------+
              |   ATCAIfaceCfg cfg = {...}       |
              |   iface=I2C, addr=0xC0, bus=0,   |
              |   baud=100kHz, wake_delay=1500   |
              +----------------+-----------------+
                               |
                               v
              +--------------------------------+
              |          atcab_init(&cfg)        |
              |  - internal I2C HAL init (hidden) |
              |  - wake the chip (hidden)         |
              |  - status != OK? -> log/return    |
              +----------------+-----------------+
                               | OK
                               v
              +--------------------------------+
              | uint8_t config_data[128] = {0}   |
              | atcab_read_config_zone(config)    |
              |  - sends Read command (hidden)    |
              |  - assembles the full 128 bytes   |
              |  - status != OK? -> log/return    |
              +----------------+-----------------+
                               | OK
                               v
              +--------------------------------+
              |   for i = 0 .. 128 step 16       |
              |     log 16 bytes per line         |
              |     (hex dump, offset [i])        |
              +--------------------------------+
*/