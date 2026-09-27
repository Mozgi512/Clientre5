#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AES-256-GCM helpers. Device key is generated once and kept in NVS. */
void secure_store_init(void);
/* Encrypt with the device key. Output is base64 "v1:" + nonce + tag + ciphertext. Caller frees. */
char *secure_encrypt_str(const char *plain);
char *secure_decrypt_str(const char *blob);          /* NULL on failure; plain text passthrough if not "v1:" */
/* Password based (PBKDF2-HMAC-SHA256, 20k rounds). */
bool secure_encrypt_pw(const char *password, const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
bool secure_decrypt_pw(const char *password, const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
bool secure_sha256_file(const char *path, uint8_t out[32], void (*progress)(size_t done, size_t total, void *), void *ud);
void secure_hex(const uint8_t *in, size_t n, char *out);
#ifdef __cplusplus
}
#endif
