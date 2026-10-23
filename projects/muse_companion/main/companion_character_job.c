// SPDX-License-Identifier: Apache-2.0
#include "companion_character_job.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { JOB_IDLE, JOB_BUILDING, JOB_READY_HELD, JOB_FAILED, JOB_TIMEOUT,
    JOB_DOWNLOADING, JOB_DOWNLOADED, JOB_RESTORING, JOB_RESTORE_HANDOFF };
enum { QUERY_MS = 60000, WAIT_MS = 900000, VALID_UTC = 1577836800 };
struct companion_character_job_t {
    companion_character_job_config_t storage;
    character_command_t command;
    character_command_t ready_command;
    uint8_t wire[COMPANION_CHARACTER_JOB_RECORD_BYTES];
    char id[64];
    char frozen_hash[65];
    unsigned state;
    uint64_t deadline_utc, deadline_ms, query_ms;
    bool querying;
    uint64_t download_token;
};
static uint32_t crc(const uint8_t *p, size_t n)
{
    uint32_t c = UINT32_MAX;
    for (size_t i=0;i<n;++i) {
        c ^= p[i];
        for (unsigned b=0;b<8;++b) c=(c>>1)^(0xedb88320U & (0U-(c&1U)));
    }
    return ~c;
}
static void put(uint8_t *p,uint64_t v,unsigned bytes)
{ for(unsigned i=0;i<bytes;++i)p[i]=(uint8_t)(v>>(8*i)); }
static uint64_t get(const uint8_t *p,unsigned bytes)
{ uint64_t v=0;for(unsigned i=0;i<bytes;++i)v|=(uint64_t)p[i]<<(8*i);return v; }
static uint64_t expires_utc(const char *s)
{
    /* The protocol parser already validated the complete UTC calendar date. */
    unsigned y=(s[0]-'0')*1000U+(s[1]-'0')*100U+(s[2]-'0')*10U+s[3]-'0';
    unsigned month=(s[5]-'0')*10U+s[6]-'0',day=(s[8]-'0')*10U+s[9]-'0';
    uint64_t days=0;
    for(unsigned year=1970;year<y;++year)days+=365U+(unsigned)(year%4==0&&(year%100!=0||year%400==0));
    static const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(unsigned m=1;m<month;++m)days+=lengths[m-1]+(unsigned)(m==2&&y%4==0&&(y%100!=0||y%400==0));
    return (days+day-1)*86400U+((s[11]-'0')*10U+s[12]-'0')*3600U+
        ((s[14]-'0')*10U+s[15]-'0')*60U+(s[17]-'0')*10U+s[18]-'0';
}
static esp_err_t save(companion_character_job_handle_t h)
{
    memset(h->wire,0,sizeof(h->wire));memcpy(h->wire,"MCJOB002",8);
    put(h->wire+8,h->state,4);put(h->wire+16,h->deadline_utc,8);
    memcpy(h->wire+24,h->id,sizeof(h->id));
    memcpy(h->wire+92,h->frozen_hash,64);put(h->wire+156,crc(h->wire,156),4);
    esp_err_t err=h->storage.save(h->storage.context,h->wire,sizeof(h->wire));
    /* A persistence fault never leaves a live poller running. A reboot may
     * load the previous job, whose original deadline still bounds queries. */
    if(err!=ESP_OK){h->state=JOB_FAILED;h->querying=false;}
    return err;
}
static bool decode(companion_character_job_handle_t h)
{
    bool legacy=!memcmp(h->wire,"MCJOB001",8);
    if(legacy){
        if(get(h->wire+88,4)!=crc(h->wire,88))return false;
        for(unsigned i=92;i<160;++i)if(h->wire[i])return false;
    }else if(memcmp(h->wire,"MCJOB002",8)||get(h->wire+156,4)!=crc(h->wire,156))return false;
    for(unsigned i=12;i<16;++i)if(h->wire[i])return false;
    if(!legacy){
        for(unsigned i=88;i<92;++i)if(h->wire[i])return false;
        memcpy(h->frozen_hash,h->wire+92,64);
        bool any=false,empty=false;
        for(unsigned i=0;i<64;++i){
            char c=h->frozen_hash[i];
            if(!c){empty=true;continue;}
            if(empty||!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
            any=true;
        }
        if(any&&empty)return false;
    }
    h->state=(unsigned)get(h->wire+8,4);h->deadline_utc=get(h->wire+16,8);
    memcpy(h->id,h->wire+24,sizeof(h->id));
    if(h->state<JOB_BUILDING||h->state>(legacy?JOB_TIMEOUT:JOB_RESTORE_HANDOFF)||!h->id[0]||h->id[63])return false;
    if((h->state==JOB_DOWNLOADING||h->state==JOB_DOWNLOADED)&&!h->frozen_hash[0])return false;
    bool ended=false;
    for(unsigned i=0;i<sizeof(h->id);++i){
        unsigned char c=h->id[i];if(!c){ended=true;continue;}if(ended)return false;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    }
    return true;
}
esp_err_t companion_character_job_create(const companion_character_job_config_t *config,
    uint64_t now,uint64_t utc,companion_character_job_handle_t *out)
{
    if(!config||!config->load||!config->save||!out||
        (!!config->download_start != !!config->download_poll)||
        (!!config->restore_start != !!config->restore_poll))return ESP_ERR_INVALID_ARG;
    *out=NULL;companion_character_job_handle_t h=calloc(1,sizeof(*h));if(!h)return ESP_ERR_NO_MEM;
    h->storage=*config;esp_err_t err=config->load(config->context,h->wire,sizeof(h->wire));
    if(err==ESP_ERR_NOT_FOUND){*out=h;return ESP_OK;}
    if(err!=ESP_OK||!decode(h)){free(h);return err==ESP_OK?ESP_ERR_INVALID_STATE:err;}
    if(h->state==JOB_RESTORING||h->state==JOB_RESTORE_HANDOFF){
        /* Local restore interrupted before durable handoff is safely canceled.
         * A committed transaction is owned by Recovery/app boot admission, not
         * reissued here. Retire either state to a non-polling terminal record,
         * preserving its ID without claiming whether display acceptance passed.
         * No Anna query or new restore is sent after reboot. */
        h->state=JOB_FAILED;err=save(h);
    }
    if(h->state==JOB_BUILDING||h->state==JOB_DOWNLOADING){
        /* Interrupted downloads re-query the same read-only job. Its frozen
         * package hash and original deadline survive; partial files are not
         * treated as validated and no installation record is produced. */
        h->state=JOB_BUILDING;
        /* Unknown time on reboot safely cancels instead of extending 15 min. */
        if(utc<VALID_UTC||h->deadline_utc<=utc||h->deadline_utc-utc>WAIT_MS/1000){
            h->state=JOB_TIMEOUT;err=save(h);
        }else{h->deadline_ms=now+(h->deadline_utc-utc)*1000;h->query_ms=now+QUERY_MS;}
    }
    if(err!=ESP_OK){free(h);return err;}*out=h;return ESP_OK;
}
void companion_character_job_delete(companion_character_job_handle_t h){free(h);}
character_command_kind_t companion_character_job_reply(companion_character_job_handle_t h,
    char *text,uint64_t now,uint64_t utc,companion_character_job_event_t *event)
{
    if(!h||!event)return CHARACTER_INVALID;
    *event=CHARACTER_JOB_NONE;
    character_command_kind_t kind=companion_character_extract(text,&h->command);
    if(kind==CHARACTER_NONE)return kind;
    if(kind==CHARACTER_INVALID){*event=CHARACTER_JOB_REJECTED;return kind;}
    if(kind==CHARACTER_RESTORE){
        if(!strcmp(h->id,h->command.job))return kind;
        if(!h->storage.restore_start||h->state==JOB_BUILDING||h->state==JOB_DOWNLOADING||
            h->state==JOB_READY_HELD||h->state==JOB_RESTORING||h->state==JOB_RESTORE_HANDOFF){
            *event=CHARACTER_JOB_REJECTED;return kind;
        }
        memset(h->id,0,sizeof(h->id));strcpy(h->id,h->command.job);
        memset(h->frozen_hash,0,sizeof(h->frozen_hash));h->download_token=0;
        h->state=JOB_RESTORING;h->querying=false;h->deadline_ms=now+WAIT_MS;
        h->deadline_utc=utc>=VALID_UTC?utc+WAIT_MS/1000:0;
        *event=save(h)==ESP_OK?CHARACTER_JOB_STARTED:CHARACTER_JOB_RESTORE_FAILED;
        return kind;
    }
    if(kind==CHARACTER_PACK){
        if(!strcmp(h->id,h->command.job))return kind; // includes terminal jobs
        if(h->state==JOB_BUILDING||h->state==JOB_DOWNLOADING||h->state==JOB_RESTORING||
            h->state==JOB_RESTORE_HANDOFF){*event=CHARACTER_JOB_REJECTED;return kind;}
        memset(h->id,0,sizeof(h->id));memcpy(h->id,h->command.job,strlen(h->command.job));
        h->state=JOB_BUILDING;h->querying=false;h->deadline_ms=now+WAIT_MS;h->query_ms=now+QUERY_MS;
        memset(h->frozen_hash,0,sizeof(h->frozen_hash));h->download_token=0;
        h->deadline_utc=utc>=VALID_UTC?utc+WAIT_MS/1000:0;
        *event=save(h)==ESP_OK?CHARACTER_JOB_STARTED:CHARACTER_JOB_FAILED;
    }else if(h->state==JOB_BUILDING&&!strcmp(h->id,h->command.job)){
        if(now>=h->deadline_ms){h->state=JOB_TIMEOUT;save(h);*event=CHARACTER_JOB_TIMEOUT;}
        else if(h->command.status!=CHARACTER_BUILDING){
            bool ready=h->command.status==CHARACTER_READY&&utc>=VALID_UTC&&expires_utc(h->command.expires_at)>utc;
            if(ready&&h->frozen_hash[0]&&strcmp(h->frozen_hash,h->command.sha256))ready=false;
            if(ready&&h->storage.download_start){
                h->ready_command=h->command;
                h->ready_command.deadline_ms=h->deadline_ms;
                memcpy(h->frozen_hash,h->command.sha256,sizeof(h->frozen_hash));
                h->state=JOB_DOWNLOADING;h->download_token=0;
            }else h->state=ready?JOB_READY_HELD:JOB_FAILED;
            h->querying=false;
            if(save(h)!=ESP_OK)*event=CHARACTER_JOB_FAILED;
            else if(h->state==JOB_READY_HELD)*event=CHARACTER_JOB_READY_HELD;
            else if(h->state==JOB_FAILED)*event=CHARACTER_JOB_FAILED;
        }
    }
    return kind;
}
bool companion_character_job_take_query(companion_character_job_handle_t h,uint64_t now,
    bool busy,bool connected,char *query,size_t capacity)
{
    if(!h||!query||!capacity||busy||!connected||h->querying||h->state!=JOB_BUILDING||
        now<h->query_ms||now>=h->deadline_ms)return false;
    int n=snprintf(query,capacity,"[CHARACTER_QUERY job=\"%s\"]",h->id);
    if(n<0||(size_t)n>=capacity)return false;
    h->querying=true;h->query_ms=now+QUERY_MS;return true;
}
void companion_character_job_query_finished(companion_character_job_handle_t h)
{if(h)h->querying=false;}
companion_character_job_event_t companion_character_job_tick(companion_character_job_handle_t h,uint64_t now)
{
    if(!h||(h->state!=JOB_BUILDING&&h->state!=JOB_DOWNLOADING&&h->state!=JOB_RESTORING)||now<h->deadline_ms)return CHARACTER_JOB_NONE;
    h->state=JOB_TIMEOUT;h->querying=false;
    return save(h)==ESP_OK?CHARACTER_JOB_TIMEOUT:CHARACTER_JOB_FAILED;
}

companion_character_job_event_t companion_character_job_download_tick(companion_character_job_handle_t h,
    uint64_t now,bool busy)
{
    companion_character_job_event_t event=companion_character_job_tick(h,now);
    if(event!=CHARACTER_JOB_NONE||!h||(h->state!=JOB_DOWNLOADING&&h->state!=JOB_RESTORING))return event;
    bool restoring=h->state==JOB_RESTORING;
    esp_err_t err;
    if(!h->download_token){
        if(busy)return CHARACTER_JOB_NONE;
        uint64_t token=0;
        err=restoring?h->storage.restore_start(h->storage.context,h->id,&token):
            h->storage.download_start(h->storage.context,&h->ready_command,&token);
        if(err==ESP_OK&&token){h->download_token=token;return CHARACTER_JOB_NONE;}
        if(err==ESP_ERR_INVALID_STATE)return CHARACTER_JOB_NONE;
    }else{
        bool finished=false;esp_err_t result=ESP_FAIL;
        err=restoring?h->storage.restore_poll(h->storage.context,h->download_token,&finished,&result):
            h->storage.download_poll(h->storage.context,h->download_token,&finished,&result);
        if(err==ESP_ERR_INVALID_STATE)return CHARACTER_JOB_NONE;
        if(err==ESP_OK&&!finished)return CHARACTER_JOB_NONE;
        if(err==ESP_OK&&result==ESP_OK){
            h->state=restoring?JOB_RESTORE_HANDOFF:JOB_DOWNLOADED;
            return save(h)==ESP_OK?(restoring?CHARACTER_JOB_RESTORE_HANDOFF:CHARACTER_JOB_DOWNLOADED):
                (restoring?CHARACTER_JOB_RESTORE_FAILED:CHARACTER_JOB_FAILED);
        }
        if(restoring&&err==ESP_OK&&result==ESP_ERR_NOT_SUPPORTED){
            h->state=JOB_FAILED;
            return save(h)==ESP_OK?CHARACTER_JOB_RESTORE_DISABLED:CHARACTER_JOB_RESTORE_FAILED;
        }
    }
    h->state=JOB_FAILED;
    save(h);
    return restoring?CHARACTER_JOB_RESTORE_FAILED:CHARACTER_JOB_FAILED;
}

companion_character_progress_t companion_character_job_progress(companion_character_job_handle_t h)
{
    if(!h)return COMP_CHARACTER_NONE;
    switch(h->state){
    case JOB_BUILDING:case JOB_READY_HELD:return COMP_CHARACTER_BUILDING;
    case JOB_DOWNLOADING:return COMP_CHARACTER_DOWNLOADING;
    case JOB_RESTORING:case JOB_RESTORE_HANDOFF:return COMP_CHARACTER_APPLYING;
    default:return COMP_CHARACTER_NONE;
    }
}
