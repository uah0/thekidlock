#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { char phone[14]; uint64_t id; int64_t time; char mac[65]; } cloud_open_t;
bool cloud_auth_parse(const char *text, cloud_open_t *out);
bool cloud_auth_time(int64_t issued,int64_t now,bool synced);
bool cloud_auth_sign(const uint8_t key[16],const char *text,char out[65]);
bool cloud_auth_verify(const uint8_t key[16],const char *text,const char *signature);
bool cloud_auth_selftest(void);
