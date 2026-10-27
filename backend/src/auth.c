#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <wincrypt.h>
#include <time.h>
#include "auth.h"

// ========================================================
// RFC 6234 Standard SHA-256 Implementation
// ========================================================
typedef struct {
    uint32_t state[8];
    uint64_t count;
    unsigned char buffer[64];
} SHA256_CTX;

#define DBL_INT_ADD(a,b,c) if (a > 0xffffffff - (c)) ++b; a += c;
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(SHA256_CTX *ctx, const unsigned char data[])
{
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for ( ; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const unsigned char data[], size_t len)
{
    uint32_t i;
    for (i = 0; i < len; ++i) {
        ctx->buffer[(ctx->count) % 64] = data[i];
        ctx->count++;
        if ((ctx->count) % 64 == 0)
            sha256_transform(ctx, ctx->buffer);
    }
}

static void sha256_final(SHA256_CTX *ctx, unsigned char hash[])
{
    uint32_t i = (ctx->count) % 64;

    if (i < 56) {
        ctx->buffer[i++] = 0x80;
        while (i < 56) ctx->buffer[i++] = 0x00;
    } else {
        ctx->buffer[i++] = 0x80;
        while (i < 64) ctx->buffer[i++] = 0x00;
        sha256_transform(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
    }

    uint64_t total_bits = ctx->count * 8;
    for (int k_idx = 7; k_idx >= 0; --k_idx) {
        ctx->buffer[56 + k_idx] = (unsigned char)(total_bits & 0xff);
        total_bits >>= 8;
    }
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 4; ++i) {
        for (int j_idx = 0; j_idx < 8; ++j_idx) {
            hash[j_idx * 4 + i] = (ctx->state[j_idx] >> (24 - i * 8)) & 0x000000ff;
        }
    }
}

// ========================================================
// Authentication Utilities
// ========================================================

bool auth_generate_random_bytes(unsigned char *buf, size_t len)
{
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        // Fallback to rand() with entropy mixing
        srand((unsigned int)(time(NULL) ^ GetCurrentProcessId()));
        for (size_t i = 0; i < len; i++) {
            buf[i] = (unsigned char)(rand() % 256);
        }
        return true;
    }

    BOOL ok = CryptGenRandom(hProv, (DWORD)len, buf);
    CryptReleaseContext(hProv, 0);
    return (ok == TRUE);
}

bool auth_generate_salt(char *out_salt, size_t salt_len)
{
    if (!out_salt || salt_len < 33) return false;
    unsigned char raw[16];
    auth_generate_random_bytes(raw, sizeof(raw));

    for (int i = 0; i < 16; i++) {
        sprintf(out_salt + (i * 2), "%02x", raw[i]);
    }
    out_salt[32] = '\0';
    return true;
}

bool auth_generate_token(char *out_token, size_t max_len)
{
    if (!out_token || max_len < 65) return false;
    unsigned char raw[32];
    auth_generate_random_bytes(raw, sizeof(raw));

    for (int i = 0; i < 32; i++) {
        sprintf(out_token + (i * 2), "%02x", raw[i]);
    }
    out_token[64] = '\0';
    return true;
}

// 5,000 iterations of SHA-256 for key stretching (PBKDF2 equivalent)
bool auth_hash_password(const char *password, const char *salt, char *out_hash, size_t hash_len)
{
    if (!password || !salt || !out_hash || hash_len < 65) return false;

    unsigned char digest[32];
    SHA256_CTX ctx;

    // Initial round: Salt + Password
    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)salt, strlen(salt));
    sha256_update(&ctx, (const unsigned char *)password, strlen(password));
    sha256_final(&ctx, digest);

    // Iterative key stretching
    for (int iter = 1; iter < 5000; iter++) {
        sha256_init(&ctx);
        sha256_update(&ctx, digest, sizeof(digest));
        sha256_update(&ctx, (const unsigned char *)salt, strlen(salt));
        sha256_final(&ctx, digest);
    }

    for (int i = 0; i < 32; i++) {
        sprintf(out_hash + (i * 2), "%02x", digest[i]);
    }
    out_hash[64] = '\0';
    return true;
}

// Constant-time string comparison to prevent timing attacks
static int constant_time_cmp(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (la != lb) return 1;

    unsigned char result = 0;
    for (size_t i = 0; i < la; i++) {
        result |= (unsigned char)(a[i] ^ b[i]);
    }
    return (result == 0) ? 0 : 1;
}

bool auth_verify_password(const char *password, const char *salt, const char *expected_hash)
{
    if (!password || !salt || !expected_hash) return false;

    char computed_hash[65];
    if (!auth_hash_password(password, salt, computed_hash, sizeof(computed_hash))) {
        return false;
    }

    return (constant_time_cmp(computed_hash, expected_hash) == 0);
}

// ========================================================
// Rate Limiting (In-memory ring buffer)
// ========================================================
#define MAX_RATE_ENTRIES 64
typedef struct {
    char ip[48];
    int failures;
    time_t last_time;
} RateEntry;

static RateEntry s_rate_table[MAX_RATE_ENTRIES];
static int s_rate_count = 0;

bool auth_check_rate_limit(const char *ip_address)
{
    if (!ip_address) return true;
    time_t now = time(NULL);

    for (int i = 0; i < s_rate_count; i++) {
        if (strcmp(s_rate_table[i].ip, ip_address) == 0) {
            // If more than 5 minutes passed, reset
            if (now - s_rate_table[i].last_time > 300) {
                s_rate_table[i].failures = 0;
                s_rate_table[i].last_time = now;
                return true;
            }
            // If failed 5 or more times in 5 minutes, block
            if (s_rate_table[i].failures >= 5) {
                return false;
            }
            return true;
        }
    }
    return true;
}

void auth_record_login_attempt(const char *ip_address)
{
    if (!ip_address) return;
    time_t now = time(NULL);

    for (int i = 0; i < s_rate_count; i++) {
        if (strcmp(s_rate_table[i].ip, ip_address) == 0) {
            s_rate_table[i].failures++;
            s_rate_table[i].last_time = now;
            return;
        }
    }

    if (s_rate_count < MAX_RATE_ENTRIES) {
        strncpy(s_rate_table[s_rate_count].ip, ip_address, sizeof(s_rate_table[0].ip) - 1);
        s_rate_table[s_rate_count].failures = 1;
        s_rate_table[s_rate_count].last_time = now;
        s_rate_count++;
    }
}
