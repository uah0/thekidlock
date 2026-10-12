#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Version 2 envelope: the owner LTK is selected locally; all fields are sealed. */
bool cloud_crypto_open(const uint8_t ltk[16], const char *target, const char *direction,
                       const char *phone, const char *envelope, char *plain, size_t capacity);
bool cloud_crypto_seal(const uint8_t ltk[16], const char *target, const char *direction,
                       const char *phone, const char *plain, char *envelope, size_t capacity);
bool cloud_crypto_selftest(void);
