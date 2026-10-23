// SPDX-License-Identifier: Apache-2.0
#include "companion_model.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
enum { DIM_TRANSITION_MS=2000,
    DIM_BRIGHTNESS=8, MAX_RECORD_MS=60000, MAX_WAIT_MS=90000 };
struct companion_model_t { companion_snapshot_t s; uint32_t idle_ms, phase_ms; };
static void wake(companion_model_handle_t m)
{
    m->idle_ms=0; m->s.asleep=false; m->s.display_brightness=m->s.brightness;
    if(m->s.mode==COMP_SLEEPY) m->s.mode=COMP_IDLE;
}
companion_model_handle_t companion_model_create(void)
{
    companion_model_handle_t m=calloc(1,sizeof(*m));
    if(m) { m->s.volume=55; m->s.brightness=70; m->s.display_brightness=70;
        m->s.battery=-1; m->s.sleep_seconds=15; m->s.speech_rate=100; m->s.page_seconds=7; }
    return m;
}
void companion_model_delete(companion_model_handle_t m) { free(m); }
unsigned companion_model_action(companion_model_handle_t m,
    companion_action_t a,unsigned value)
{
    if(!m) return 0;
    /* Timer-driven reading is not user activity and must not wake the screen. */
    if(a==COMP_AUTO_PAGE_NEXT){
        if(m->s.reply[0]&&!m->s.settings&&!m->s.asleep&&
            (m->s.mode==COMP_IDLE||m->s.mode==COMP_SPEAKING))++m->s.page;
        return 0;
    }
    wake(m);
    switch(a) {
    case COMP_PRESS:
        if(m->s.key_down) return 0;
        m->s.key_down=true; m->s.settings=false;
        ++m->s.generation; m->phase_ms=0; m->s.page=0;
        m->s.error[0]='\0'; m->s.reply[0]='\0';
        if(!m->s.online || !m->s.paired) {
            m->s.mode=COMP_ERROR;
            snprintf(m->s.error,sizeof(m->s.error),"%s",
                !m->s.paired ? "请先在 Muse App 配对" : "网络未连接，请稍后重试");
            return COMP_EFFECT_CANCEL;
        }
        m->s.mode=COMP_LISTENING; return COMP_EFFECT_CANCEL|COMP_EFFECT_BEGIN;
    case COMP_RELEASE:
        if(!m->s.key_down) return 0;
        m->s.key_down=false;
        if(m->s.mode!=COMP_LISTENING) return 0;
        m->s.mode=COMP_THINKING; m->phase_ms=0; return COMP_EFFECT_END;
    case COMP_CANCEL:
        ++m->s.generation; m->s.mode=COMP_IDLE; m->s.error[0]='\0';
        return COMP_EFFECT_CANCEL; /* release after cancel cannot send */
    case COMP_MUTE:
        m->s.muted=!m->s.muted;
        if(m->s.muted && m->s.mode==COMP_SPEAKING) m->s.mode=COMP_IDLE;
        return COMP_EFFECT_PREFS;
    case COMP_SETTINGS:
        m->s.settings=true; return 0;
    case COMP_BACK: m->s.settings=false; return 0;
    case COMP_VOLUME:
        m->s.volume=value>100 ? 100 : value; return COMP_EFFECT_PREFS;
    case COMP_BRIGHTNESS:
        m->s.brightness=value<5 ? 5 : value>100 ? 100 : value;
        m->s.display_brightness=m->s.brightness; return COMP_EFFECT_PREFS;
    case COMP_LANGUAGE: m->s.language=value?1:0; return COMP_EFFECT_PREFS;
    case COMP_SLEEP_TIMEOUT:
        m->s.sleep_seconds=value==5||value==10||value==15||value==20||value==30||value==60?value:15; return COMP_EFFECT_PREFS;
    case COMP_SPEECH_RATE:
        m->s.speech_rate=value>=75&&value<=150?value:100; return COMP_EFFECT_PREFS;
    case COMP_PAGE_INTERVAL:
        m->s.page_seconds=value>=3&&value<=15?value:7; return COMP_EFFECT_PREFS;
    case COMP_PAGE_SET: m->s.page=value; return 0;
    case COMP_PAGE_NEXT: ++m->s.page; return 0;
    case COMP_PAGE_PREV: if(m->s.page) --m->s.page; return 0;
    case COMP_RETRY:
        if(m->s.reply[0] && !m->s.muted) {
            m->s.error[0]='\0';m->s.mode=COMP_SPEAKING;
            return COMP_EFFECT_RETRY;
        }
        return 0;
    default:return 0;
    }
}
void companion_model_tick(companion_model_handle_t m,uint32_t ms)
{
    if(!m) return;
    bool busy=m->s.mode==COMP_LISTENING||m->s.mode==COMP_THINKING||m->s.mode==COMP_SPEAKING;
    if(busy) { m->idle_ms=0; m->phase_ms+=ms; return; }
    uint32_t off_ms=m->s.sleep_seconds*1000, dim_ms=off_ms-DIM_TRANSITION_MS;
    if(m->idle_ms<off_ms) m->idle_ms+=ms;
    unsigned level=m->s.brightness;
    if(m->idle_ms>=off_ms) { level=0; m->s.asleep=true; }
    else if(m->idle_ms>=dim_ms) {
        uint32_t progress=m->idle_ms-dim_ms;
        unsigned low=level<DIM_BRIGHTNESS ? level : DIM_BRIGHTNESS;
        level=progress>=DIM_TRANSITION_MS ? low :
            level-(level-low)*progress/DIM_TRANSITION_MS;
        if(m->s.mode==COMP_IDLE) m->s.mode=COMP_SLEEPY;
    }
    m->s.display_brightness=level;
}
void companion_model_connection(companion_model_handle_t m,bool online,bool paired,int battery)
{ if(m) { m->s.online=online;m->s.paired=paired;m->s.battery=battery; } }
void companion_model_reply(companion_model_handle_t m,uint32_t gen,const char *text,bool speaking)
{
    if(!m||gen!=m->s.generation||m->s.mode==COMP_LISTENING) return;
    snprintf(m->s.reply,sizeof(m->s.reply),"%s",text?text:"");
    m->s.page=0;m->s.error[0]='\0';
    m->s.mode=speaking&&!m->s.muted ? COMP_SPEAKING : COMP_IDLE;
    m->phase_ms=0; wake(m);
}
void companion_model_done(companion_model_handle_t m,uint32_t gen)
{ if(m&&gen==m->s.generation&&m->s.mode!=COMP_LISTENING) {
    m->s.mode=COMP_IDLE;wake(m); } }
void companion_model_error(companion_model_handle_t m,uint32_t gen,const char *text)
{ if(m&&gen==m->s.generation) {
    m->s.mode=COMP_ERROR;snprintf(m->s.error,sizeof(m->s.error),"%s",text?text:"连接失败");wake(m); } }
void companion_model_prefs(companion_model_handle_t m,unsigned v,unsigned b,bool muted)
{ if(m) {m->s.volume=v<=100?v:55;m->s.brightness=b>=5&&b<=100?b:70;
    m->s.muted=muted;m->s.display_brightness=m->s.brightness;} }
void companion_model_options(companion_model_handle_t m,unsigned language,unsigned sleep,unsigned rate,unsigned page)
{
    if(!m)return;
    companion_model_action(m,COMP_LANGUAGE,language);
    companion_model_action(m,COMP_SLEEP_TIMEOUT,sleep);
    companion_model_action(m,COMP_SPEECH_RATE,rate);
    companion_model_action(m,COMP_PAGE_INTERVAL,page);
}
void companion_model_snapshot(companion_model_handle_t m,companion_snapshot_t *out)
{ if(m&&out) *out=m->s; }
