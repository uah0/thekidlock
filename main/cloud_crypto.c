#include "cloud_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

static const char digits[]="0123456789abcdef";
static bool from_hex(const char *src, uint8_t *dst, size_t count)
{
    if(!src||strlen(src)!=count*2)return false;
    for(size_t i=0;i<count;i++){
        const char *hi=strchr(digits,src[i*2]),*lo=strchr(digits,src[i*2+1]);
        if(!hi||!lo)return false;
        dst[i]=(uint8_t)(((hi-digits)<<4)|(lo-digits));
    }
    return true;
}
static void to_hex(const uint8_t *src, size_t count, char *dst)
{
    for(size_t i=0;i<count;i++){dst[i*2]=digits[src[i]>>4];dst[i*2+1]=digits[src[i]&15];}
    dst[count*2]=0;
}
static bool valid_phone(const char *phone)
{
    if(!phone||strlen(phone)!=13||(phone[0]!='0'&&phone[0]!='1'))return false;
    for(size_t i=1;i<13;i++)if(!strchr(digits,phone[i]))return false;
    return true;
}
static bool context(const uint8_t ltk[16],const char *target,const char *direction,
                    const char *phone,char aad[96],uint8_t key[16])
{
    if(!valid_phone(phone)||!target||!direction)return false;
    int n=snprintf(aad,96,"BLELOCK2|%s|%s|%s",direction,target,phone);
    if(n<0||n>=96)return false;
    static const unsigned char salt[]="BLELOCK2|mqtt";
    return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),salt,sizeof(salt)-1,
                        ltk,16,(const unsigned char*)aad,n,key,16)==0;
}
static cJSON *parse_envelope(const char *text)
{
    if(!text||strlen(text)>1024||strstr(text,"\\u0000"))return NULL;
    cJSON *j=cJSON_ParseWithOpts(text,NULL,true);
    if(!cJSON_IsObject(j)){cJSON_Delete(j);return NULL;}
    const char *names[]={"v","nonce","data"};unsigned count=0;
    cJSON *item;
    cJSON_ArrayForEach(item,j){
        bool found=false;count++;
        for(unsigned i=0;i<3;i++)if(item->string&&!strcmp(item->string,names[i]))found=true;
        if(!found){cJSON_Delete(j);return NULL;}
    }
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"v");
    cJSON *nonce=cJSON_GetObjectItemCaseSensitive(j,"nonce");
    cJSON *data=cJSON_GetObjectItemCaseSensitive(j,"data");
    if(count!=3||!cJSON_IsNumber(v)||v->valuedouble!=2||
       !cJSON_IsString(nonce)||strlen(nonce->valuestring)!=24||
       !cJSON_IsString(data)||strlen(data->valuestring)<34||strlen(data->valuestring)>768||
       strlen(data->valuestring)%2){cJSON_Delete(j);return NULL;}
    return j;
}
bool cloud_crypto_open(const uint8_t ltk[16],const char *target,const char *direction,
                       const char *phone,const char *envelope,char *plain,size_t capacity)
{
    cJSON *j=parse_envelope(envelope);if(!j||!plain){cJSON_Delete(j);return false;}
    const char *nh=cJSON_GetObjectItemCaseSensitive(j,"nonce")->valuestring;
    const char *dh=cJSON_GetObjectItemCaseSensitive(j,"data")->valuestring;
    size_t len=strlen(dh)/2-16;bool ok=false;
    uint8_t key[16]={0},nonce[12],data[384];char aad[96];
    if(len+1>capacity||len>sizeof(data)-16||
       !from_hex(nh,nonce,12)||!from_hex(dh,data,len+16)||
       !context(ltk,target,direction,phone,aad,key))goto done;
    mbedtls_gcm_context gcm;mbedtls_gcm_init(&gcm);
    if(mbedtls_gcm_setkey(&gcm,MBEDTLS_CIPHER_ID_AES,key,128)==0&&
       mbedtls_gcm_auth_decrypt(&gcm,len,nonce,12,(const unsigned char*)aad,strlen(aad),
                                data+len,16,data,(unsigned char*)plain)==0){plain[len]=0;ok=true;}
    mbedtls_gcm_free(&gcm);
done:
    mbedtls_platform_zeroize(key,sizeof(key));mbedtls_platform_zeroize(data,sizeof(data));
    cJSON_Delete(j);return ok;
}
bool cloud_crypto_seal(const uint8_t ltk[16],const char *target,const char *direction,
                       const char *phone,const char *plain,char *envelope,size_t capacity)
{
    if(!plain||!envelope)return false;
    size_t len=strlen(plain);if(!len||len>352||capacity<2*len+128)return false;
    uint8_t key[16]={0},nonce[12],data[368];char aad[96],nh[25],dh[737];bool ok=false;
    if(!context(ltk,target,direction,phone,aad,key))goto done;
    esp_fill_random(nonce,sizeof(nonce));
    mbedtls_gcm_context gcm;mbedtls_gcm_init(&gcm);
    if(mbedtls_gcm_setkey(&gcm,MBEDTLS_CIPHER_ID_AES,key,128)==0&&
       mbedtls_gcm_crypt_and_tag(&gcm,MBEDTLS_GCM_ENCRYPT,len,nonce,12,
                                 (const unsigned char*)aad,strlen(aad),
                                 (const unsigned char*)plain,data,16,data+len)==0){
        to_hex(nonce,12,nh);to_hex(data,len+16,dh);
        int n=snprintf(envelope,capacity,"{\"v\":2,\"nonce\":\"%s\",\"data\":\"%s\"}",nh,dh);
        ok=n>0&&(size_t)n<capacity;
    }
    mbedtls_gcm_free(&gcm);
done:
    mbedtls_platform_zeroize(key,sizeof(key));mbedtls_platform_zeroize(data,sizeof(data));
    return ok;
}
bool cloud_crypto_selftest(void)
{
    /* RFC 5869 test case 1, first 16 bytes of OKM. */
    uint8_t ikm[22],salt[13],info[10],okm[16];memset(ikm,0x0b,sizeof(ikm));
    for(unsigned i=0;i<13;i++)salt[i]=i;
    for(unsigned i=0;i<10;i++)info[i]=0xf0+i;
    static const uint8_t expected[16]={0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a};
    if(mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),salt,13,ikm,22,info,10,okm,16)!=0||
       memcmp(okm,expected,16))return false;
    uint8_t ltk[16],key[16];memset(ltk,0x0b,sizeof(ltk));char aad[96];
    if(!context(ltk,"a0:f2:62:f1:14:b8","command","04c8e193a7065",aad,key))return false;
    static const uint8_t expected_key[16]={0x62,0x07,0x0d,0x12,0xe3,0xca,0x6a,0xf3,0x9a,0x17,0x12,0xae,0xb7,0x96,0x63,0xde};
    bool valid=memcmp(key,expected_key,16)==0;
    mbedtls_platform_zeroize(key,sizeof(key));
    char envelope[256],plain[32];
    valid=valid&&cloud_crypto_seal(ltk,"a0:f2:62:f1:14:b8","command","04c8e193a7065","{\"test\":1}",envelope,sizeof(envelope))&&
        cloud_crypto_open(ltk,"a0:f2:62:f1:14:b8","command","04c8e193a7065",envelope,plain,sizeof(plain))&&
        strcmp(plain,"{\"test\":1}")==0;
    return valid;
}
