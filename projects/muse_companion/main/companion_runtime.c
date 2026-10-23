// SPDX-License-Identifier: Apache-2.0
#include "companion_runtime.h"
#include "companion_bridge.h"
#include "companion_platform.h"
#include "companion_setup.h"
#include "companion_tts.h"
#include "companion_generation.h"
#include "companion_character_job.h"
#include "companion_character_storage.h"
#include "bsp/esp_mosaico.h"
#include "app.h"
#include "mosaico_hooks.h"
#include "muse_chat.h"
#include "muse_link.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "nvs.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
enum { AUDIO_RATE=16000, AUDIO_FRAMES=256, AUDIO_QUEUE_DEPTH=48,
    CONTROL_QUEUE_DEPTH=12, MESSAGE_QUEUE_DEPTH=4, WORKER_STACK=8192 };
bool companion_platform_setup(const char **ssid,const char **password)
{return companion_setup_active(ssid,password);}
bool companion_platform_setup_request(void)
{return companion_setup_request()==ESP_OK;}
bool companion_platform_setup_request_failed(void)
{return companion_setup_request_failed();}
typedef struct {unsigned effects;uint32_t generation;unsigned volume,brightness,language,sleep_seconds,speech_rate,page_seconds;bool muted;} command_t;
typedef struct {uint32_t generation;int16_t pcm[AUDIO_FRAMES];} audio_t;
/* App-lifetime service. UI owns no hardware; SDK owns radios; capture task owns
 * microphone reads; voice worker alone calls muse_hatch_turn_*; TTS worker owns
 * speaker writes. Queue messages and atomics are the only shared state. */
struct runtime_t {
    QueueHandle_t control,messages,audio;
    companion_tts_handle_t tts;
    companion_bridge_handle_t bridge;
    bool remote,bridge_attempted;
    companion_generation_handle_t generator;
    companion_character_job_handle_t character_job;
    companion_character_job_event_t character_notice_pending;
    character_command_t discarded_control;
    esp_codec_dev_handle_t mic,spk;
    button_handle_t buttons[BSP_BUTTON_NUM];
    atomic_uint generation;
    atomic_bool recording,key,overrun,muted,wants_record;
    command_t command;
    companion_message_t message;
    audio_t captured;
    char event_text[128],reply[COMP_REPLY_CAP],incoming[COMP_REPLY_CAP],character_query[96];
    uint32_t recorded_frames;
    unsigned character_progress;
    unsigned volume,brightness,last_brightness,language,sleep_seconds,speech_rate,page_seconds;
    int64_t phase_started_us;
    uint32_t ui_ms;
    bool active,waiting,sntp,imu_ready,battery_ready,accel_valid,generating,querying;
    float x,y,z;
    int battery;
};
static struct runtime_t *runtime;
static void publish(struct runtime_t *r,companion_message_kind_t kind,
    uint32_t gen,const char *text,bool speaking)
{
    companion_message_t *msg=calloc(1,sizeof(*msg));
    if(!msg){ESP_LOGE("companion","no message memory");return;}
    msg->kind=kind;msg->generation=gen;msg->speaking=speaking;
    snprintf(msg->text,sizeof(msg->text),"%s",text?text:"");
    if(xQueueSend(r->messages,msg,pdMS_TO_TICKS(100))!=pdTRUE)
        ESP_LOGE("companion","UI message queue full");
    free(msg);
}
static void tts_done(void *ctx,uint32_t gen,bool ok)
{publish(ctx,ok?COMP_MSG_DONE:COMP_MSG_ERROR,gen,
    ok?"":"朗读失败，文字已保留，可重试",false);}
static void capture(void *ctx)
{
    struct runtime_t *r=ctx;audio_t *a=calloc(1,sizeof(*a));
    if(!a){atomic_store(&r->overrun,true);vTaskDeleteWithCaps(NULL);return;}
    for(;;){
        bool rec=atomic_load(&r->recording);uint32_t gen=atomic_load(&r->generation);
        int ret=esp_codec_dev_read(r->mic,a->pcm,sizeof(a->pcm));
        if(ret!=ESP_CODEC_DEV_OK){atomic_store(&r->overrun,true);vTaskDelay(pdMS_TO_TICKS(30));continue;}
        if(rec&&atomic_load(&r->recording)&&gen==atomic_load(&r->generation)){
            a->generation=gen;
            if(xQueueSend(r->audio,a,0)!=pdTRUE)atomic_store(&r->overrun,true);
        }
    }
}
static void persist(struct runtime_t *r,bool muted)
{
    nvs_handle_t h;
    esp_err_t err=nvs_open("companion",NVS_READWRITE,&h);
    if(err!=ESP_OK)return;
    err=nvs_set_u8(h,"volume",r->volume);
    if(err==ESP_OK)err=nvs_set_u8(h,"brightness",r->brightness);
    if(err==ESP_OK)err=nvs_set_u8(h,"muted",muted);
    if(err==ESP_OK)err=nvs_set_u8(h,"language",r->language);
    if(err==ESP_OK)err=nvs_set_u8(h,"sleep_sec",r->sleep_seconds);
    if(err==ESP_OK)err=nvs_set_u8(h,"speech_rate",r->speech_rate);
    if(err==ESP_OK)err=nvs_set_u8(h,"page_sec",r->page_seconds);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err!=ESP_OK)ESP_LOGW("companion","preference save failed: %s",esp_err_to_name(err));
}
static void drain_audio(struct runtime_t *r)
{
    while(xQueueReceive(r->audio,&r->captured,0)==pdTRUE){
        if(r->active&&r->captured.generation==r->command.generation){
            muse_hatch_turn_audio(r->captured.pcm,AUDIO_FRAMES);
            r->recorded_frames+=AUDIO_FRAMES;
        }
    }
}
static void command(struct runtime_t *r)
{
    command_t *c=&r->command;
    if(c->effects&(COMP_EFFECT_CANCEL|COMP_EFFECT_BEGIN)){
        companion_bridge_interrupt(r->bridge);
        if(r->remote){muse_hatch_turn_cancel();companion_bridge_finish(r->bridge,NULL,"local_interruption");r->remote=false;}
    }
    r->volume=c->volume;r->brightness=c->brightness;
    r->language=c->language;r->sleep_seconds=c->sleep_seconds;
    r->speech_rate=c->speech_rate;r->page_seconds=c->page_seconds;
    muse_companion_language(r->language);
    if(c->effects&COMP_EFFECT_CANCEL){
        if(r->querying){companion_character_job_query_finished(r->character_job);r->querying=false;}
        if(r->generating){companion_generation_finish(r->generator,"interrupted by AI key");r->generating=false;}
        muse_hatch_turn_cancel();r->active=false;r->waiting=false;r->phase_started_us=esp_timer_get_time();
    }
    if(c->effects&COMP_EFFECT_PREFS)persist(r,c->muted);
    if(c->effects&COMP_EFFECT_BEGIN){
        r->recorded_frames=0;
        if(!muse_hatch_ready()){
            atomic_store(&r->recording,false);
            publish(r,COMP_MSG_ERROR,c->generation,"Muse 尚未就绪，请检查配对和网络",false);
        }else if(!companion_tts_idle(r->tts)){
            /* HTTP cancellation is bounded by timeout; do not feed speaker
             * echo to a new recording while the previous worker drains. */
            unsigned waits=0;
            while(!companion_tts_idle(r->tts)&&waits++<700)vTaskDelay(pdMS_TO_TICKS(5));
            if(!companion_tts_idle(r->tts)){
                atomic_store(&r->recording,false);
                publish(r,COMP_MSG_ERROR,c->generation,"语音播放仍在停止，请重试",false);
            }else{muse_hatch_turn_begin();r->active=true;r->phase_started_us=esp_timer_get_time();atomic_store(&r->recording,atomic_load(&r->wants_record));}
        }else{muse_hatch_turn_begin();r->active=true;r->phase_started_us=esp_timer_get_time();atomic_store(&r->recording,atomic_load(&r->wants_record));}
    }
    if((c->effects&COMP_EFFECT_END)&&r->active){
        drain_audio(r);
        ESP_LOGI("companion","recording released: generation=%u frames=%u",(unsigned)c->generation,(unsigned)r->recorded_frames);
        muse_hatch_turn_end();r->active=false;r->waiting=true;r->phase_started_us=esp_timer_get_time();
    }
    if((c->effects&COMP_EFFECT_RETRY)&&r->reply[0]){
        if(companion_tts_submit(r->tts,c->generation,r->reply,r->volume,r->speech_rate)!=ESP_OK)
            publish(r,COMP_MSG_ERROR,c->generation,"无法开始朗读，请重试",false);
    }
}
static void character_notice(struct runtime_t *r,companion_character_job_event_t event)
{
    if(event!=CHARACTER_JOB_NONE&&event!=CHARACTER_JOB_STARTED)r->character_notice_pending=event;
    if(r->remote||r->active||r->waiting||r->querying||r->generating||atomic_load(&r->key)||
        atomic_load(&r->wants_record)||!companion_tts_idle(r->tts))return;
    event=r->character_notice_pending;r->character_notice_pending=CHARACTER_JOB_NONE;
    const char *text=NULL;
    if(event==CHARACTER_JOB_READY_HELD)text=r->language?
        "Character pack is ready. Installation is not enabled in this development firmware.":
        "角色包已就绪，当前开发固件尚未开放安装。";
    else if(event==CHARACTER_JOB_DOWNLOADED)text=r->language?
        "Character pack verified. Preparing to apply it; please wait.":
        "角色包已通过校验，正在准备换装，请等待。";
    else if(event==CHARACTER_JOB_FAILED)text=r->language?
        "Character task failed. No successful change has been confirmed.":"换装任务失败，尚未确认完成换装。";
    else if(event==CHARACTER_JOB_TIMEOUT)text=r->language?
        "Character task timed out. You can continue using the device.":"等待换装超时，可以继续正常使用。";
    else if(event==CHARACTER_JOB_REJECTED)text=r->language?
        "Character instruction was rejected. Your current character is unchanged.":"换装指令未被接受，当前角色未更换。";
    else if(event==CHARACTER_JOB_RESTORE_DISABLED)text=r->language?
        "Initial character restore is not enabled in this development firmware. Your current character is unchanged.":
        "当前尚未开放换装，当前角色未更换。";
    else if(event==CHARACTER_JOB_RESTORE_HANDOFF)text=r->language?
        "Preparing to restore the initial character. Please wait.":
        "正在换装，请等待。";
    else if(event==CHARACTER_JOB_RESTORE_FAILED)text=r->language?
        "Initial character restore did not complete. No successful restoration has been confirmed.":
        "换装失败，请重试。";
    if(text){
        snprintf(r->reply,sizeof(r->reply),"%s",text);
        publish(r,COMP_MSG_REPLY,r->command.generation,r->reply,false);
    }
}
static bool character_reply(struct runtime_t *r,bool from_query)
{
    companion_character_job_event_t event=CHARACTER_JOB_NONE;
    character_command_kind_t kind=r->character_job?companion_character_job_reply(r->character_job,
        r->incoming,(uint64_t)(esp_timer_get_time()/1000),(uint64_t)time(NULL),&event):
        companion_character_extract(r->incoming,&r->discarded_control);
    if(!r->character_job&&kind!=CHARACTER_NONE)event=CHARACTER_JOB_REJECTED;
    character_notice(r,event);
    /* Preserve ordinary prose around controls; control-only replies must not
     * replace the previous user reply or enter the TTS retry buffer. */
    const char *p=r->incoming;while(*p&&isspace((unsigned char)*p))++p;
    return *p && (!from_query || kind!=CHARACTER_NONE);
}
static esp_err_t restore_start_adapter(void *context,const char *job,uint64_t *token)
{(void)context;return companion_character_storage_restore_start(job,token);}
static esp_err_t restore_poll_adapter(void *context,uint64_t token,bool *finished,esp_err_t *result)
{(void)context;return companion_character_storage_restore_poll(token,finished,result);}
static void voice(void *ctx)
{
    struct runtime_t *r=ctx;
    /* A persisted job may query before the first UI action. Honor saved mute,
     * language and speech settings for any ordinary prose around its status. */
    r->speech_rate=100;
    nvs_handle_t prefs;
    if(nvs_open("companion",NVS_READONLY,&prefs)==ESP_OK){
        uint8_t value;
        if(nvs_get_u8(prefs,"muted",&value)==ESP_OK)atomic_store(&r->muted,value!=0);
        if(nvs_get_u8(prefs,"language",&value)==ESP_OK)r->language=value!=0;
        if(nvs_get_u8(prefs,"volume",&value)==ESP_OK&&value<=100)r->volume=value;
        if(nvs_get_u8(prefs,"speech_rate",&value)==ESP_OK&&value>=75&&value<=150)r->speech_rate=value;
        nvs_close(prefs);
    }
    muse_companion_language(r->language);
    while(!muse_port_initialized())vTaskDelay(pdMS_TO_TICKS(50));
    muse_hatch_start();
    const companion_character_job_config_t job_config={.load=companion_character_job_load,.save=companion_character_job_save,
        .download_start=companion_character_storage_download_start,.download_poll=companion_character_storage_download_poll,
        .restore_start=restore_start_adapter,.restore_poll=restore_poll_adapter};
    esp_err_t job_error=companion_character_job_create(&job_config,(uint64_t)(esp_timer_get_time()/1000),
        (uint64_t)time(NULL),&r->character_job);
    if(job_error!=ESP_OK)ESP_LOGE("companion","character job unavailable: %s",esp_err_to_name(job_error));
    for(;;){
        /* SDK initialization is signalled before lwIP's mailbox exists.
         * Wait for a real STA connection before opening any listening socket.
         * Generate the persisted key while the radio supplies RNG entropy. */
        if(!r->bridge_attempted&&wifi_mgr_is_connected()){
            r->bridge_attempted=true;
            esp_err_t bridge_error=companion_bridge_create(&r->bridge);
            if(bridge_error==ESP_OK)bridge_error=companion_bridge_start(r->bridge);
            if(bridge_error!=ESP_OK){ESP_LOGE("companion","LAN bridge unavailable: %s",esp_err_to_name(bridge_error));r->bridge=NULL;}
            else ESP_LOGI("companion","LAN Muse bridge listening on port 4397");
        }
        if(!r->sntp&&wifi_mgr_is_connected()){
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_init();r->sntp=true;
        }
        command_t next;
        if(xQueueReceive(r->control,&next,pdMS_TO_TICKS(10))==pdTRUE
            && next.generation==atomic_load(&r->generation)){r->command=next;command(r);}
        if(r->generating&&companion_generation_cancelled(r->generator)){
            muse_hatch_turn_cancel();companion_generation_finish(r->generator,"cancelled");r->generating=false;
        }
        if(!r->remote&&!r->generating&&!r->querying&&!r->active&&!r->waiting&&
            !atomic_load(&r->key)&&!atomic_load(&r->wants_record)&&companion_tts_idle(r->tts)){
            const char *prompt=companion_generation_take(r->generator);
            if(prompt){
                if(muse_companion_text_begin(prompt)){
                    r->generating=true;r->phase_started_us=esp_timer_get_time();
                    ESP_LOGI("companion","Muse character generation started");
                }else {muse_hatch_turn_cancel();companion_generation_finish(r->generator,"could not send request");}
            }
        }
        companion_character_job_event_t job_event=companion_character_job_tick(r->character_job,
            (uint64_t)(esp_timer_get_time()/1000));
        character_notice(r,job_event);
        bool character_busy=r->remote||r->generating||r->active||r->waiting||r->querying||
            atomic_load(&r->key)||atomic_load(&r->recording)||atomic_load(&r->wants_record)||!companion_tts_idle(r->tts);
        character_notice(r,companion_character_job_download_tick(r->character_job,
            (uint64_t)(esp_timer_get_time()/1000),character_busy));
        unsigned progress=companion_character_job_progress(r->character_job);
        if(progress!=r->character_progress){
            companion_message_t *notice=calloc(1,sizeof(*notice));
            if(notice){
                notice->kind=COMP_MSG_CHARACTER;notice->generation=progress;
                if(xQueueSend(r->messages,notice,0)==pdTRUE)r->character_progress=progress;
                free(notice);
            }
        }
        if(companion_character_job_take_query(r->character_job,(uint64_t)(esp_timer_get_time()/1000),
            character_busy,muse_hatch_ready(),r->character_query,sizeof(r->character_query))){
            if(muse_companion_text_begin(r->character_query)){
                r->querying=true;r->phase_started_us=esp_timer_get_time();
            }else{muse_hatch_turn_cancel();companion_character_job_query_finished(r->character_job);}
        }
        drain_audio(r);
        bool local_idle=!r->remote&&!r->generating&&!r->querying&&!r->active&&!r->waiting&&
            !atomic_load(&r->key)&&!atomic_load(&r->wants_record)&&companion_tts_idle(r->tts);
        const char *remote_text=companion_bridge_take(r->bridge,muse_hatch_ready(),local_idle);
        if(remote_text){
            if(muse_companion_text_begin(remote_text))r->remote=true;
            else {muse_hatch_turn_cancel();companion_bridge_finish(r->bridge,NULL,"send_failed");}
        }
        if(r->remote&&(atomic_load(&r->key)||companion_bridge_cancelled(r->bridge))){
            if(atomic_load(&r->key))companion_bridge_interrupt(r->bridge);
            muse_hatch_turn_cancel();companion_bridge_finish(r->bridge,NULL,"cancelled_or_timeout");r->remote=false;
        }
        if(atomic_exchange(&r->overrun,false)&&(r->active||r->waiting)){
            atomic_store(&r->recording,false);muse_hatch_turn_cancel();r->active=false;r->waiting=false;
            publish(r,COMP_MSG_ERROR,r->command.generation,"录音传输未跟上，请重试",false);
        }
        muse_hatch_ev_t ev=muse_hatch_turn_event(r->event_text,sizeof(r->event_text));
        if(r->remote){
            if(ev==MUSE_HATCH_EV_DONE||ev==MUSE_HATCH_EV_ERROR){
                bool got=ev==MUSE_HATCH_EV_DONE&&muse_companion_reply_copy(r->incoming,sizeof(r->incoming));
                companion_bridge_finish(r->bridge,got?r->incoming:NULL,got?NULL:"muse_failed");
                if(!got)muse_hatch_turn_cancel();
                r->remote=false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));continue;
        }
        if(r->querying){
            if(ev==MUSE_HATCH_EV_DONE||ev==MUSE_HATCH_EV_ERROR||
                esp_timer_get_time()-r->phase_started_us>90000000LL||job_event==CHARACTER_JOB_TIMEOUT||job_event==CHARACTER_JOB_FAILED){
                bool prose=false;
                if(ev==MUSE_HATCH_EV_DONE&&muse_companion_reply_copy(r->incoming,sizeof(r->incoming)))
                    prose=character_reply(r,true);
                else muse_hatch_turn_cancel();
                companion_character_job_query_finished(r->character_job);r->querying=false;
                if(prose){
                    snprintf(r->reply,sizeof(r->reply),"%s",r->incoming);
                    bool speaking=!atomic_load(&r->muted);
                    publish(r,COMP_MSG_REPLY,r->command.generation,r->reply,speaking);
                    if(speaking&&companion_tts_submit(r->tts,r->command.generation,r->reply,r->volume,r->speech_rate)!=ESP_OK)
                        publish(r,COMP_MSG_ERROR,r->command.generation,"无法开始朗读，文字已保留",false);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));continue;
        }
        if(r->generating){
            if(ev==MUSE_HATCH_EV_DONE||ev==MUSE_HATCH_EV_ERROR){
                if(ev==MUSE_HATCH_EV_DONE&&muse_companion_reply_copy(r->incoming,sizeof(r->incoming)))
                    (void)character_reply(r,false);
                companion_generation_finish(r->generator,ev==MUSE_HATCH_EV_ERROR?"Muse request failed":NULL);
                r->generating=false;ESP_LOGI("companion","Muse character generation finished");
            }else if(esp_timer_get_time()-r->phase_started_us>300000000LL){
                muse_hatch_turn_cancel();companion_generation_finish(r->generator,"generation timed out");r->generating=false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));continue;
        }
        uint32_t gen=r->command.generation;
        if(ev==MUSE_HATCH_EV_ERROR){
            atomic_store(&r->recording,false);r->active=false;r->waiting=false;
            publish(r,COMP_MSG_ERROR,gen,"Muse 对话失败，请检查连接后重试",false);
        }else if(ev==MUSE_HATCH_EV_DONE){
            r->waiting=false;
            if(muse_companion_reply_copy(r->incoming,sizeof(r->incoming))){
                if(!character_reply(r,false)){
                    publish(r,COMP_MSG_DONE,gen,"",false);
                    vTaskDelay(pdMS_TO_TICKS(1));continue;
                }
                snprintf(r->reply,sizeof(r->reply),"%s",r->incoming);
                ESP_LOGI("companion","reply received: generation=%u bytes=%u",(unsigned)gen,(unsigned)strlen(r->reply));
                bool speaking=!atomic_load(&r->muted);
                publish(r,COMP_MSG_REPLY,gen,r->reply,speaking);
                if(speaking&&companion_tts_submit(r->tts,gen,r->reply,r->volume,r->speech_rate)!=ESP_OK)
                    publish(r,COMP_MSG_ERROR,gen,"无法开始朗读，文字已保留",false);
            }else publish(r,COMP_MSG_ERROR,gen,"没有收到文字回答，请重试",false);
        }
        if(r->active||r->waiting){
            if(esp_timer_get_time()-r->phase_started_us>(r->active?60000000LL:90000000LL)){
                atomic_store(&r->recording,false);muse_hatch_turn_cancel();r->active=false;r->waiting=false;
                publish(r,COMP_MSG_ERROR,gen,"本次等待超时，请重新提问",false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
static void sdk(void *ctx){(void)ctx;app_run();vTaskDelete(NULL);}
static void key_changed(void *button,void *ctx)
{(void)ctx;atomic_store(&runtime->key,iot_button_get_event(button)==BUTTON_PRESS_DOWN);}
esp_err_t companion_runtime_start(void)
{
    if(runtime)return ESP_ERR_INVALID_STATE;
    struct runtime_t *r=calloc(1,sizeof(*r));if(!r)return ESP_ERR_NO_MEM;
    r->battery=-1;r->volume=55;r->brightness=70;r->last_brightness=101;
    r->control=xQueueCreate(CONTROL_QUEUE_DEPTH,sizeof(command_t));
    r->messages=xQueueCreate(MESSAGE_QUEUE_DEPTH,sizeof(companion_message_t));
    r->audio=xQueueCreate(AUDIO_QUEUE_DEPTH,sizeof(audio_t));
    if(!r->control||!r->messages||!r->audio)goto nomem;
    r->mic=bsp_audio_codec_microphone_init();r->spk=bsp_audio_codec_speaker_init();
    if(!r->mic||!r->spk)goto fail;
    esp_codec_dev_sample_info_t format={.sample_rate=AUDIO_RATE,.bits_per_sample=16,.channel=1};
    if(esp_codec_dev_open(r->mic,&format)!=ESP_CODEC_DEV_OK)goto fail;
    if(esp_codec_dev_open(r->spk,&format)!=ESP_CODEC_DEV_OK)goto fail;
    esp_codec_dev_set_in_gain(r->mic,30.0f);
    if(companion_tts_create(r->spk,tts_done,r,&r->tts)!=ESP_OK)goto fail;
    /* From this point the TTS worker borrows r: keep app-lifetime resources
     * valid on subsequent startup failure, with maintenance still reachable. */
    runtime=r;
    ESP_RETURN_ON_ERROR(companion_generation_create(&r->generator),"companion","generation service");
    int count=0;ESP_RETURN_ON_ERROR(bsp_iot_button_create(r->buttons,&count,BSP_BUTTON_NUM),"companion","buttons");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(r->buttons[BSP_BUTTON_AI],BUTTON_PRESS_DOWN,NULL,key_changed,NULL),"companion","AI press");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(r->buttons[BSP_BUTTON_AI],BUTTON_PRESS_UP,NULL,key_changed,NULL),"companion","AI release");
    r->battery_ready=bsp_battery_init()==ESP_OK;
    if(bsp_imu_init()==ESP_OK){const bsp_imu_config_t cfg=BSP_IMU_CONFIG_DEFAULT();r->imu_ready=bsp_imu_start(&cfg)==ESP_OK;}
    /* Radio/NVS and preference tasks retain internal stacks. Continuous
     * codec capture and TTS never perform flash operations; their PSRAM
     * stacks preserve the SDK's 16 KiB DMA transmission burst margin. */
    if(xTaskCreate(sdk,"muse_link",12288,r,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xTaskCreateWithCaps(capture,"companion_mic",4096,r,5,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xTaskCreate(voice,"companion_voice",WORKER_STACK,r,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
nomem:
    if(r->control)vQueueDelete(r->control);
    if(r->messages)vQueueDelete(r->messages);
    if(r->audio)vQueueDelete(r->audio);
    free(r);return ESP_ERR_NO_MEM;
fail:
    if(r->mic)esp_codec_dev_close(r->mic);
    if(r->spk)esp_codec_dev_close(r->spk);
    if(r->control)vQueueDelete(r->control);
    if(r->messages)vQueueDelete(r->messages);
    if(r->audio)vQueueDelete(r->audio);
    free(r);return ESP_FAIL;
}
void companion_platform_tick(uint32_t ms){if(runtime)runtime->ui_ms+=ms;}
void companion_platform_status(bool *online,bool *paired,int *battery,bool *key,bool *motion)
{
    struct runtime_t *r=runtime;*online=false;*paired=false;*battery=-1;*key=false;*motion=false;
    if(!r)return;
    *key=atomic_load(&r->key);
    if(muse_port_initialized()){
        *paired=muse_link_hatch_linked();*online=muse_link_req_ready();
    }
    if(r->battery_ready&&r->ui_ms%5000==0){
        bsp_battery_status_t *b=malloc(sizeof(*b));
        if(b){if(bsp_battery_read(b)==ESP_OK)r->battery=b->state_of_charge;free(b);}
    }
    *battery=r->battery;
    if(r->imu_ready){
        float x,y,z;
        if(bsp_imu_get_accel(&x,&y,&z)==ESP_OK){
            if(r->accel_valid&&fabsf(x-r->x)+fabsf(y-r->y)+fabsf(z-r->z)>0.25f)*motion=true;
            r->x=x;r->y=y;r->z=z;r->accel_valid=true;
        }
    }
}
bool companion_platform_confirm_pairing(void)
{return muse_port_initialized()&&app_confirm_pairing_press();}
bool companion_platform_message(companion_message_t *out)
{return runtime&&xQueueReceive(runtime->messages,out,0)==pdTRUE;}
bool companion_platform_effect(unsigned effects,const companion_snapshot_t *s)
{
    if(!runtime)return false;
    ESP_LOGI("companion","interaction: effects=%u generation=%u muted=%u",effects,(unsigned)s->generation,(unsigned)s->muted);
    command_t c={.effects=effects,.generation=s->generation,.volume=s->volume,
        .brightness=s->brightness,.muted=s->muted,.language=s->language,
        .sleep_seconds=s->sleep_seconds,.speech_rate=s->speech_rate,.page_seconds=s->page_seconds};
    atomic_store(&runtime->generation,s->generation);
    atomic_store(&runtime->muted,s->muted);
    companion_tts_cancel(runtime->tts,s->generation,s->muted);
    companion_tts_set_volume(runtime->tts,s->volume);
    if(effects&COMP_EFFECT_CANCEL){atomic_store(&runtime->recording,false);atomic_store(&runtime->wants_record,false);}
    if(effects&COMP_EFFECT_BEGIN)atomic_store(&runtime->wants_record,true);
    /* Worker enables capture only after cancelled playback has drained. */
    if(effects&COMP_EFFECT_END){atomic_store(&runtime->recording,false);atomic_store(&runtime->wants_record,false);}
    bool queued=xQueueSend(runtime->control,&c,0)==pdTRUE;
    if(!queued)atomic_store(&runtime->recording,false);
    return queued;
}
void companion_platform_brightness(unsigned value)
{esp_err_t err=bsp_display_brightness_set(value);if(err!=ESP_OK)ESP_LOGW("companion","brightness failed: %s",esp_err_to_name(err));}
void companion_platform_load_prefs(companion_snapshot_t *prefs)
{
    nvs_handle_t h;if(nvs_open("companion",NVS_READONLY,&h)!=ESP_OK)return;
    uint8_t value;if(nvs_get_u8(h,"volume",&value)==ESP_OK)prefs->volume=value;
    if(nvs_get_u8(h,"brightness",&value)==ESP_OK)prefs->brightness=value;
    if(nvs_get_u8(h,"muted",&value)==ESP_OK)prefs->muted=value!=0;
    if(nvs_get_u8(h,"language",&value)==ESP_OK)prefs->language=value;
    if(nvs_get_u8(h,"sleep_sec",&value)==ESP_OK)prefs->sleep_seconds=value;
    if(nvs_get_u8(h,"speech_rate",&value)==ESP_OK)prefs->speech_rate=value;
    if(nvs_get_u8(h,"page_sec",&value)==ESP_OK)prefs->page_seconds=value;
    nvs_close(h);
}
