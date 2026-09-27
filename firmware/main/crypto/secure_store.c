#include "secure_store.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "settings/settings.h"

static const char *TAG = "secure";
static uint8_t s_key[32];
static bool s_ready = false;

void secure_store_init(void)
{
    void *blob = NULL; size_t len = 0;
    if (nvs_get_blob_alloc("sec", "devkey", &blob, &len) && len == 32) { memcpy(s_key, blob, 32); free(blob); }
    else {
        esp_fill_random(s_key, 32);
        nvs_set_blob_ns("sec", "devkey", s_key, 32);
        ESP_LOGI(TAG, "generated device key");
    }
    s_ready = true;
}

static bool gcm(bool enc, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[16])
{
    mbedtls_gcm_context g; mbedtls_gcm_init(&g);
    int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        if (enc) rc = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, nonce, 12, NULL, 0, in, out, 16, tag);
        else rc = mbedtls_gcm_auth_decrypt(&g, n, nonce, 12, NULL, 0, tag, 16, in, out);
    }
    mbedtls_gcm_free(&g);
    return rc == 0;
}

char *secure_encrypt_str(const char *plain)
{
    if (!s_ready) secure_store_init();
    size_t n = strlen(plain);
    uint8_t *raw = malloc(12 + 16 + n);
    esp_fill_random(raw, 12);
    if (!gcm(true, s_key, raw, (const uint8_t *)plain, n, raw + 28, raw + 12)) { free(raw); return NULL; }
    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, raw, 28 + n);
    char *out = malloc(olen + 4);
    memcpy(out, "v1:", 3);
    mbedtls_base64_encode((unsigned char *)out + 3, olen + 1, &olen, raw, 28 + n);
    out[3 + olen] = 0;
    free(raw);
    return out;
}

char *secure_decrypt_str(const char *blob)
{
    if (!blob) return NULL;
    if (strncmp(blob, "v1:", 3) != 0) return strdup(blob);
    if (!s_ready) secure_store_init();
    size_t bl = strlen(blob + 3), rl = 0;
    uint8_t *raw = malloc(bl + 1);
    if (mbedtls_base64_decode(raw, bl + 1, &rl, (const unsigned char *)blob + 3, bl) != 0 || rl < 28) { free(raw); return NULL; }
    size_t n = rl - 28;
    char *out = malloc(n + 1);
    if (!gcm(false, s_key, raw, raw + 28, n, (uint8_t *)out, raw + 12)) { free(raw); free(out); return NULL; }
    out[n] = 0;
    free(raw);
    return out;
}

static bool derive(const char *pw, const uint8_t salt[16], uint8_t key[32])
{
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)pw, strlen(pw), salt, 16, 20000, 32, key) == 0;
}

/* layout: "TB5E" | salt16 | nonce12 | tag16 | ciphertext */
bool secure_encrypt_pw(const char *password, const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len)
{
    uint8_t *o = malloc(48 + in_len);
    memcpy(o, "TB5E", 4);
    esp_fill_random(o + 4, 28);
    uint8_t key[32];
    if (!derive(password, o + 4, key)) { free(o); return false; }
    if (!gcm(true, key, o + 20, in, in_len, o + 48, o + 32)) { free(o); return false; }
    *out = o; *out_len = 48 + in_len;
    return true;
}

bool secure_decrypt_pw(const char *password, const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len)
{
    if (in_len < 48 || memcmp(in, "TB5E", 4) != 0) return false;
    uint8_t key[32];
    if (!derive(password, in + 4, key)) return false;
    size_t n = in_len - 48;
    uint8_t *o = malloc(n + 1);
    uint8_t tag[16]; memcpy(tag, in + 32, 16);
    if (!gcm(false, key, in + 20, in + 48, n, o, tag)) { free(o); return false; }
    o[n] = 0;
    *out = o; *out_len = n;
    return true;
}

bool secure_sha256_file(const char *path, uint8_t out[32], void (*progress)(size_t, size_t, void *), void *ud)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); size_t total = ftell(f); fseek(f, 0, SEEK_SET);
    mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
    uint8_t *buf = malloc(16384); size_t n, done = 0;
    while ((n = fread(buf, 1, 16384, f)) > 0) { mbedtls_sha256_update(&c, buf, n); done += n; if (progress) progress(done, total, ud); }
    mbedtls_sha256_finish(&c, out);
    mbedtls_sha256_free(&c);
    free(buf); fclose(f);
    return true;
}

void secure_hex(const uint8_t *in, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", in[i]);
    out[n * 2] = 0;
}
