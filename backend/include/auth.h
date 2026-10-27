#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include "common.h"

// Generate cryptographically secure random bytes
bool auth_generate_random_bytes(unsigned char *buf, size_t len);

// Generate random hex token (e.g. for session tokens)
bool auth_generate_token(char *out_token, size_t max_len);

// Salted password hashing (PBKDF2-SHA256 equivalent iterated hashing)
bool auth_hash_password(const char *password, const char *salt, char *out_hash, size_t hash_len);

// Verify password against salt and expected hash
bool auth_verify_password(const char *password, const char *salt, const char *expected_hash);

// Generate unique salt hex string
bool auth_generate_salt(char *out_salt, size_t salt_len);

// Rate limiting check: returns true if IP is allowed, false if rate limited
bool auth_check_rate_limit(const char *ip_address);
void auth_record_login_attempt(const char *ip_address);

#endif // AUTH_H
