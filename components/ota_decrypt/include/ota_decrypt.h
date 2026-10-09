#pragma once
#include "esp_encrypted_img.h"
#include "esp_https_ota.h"

esp_decrypt_handle_t ota_decrypt_start(void);
esp_err_t ota_decrypt_https(decrypt_cb_arg_t *args, void *ctx);
