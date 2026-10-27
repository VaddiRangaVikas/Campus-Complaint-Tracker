#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// SHA-256 context
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]);

// HMAC-SHA256
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t mac[32]);

// PBKDF2-HMAC-SHA256 password derivation
void pbkdf2_sha256(const char *password, size_t pass_len,
                   const uint8_t *salt, size_t salt_len,
                   uint32_t iterations,
                   uint8_t *out_key, size_t out_len);

// Generate cryptographically secure random bytes
bool crypto_random_bytes(uint8_t *buf, size_t len);

// Hash password with fresh random salt (Generates 64-char hex salt and 64-char hex hash)
bool crypto_hash_password(const char *password, char *out_salt_hex, size_t salt_hex_size,
                          char *out_hash_hex, size_t hash_hex_size);

// Constant-time password verification
bool crypto_verify_password(const char *password, const char *salt_hex, const char *expected_hash_hex);

// Generate cryptographically secure session token (64 hex characters)
bool crypto_generate_token(char *out_token, size_t token_size);

#endif // CRYPTO_H
