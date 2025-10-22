/*
 * Copied and adapted from Espressif BLUFI example (security part).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
#include "esp_bt.h"
#endif

#include "esp_blufi_api.h"

// mbedTLS
#include "mbedtls/aes.h"
#include "mbedtls/dhm.h"
#include "mbedtls/md5.h"
#include "esp_crc.h"


struct blufi_security
{
#define DH_SELF_PUB_KEY_LEN 128
    uint8_t self_public_key[DH_SELF_PUB_KEY_LEN];

#define SHARE_KEY_LEN 128
    uint8_t share_key[SHARE_KEY_LEN];
    size_t share_len;

#define PSK_LEN 16
    uint8_t psk[PSK_LEN];

    uint8_t *dh_param;
    int dh_param_len;

    uint8_t iv[16];

    mbedtls_dhm_context dhm;
    mbedtls_aes_context aes;
};

static struct blufi_security *blufi_sec;

static int myrand(void *rng_state, unsigned char *output, size_t len)
{
    esp_fill_random(output, len);
    return 0;
}

extern void btc_blufi_report_error(esp_blufi_error_state_t state);

#define SEC_TYPE_DH_PARAM_LEN 0x00
#define SEC_TYPE_DH_PARAM_DATA 0x01

void blufi_dh_negotiate_data_handler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free)
{
    if (data == NULL || len < 3) {
        esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
        return;
    }

    uint8_t type = data[0];

    if (blufi_sec == NULL) {
        esp_blufi_send_error_info(ESP_BLUFI_INIT_SECURITY_ERROR);
        return;
    }

    switch (type) {
    case SEC_TYPE_DH_PARAM_LEN: {
        blufi_sec->dh_param_len = ((data[1] << 8) | data[2]);
        if (blufi_sec->dh_param) {
            free(blufi_sec->dh_param);
            blufi_sec->dh_param = NULL;
        }
        blufi_sec->dh_param = (uint8_t *)malloc(blufi_sec->dh_param_len);
        if (blufi_sec->dh_param == NULL) {
            blufi_sec->dh_param_len = 0;
            btc_blufi_report_error(ESP_BLUFI_DH_MALLOC_ERROR);
        }
        break;
    }
    case SEC_TYPE_DH_PARAM_DATA: {
        if (blufi_sec->dh_param == NULL) {
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }
        memcpy(blufi_sec->dh_param, &data[1], blufi_sec->dh_param_len);

        int ret;
        mbedtls_mpi p, g;
        mbedtls_mpi_init(&p);
        mbedtls_mpi_init(&g);

        uint16_t pubkey_len = 0;
        uint8_t *pubkey = &blufi_sec->self_public_key[0];

        // Parse DH parameters: p|g
        do {
            uint8_t *param = blufi_sec->dh_param;
            uint16_t plen = (param[0] << 8) | param[1];
            param += 2;
            if ((ret = mbedtls_mpi_read_binary(&p, param, plen)) != 0) {
                btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
                break;
            }
            param += plen;
            uint16_t glen = (param[0] << 8) | param[1];
            param += 2;
            if ((ret = mbedtls_mpi_read_binary(&g, param, glen)) != 0) {
                btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
                break;
            }

            // Setup DHM
            if ((ret = mbedtls_dhm_set_group(&blufi_sec->dhm, &p, &g)) != 0) {
                btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
                break;
            }

            // Make public key
            size_t p_len = mbedtls_dhm_get_len(&blufi_sec->dhm); // prime length in bytes
            size_t out_len = p_len < DH_SELF_PUB_KEY_LEN ? p_len : DH_SELF_PUB_KEY_LEN;
            if ((ret = mbedtls_dhm_make_public(&blufi_sec->dhm, (int)p_len, pubkey, out_len, myrand, NULL)) != 0) {
                btc_blufi_report_error(ESP_BLUFI_MAKE_PUBLIC_ERROR);
                break;
            }
            pubkey_len = (uint16_t)out_len;

            // Read peer public key and calc secret
            param += glen;
            uint16_t peerkey_len = (param[0] << 8) | param[1];
            param += 2;
            if ((ret = mbedtls_dhm_read_public(&blufi_sec->dhm, param, peerkey_len)) != 0) {
                btc_blufi_report_error(ESP_BLUFI_READ_PARAM_ERROR);
                break;
            }
            if ((ret = mbedtls_dhm_calc_secret(&blufi_sec->dhm, blufi_sec->share_key, SHARE_KEY_LEN, &blufi_sec->share_len, myrand, NULL)) != 0) {
                btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
                break;
            }

            // Derive PSK via MD5
            mbedtls_md5_context md5_ctx;
            mbedtls_md5_init(&md5_ctx);
            mbedtls_md5_starts(&md5_ctx);
            mbedtls_md5_update(&md5_ctx, blufi_sec->share_key, blufi_sec->share_len);
            mbedtls_md5_finish(&md5_ctx, blufi_sec->psk);
            mbedtls_md5_free(&md5_ctx);

            // Setup AES key
            mbedtls_aes_setkey_enc(&blufi_sec->aes, blufi_sec->psk, 128);
        } while (0);

        mbedtls_mpi_free(&p);
        mbedtls_mpi_free(&g);

        *output_data = &blufi_sec->self_public_key[0];
        *output_len = pubkey_len;
        *need_free = false;
        break;
    }
    default:
        break;
    }
}

int blufi_aes_encrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len)
{
    int ret;
    size_t iv_offset = 0;
    uint8_t iv0[16];
    if (!blufi_sec) return -1;
    memcpy(iv0, blufi_sec->iv, sizeof(blufi_sec->iv));
    iv0[0] = iv8;
    ret = mbedtls_aes_crypt_cfb128(&blufi_sec->aes, MBEDTLS_AES_ENCRYPT, crypt_len, &iv_offset, iv0, crypt_data, crypt_data);
    if (ret) return -1;
    return crypt_len;
}

int blufi_aes_decrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len)
{
    int ret;
    size_t iv_offset = 0;
    uint8_t iv0[16];
    if (!blufi_sec) return -1;
    memcpy(iv0, blufi_sec->iv, sizeof(blufi_sec->iv));
    iv0[0] = iv8;
    ret = mbedtls_aes_crypt_cfb128(&blufi_sec->aes, MBEDTLS_AES_DECRYPT, crypt_len, &iv_offset, iv0, crypt_data, crypt_data);
    if (ret) return -1;
    return crypt_len;
}

uint16_t blufi_crc_checksum(uint8_t iv8, uint8_t *data, int len)
{
    return esp_crc16_be(0, data, len);
}

int blufi_security_init(void)
{
    blufi_sec = (struct blufi_security *)malloc(sizeof(struct blufi_security));
    if (blufi_sec == NULL) {
        return ESP_FAIL;
    }
    memset(blufi_sec, 0, sizeof(struct blufi_security));
    mbedtls_dhm_init(&blufi_sec->dhm);
    mbedtls_aes_init(&blufi_sec->aes);
    memset(blufi_sec->iv, 0, sizeof(blufi_sec->iv));
    return 0;
}

void blufi_security_deinit(void)
{
    if (blufi_sec == NULL) return;
    if (blufi_sec->dh_param) {
        free(blufi_sec->dh_param);
        blufi_sec->dh_param = NULL;
    }
    mbedtls_dhm_free(&blufi_sec->dhm);
    mbedtls_aes_free(&blufi_sec->aes);
    memset(blufi_sec, 0, sizeof(struct blufi_security));
    free(blufi_sec);
    blufi_sec = NULL;
}
