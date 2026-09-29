#include "hal/ATECC608ASignProvider.hpp"
#include "esp_log.h"

static const char *TAG = "atecc608a_sign_provider";

ATECC608ASignProvider::ATECC608ASignProvider(uint8_t i2cAddress, uint8_t keySlot)
    : keySlot_(keySlot), initialized_(false)
{
    /* Same config verified working on hardware in Lab 4a, step 2/3 -
     * .address must be the 8-bit pre-shifted value (0xC0 for 0x60),
     * see Lab 4a Error 2 for why. */
    cfg_ = {
        .iface_type = ATCA_I2C_IFACE,
        .devtype    = ATECC608A,
        .atcai2c = {
            .address = i2cAddress,
            .bus     = 0,
            .baud    = 100000,
        },
        .wake_delay = 1500,
        .rx_retries = 20,
    };
}

esp_err_t ATECC608ASignProvider::init()
{
    ATCA_STATUS status = atcab_init(&cfg_);
    if (status != ATCA_SUCCESS) {
        ESP_LOGE(TAG, "atcab_init failed: 0x%02X", status);
        return ESP_FAIL;
    }
    initialized_ = true;
    ESP_LOGI(TAG, "ATECC608A initialized (slot %u)", keySlot_);
    return ESP_OK;
}

esp_err_t ATECC608ASignProvider::generateKey(uint8_t outPublicKey[PUBKEY_SIZE])
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    /* GenKey with a slot number, no input key, generates a new key pair
     * INSIDE the chip and returns only the public key. The private key
     * never leaves the slot - that guarantee is enforced by the chip's
     * silicon, not by this code. */
    ATCA_STATUS status = atcab_genkey(keySlot_, outPublicKey);
    if (status != ATCA_SUCCESS) {
        ESP_LOGE(TAG, "atcab_genkey failed: 0x%02X", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ATECC608ASignProvider::sign(const uint8_t hash[HASH_SIZE],
                                       uint8_t outSignature[SIGNATURE_SIZE])
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Sign operates on a pre-computed hash using the private key that
     * lives in keySlot_ - again, that key is never read out, only used
     * internally by the chip to produce the signature. */
    ATCA_STATUS status = atcab_sign(keySlot_, hash, outSignature);
    if (status != ATCA_SUCCESS) {
        ESP_LOGE(TAG, "atcab_sign failed: 0x%02X", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

const char *ATECC608ASignProvider::name() const
{
    return "ATECC608A (hardware secure element)";
}