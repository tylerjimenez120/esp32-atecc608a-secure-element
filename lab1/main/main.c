/*
 * Lab 4a - ATECC608A exploration
 * I2C bus scanner + manual wake sequence for the ATECC608A.
 *
 * Hardware:
 *   SDA -> GPIO21
 *   SCL -> GPIO22
 *   ATECC608A default I2C address: 0x60
 *
 * Wake sequence (verified against the official datasheet
 * ATECC608A Summary Data Sheet DS40001977B, Table 2-2 AC Parameters):
 *   tWLO (minimum SDA low time to wake the device) = 60 us
 *   tWHI (wait time after wake before the device can receive commands) = 1500 us
 *
 * Note: the wake is NOT a normal I2C transaction (there is no address
 * ACK). It's achieved by holding SDA low for >= 60us. The simplest and
 * most reliable way with the standard I2C driver is to send a 0x00 byte
 * to "address" 0x00 (general call) at 100kHz: at that speed, one byte
 * takes ~90us, more than enough to satisfy tWLO. The device will NOT
 * ACK that byte (this is expected, not a real error), but the electrical
 * effect on the bus does wake the chip.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"  /* new i2c_master driver API (IDF 5.2+) */
#include "esp_log.h"
#include "rom/ets_sys.h"        /* declares ets_delay_us() */

static const char *TAG = "i2c_scanner";  /* log tag used by every ESP_LOGx call in this file */

#define I2C_MASTER_SDA_IO      21       /* GPIO used for the I2C data line (SDA) */
#define I2C_MASTER_SCL_IO      22       /* GPIO used for the I2C clock line (SCL) */
#define I2C_MASTER_FREQ_HZ     100000   /* bus clock speed: 100kHz = I2C Standard Mode */
#define I2C_MASTER_PORT        0        /* which of the ESP32's 2 physical I2C controllers to use (I2C_NUM_0) */

#define ATECC608A_ADDR         0x60     /* ATECC608A default 7-bit I2C address */

/* handle for the initialized I2C bus (port+pins+clock); shared by every
 * device (real or temporary) created on top of it */
static i2c_master_bus_handle_t bus_handle;

static void i2c_master_bus_setup(void)
{
    /* logical/physical setup of the bus: which controller, which pins,
     * which clock source, noise filtering, pull-ups. This does NOT talk
     * to any device yet - it only brings the hardware peripheral up. */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_PORT,           /* controller 0 */
        .sda_io_num = I2C_MASTER_SDA_IO,       /* GPIO21 */
        .scl_io_num = I2C_MASTER_SCL_IO,       /* GPIO22 */
        .clk_source = I2C_CLK_SRC_DEFAULT,     /* let the driver pick the internal clock source */
        .glitch_ignore_cnt = 7,                /* ignore electrical glitches shorter than 7 clock cycles */
        .flags.enable_internal_pullup = true,  /* use ESP32's internal pull-up resistors on SDA/SCL */
    };
    /* creates the bus peripheral instance from bus_config and returns its
     * handle in bus_handle; ESP_ERROR_CHECK aborts if it fails (should
     * only fail on bad config or already-in-use resources) */
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    ESP_LOGI(TAG, "I2C master bus initialized (SDA=%d, SCL=%d, %d Hz)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
}

/*
 * Wake attempt: writes a 0x00 byte to address 0x00 (general call).
 * No real ACK is expected from the ATECC608A; the purpose is only to
 * hold SDA low long enough (tWLO >= 60us, comfortably met at 100kHz)
 * to trigger the chip's internal wake. After that, tWHI (1500us) is
 * observed before attempting to communicate.
 */
static void atecc608a_wake_attempt(void)
{
    /* config for a FAKE/throwaway device at address 0x00 (general call).
     * This is NOT the ATECC608A's own address (0x60) - it exists only to
     * let us transmit a byte and hold SDA low, which is what actually
     * wakes the chip. This handle is temporary: used once, then destroyed. */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,   /* standard 7-bit addressing */
        .device_address = 0x00,                  /* general call address, not the ATECC's real address */
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,       /* same 100kHz bus speed */
    };
    i2c_master_dev_handle_t dev_handle;
    /* binds dev_cfg to bus_handle and returns the new (temporary) device
     * handle in dev_handle; bails out if the driver couldn't create it */
    if (i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle) != ESP_OK) {
        ESP_LOGW(TAG, "Could not create device handle for the wake attempt");
        return;
    }

    uint8_t wake_byte = 0x00;  /* actual byte value is irrelevant; only its
                                 * transmission duration on the bus matters */
    /* Result is ignored: a NACK here is expected, not a failure. */
    i2c_master_transmit(dev_handle, &wake_byte, 1, 50);  /* handle, buffer, 1 byte, 50ms timeout */

    /* the temporary handle already did its job (generated the pulse);
     * remove it since 0x00 is not a real device we'll talk to again */
    i2c_master_bus_rm_device(dev_handle);

    /* tWHI: minimum time before the chip can receive commands */
    ets_delay_us(1500);
    ESP_LOGI(TAG, "Wake attempt sent, waited %d us (tWHI)", 1500);
}

/*
 * Full-range scan (0x01-0x7E) instead of a single probe at ATECC608A_ADDR.
 * The target address is already known (0x60); this isn't a discovery scan.
 * It's used deliberately as a diagnostic/documentation step: it confirms
 * the wiring and wake sequence work end-to-end (indirect proof the wake
 * succeeded, since the wake itself has no direct ACK), rules out generic
 * bus/wiring problems, and would reveal any other device or an
 * unexpectedly reconfigured address. For production code, a single
 * i2c_master_probe(bus_handle, ATECC608A_ADDR, ...) would be enough.
 */
static void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus (addresses 0x01 - 0x7E)...");
    int found = 0;  /* counts how many addresses ACKed */

    /* 7-bit address space: 0x00 (general call) and 0x7F (reserved) are
     * skipped, so the usable range is 0x01-0x7E */
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        /* i2c_master_probe() internally creates a temporary device handle
         * for `addr`, attempts a minimal transaction, and reports whether
         * it got an ACK - same idea as the manual wake, but done by the
         * driver for us */
        esp_err_t ret = i2c_master_probe(bus_handle, addr, 50);
        if (ret == ESP_OK) {  /* ESP_OK here means ACK: a real device answered */
            found++;
            if (addr == ATECC608A_ADDR) {
                ESP_LOGI(TAG, "  -> 0x%02X  ACK  (expected ATECC608A address)", addr);
            } else {
                ESP_LOGI(TAG, "  -> 0x%02X  ACK", addr);
            }
        }
        /* ret != ESP_OK (NACK/timeout) means nothing is at that address;
         * silently continue to the next one */
    }

    if (found == 0) {
        ESP_LOGW(TAG, "No device found on the bus.");
    } else {
        ESP_LOGI(TAG, "Scan complete: %d device(s) found.", found);
    }
}

void app_main(void)
{
    i2c_master_bus_setup();     /* 1. bring up the I2C peripheral (pins/clock/port) */

    atecc608a_wake_attempt();   /* 2. wake the ATECC608A (assumed successful, not confirmed here) */
    i2c_bus_scan();             /* 3. scan the bus; an ACK at 0x60 is the indirect confirmation of step 2 */
}


/*
                    +---------------------+
                    |      app_main()     |
                    +----------+----------+
                               |
                               v
              +--------------------------------+
              |     i2c_master_bus_setup()     |
              |  - Config SDA=21 SCL=22 100kHz |
              |  - i2c_new_master_bus()        |
              |  -> bus_handle                 |
              +----------------+-----------------+
                               |
                               v
              +--------------------------------+
              |    atecc608a_wake_attempt()     |
              |  1. add_device(addr=0x00)       |
              |     -> dev_handle (temporary)   |
              |  2. transmit(0x00) [NACK expected]
              |  3. rm_device(dev_handle)       |
              |  4. ets_delay_us(1500)  (tWHI)   |
              +----------------+-----------------+
                               |
                    (Assumed awake,
                     no direct confirmation)
                               |
                               v
              +--------------------------------+
              |        i2c_bus_scan()          |
              |  for addr = 0x01 .. 0x7E:       |
              |     i2c_master_probe(bus, addr) |
              |       |ACK?                     |
              |       +-- yes -> found++        |
              |       |          log address    |
              |       +-- no  -> continue       |
              +----------------+-----------------+
                               |
                               v
                    +---------------------+
                    |  Log Final Result   |
                    |  found==0 -> warn   |
                    |  found>0  -> info   |
                    |  (Wake indirectly   |
                    |   confirmed via     |
                    |   ACK at 0x60)      |
                    +---------------------+
*/