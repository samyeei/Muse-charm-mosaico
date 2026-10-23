#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "companion_model.h"
int main(void) {
    companion_model_handle_t m=companion_model_create();
    companion_snapshot_t *s=malloc(sizeof(*s));assert(m&&s);
    companion_model_snapshot(m,s);assert(!s->muted);
    companion_model_action(m,COMP_PRESS,0);companion_model_snapshot(m,s);assert(s->mode==COMP_ERROR);
    assert(!(companion_model_action(m,COMP_RELEASE,0)&COMP_EFFECT_END));
    companion_model_connection(m,true,true,82);
    assert(companion_model_action(m,COMP_PRESS,0)&COMP_EFFECT_BEGIN);
    companion_model_snapshot(m,s);uint32_t old=s->generation;
    companion_model_action(m,COMP_CANCEL,0);
    assert(!(companion_model_action(m,COMP_RELEASE,0)&COMP_EFFECT_END));
    companion_model_reply(m,old,"stale",true);companion_model_snapshot(m,s);assert(!s->reply[0]);
    assert(companion_model_action(m,COMP_PRESS,0)&COMP_EFFECT_BEGIN);
    assert(companion_model_action(m,COMP_RELEASE,0)&COMP_EFFECT_END);
    companion_model_snapshot(m,s);assert(s->mode==COMP_THINKING);
    companion_model_reply(m,s->generation,"hello",true);companion_model_snapshot(m,s);assert(s->mode==COMP_SPEAKING);
    companion_model_action(m,COMP_MUTE,0);companion_model_snapshot(m,s);assert(s->muted&&s->mode==COMP_IDLE&&strcmp(s->reply,"hello")==0);
    companion_model_action(m,COMP_VOLUME,30);companion_model_action(m,COMP_BRIGHTNESS,65);
    companion_model_snapshot(m,s);assert(s->page_seconds==7&&s->speech_rate==100&&s->sleep_seconds==15);
    companion_model_tick(m,12500);
    companion_model_action(m,COMP_AUTO_PAGE_NEXT,0);
    companion_model_tick(m,1000);companion_model_snapshot(m,s);
    assert(s->page==1&&s->display_brightness<65&&s->display_brightness>8);
    companion_model_tick(m,1500);companion_model_snapshot(m,s);assert(s->asleep&&s->display_brightness==0);
    const unsigned timeouts[]={5,10,15,20,30,60};
    for(unsigned i=0;i<sizeof(timeouts)/sizeof(timeouts[0]);++i){
        unsigned seconds=timeouts[i];
        companion_model_action(m,COMP_SLEEP_TIMEOUT,seconds);
        companion_model_tick(m,seconds*1000-2100);companion_model_snapshot(m,s);assert(!s->asleep&&s->display_brightness==65);
        companion_model_tick(m,1100);companion_model_snapshot(m,s);assert(!s->asleep&&s->display_brightness<65);
        companion_model_tick(m,1000);companion_model_snapshot(m,s);assert(s->asleep&&s->display_brightness==0);
        companion_model_action(m,COMP_WAKE,0);companion_model_snapshot(m,s);assert(!s->asleep&&s->display_brightness==65);
    }
    companion_model_options(m,1,5,150,3);companion_model_snapshot(m,s);
    assert(s->language==1&&s->sleep_seconds==5&&s->speech_rate==150&&s->page_seconds==3);
    companion_model_action(m,COMP_SPEECH_RATE,113);companion_model_snapshot(m,s);assert(s->speech_rate==113);
    companion_model_options(m,0,999,999,999);companion_model_snapshot(m,s);
    assert(s->sleep_seconds==15&&s->speech_rate==100&&s->page_seconds==7);
    companion_model_action(m,COMP_PRESS,0);companion_model_snapshot(m,s);assert(!s->asleep&&s->display_brightness==65);
    companion_model_tick(m,100000);companion_model_snapshot(m,s);assert(!s->asleep&&s->mode==COMP_LISTENING);
    companion_model_action(m,COMP_RELEASE,0);companion_model_snapshot(m,s);
    assert(s->mode==COMP_THINKING);old=s->generation;
    companion_model_done(m,old);companion_model_snapshot(m,s);assert(s->mode==COMP_IDLE&&!s->reply[0]);
    companion_model_action(m,COMP_PRESS,0);
    companion_model_done(m,old);companion_model_snapshot(m,s);assert(s->mode==COMP_LISTENING);
    companion_model_delete(m);free(s);puts("PASS: hold/release, cancel, stale replies, speaker mute, dimming, wake, busy sleep suppression, control-only DONE");return 0;
}
