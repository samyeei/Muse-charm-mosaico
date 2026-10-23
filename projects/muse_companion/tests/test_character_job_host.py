"""Execute actual job/parser logic with persistent storage and scheduling faults."""
from pathlib import Path
import shutil
import subprocess


def test_character_job_serialization_persistence_and_control_stripping(tmp_path):
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    main = Path(__file__).parents[1] / "main"
    (tmp_path / "esp_err.h").write_text("""
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_NOT_SUPPORTED 6
""", encoding="utf-8")
    source = tmp_path / "job_test.c"
    source.write_text(r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "companion_character_job.h"
static uint8_t disk[160];
static bool exists;
static int saves,save_error,load_error;
static esp_err_t load(void *ctx,uint8_t *p,size_t n)
{(void)ctx;assert(n==160);if(load_error)return load_error;if(!exists)return ESP_ERR_NOT_FOUND;memcpy(p,disk,n);return ESP_OK;}
static esp_err_t save(void *ctx,const uint8_t *p,size_t n)
{(void)ctx;assert(n==160);++saves;if(save_error)return save_error;exists=true;memcpy(disk,p,n);return ESP_OK;}
static int starts,polls,start_error,poll_error,download_error;
static bool finished;
static int restore_starts,restore_polls,restore_error;
static esp_err_t restore_start(void *ctx,const char *job,uint64_t *token)
{(void)ctx;assert(!strncmp(job,"restore-",8));++restore_starts;
 if(start_error)return start_error;*token=77;return ESP_OK;}
static esp_err_t restore_poll(void *ctx,uint64_t token,bool *done,esp_err_t *result)
{(void)ctx;assert(token==77);++restore_polls;*done=finished;*result=restore_error;return poll_error;}
static character_command_t captured;
static esp_err_t start(void *ctx,const character_command_t *cmd,uint64_t *token)
{(void)ctx;++starts;if(start_error)return start_error;captured=*cmd;*token=42;return ESP_OK;}
static esp_err_t poll(void *ctx,uint64_t token,bool *done,esp_err_t *result)
{(void)ctx;assert(token==42);++polls;*done=finished;*result=download_error;return poll_error;}
static uint32_t wire_crc(const uint8_t *p,size_t n)
{uint32_t c=UINT32_MAX;for(size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320U&(0U-(c&1U)));}return ~c;}
static void ready(char *text,const char *hash)
{sprintf(text,"[CHARACTER_STATUS job=\"download-1\" status=\"ready\" pack_url=\"https://muse.ai/files/test.cpack\" sha256=\"%s\" size=\"2480324\" expires_at=\"2026-10-07T12:07:24Z\" character_version=\"role-1\" compat_version=\"candidate\" error=\"\"]",hash);}
int main(void)
{
 companion_character_job_config_t cfg={.load=load,.save=save};
 companion_character_job_handle_t h=NULL;
 const uint64_t utc=1791200000;
 assert(companion_character_job_create(&cfg,1000,utc,&h)==ESP_OK && h);
 char text[2048],query[96];companion_character_job_event_t event;
 strcpy(text,"Hello [CHARACTER_PACK job=\"job-1\"] wait.");
 assert(companion_character_job_reply(h,text,1000,utc,&event)==CHARACTER_PACK && event==CHARACTER_JOB_STARTED);
 assert(!strcmp(text,"Hello  wait.") && saves==1);
 assert(!companion_character_job_take_query(h,60999,false,true,query,sizeof(query)));
 assert(!companion_character_job_take_query(h,61000,true,true,query,sizeof(query))); // recording/wait/TTS/key busy
 assert(!companion_character_job_take_query(h,61000,false,false,query,sizeof(query)));
 assert(companion_character_job_take_query(h,61000,false,true,query,sizeof(query)));
 assert(!strcmp(query,"[CHARACTER_QUERY job=\"job-1\"]"));
 assert(!companion_character_job_take_query(h,121000,false,true,query,sizeof(query))); // one in-flight request
 companion_character_job_query_finished(h);
 assert(companion_character_job_take_query(h,121000,false,true,query,sizeof(query)));
 companion_character_job_query_finished(h);
 strcpy(text,"[CHARACTER_PACK job=\"job-1\"]");
 assert(companion_character_job_reply(h,text,121000,utc,&event)==CHARACTER_PACK && event==CHARACTER_JOB_NONE && saves==1);
 strcpy(text,"[CHARACTER_PACK job=\"job-2\"]");
 assert(companion_character_job_reply(h,text,121000,utc,&event)==CHARACTER_PACK && event==CHARACTER_JOB_REJECTED && saves==1);
 strcpy(text,"[CHARACTER_STATUS job=\"other-job\" status=\"failed\" error=\"unknown job\"]");
 assert(companion_character_job_reply(h,text,121000,utc,&event)==CHARACTER_STATUS && event==CHARACTER_JOB_NONE && saves==1);
 companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,2000,utc+100,&h)==ESP_OK);
 assert(!companion_character_job_take_query(h,61999,false,true,query,sizeof(query)));
 assert(companion_character_job_take_query(h,62000,false,true,query,sizeof(query)));
 companion_character_job_query_finished(h);
 assert(companion_character_job_tick(h,801999)==CHARACTER_JOB_NONE);
 assert(companion_character_job_tick(h,802000)==CHARACTER_JOB_TIMEOUT && saves==2);
 assert(!companion_character_job_take_query(h,803000,false,true,query,sizeof(query)));
 companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,0,utc+901,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"job-1\"]");
 assert(companion_character_job_reply(h,text,0,utc+901,&event)==CHARACTER_PACK && event==CHARACTER_JOB_NONE);
 strcpy(text,"[CHARACTER_PACK job=\"job-2\"]");
 assert(companion_character_job_reply(h,text,0,utc+901,&event)==CHARACTER_PACK && event==CHARACTER_JOB_STARTED);
 strcpy(text,"[CHARACTER_STATUS job=\"job-2\" status=\"ready\" pack_url=\"https://muse.ai/files/test.cpack\" sha256=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" size=\"2480324\" expires_at=\"2026-10-07T12:07:24Z\" character_version=\"role-1\" compat_version=\"candidate\" error=\"\"]");
 assert(companion_character_job_reply(h,text,60000,utc+961,&event)==CHARACTER_STATUS && event==CHARACTER_JOB_READY_HELD);
 assert(!text[0] && !companion_character_job_take_query(h,120000,false,true,query,sizeof(query)));
 companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,0,utc+1000,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"job-2\"]");
 assert(companion_character_job_reply(h,text,0,utc+1000,&event)==CHARACTER_PACK && event==CHARACTER_JOB_NONE);
 strcpy(text,"[CHARACTER_PACK job=\"job-3\"]");
 assert(companion_character_job_reply(h,text,0,0,&event)==CHARACTER_PACK && event==CHARACTER_JOB_STARTED);
 companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,0,0,&h)==ESP_OK);
 assert(!companion_character_job_take_query(h,60000,false,true,query,sizeof(query))); // reboot without a reliable clock cancels safely
 strcpy(text,"[CHARACTER_PACK job=\"job-4\"]");save_error=ESP_FAIL;
 assert(companion_character_job_reply(h,text,0,utc,&event)==CHARACTER_PACK && event==CHARACTER_JOB_FAILED);
 assert(!companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 save_error=0;companion_character_job_delete(h);
 disk[30]^=1;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_ERR_INVALID_STATE && !h);
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"OK [CHARACTER_PACK job=\"bad\" job=\"two\"] prose");
 assert(companion_character_job_reply(h,text,0,utc,&event)==CHARACTER_INVALID && event==CHARACTER_JOB_REJECTED);
 assert(!strcmp(text,"OK  prose"));
 strcpy(text,"Before [CHARACTER_STATUS job=\"ghost\" status=\"failed\" error=\"bad [job]\"] after");
 assert(companion_character_job_reply(h,text,0,utc,&event)==CHARACTER_STATUS);
 assert(!strcmp(text,"Before  after"));
 strcpy(text,"Before [CHARACTER_STATUS job=\"ghost\" status=\"failed\" unknown=\"hidden] tail\"] after");
 assert(companion_character_job_reply(h,text,0,utc,&event)==CHARACTER_INVALID);
 assert(strstr(text,"CHARACTER_")==NULL && strstr(text,"tail")==NULL);
 strcpy(text,"Ordinary answer");
 assert(companion_character_job_reply(h,text,0,utc,&event)==CHARACTER_NONE && !strcmp(text,"Ordinary answer"));
 strcpy(text,"[CHARACTER_PACK job=\"expired-link\"]");
 assert(companion_character_job_reply(h,text,0,2000000000,&event)==CHARACTER_PACK && event==CHARACTER_JOB_STARTED);
 strcpy(text,"[CHARACTER_STATUS job=\"expired-link\" status=\"ready\" pack_url=\"https://muse.ai/files/test.cpack\" sha256=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" size=\"2480324\" expires_at=\"2026-10-07T12:07:24Z\" character_version=\"role-1\" compat_version=\"candidate\" error=\"\"]");
 assert(companion_character_job_reply(h,text,1000,2000000001,&event)==CHARACTER_STATUS && event==CHARACTER_JOB_FAILED);
 companion_character_job_delete(h);
 // Production adapters enable download only; no installation callback exists.
 exists=false;cfg.download_start=start;cfg.download_poll=poll;
 assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"download-1\"]");
 companion_character_job_reply(h,text,0,utc,&event);
 const char *hash="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 ready(text,hash);
 assert(companion_character_job_reply(h,text,1000,utc+1,&event)==CHARACTER_STATUS && event==CHARACTER_JOB_NONE);
 assert(!companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 assert(companion_character_job_download_tick(h,1001,true)==CHARACTER_JOB_NONE && starts==0);
 // Subsequent parser calls cannot corrupt the retained command.
 strcpy(text,"[CHARACTER_STATUS job=\"wrong\" status=\"failed\" error=\"x\"]");
 companion_character_job_reply(h,text,1002,utc+1,&event);
 start_error=ESP_ERR_INVALID_STATE;
 assert(companion_character_job_download_tick(h,1003,false)==CHARACTER_JOB_NONE);
 start_error=0;
 assert(companion_character_job_download_tick(h,1004,false)==CHARACTER_JOB_NONE && starts==2);
 assert(!strcmp(captured.job,"download-1") && !strcmp(captured.sha256,hash));
 assert(captured.deadline_ms==900000);
 strcpy(text,"[CHARACTER_PACK job=\"another\"]");
 companion_character_job_reply(h,text,1005,utc+1,&event);assert(event==CHARACTER_JOB_REJECTED);
 assert(companion_character_job_download_tick(h,1006,true)==CHARACTER_JOB_NONE && polls==1);
 // A power cut requires a read-only re-query, never trusts partial NAND files.
 companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,0,utc+10,&h)==ESP_OK);
 assert(companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 companion_character_job_query_finished(h);
 ready(text,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
 companion_character_job_reply(h,text,60001,utc+70,&event);
 assert(event==CHARACTER_JOB_FAILED); // changed frozen package hash rejected
 assert(companion_character_job_download_tick(h,60002,false)==CHARACTER_JOB_NONE && starts==2);
 companion_character_job_delete(h);
 // Fresh task, completed download survives reboot without a second download.
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"download-1\"]");companion_character_job_reply(h,text,0,utc,&event);
 ready(text,hash);companion_character_job_reply(h,text,1,utc,&event);
 companion_character_job_download_tick(h,2,false);finished=true;
 assert(companion_character_job_download_tick(h,3,false)==CHARACTER_JOB_DOWNLOADED);
 assert(companion_character_job_progress(h)==COMP_CHARACTER_NONE);
 companion_character_job_delete(h);assert(companion_character_job_create(&cfg,0,utc+1,&h)==ESP_OK);
 assert(companion_character_job_download_tick(h,1000,false)==CHARACTER_JOB_NONE && starts==3);
 assert(companion_character_job_progress(h)==COMP_CHARACTER_NONE);
 assert(!companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 companion_character_job_delete(h);
 // Same package after interruption resumes download within the old deadline.
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"download-1\"]");companion_character_job_reply(h,text,0,utc,&event);
 ready(text,hash);companion_character_job_reply(h,text,1,utc,&event);
 companion_character_job_delete(h);assert(companion_character_job_create(&cfg,0,utc+100,&h)==ESP_OK);
 assert(companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 companion_character_job_query_finished(h);
 ready(text,hash);companion_character_job_reply(h,text,60001,utc+160,&event);
 assert(event==CHARACTER_JOB_NONE);
 finished=false;companion_character_job_download_tick(h,60002,false);
 assert(starts==4 && !strcmp(captured.sha256,hash));
 assert(companion_character_job_download_tick(h,800000,false)==CHARACTER_JOB_TIMEOUT);
 companion_character_job_delete(h);
 // Persistence failure prevents any network operation.
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"download-1\"]");companion_character_job_reply(h,text,0,utc,&event);
 save_error=ESP_FAIL;ready(text,hash);companion_character_job_reply(h,text,1,utc,&event);
 assert(event==CHARACTER_JOB_FAILED);
 assert(companion_character_job_download_tick(h,2,false)==CHARACTER_JOB_NONE && starts==4);
 save_error=0;companion_character_job_delete(h);
 // Worker failure and original deadline timeout never report success.
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"download-1\"]");companion_character_job_reply(h,text,0,utc,&event);
 ready(text,hash);companion_character_job_reply(h,text,1,utc,&event);
 companion_character_job_download_tick(h,2,false);download_error=ESP_FAIL;finished=true;
 assert(companion_character_job_download_tick(h,3,false)==CHARACTER_JOB_FAILED);
 companion_character_job_delete(h);
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"download-1\"]");companion_character_job_reply(h,text,0,utc,&event);
 ready(text,hash);companion_character_job_reply(h,text,1,utc,&event);
 assert(companion_character_job_download_tick(h,900000,false)==CHARACTER_JOB_TIMEOUT);
 companion_character_job_delete(h);
 // Old 96-byte records migrate only after their original CRC is checked.
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_PACK job=\"legacy\"]");companion_character_job_reply(h,text,0,utc,&event);
 companion_character_job_delete(h);
 memcpy(disk,"MCJOB001",8);memset(disk+88,0,72);
 uint32_t crc_value=wire_crc(disk,88);
 for(unsigned i=0;i<4;++i)disk[88+i]=(uint8_t)(crc_value>>(i*8));
 assert(companion_character_job_create(&cfg,0,utc+100,&h)==ESP_OK);
 assert(companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 assert(!strcmp(query,"[CHARACTER_QUERY job=\"legacy\"]"));
 companion_character_job_delete(h);
 disk[94]=1;assert(companion_character_job_create(&cfg,0,utc+100,&h)==ESP_ERR_INVALID_STATE);
 // Strict local restore syntax. Neither other targets nor mixed controls act.
 character_command_t control;
 strcpy(text,"before [CHARACTER_RESTORE job=\"restore-1\" target=\"initial\"] after");
 assert(companion_character_extract(text,&control)==CHARACTER_RESTORE);
 assert(!strcmp(text,"before  after") && !strcmp(control.job,"restore-1"));
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-1\" target=\"other\"]");
 assert(companion_character_extract(text,&control)==CHARACTER_INVALID && !text[0]);
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-1\" target=\"initial\" pack_url=\"https://muse.ai/test\"]");
 assert(companion_character_extract(text,&control)==CHARACTER_INVALID);
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-1\" target=\"initial\"][CHARACTER_PACK job=\"other\"]");
 assert(companion_character_extract(text,&control)==CHARACTER_INVALID && !text[0]);
 cfg.restore_start=restore_start;cfg.restore_poll=restore_poll;
 exists=false;start_error=poll_error=0;finished=false;restore_error=ESP_ERR_NOT_SUPPORTED;
 assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"OK [CHARACTER_RESTORE job=\"restore-1\" target=\"initial\"]");
 assert(companion_character_job_reply(h,text,0,utc,&event)==CHARACTER_RESTORE && event==CHARACTER_JOB_STARTED);
 assert(!strcmp(text,"OK "));
 int previous_saves=saves;
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-1\" target=\"initial\"]");
 assert(companion_character_job_reply(h,text,1,utc,&event)==CHARACTER_RESTORE && event==CHARACTER_JOB_NONE && saves==previous_saves);
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-2\" target=\"initial\"]");
 companion_character_job_reply(h,text,2,utc,&event);assert(event==CHARACTER_JOB_REJECTED);
 strcpy(text,"[CHARACTER_PACK job=\"other\"]");
 companion_character_job_reply(h,text,2,utc,&event);assert(event==CHARACTER_JOB_REJECTED);
 assert(companion_character_job_download_tick(h,3,true)==CHARACTER_JOB_NONE && !restore_starts); // voice/TTS busy
 assert(!companion_character_job_take_query(h,60000,false,true,query,sizeof(query))); // no Anna polling
 start_error=ESP_ERR_INVALID_STATE;
 assert(companion_character_job_download_tick(h,4,false)==CHARACTER_JOB_NONE && restore_starts==1);
 start_error=0;
 assert(companion_character_job_download_tick(h,5,false)==CHARACTER_JOB_NONE && restore_starts==2);
 assert(companion_character_job_download_tick(h,6,false)==CHARACTER_JOB_NONE && restore_polls==1);
 finished=true;
 assert(companion_character_job_download_tick(h,7,false)==CHARACTER_JOB_RESTORE_DISABLED);
 companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 assert(companion_character_job_download_tick(h,0,false)==CHARACTER_JOB_NONE && restore_starts==2);
 companion_character_job_delete(h);
 // Interrupted local request is canceled on restart, not resent or queried.
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-2\" target=\"initial\"]");
 companion_character_job_reply(h,text,0,utc,&event);companion_character_job_delete(h);
 assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 assert(companion_character_job_download_tick(h,0,false)==CHARACTER_JOB_NONE && restore_starts==2);
 assert(!companion_character_job_take_query(h,60000,false,true,query,sizeof(query)));
 // A fresh ID can restore; successful staging means handoff, never display success.
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-3\" target=\"initial\"]");
 companion_character_job_reply(h,text,0,utc,&event);assert(event==CHARACTER_JOB_STARTED);
 restore_error=ESP_OK;companion_character_job_download_tick(h,1,false);
 assert(companion_character_job_download_tick(h,2,false)==CHARACTER_JOB_RESTORE_HANDOFF);
 companion_character_job_delete(h);assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);
 assert(companion_character_job_download_tick(h,0,false)==CHARACTER_JOB_NONE && restore_starts==3);
 strcpy(text,"[CHARACTER_PACK job=\"post-restore\"]");
 companion_character_job_reply(h,text,0,utc,&event);assert(event==CHARACTER_JOB_STARTED);
 // An in-progress generation cannot be replaced by a restore.
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-4\" target=\"initial\"]");
 companion_character_job_reply(h,text,1,utc,&event);assert(event==CHARACTER_JOB_REJECTED);
 companion_character_job_delete(h);
 exists=false;assert(companion_character_job_create(&cfg,0,utc,&h)==ESP_OK);save_error=ESP_FAIL;
 strcpy(text,"[CHARACTER_RESTORE job=\"restore-4\" target=\"initial\"]");
 companion_character_job_reply(h,text,0,utc,&event);assert(event==CHARACTER_JOB_RESTORE_FAILED);
 assert(companion_character_job_download_tick(h,1,false)==CHARACTER_JOB_NONE && restore_starts==3);
 companion_character_job_delete(h);save_error=0;
 return 0;
}
''', encoding="utf-8")
    executable = tmp_path / "job_test.exe"
    subprocess.run([compiler, "-Wall", "-Wextra", "-Werror", "-std=c11", "-I", str(tmp_path),
        "-I", str(main), str(source), str(main / "companion_character_job.c"),
        str(main / "companion_character_protocol.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
