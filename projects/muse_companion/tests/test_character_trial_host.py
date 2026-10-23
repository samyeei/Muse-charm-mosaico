"""Run actual boot-owner C logic against platform faults; no hardware claims."""
from pathlib import Path
import shutil
import subprocess
import pytest


@pytest.mark.parametrize("admitted", [False, True, "generic"])
def test_character_trial_boot_and_confirmation(tmp_path, admitted):
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    main = Path(__file__).parents[1] / "main"
    component = Path(__file__).parents[3] / "submodule/esp-mosaico-utils/esp-mosaico-vibe-mode/components/esp_mosaico_character_journal"
    headers = {
        "esp_err.h": """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_INVALID_CRC 6
#define ESP_ERR_NOT_SUPPORTED 7
#define ESP_ERR_TIMEOUT 8
""",
        "esp_gsp.h": """
#pragma once
#include <stdint.h>
typedef void *esp_gsp_handle_t;
typedef int esp_gsp_err_t;
typedef int gsp_err_t;
#define ESP_GSP_OK 0
#define GSP_OK 0
int esp_gsp_flush(void *, uint32_t);
void esp_gsp_render_error_stats(void *,uint32_t *,gsp_err_t *);
""",
        "mosaico_nand_store.h": """
#include "esp_err.h"
#define MOSAICO_NAND_MOUNT_PATH "/nand"
esp_err_t mosaico_nand_store_mount(void);
""",
        "esp_partition.h": """
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t address,size;int type,subtype;} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define ESP_PARTITION_SUBTYPE_ANY 0
const esp_partition_t *esp_partition_find_first(int,int,const char *);
esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);
""",
        "esp_ota_ops.h": """
#include "esp_partition.h"
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
const esp_partition_t *esp_ota_get_running_partition(void);
""",
        "psa/crypto.h": """
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT 0
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
int psa_crypto_init(void);
int psa_hash_setup(int *,int);
int psa_hash_update(int *,const uint8_t *,size_t);
int psa_hash_finish(int *,uint8_t *,size_t,size_t *);
int psa_hash_abort(int *);
""",
    }
    for name, content in headers.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    source = tmp_path / "trial_test.c"
    source.write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "companion_character_trial.c"
static esp_partition_t part={0xd00000,0x300000,1,0};
static esp_partition_t app={0x210000,0xaf0000,0,16};
static esp_partition_t factory={0x20000,0x1c0000,0,0};
static int boot_error,boot_selections;
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{assert(p==&app || p==&factory);++boot_selections;return boot_error;}
const esp_partition_t *esp_ota_get_running_partition(void){return &app;}
static int mount_error,load_error=ESP_ERR_NOT_FOUND,save_error,flush_error,read_error;
static uint32_t render_failures;
static gsp_err_t render_last;
static int saves,reads,flushes,store_deletes;
static mosaico_character_record_t disk;
esp_err_t mosaico_nand_store_mount(void) {return mount_error;}
esp_err_t mosaico_character_journal_store_create(const char *p,mosaico_character_journal_store_handle_t *o)
{(void)p;*o=(void *)1;return ESP_OK;}
void mosaico_character_journal_store_delete(mosaico_character_journal_store_handle_t h)
{if(h)++store_deletes;}
esp_err_t mosaico_character_journal_store_load(mosaico_character_journal_store_handle_t h,mosaico_character_record_t *o)
{(void)h;*o=disk;return load_error;}
esp_err_t mosaico_character_journal_store_save(mosaico_character_journal_store_handle_t h,const mosaico_character_record_t *r)
{(void)h;++saves;if(!save_error)disk=*r;return save_error;}
const esp_partition_t *esp_partition_find_first(int t,int s,const char *n)
{(void)s;(void)n;return t==ESP_PARTITION_TYPE_APP ? &factory : &part;}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t o,void *b,size_t n)
{(void)p;(void)o;memset(b,0,n);++reads;return read_error;}
int psa_crypto_init(void){return 0;}
int psa_hash_setup(int *p,int a){(void)p;(void)a;return 0;}
int psa_hash_update(int *p,const uint8_t *b,size_t n){(void)p;(void)b;(void)n;return 0;}
int psa_hash_finish(int *p,uint8_t *b,size_t n,size_t *z)
{(void)p;memset(b,0,n);b[0]=1;*z=32;return 0;}
int psa_hash_abort(int *p){(void)p;return 0;}
int esp_gsp_flush(void *u,uint32_t ms){(void)u;assert(ms==5000);++flushes;return flush_error;}
void esp_gsp_render_error_stats(void *u,uint32_t *f,gsp_err_t *e)
{(void)u;*f=render_failures;*e=render_last;}
static void seed(void)
{
 memset(&disk,0,sizeof(disk));disk.sequence=4;disk.phase=MOSAICO_CHARACTER_TRIAL;
 disk.partition_address=part.address;disk.partition_bytes=part.size;disk.image_bytes=9000;
 disk.image_sha256[0]=1;disk.backup_sha256[0]=2;
 strcpy(disk.job,"test-job");strcpy(disk.compat_version,"candidate");strcpy(disk.character_version,"test-role");
}
int main(void)
{
 companion_character_trial_handle_t h=NULL;
 assert(companion_character_trial_create(&h)==ESP_OK && h && !h->trial);
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_OK && saves==0 && flushes==0);
 companion_character_trial_delete(h);
 mount_error=ESP_FAIL;
 assert(companion_character_trial_create(&h)==ESP_FAIL && !h);mount_error=0;
 mount_error=ESP_ERR_NOT_FOUND;
 assert(companion_character_trial_create(&h)==ESP_ERR_NOT_FOUND && !h);mount_error=0;
 load_error=ESP_ERR_INVALID_CRC;
 assert(companion_character_trial_create(&h)==ESP_ERR_INVALID_CRC && !h);
 seed();load_error=0;
#ifdef TEST_ADMITTED
#ifdef TEST_RESTRICTED
 strcpy(disk.job,"wrong-job");
 assert(companion_character_trial_create(&h)==ESP_ERR_NOT_SUPPORTED && !h && saves==0 && reads==0);
 seed();
#else
 strcpy(disk.job,"fresh-voice-job");
#endif
 assert(companion_character_trial_create(&h)==ESP_OK && h && h->trial && reads>0);
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_OK && disk.phase==MOSAICO_CHARACTER_CONFIRMED);
 companion_character_trial_delete(h);h=NULL;
 seed();saves=reads=flushes=0;
#else
 assert(companion_character_trial_create(&h)==ESP_ERR_NOT_SUPPORTED && !h && saves==0 && reads==0);
#endif
 strcpy(disk.compat_version,MOSAICO_CHARACTER_INITIAL_COMPAT);
 strcpy(disk.character_version,MOSAICO_CHARACTER_INITIAL_VERSION);
 assert(mosaico_character_record_is_initial(&disk));
 assert(companion_character_trial_create(&h)==ESP_OK && h && h->trial && reads>0 && saves==0);
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_OK && disk.phase==MOSAICO_CHARACTER_CONFIRMED);
 companion_character_trial_delete(h);h=NULL;
 saves=reads=flushes=0;
 disk.phase=MOSAICO_CHARACTER_WRITING;
 assert(companion_character_trial_create(&h)==ESP_ERR_INVALID_STATE && !h);
 disk.phase=MOSAICO_CHARACTER_CONFIRMED;
 assert(companion_character_trial_create(&h)==ESP_OK && h);companion_character_trial_delete(h);
 disk.partition_address=0;
 assert(companion_character_trial_create(&h)==ESP_ERR_INVALID_STATE && !h);
 /* Simulate a future already-admitted trial owner, without changing the
  * production compatibility gate. Exercise confirmation side-effect order. */
 seed();h=calloc(1,sizeof(*h));assert(h);h->trial=true;h->record=disk;h->partition=&part;h->store=(void *)1;
 flush_error=1;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_ERR_TIMEOUT && saves==0 && reads==0);
 flush_error=0;render_failures=1;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_FAIL && saves==0 && reads==0);
 render_failures=0;render_last=1;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_FAIL && saves==0);
 render_last=0;read_error=ESP_FAIL;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_FAIL && saves==0);
 read_error=0;h->record.image_sha256[0]=3;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_ERR_INVALID_CRC && saves==0);
 h->record.image_sha256[0]=1;
 boot_error=ESP_FAIL;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_FAIL && saves==0);
 boot_error=0;
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_OK && saves==1);
 assert(disk.phase==MOSAICO_CHARACTER_CONFIRMED && disk.sequence==5);
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_OK && saves==1);
 companion_character_trial_delete(h);assert(store_deletes>0);
 seed();disk.phase=MOSAICO_CHARACTER_ROLLED_BACK;disk.backup_sha256[0]=1;
 int previous_saves=saves,previous_reads=reads;
 assert(companion_character_trial_create(&h)==ESP_OK && h && h->restored && h->trial);
 assert(disk.phase==MOSAICO_CHARACTER_ROLLBACK_TRIAL && saves==previous_saves+1);
 assert(reads==previous_reads+(int)(part.size/4096));
 assert(companion_character_trial_confirm(h,(void *)1)==ESP_OK);
 assert(disk.phase==MOSAICO_CHARACTER_ROLLBACK_CONFIRMED && disk.sequence==6);
 companion_character_trial_delete(h);
 seed();disk.phase=MOSAICO_CHARACTER_ROLLBACK_TRIAL;disk.backup_sha256[0]=1;
 assert(companion_character_trial_create(&h)==ESP_OK && h->restored);companion_character_trial_delete(h);
 seed();disk.phase=MOSAICO_CHARACTER_ROLLED_BACK;disk.backup_sha256[0]=3;
 assert(companion_character_trial_create(&h)==ESP_ERR_INVALID_CRC && !h);
 return 0;
}
''', encoding="utf-8")
    executable = tmp_path / "trial_test.exe"
    defines = ['-DTEST_ADMITTED=1', '-DMOSAICO_CHARACTER_INSTALL_COMPAT="candidate"',
                ] if admitted else []
    if admitted is True:
        defines += ['-DTEST_RESTRICTED=1', '-DMOSAICO_CHARACTER_INSTALL_JOB="test-job"']
    subprocess.run([compiler, *defines, "-Wall", "-Wextra", "-Werror", "-std=c11",
        "-I", str(tmp_path), "-I", str(main), "-I", str(component / "include"),
        str(source), str(component / "character_journal.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
