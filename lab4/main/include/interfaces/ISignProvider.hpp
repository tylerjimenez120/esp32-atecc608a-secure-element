#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_err.h"

/*
 * Lab 4b - ISignProvider: abstract interface for a "digital signature"
 * capability, decoupled from WHERE the private key lives.
 *
 * Two concrete implementations satisfy this contract (in hal/):
 *   - ATECC608ASignProvider: private key generated and used INSIDE the
 *     ATECC608A secure element, never exported.
 *   - SoftwareSignProvider: private key generated and held in ESP32
 *     RAM/software, representing the eFuse/software-managed model.
 *
 * main.cpp depends only on this interface (real C++ polymorphism -
 * virtual dispatch) - it never knows or cares which concrete
 * implementation it's talking to.
 */
class ISignProvider {
public:
    static constexpr size_t PUBKEY_SIZE    = 64; /* uncompressed P-256 public key, X||Y */
    static constexpr size_t SIGNATURE_SIZE = 64; /* P-256 ECDSA signature, R||S */
    static constexpr size_t HASH_SIZE      = 32; /* SHA-256 digest size - what gets signed */

    virtual ~ISignProvider() = default;

    /* One-time setup: wakes/initializes the hardware (ATECC608A) or the
     * software crypto context, as applicable. Must be called once before
     * any other method. */
    virtual esp_err_t init() = 0;

    /* Generates a new key pair. Returns the PUBLIC key in outPublicKey
     * (caller-allocated, PUBKEY_SIZE bytes). The private key is never
     * returned through this interface. */
    virtual esp_err_t generateKey(uint8_t outPublicKey[PUBKEY_SIZE]) = 0;

    /* Signs a pre-computed SHA-256 hash (not the raw message - hashing
     * is the caller's responsibility, kept outside this interface).
     * Writes the signature into outSignature (caller-allocated,
     * SIGNATURE_SIZE bytes). */
    virtual esp_err_t sign(const uint8_t hash[HASH_SIZE],
                            uint8_t outSignature[SIGNATURE_SIZE]) = 0;

    /* Short identifying name for logging/comparison output, e.g.
     * "ATECC608A (hardware)" or "Software (RAM key)". */
    virtual const char *name() const = 0;
};