#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// ==========================================
// SHA-256 Implementation (FIPS 180-4)
// ==========================================

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define s1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_init(SHA256_CTX *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = s1(w[i - 2]) + w[i - 7] + s0(w[i - 15]) + w[i - 16];
    }

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + S1(e) + Ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = S0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
    size_t buffer_idx = (size_t)(ctx->count % 64);
    ctx->count += len;

    size_t part_len = 64 - buffer_idx;
    size_t i = 0;

    if (len >= part_len) {
        memcpy(&ctx->buffer[buffer_idx], data, part_len);
        sha256_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha256_transform(ctx->state, &data[i]);
        }
        buffer_idx = 0;
    }

    if (i < len) {
        memcpy(&ctx->buffer[buffer_idx], &data[i], len - i);
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]) {
    uint8_t final_count[8];
    uint64_t bits = ctx->count * 8;
    for (int i = 0; i < 8; i++) {
        final_count[7 - i] = (uint8_t)(bits >> (i * 8));
    }

    size_t buffer_idx = (size_t)(ctx->count % 64);
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);

    uint8_t zero = 0;
    while ((ctx->count % 64) != 56) {
        sha256_update(ctx, &zero, 1);
    }

    sha256_update(ctx, final_count, 8);

    for (int i = 0; i < 8; i++) {
        hash[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

// ==========================================
// HMAC-SHA256 Implementation
// ==========================================

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t mac[32]) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, k);
    } else {
        memcpy(k, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    uint8_t inner_hash[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, mac);
}

// ==========================================
// PBKDF2-HMAC-SHA256
// ==========================================

void pbkdf2_sha256(const char *password, size_t pass_len,
                   const uint8_t *salt, size_t salt_len,
                   uint32_t iterations,
                   uint8_t *out_key, size_t out_len) {
    uint8_t u[32];
    uint8_t t[32];
    uint8_t salt_block[256];
    size_t offset = 0;
    uint32_t block_index = 1;

    while (offset < out_len) {
        size_t block_salt_len = salt_len + 4;
        if (block_salt_len > sizeof(salt_block)) break;

        memcpy(salt_block, salt, salt_len);
        salt_block[salt_len]     = (uint8_t)(block_index >> 24);
        salt_block[salt_len + 1] = (uint8_t)(block_index >> 16);
        salt_block[salt_len + 2] = (uint8_t)(block_index >> 8);
        salt_block[salt_len + 3] = (uint8_t)(block_index);

        hmac_sha256((const uint8_t *)password, pass_len, salt_block, block_salt_len, u);
        memcpy(t, u, 32);

        for (uint32_t iter = 1; iter < iterations; iter++) {
            hmac_sha256((const uint8_t *)password, pass_len, u, 32, u);
            for (int j = 0; j < 32; j++) {
                t[j] ^= u[j];
            }
        }

        size_t to_copy = (out_len - offset < 32) ? (out_len - offset) : 32;
        memcpy(out_key + offset, t, to_copy);
        offset += to_copy;
        block_index++;
    }
}

// ==========================================
// Random Bytes Generation
// ==========================================

bool crypto_random_bytes(uint8_t *buf, size_t len) {
    if (!buf || len == 0) return false;

#if defined(_WIN32) || defined(_WIN64)
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        BOOL ok = CryptGenRandom(hProv, (DWORD)len, buf);
        CryptReleaseContext(hProv, 0);
        if (ok) return true;
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t r = read(fd, buf, len);
        close(fd);
        if (r == (ssize_t)len) return true;
    }
#endif

    // Fallback pseudo-random if OS RNG unavailable
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand() & 0xFF);
    }
    return true;
}

// Utility: bytes to lowercase hex
static void bytes_to_hex(const uint8_t *src, size_t src_len, char *dst, size_t dst_size) {
    if (dst_size < src_len * 2 + 1) return;
    for (size_t i = 0; i < src_len; i++) {
        sprintf(dst + (i * 2), "%02x", src[i]);
    }
    dst[src_len * 2] = '\0';
}

// Utility: hex to bytes
static bool hex_to_bytes(const char *hex, uint8_t *dst, size_t dst_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != dst_len * 2) return false;
    for (size_t i = 0; i < dst_len; i++) {
        unsigned int val;
        if (sscanf(hex + (i * 2), "%02x", &val) != 1) return false;
        dst[i] = (uint8_t)val;
    }
    return true;
}

#define PBKDF2_ROUNDS 10000

bool crypto_hash_password(const char *password, char *out_salt_hex, size_t salt_hex_size,
                          char *out_hash_hex, size_t hash_hex_size) {
    if (!password || !out_salt_hex || !out_hash_hex) return false;
    if (salt_hex_size < 65 || hash_hex_size < 65) return false;

    uint8_t salt[32];
    if (!crypto_random_bytes(salt, sizeof(salt))) return false;

    uint8_t derived_key[32];
    pbkdf2_sha256(password, strlen(password), salt, sizeof(salt), PBKDF2_ROUNDS, derived_key, sizeof(derived_key));

    bytes_to_hex(salt, sizeof(salt), out_salt_hex, salt_hex_size);
    bytes_to_hex(derived_key, sizeof(derived_key), out_hash_hex, hash_hex_size);

    return true;
}

bool crypto_verify_password(const char *password, const char *salt_hex, const char *expected_hash_hex) {
    if (!password || !salt_hex || !expected_hash_hex) return false;

    uint8_t salt[32];
    if (!hex_to_bytes(salt_hex, salt, sizeof(salt))) return false;

    uint8_t derived_key[32];
    pbkdf2_sha256(password, strlen(password), salt, sizeof(salt), PBKDF2_ROUNDS, derived_key, sizeof(derived_key));

    char computed_hash_hex[65];
    bytes_to_hex(derived_key, sizeof(derived_key), computed_hash_hex, sizeof(computed_hash_hex));

    // Constant-time comparison
    size_t len_a = strlen(computed_hash_hex);
    size_t len_b = strlen(expected_hash_hex);
    if (len_a != len_b) return false;

    int diff = 0;
    for (size_t i = 0; i < len_a; i++) {
        diff |= (computed_hash_hex[i] ^ expected_hash_hex[i]);
    }
    return (diff == 0);
}

bool crypto_generate_token(char *out_token, size_t token_size) {
    if (!out_token || token_size < 65) return false;

    uint8_t rand_bytes[32];
    if (!crypto_random_bytes(rand_bytes, sizeof(rand_bytes))) return false;

    bytes_to_hex(rand_bytes, sizeof(rand_bytes), out_token, token_size);
    return true;
}
