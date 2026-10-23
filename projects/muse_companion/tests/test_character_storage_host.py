"""Actual storage dispatcher/worker C: serialization, tokens and USB admission."""
from pathlib import Path
import shutil
import subprocess


def test_restore_worker_and_maintenance_dispatch(tmp_path):
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    main = Path(__file__).parents[1] / "main"
    headers = {
        "esp_err.h": """
#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP_ERR_NOT_ALLOWED 7
#define ESP_ERR_TIMEOUT 8
""",
        "esp_iris.h": """
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#define ESP_IRIS_TRANSPORT_KIND_USB 1
typedef struct {bool session_ready;int transport;} esp_iris_status_t;
typedef struct {const uint8_t *payload;size_t payload_size;} esp_iris_rpc_request_t;
typedef esp_err_t (*handler_t)(const esp_iris_rpc_request_t *,uint8_t *,size_t,size_t *,void *);
esp_err_t esp_iris_get_status(esp_iris_status_t *);
esp_err_t esp_iris_mark_planned_restart(void);
esp_err_t esp_iris_rpc_register(unsigned,unsigned,handler_t,void *);
""",
        "esp_random.h": "#include <stddef.h>\nvoid esp_fill_random(void *,size_t);\n",
        "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
        "esp_system.h": "void esp_restart(void);\n",
        "freertos/FreeRTOS.h": """
#pragma once
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(n) (n)
""",
        "freertos/semphr.h": """
#pragma once
typedef int StaticSemaphore_t;
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
""",
        "freertos/task.h": """
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,int,void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
""",
        "mosaico_nand_store.h": """
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#define MOSAICO_NAND_MOUNT_PATH "."
typedef struct {
 char filesystem_error[128];bool raw;uint64_t logical_bytes;unsigned page_bytes;
 int sample_result[2];unsigned sample_non_ff[2],sample_zero[2];
 unsigned raw_blocks_checked,raw_blocks_non_ff,raw_blocks_read_errors,sample_format[2],denied_writes;
 char sample_prefix_hex[2][65];
} mosaico_nand_diagnostics_t;
esp_err_t mosaico_nand_store_usage(size_t *,size_t *);
esp_err_t mosaico_nand_store_mount(void);
esp_err_t mosaico_nand_store_sync(void);
esp_err_t mosaico_nand_store_initialize_empty(void);
esp_err_t mosaico_nand_store_readonly_bad_blocks(uint32_t,uint8_t *,uint32_t);
esp_err_t mosaico_nand_store_readonly_read(bool,uint32_t,uint8_t *,uint32_t);
const mosaico_nand_diagnostics_t *mosaico_nand_store_diagnostics(void);
const char *mosaico_nand_store_mount_stage(void);
""",
    }
    for name, text in headers.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    source = tmp_path / "storage_test.c"
    source.write_text(r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define fsync _commit
#define mkdir(path,mode) _mkdir(path)
#endif
#include "companion_character_storage.c"
static void (*queued)(void *);static void *argument;
static int task_fail,mutex_busy,task_calls,stage_calls,usage_calls;
static int downloaded_stages;
static int64_t clock_us;
int64_t esp_timer_get_time(void){return clock_us;}
static int session_transport=1;
static bool session_ready=true;
static int stage_error=ESP_ERR_NOT_SUPPORTED;static bool stage_prepared;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){return s;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned t){(void)h;(void)t;return !mutex_busy;}
void xSemaphoreGive(SemaphoreHandle_t h){(void)h;}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned bytes,void *arg,int pri,void *handle)
{(void)n;(void)pri;(void)handle;assert(bytes>=2048);++task_calls;if(task_fail)return 0;queued=fn;argument=arg;return 1;}
void vTaskDelete(void *p){assert(!p);}
void vTaskDelay(unsigned t){(void)t;}
void esp_fill_random(void *p,size_t n){memset(p,1,n);}
void esp_restart(void){}
esp_err_t esp_iris_get_status(esp_iris_status_t *s){s->session_ready=session_ready;s->transport=session_transport;return ESP_OK;}
esp_err_t esp_iris_mark_planned_restart(void){return ESP_OK;}
esp_err_t esp_iris_rpc_register(unsigned s,unsigned m,handler_t fn,void *ctx)
{assert(s==0x7e12 && m==1 && fn==storage_rpc && !ctx);return ESP_OK;}
esp_err_t mosaico_nand_store_usage(size_t *t,size_t *u){++usage_calls;*t=10000000;*u=3000000;return ESP_OK;}
esp_err_t mosaico_nand_store_mount(void){return ESP_OK;}
esp_err_t mosaico_nand_store_sync(void){return ESP_OK;}
esp_err_t mosaico_nand_store_initialize_empty(void){return ESP_OK;}
esp_err_t mosaico_nand_store_readonly_bad_blocks(uint32_t a,uint8_t *b,uint32_t n){(void)a;(void)b;(void)n;return ESP_OK;}
esp_err_t mosaico_nand_store_readonly_read(bool a,uint32_t b,uint8_t *c,uint32_t d){(void)a;(void)b;(void)c;(void)d;return ESP_OK;}
const mosaico_nand_diagnostics_t *mosaico_nand_store_diagnostics(void){static mosaico_nand_diagnostics_t d;return &d;}
const char *mosaico_nand_store_mount_stage(void){return "mounted";}
esp_err_t companion_character_backup_initial(void){return ESP_OK;}
esp_err_t companion_character_stage_initial(const char *job,bool *prepared)
{assert(!strcmp(job,restore_job));++stage_calls;*prepared=stage_prepared;return stage_error;}
esp_err_t companion_character_stage(const character_command_t *c,bool *p){(void)c;++downloaded_stages;*p=false;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t companion_character_download(const character_command_t *c,companion_character_download_result_t *r)
{(void)c;(void)r;return ESP_OK;}
character_command_kind_t companion_character_extract(char *t,character_command_t *c)
{(void)t;(void)c;return CHARACTER_INVALID;}
static esp_err_t rpc(unsigned method,const char *text)
{uint8_t payload[256],response[2048];size_t out;payload[0]=(uint8_t)method;
 size_t bytes=1;if(text){bytes+=strlen(text);memcpy(payload+1,text,bytes-1);}
 esp_iris_rpc_request_t request={.payload=payload,.payload_size=bytes};
 return storage_rpc(&request,response,sizeof(response),&out,NULL);}
static void complete(void)
{assert(queued);void (*fn)(void *)=queued;void *arg=argument;queued=NULL;argument=NULL;fn(arg);}
int main(void)
{
 assert(companion_character_storage_start()==ESP_OK);
 uint64_t token=0,duplicate=0;bool finished;esp_err_t result;
 assert(companion_character_storage_restore_start("bad job",&token)==ESP_ERR_INVALID_ARG && !task_calls);
 task_fail=1;assert(companion_character_storage_restore_start("restore-1",&token)==ESP_ERR_NO_MEM);
 assert(!restore_token && restore_state==BACKUP_NONE);task_fail=0;
 char request_job[]="restore-1";
 assert(companion_character_storage_restore_start(request_job,&token)==ESP_OK && token==1);
 request_job[0]='X'; // caller buffer can change immediately; worker uses its copy
 int prior=task_calls;
 assert(companion_character_storage_restore_start("restore-1",&duplicate)==ESP_OK && duplicate==token && task_calls==prior);
 assert(companion_character_storage_restore_start("restore-2",&duplicate)==ESP_ERR_INVALID_STATE);
 character_command_t cmd={.kind=CHARACTER_STATUS,.status=CHARACTER_READY};
 assert(companion_character_storage_download_start(NULL,&cmd,&duplicate)==ESP_ERR_INVALID_STATE);
 assert(rpc(WRITE_PROBE,NULL)==ESP_ERR_INVALID_STATE);
 assert(rpc(RESTORE_STATUS,NULL)==ESP_OK && rpc(STATUS,NULL)==ESP_OK);
 assert(rpc(RESTORE_INITIAL,"RESTORE_INITIAL_V1 restore-1")==ESP_OK && task_calls==prior);
 assert(companion_character_storage_restore_poll(token,&finished,&result)==ESP_OK && !finished);
 assert(companion_character_storage_restore_poll(2,&finished,&result)==ESP_ERR_NOT_FOUND);
 complete();assert(stage_calls==1 && !usage_calls && !restarting);
 assert(companion_character_storage_restore_poll(token,&finished,&result)==ESP_OK && finished && result==ESP_ERR_NOT_SUPPORTED);
 assert(companion_character_storage_restore_start("restore-1",&duplicate)==ESP_OK && task_calls==prior);
 session_transport=2;assert(rpc(RESTORE_INITIAL,"RESTORE_INITIAL_V1 restore-3")==ESP_ERR_NOT_ALLOWED);
 session_transport=1;session_ready=false;
 assert(rpc(RESTORE_STATUS,NULL)==ESP_ERR_NOT_ALLOWED);session_ready=true;
 assert(rpc(RESTORE_INITIAL,"restore-3")==ESP_ERR_INVALID_ARG);
 assert(rpc(RESTORE_INITIAL,"RESTORE_INITIAL_V1 bad/job")==ESP_ERR_INVALID_ARG);
 assert(rpc(RESTORE_STATUS,"extra")==ESP_ERR_INVALID_ARG);
 mutex_busy=1;assert(companion_character_storage_restore_start("restore-3",&duplicate)==ESP_ERR_INVALID_STATE);mutex_busy=0;
 assert(rpc(RESTORE_INITIAL,"RESTORE_INITIAL_V1 restore-3")==ESP_OK && restore_token==2);
 stage_prepared=true;stage_error=ESP_FAIL;complete(); // uncertain journal publication locks mutations
 assert(restarting && !usage_calls);
 assert(rpc(RESTORE_STATUS,NULL)==ESP_OK);
 assert(companion_character_storage_restore_poll(2,&finished,&result)==ESP_OK && finished && result==ESP_FAIL);
 assert(rpc(WRITE_PROBE,NULL)==ESP_ERR_INVALID_STATE && rpc(SYNC_REBOOT,NULL)==ESP_ERR_INVALID_STATE);
 assert(companion_character_storage_restore_start("restore-4",&duplicate)==ESP_ERR_INVALID_STATE);
 assert(companion_character_storage_download_start(NULL,&cmd,&duplicate)==ESP_ERR_INVALID_STATE);
 // A timed-out downloader must not stage, and storage remains busy until worker completion.
 restarting=false;
 cmd.deadline_ms=100;clock_us=100000;
 assert(companion_character_storage_download_start(NULL,&cmd,&token)==ESP_OK);
 assert(companion_character_storage_download_start(NULL,&cmd,&duplicate)==ESP_ERR_INVALID_STATE);
 complete();
 assert(download_result==ESP_ERR_TIMEOUT && !downloaded_stages && !download_handoff_pending);
 // Voice downloads must report rejected admission as failure, not diagnostic success.
 clock_us=0;cmd.deadline_ms=100;
 assert(companion_character_storage_download_start(NULL,&cmd,&token)==ESP_OK);
 complete();assert(download_result==ESP_ERR_NOT_SUPPORTED && downloaded_stages==1);
 return 0;
}
''', encoding="utf-8")
    executable = tmp_path / "storage_test.exe"
    subprocess.run([compiler, "-Wall", "-Wextra", "-Werror", "-std=c11",
                    "-I", str(tmp_path), "-I", str(main), str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=tmp_path, check=True)
