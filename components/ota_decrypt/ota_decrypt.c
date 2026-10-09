#include "ota_decrypt.h"
#include <stdlib.h>

extern const char key_start[] asm("_binary_ota_decryption_key_pem_start");
extern const char key_end[] asm("_binary_ota_decryption_key_pem_end");

esp_decrypt_handle_t ota_decrypt_start(void)
{
    esp_decrypt_cfg_t config = {
        .rsa_priv_key = key_start,
        .rsa_priv_key_len = key_end - key_start,
    };
    return esp_encrypted_img_decrypt_start(&config);
}

esp_err_t ota_decrypt_https(decrypt_cb_arg_t *args, void *ctx)
{
    pre_enc_decrypt_arg_t data = {
        .data_in = args->data_in,
        .data_in_len = args->data_in_len,
    };
    esp_err_t err = esp_encrypted_img_decrypt_data(ctx, &data);
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        free(data.data_out);
        return err;
    }
    args->data_out = data.data_out;
    args->data_out_len = data.data_out_len;
    return ESP_OK;
}
