#include "interfaces/ISignProvider.hpp"
#include "hal/ATECC608ASignProvider.hpp"
#include "esp_log.h"

extern "C" {
#include "cryptoauthlib.h"
}

static const char *TAG = "lab4b_main";

extern "C" void app_main(void)
{
    /* main.cpp only ever talks to the abstract interface - swapping the
     * concrete implementation here (e.g. back to a software one) would
     * not require touching anything below this line. */
    ISignProvider *provider = new ATECC608ASignProvider(/*i2cAddress=*/0xC0, /*keySlot=*/0);

    if (provider->init() != ESP_OK) {
        ESP_LOGE(TAG, "init failed - aborting");
        return;
    }
    ESP_LOGI(TAG, "Provider: %s", provider->name());

    /* ONE-TIME, IRREVERSIBLE: locks the config zone as-is (factory
     * default already configures slot 0 as an ECC private key slot,
     * per the dump from Lab 4a - nothing is being rewritten here) and
     * the data zone, which is required before GenKey/Sign will execute.
     * Verified sequence: https://github.com/MicrochipTech/cryptoauthlib/issues/25
     * atcab_is_config_locked/atcab_is_data_locked make this safe to run
     * on every boot - only the first run actually locks anything. */
    bool isLocked = false;
    atcab_is_config_locked(&isLocked);
    if (!isLocked) {
        ESP_LOGW(TAG, "Config zone NOT locked - locking now (permanent, one-time)");
        ATCA_STATUS lockStatus = atcab_lock_config_zone();
        if (lockStatus != ATCA_SUCCESS) {
            ESP_LOGE(TAG, "atcab_lock_config_zone failed: 0x%02X", lockStatus);
            return;
        }
        ESP_LOGI(TAG, "Config zone locked");
    } else {
        ESP_LOGI(TAG, "Config zone already locked");
    }

    atcab_is_data_locked(&isLocked);
    if (!isLocked) {
        ESP_LOGW(TAG, "Data zone NOT locked - locking now (permanent, one-time)");
        ATCA_STATUS lockStatus = atcab_lock_data_zone();
        if (lockStatus != ATCA_SUCCESS) {
            ESP_LOGE(TAG, "atcab_lock_data_zone failed: 0x%02X", lockStatus);
            return;
        }
        ESP_LOGI(TAG, "Data zone locked");
    } else {
        ESP_LOGI(TAG, "Data zone already locked");
    }

    /* Step 1: generate a key pair. The private half never leaves the
     * chip - only the public key comes back here. */
    uint8_t publicKey[ISignProvider::PUBKEY_SIZE];
    if (provider->generateKey(publicKey) != ESP_OK) {
        ESP_LOGE(TAG, "generateKey failed - aborting");
        return;
    }
    ESP_LOGI(TAG, "Public key (X||Y, %d bytes):", ISignProvider::PUBKEY_SIZE);
    ESP_LOG_BUFFER_HEX(TAG, publicKey, ISignProvider::PUBKEY_SIZE);

    /* Step 2: sign a test message. Hashing stays outside the interface -
     * the caller (here, main.cpp) is responsible for it. */
    const char *message = "Lab 4b - ATECC608A structural key protection demo";
    uint8_t hash[ISignProvider::HASH_SIZE];
    atcac_sw_sha2_256(reinterpret_cast<const uint8_t *>(message), strlen(message), hash);

    uint8_t signature[ISignProvider::SIGNATURE_SIZE];
    if (provider->sign(hash, signature) != ESP_OK) {
        ESP_LOGE(TAG, "sign failed - aborting");
        return;
    }
    ESP_LOGI(TAG, "Signature (R||S, %d bytes):", ISignProvider::SIGNATURE_SIZE);
    ESP_LOG_BUFFER_HEX(TAG, signature, ISignProvider::SIGNATURE_SIZE);

    /* Step 3: THE comparison - attempt to read the private key slot's
     * data zone directly. This is not an eFuse-style "flag" that could
     * be misconfigured (like RD_DIS on the ESP32) - the ATECC608A's
     * command protocol itself never exposes an ECC private key in
     * cleartext through the Read command, by construction. Expected
     * result: an execution error from the chip, every time. */
    uint8_t attemptedReadBuf[32];
    ATCA_STATUS status = atcab_read_zone(ATCA_ZONE_DATA, /*slot=*/0,
                                          /*block=*/0, /*offset=*/0,
                                          attemptedReadBuf, sizeof(attemptedReadBuf));
    if (status != ATCA_SUCCESS) {
        ESP_LOGW(TAG, "Private key slot read REJECTED by the chip (status 0x%02X) - "
                       "this is the structural guarantee eFuse RD_DIS cannot match.",
                 status);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED: private key slot read succeeded - check slot config");
    }

    delete provider;
}


/*
mensaje  →  hash(mensaje)  →  sign(hash)  →  firma (r,s)
   ↑              ↑                ↑
  ESP32         ESP32            ATECC608A
 (tuyo)      (atcab_sw_sha2_256,   (usa la clave
              corre en el ESP32,    privada interna)
              NO en el chip)

1.-Instantiate the class — pass the device address and the slot to the constructor.
2.-Call init() to give it the setup from step 1.
3.-Check and, if needed, lock the config zone and the data zone (one-time, irreversible step — without this, generateKey/sign fail with ATCA_EXECUTION_ERROR).
4.-Call provider->generateKey, which returns the public key.
5.-We have the message and generate its hash.
6.-Call provider->sign, which takes the hash from step 5 and returns the signature.
7.-Attempt to read the zone where the private key lives (chip) and it obviously fails, because it's locked by the chip at the protocol level — not by any firmware configuration of ours.
 

new ATECC608ASignProvider(addr, slot)
        |
        v
    init()  --------------------> FAIL -> abort
        |
        v
lock config zone (if not locked)  [IRREVERSIBLE]
lock data zone   (if not locked)  [IRREVERSIBLE]
        |
        v
  generateKey()  ---------------> FAIL -> abort
  (returns public key)
        |
        v
  hash = SHA-256(message)   <- runs on ESP32, not the chip
        |
        v
  sign(hash)  -------------------> FAIL -> abort
  (returns signature r||s)
        |
        v
attempt read_zone(private key slot)
        |
        v
REJECTED (ATCA_EXECUTION_ERROR) <- expected, always
        |
        v
    delete provider
*/