// SPDX-License-Identifier: Apache-2.0
#include "companion_bridge.h"
#include "esp_http_server.h"
#include "esp_iris.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { PORT=4397, SLOTS=16, TEXT_CAP=2049, REPLY_CAP=4096, BODY_CAP=8192 };
typedef enum { EMPTY, QUEUED, RUNNING, COMPLETED, FAILED, CANCELLED } state_t;
typedef struct {
    char id[65],text[TEXT_CAP],reply[REPLY_CAP],error[96];
    state_t state;
    int64_t changed;
} job_t;
struct companion_bridge {
    SemaphoreHandle_t lock;
    httpd_handle_t server;
    job_t jobs[SLOTS];
    char key[65],body[BODY_CAP];
    int current;
    uint32_t boot;
    bool ready,idle;
};
static int64_t now(void){return esp_timer_get_time()/1000;}
static const char *state_name(state_t s)
{static const char *names[]={"empty","queued","running","completed","failed","cancelled"};return names[s];}
static void trim_utf8_tail(char *s)
{
    size_t n=strlen(s);if(!n)return;
    size_t start=n-1;
    while(start&&((unsigned char)s[start]&0xc0)==0x80)--start;
    unsigned char lead=(unsigned char)s[start];
    size_t width=lead>=0xf0?4:lead>=0xe0?3:lead>=0xc0?2:1;
    if(n-start<width)s[start]=0;
}
static void lock(companion_bridge_handle_t b){xSemaphoreTake(b->lock,portMAX_DELAY);}
static void unlock(companion_bridge_handle_t b){xSemaphoreGive(b->lock);}
static void expire(companion_bridge_handle_t b)
{
    for(int i=0;i<SLOTS;i++){
        job_t *j=&b->jobs[i];
        if(j->state==QUEUED&&now()-j->changed>30000){j->state=FAILED;strcpy(j->error,"queue_timeout");j->changed=now();}
        if(i!=b->current&&j->state>=COMPLETED&&now()-j->changed>300000)memset(j,0,sizeof(*j));
    }
}
static cJSON *health(companion_bridge_handle_t b)
{
    cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"service","muse-companion-bridge");
    cJSON_AddNumberToObject(o,"protocol",1);cJSON_AddNumberToObject(o,"port",PORT);
    cJSON_AddNumberToObject(o,"boot",b->boot);
    cJSON_AddBoolToObject(o,"ready",b->ready);cJSON_AddBoolToObject(o,"idle",b->idle);
    esp_netif_t *n=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;char address[20]={0};
    if(n&&esp_netif_get_ip_info(n,&ip)==ESP_OK)snprintf(address,sizeof(address),IPSTR,IP2STR(&ip.ip));
    cJSON_AddStringToObject(o,"ip",address);return o;
}
static cJSON *job_json(const job_t *j)
{
    cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"request_id",j->id);
    cJSON_AddStringToObject(o,"status",state_name(j->state));
    if(j->state==COMPLETED)cJSON_AddStringToObject(o,"reply",j->reply);
    if(j->error[0])cJSON_AddStringToObject(o,"error",j->error);
    return o;
}
static esp_err_t send_json(httpd_req_t *q,const char *status,cJSON *o)
{
    char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);
    if(!s)return ESP_ERR_NO_MEM;
    httpd_resp_set_status(q,status);httpd_resp_set_type(q,"application/json; charset=utf-8");
    httpd_resp_set_hdr(q,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_send(q,s,HTTPD_RESP_USE_STRLEN);free(s);return e;
}
static esp_err_t error(httpd_req_t *q,const char *status,const char *message)
{cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"error",message);return send_json(q,status,o);}
static bool valid_id(const char *id)
{
    if(!id||!*id||strlen(id)>64)return false;
    for(const char *p=id;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')return false;
    return true;
}
static esp_err_t http(httpd_req_t *q)
{
    companion_bridge_handle_t b=q->user_ctx;
    char auth[80];unsigned difference=0;
    if(httpd_req_get_hdr_value_len(q,"Authorization")!=71||
       httpd_req_get_hdr_value_str(q,"Authorization",auth,sizeof(auth))!=ESP_OK)
        return error(q,"401 Unauthorized","unauthorized");
    for(int i=0;i<7;i++)difference|=(unsigned char)auth[i]^(unsigned char)"Bearer "[i];
    for(int i=0;i<64;i++)difference|=(unsigned char)auth[7+i]^(unsigned char)b->key[i];
    if(difference)return error(q,"401 Unauthorized","unauthorized");
    if(!strcmp(q->uri,"/health")&&q->method==HTTP_GET){
        lock(b);cJSON *o=health(b);unlock(b);return send_json(q,"200 OK",o);
    }
    if(!strcmp(q->uri,"/requests")&&q->method==HTTP_POST){
        if(!q->content_len||q->content_len>=BODY_CAP)return error(q,"413 Payload Too Large","body_limit");
        size_t used=0;
        while(used<q->content_len){int n=httpd_req_recv(q,b->body+used,q->content_len-used);if(n<=0)return ESP_FAIL;used+=(size_t)n;}
        b->body[used]=0;
        if(strstr(b->body,"\\u0000")||memchr(b->body,0,used))return error(q,"400 Bad Request","embedded_nul");
        const char *end=NULL;
        cJSON *o=cJSON_ParseWithLengthOpts(b->body,used+1,&end,true);
        const char *id=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,"request_id"));
        const char *text=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,"text"));
        if(!cJSON_IsObject(o)||!valid_id(id)||!text||!*text||strlen(text)>=TEXT_CAP){cJSON_Delete(o);return error(q,"400 Bad Request","invalid_request");}
        lock(b);expire(b);job_t *found=NULL,*slot=NULL;bool busy=false;
        for(int i=0;i<SLOTS;i++){
            job_t *j=&b->jobs[i];if(j->state!=EMPTY&&!strcmp(id,j->id))found=j;
            if(j->state==EMPTY)slot=j;
            if(j->state==QUEUED||j->state==RUNNING)busy=true;
        }
        if(found){
            bool same=!strcmp(found->text,text);cJSON *result=same?job_json(found):NULL;
            unlock(b);cJSON_Delete(o);
            return same?send_json(q,"200 OK",result):error(q,"409 Conflict","request_id_conflict");
        }
        if(busy||!slot||!b->ready||!b->idle){unlock(b);cJSON_Delete(o);return error(q,"503 Service Unavailable","busy_or_not_ready");}
        strcpy(slot->id,id);strcpy(slot->text,text);slot->state=QUEUED;slot->changed=now();
        cJSON *result=job_json(slot);unlock(b);cJSON_Delete(o);return send_json(q,"202 Accepted",result);
    }
    const char *prefix="/requests/";
    if(strncmp(q->uri,prefix,strlen(prefix)))return error(q,"404 Not Found","not_found");
    const char *id=q->uri+strlen(prefix);const char *suffix=strchr(id,'/');
    size_t length=suffix?(size_t)(suffix-id):strlen(id);char requested[65];
    if(!length||length>64)return error(q,"400 Bad Request","invalid_id");
    memcpy(requested,id,length);requested[length]=0;
    bool cancel=suffix&&!strcmp(suffix,"/cancel")&&q->method==HTTP_POST;
    if(!valid_id(requested)||(!cancel&&(suffix||q->method!=HTTP_GET)))return error(q,"404 Not Found","not_found");
    if(cancel&&q->content_len)return error(q,"400 Bad Request","empty_body_required");
    lock(b);expire(b);job_t *found=NULL;
    for(int i=0;i<SLOTS;i++)if(b->jobs[i].state!=EMPTY&&!strcmp(b->jobs[i].id,requested))found=&b->jobs[i];
    if(!found){unlock(b);return error(q,"404 Not Found","not_found");}
    if(cancel&&(found->state==QUEUED||found->state==RUNNING)){found->state=CANCELLED;found->changed=now();}
    cJSON *result=job_json(found);unlock(b);return send_json(q,"200 OK",result);
}
static esp_err_t rpc(const esp_iris_rpc_request_t *q,uint8_t *out,size_t cap,size_t *size,void *ctx)
{
    companion_bridge_handle_t b=ctx;if(q->payload_size)return ESP_ERR_INVALID_SIZE;
    lock(b);cJSON *o=health(b);
    /* Only the local maintenance RPC exports the bridge key; never HTTP/logs. */
    if(q->method_id==2)cJSON_AddStringToObject(o,"key",b->key);
    char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);unlock(b);
    if(!s)return ESP_ERR_NO_MEM;
    size_t n=strlen(s);if(n>cap){free(s);return ESP_ERR_INVALID_SIZE;}
    memcpy(out,s,n);*size=n;free(s);return ESP_OK;
}
esp_err_t companion_bridge_create(companion_bridge_handle_t *out)
{
    *out=NULL;
    /* Request history is large and app-lifetime; reserve internal RAM for
     * radio/DMA and the existing SDK's TLS bursts. */
    companion_bridge_handle_t b=heap_caps_calloc(1,sizeof(*b),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!b)return ESP_ERR_NO_MEM;
    b->current=-1;b->boot=esp_random();b->lock=xSemaphoreCreateMutex();
    if(!b->lock){free(b);return ESP_ERR_NO_MEM;}
    nvs_handle_t n;esp_err_t e=nvs_open("xiaoai_bridge",NVS_READWRITE,&n);
    if(e==ESP_OK){
        size_t length=sizeof(b->key);e=nvs_get_str(n,"key",b->key,&length);
        uint8_t version=0;
        (void)nvs_get_u8(n,"key_version",&version);
        if(e==ESP_ERR_NVS_NOT_FOUND||(e==ESP_OK&&version!=1)){
            for(int i=0;i<8;i++)snprintf(b->key+i*8,9,"%08lx",(unsigned long)esp_random());
            e=nvs_set_str(n,"key",b->key);
            if(e==ESP_OK)e=nvs_set_u8(n,"key_version",1);
            if(e==ESP_OK)e=nvs_commit(n);
        }
        nvs_close(n);
    }
    if(e!=ESP_OK||strlen(b->key)!=64){vSemaphoreDelete(b->lock);free(b);return e==ESP_OK?ESP_FAIL:e;}
    *out=b;return ESP_OK;
}
esp_err_t companion_bridge_start(companion_bridge_handle_t b)
{
    if(!b)return ESP_ERR_INVALID_ARG;
    httpd_config_t *c=malloc(sizeof(*c));if(!c)return ESP_ERR_NO_MEM;
    *c=(httpd_config_t)HTTPD_DEFAULT_CONFIG();c->server_port=PORT;c->ctrl_port=32770;c->stack_size=6144;
    /* HTTP never writes flash or invokes Muse; its task can use a PSRAM stack. */
    c->task_caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    c->max_open_sockets=2;c->max_uri_handlers=2;c->lru_purge_enable=true;
    c->uri_match_fn=httpd_uri_match_wildcard;c->recv_wait_timeout=3;c->send_wait_timeout=3;
    esp_err_t e=httpd_start(&b->server,c);free(c);if(e!=ESP_OK)return e;
    const httpd_uri_t get={.uri="/*",.method=HTTP_GET,.handler=http,.user_ctx=b};
    const httpd_uri_t post={.uri="/*",.method=HTTP_POST,.handler=http,.user_ctx=b};
    e=httpd_register_uri_handler(b->server,&get);if(e==ESP_OK)e=httpd_register_uri_handler(b->server,&post);
    if(e==ESP_OK)e=esp_iris_rpc_register(0x7e15,1,rpc,b);
    if(e==ESP_OK)e=esp_iris_rpc_register(0x7e15,2,rpc,b);
    if(e!=ESP_OK){httpd_stop(b->server);b->server=NULL;esp_iris_rpc_unregister(0x7e15,1);}
    return e;
}
const char *companion_bridge_take(companion_bridge_handle_t b,bool ready,bool idle)
{
    if(!b)return NULL;
    lock(b);b->ready=ready;b->idle=idle;expire(b);const char *text=NULL;
    if(b->current<0&&ready&&idle)for(int i=0;i<SLOTS;i++)if(b->jobs[i].state==QUEUED){
        b->current=i;b->jobs[i].state=RUNNING;b->jobs[i].changed=now();b->idle=false;text=b->jobs[i].text;break;
    }
    unlock(b);return text;
}
bool companion_bridge_cancelled(companion_bridge_handle_t b)
{
    if(!b)return false;
    lock(b);bool cancelled=false;
    if(b->current>=0){job_t *j=&b->jobs[b->current];cancelled=j->state==CANCELLED||now()-j->changed>90000;}
    unlock(b);return cancelled;
}
void companion_bridge_finish(companion_bridge_handle_t b,const char *reply,const char *failure)
{
    if(!b)return;
    lock(b);
    if(b->current>=0){job_t *j=&b->jobs[b->current];
        if(j->state==RUNNING){
            j->state=reply&&*reply&&!failure?COMPLETED:FAILED;
            snprintf(j->reply,sizeof(j->reply),"%s",reply?reply:"");
            /* SDK caption truncation can split the final UTF-8 character. */
            trim_utf8_tail(j->reply);
            snprintf(j->error,sizeof(j->error),"%s",failure?failure:j->state==FAILED?"empty_reply":"");
            j->changed=now();
        }
        b->current=-1;
    }
    unlock(b);
}
void companion_bridge_interrupt(companion_bridge_handle_t b)
{
    if(!b)return;
    lock(b);
    for(int i=0;i<SLOTS;i++)if(b->jobs[i].state==QUEUED||b->jobs[i].state==RUNNING){
        b->jobs[i].state=CANCELLED;strcpy(b->jobs[i].error,"local_interruption");b->jobs[i].changed=now();
    }
    b->idle=false;unlock(b);
}
