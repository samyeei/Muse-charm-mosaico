"""Actual staging coordinator C, platform fault injection; public candidate gate stays closed."""
from pathlib import Path
import shutil
import subprocess
import pytest


@pytest.mark.parametrize("admitted", [False, True, "generic"])
def test_character_staging_gate_and_durable_handoff(tmp_path, admitted):
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
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
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP_ERR_TIMEOUT 8
""",
        "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
        "esp_partition.h": """
#pragma once
#include <stdint.h>
typedef struct {uint32_t address,size;} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0
const esp_partition_t *esp_partition_find_first(int,int,const char *);
""",
        "mosaico_nand_store.h": """
#include "esp_err.h"
#include <stddef.h>
#define MOSAICO_NAND_MOUNT_PATH "/nand"
esp_err_t mosaico_nand_store_usage(size_t *,size_t *);
""",
        "iris_ota_support.h": """
#include "esp_err.h"
esp_err_t iris_ota_support_enter_recovery(void);
""",
    }
    for name, text in headers.items():
        (tmp_path / name).write_text(text, encoding="utf-8")
    source = tmp_path / "stage_test.c"
    source.write_text(r'''
#include <assert.h>
#include <string.h>
#include "companion_character_stage.c"
static esp_partition_t part={0xd00000,0x300000};
static mosaico_character_record_t disk;
static bool exists,bad_readback,publish_on_failed_save;
static int verify_error,load_error,backup_error,save_error,enter_error;
static int verifications,backups,initials,saves,entries,loads;
static size_t available=0x1000000;
static int64_t clock_us;
int64_t esp_timer_get_time(void){return clock_us;}
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name)
{(void)type;(void)subtype;assert(!strcmp(name,"ui_apps"));return &part;}
esp_err_t companion_character_download_verify(const character_command_t *c,companion_cpack_info_t *image)
{assert(c->status==CHARACTER_READY);++verifications;if(verify_error)return verify_error;image->image_size=123456;memset(image->image_sha256,0x51,32);return ESP_OK;}
esp_err_t companion_character_backup_initial(void)
{++backups;return backup_error;}
esp_err_t companion_character_backup_prepare_restore(companion_character_initial_info_t *info)
{++initials;if(backup_error)return backup_error;memset(info,0,sizeof(*info));
 info->partition_address=part.address;info->partition_bytes=part.size;
 info->image_bytes=123456;memset(info->image_sha256,0x51,32);return ESP_OK;}
esp_err_t mosaico_nand_store_usage(size_t *total,size_t *used)
{*total=available;*used=0;return ESP_OK;}
esp_err_t mosaico_character_journal_store_create(const char *path,mosaico_character_journal_store_handle_t *out)
{assert(!strcmp(path,"/nand/muse-character"));*out=(void *)1;return ESP_OK;}
void mosaico_character_journal_store_delete(mosaico_character_journal_store_handle_t h)
{(void)h;}
esp_err_t mosaico_character_journal_store_load(mosaico_character_journal_store_handle_t h,mosaico_character_record_t *record)
{(void)h;++loads;if(load_error)return load_error;if(!exists)return ESP_ERR_NOT_FOUND;*record=disk;if(bad_readback && saves)record->image_sha256[0]^=1;return ESP_OK;}
esp_err_t mosaico_character_journal_store_save(mosaico_character_journal_store_handle_t h,const mosaico_character_record_t *record)
{(void)h;++saves;assert((verifications && backups) || initials);assert(record->phase==MOSAICO_CHARACTER_STAGED);if(!save_error||publish_on_failed_save){disk=*record;exists=true;}return save_error;}
esp_err_t iris_ota_support_enter_recovery(void)
{++entries;assert(exists && disk.phase==MOSAICO_CHARACTER_STAGED);return enter_error;}
static void reset(void)
{exists=false;bad_readback=false;publish_on_failed_save=false;verify_error=load_error=backup_error=save_error=enter_error=0;verifications=backups=initials=saves=entries=loads=0;available=0x1000000;memset(&disk,0,sizeof(disk));}
int main(void)
{
 character_command_t command={.kind=CHARACTER_STATUS,.status=CHARACTER_READY};
 strcpy(command.job,"stage-1");strcpy(command.compat_version,COMPANION_CPACK_COMPAT);strcpy(command.character_version,"role-1");
 bool prepared=true;
#ifdef TEST_RESTRICTED
 strcpy(command.job,"wrong-job");
 assert(companion_character_stage(&command,&prepared)==ESP_ERR_NOT_SUPPORTED && !prepared);
 strcpy(command.job,"stage-1");
#elif !defined(TEST_ADMITTED)
 assert(companion_character_stage(&command,&prepared)==ESP_ERR_NOT_SUPPORTED && !prepared);
#endif
 assert(!verifications && !backups && !saves && !entries); // production gate: no side effects
#ifdef TEST_ADMITTED
 assert(companion_character_stage(&command,&prepared)==ESP_OK && prepared);
 assert(saves==1 && entries==1 && verifications==1 && backups==1);
 reset();
#endif
 reset();command.deadline_ms=100;clock_us=100000;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_TIMEOUT && !prepared);
 assert(!saves && !entries);
 command.deadline_ms=0;clock_us=0;reset();
 // Private admitted workflow tests exercise the actual implementation; they
 // do not open the production policy gate or perform any physical writes.
#ifdef TEST_ADMITTED
#ifndef TEST_RESTRICTED
 strcpy(command.job,"another-real-job");
 assert(companion_character_stage(&command,&prepared)==ESP_OK && prepared);
 reset();strcpy(command.job,"stage-1");
 strcpy(command.compat_version,"incompatible");
 assert(companion_character_stage(&command,&prepared)==ESP_ERR_NOT_SUPPORTED && !prepared && !saves && !entries);
 strcpy(command.compat_version,COMPANION_CPACK_COMPAT);
#endif
#endif
 assert(stage_prepare_handoff(&command,&prepared)==ESP_OK && prepared);
 assert(saves==1 && entries==1 && loads==2 && disk.sequence==1);
 assert(stage_prepare_handoff(&command,&prepared)==ESP_OK && prepared);
 assert(saves==1 && entries==2); // same staged task resumes without another save
 strcpy(command.job,"stage-2");
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_INVALID_STATE && !prepared);
 strcpy(command.job,"stage-1");
 reset();verify_error=ESP_FAIL;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_FAIL && !prepared && !backups && !saves && !entries);
 reset();load_error=ESP_FAIL;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_FAIL && !prepared && !backups && !entries);
 reset();backup_error=ESP_FAIL;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_FAIL && !prepared && !saves && !entries);
 reset();available=part.size;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_NO_MEM && !prepared && !saves && !entries);
 reset();save_error=ESP_FAIL;publish_on_failed_save=true;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_FAIL && prepared && exists && !entries);
 reset();save_error=ESP_FAIL;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_FAIL && prepared && !entries); // uncertain publication retains ownership
 reset();bad_readback=true;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_INVALID_STATE && prepared && !entries);
 reset();enter_error=ESP_FAIL;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_FAIL && prepared && saves==1 && entries==1);
 // A later job is allowed only after confirmed/canceled prior history.
 reset();assert(stage_prepare_handoff(&command,&prepared)==ESP_OK);
 disk.phase=MOSAICO_CHARACTER_WRITING;memset(disk.backup_sha256,0x44,32);
 strcpy(command.job,"stage-2");
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_INVALID_STATE && !prepared && saves==1);
 disk.phase=MOSAICO_CHARACTER_ROLLBACK_TRIAL;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_INVALID_STATE && !prepared && saves==1);
 disk.phase=MOSAICO_CHARACTER_ROLLBACK_CONFIRMED;
 assert(stage_prepare_handoff(&command,&prepared)==ESP_OK && prepared && disk.sequence==2 && saves==2);
 disk.phase=MOSAICO_CHARACTER_CONFIRMED;disk.sequence=UINT64_MAX;
 strcpy(command.job,"stage-3");
 assert(stage_prepare_handoff(&command,&prepared)==ESP_ERR_INVALID_STATE && !prepared && saves==2);
 reset();assert(companion_character_stage_initial("restore-1",&prepared)==ESP_OK && prepared);
 assert(initials==1 && !backups && !verifications && entries==1 && saves==1);
 assert(mosaico_character_record_is_initial(&disk));
 assert(stage_initial_handoff("restore-1",&prepared)==ESP_OK && prepared && saves==1);
 assert(initials==1); // committed source is never re-extracted on resume
 int prior_initials=initials;
 assert(stage_initial_handoff("restore-2",&prepared)==ESP_ERR_INVALID_STATE && !prepared);
 assert(initials==prior_initials); // reject before replacing a pending source
 disk.phase=MOSAICO_CHARACTER_WRITING;memset(disk.backup_sha256,0x44,32);
 assert(stage_initial_handoff("restore-1",&prepared)==ESP_ERR_INVALID_STATE && initials==prior_initials);
 reset();backup_error=ESP_ERR_NOT_FOUND;
 assert(stage_initial_handoff("restore-1",&prepared)==ESP_ERR_NOT_FOUND && !prepared && !saves && !entries && !backups);
 reset();save_error=ESP_FAIL;publish_on_failed_save=true;
 assert(stage_initial_handoff("restore-1",&prepared)==ESP_FAIL && prepared && !entries);
 reset();load_error=ESP_FAIL;
 assert(stage_initial_handoff("restore-1",&prepared)==ESP_FAIL && !initials && !saves);
 return 0;
}
''', encoding="utf-8")
    executable = tmp_path / "stage_test.exe"
    defines = ['-DTEST_ADMITTED=1', '-DMOSAICO_CHARACTER_INSTALL_COMPAT=COMPANION_CPACK_COMPAT',
                ] if admitted else []
    if admitted is True:
        defines += ['-DTEST_RESTRICTED=1', '-DMOSAICO_CHARACTER_INSTALL_JOB="stage-1"']
    subprocess.run([compiler, *defines, "-Wall", "-Wextra", "-Werror", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
        "-I", str(tmp_path), "-I", str(main), "-I", str(component / "include"),
        str(source), str(component / "character_journal.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
