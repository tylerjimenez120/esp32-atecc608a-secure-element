#pragma once

#include "interfaces/ISignProvider.hpp"

extern "C" {
#include "cryptoauthlib.h"
}

/*
 * Lab 4b - ATECC608ASignProvider: concrete ISignProvider implementation
 * backed by the ATECC608A secure element via cryptoauthlib.
 *
 * The private key is generated and lives INSIDE the chip's key slot -
 * this class never has access to it, only cryptoauthlib does (and only
 * inside the chip's own silicon). generateKey() returns the PUBLIC key
 * derived from that internal private key; the private key itself never
 * leaves the chip through any call here.
 */
class ATECC608ASignProvider : public ISignProvider {
public:
    /* i2cAddress: 8-bit, pre-shifted I2C address (0xC0 for the 0x60
     * 7-bit address - see Lab 4a, Error 2). keySlot: which of the 16
     * ATECC608A key slots to use (0-15) for this key pair. */
    explicit ATECC608ASignProvider(uint8_t i2cAddress = 0xC0, uint8_t keySlot = 0);

    esp_err_t init() override;
    esp_err_t generateKey(uint8_t outPublicKey[PUBKEY_SIZE]) override;
    esp_err_t sign(const uint8_t hash[HASH_SIZE],
                    uint8_t outSignature[SIGNATURE_SIZE]) override;
    const char *name() const override;

private:
    ATCAIfaceCfg cfg_;
    uint8_t keySlot_;
    bool initialized_;
};