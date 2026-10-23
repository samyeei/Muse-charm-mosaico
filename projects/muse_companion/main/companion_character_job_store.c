// SPDX-License-Identifier: Apache-2.0
#include "companion_character_job.h"
#include "nvs.h"
#include <string.h>
esp_err_t companion_character_job_load(void *context,uint8_t *wire,size_t bytes)
{
    (void)context;nvs_handle_t nvs;esp_err_t err=nvs_open("character_job",NVS_READONLY,&nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_ERR_NOT_FOUND;
    if(err!=ESP_OK)return err;
    memset(wire,0,bytes);
    size_t actual=bytes;err=nvs_get_blob(nvs,"record",wire,&actual);nvs_close(nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_ERR_NOT_FOUND;
    /* Accept only the previous exact 96-byte record for explicit migration;
     * the controller validates its magic, CRC and zero padding. */
    return err==ESP_OK&&actual!=bytes&&actual!=96?ESP_ERR_INVALID_SIZE:err;
}
esp_err_t companion_character_job_save(void *context,const uint8_t *wire,size_t bytes)
{
    (void)context;nvs_handle_t nvs;esp_err_t err=nvs_open("character_job",NVS_READWRITE,&nvs);
    if(err!=ESP_OK)return err;
    err=nvs_set_blob(nvs,"record",wire,bytes);if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);return err;
}
