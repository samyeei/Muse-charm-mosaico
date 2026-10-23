"""Actual backup/restore preparation C with filesystem and platform faults.

PSA and native GSP are test doubles; this tests orchestration and corruption
handling, not the cryptographic implementation or device NAND persistence.
"""
from pathlib import Path
import shutil
import subprocess


def test_protected_initial_restore_preparation(tmp_path):
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    main = Path(__file__).parents[1] / "main"
    component = Path(__file__).parents[3] / "submodule/esp-mosaico-utils/esp-mosaico-vibe-mode/components/esp_mosaico_character_journal"
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
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP_ERR_INVALID_CRC 7
""",
        "esp_partition.h": """
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t address,size;} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0
const esp_partition_t *esp_partition_find_first(int,int,const char *);
esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);
""",
        "mosaico_nand_store.h": """
#include "esp_err.h"
#include <stddef.h>
#define MOSAICO_NAND_MOUNT_PATH "."
esp_err_t mosaico_nand_store_usage(size_t *,size_t *);
esp_err_t mosaico_nand_store_sync(void);
""",
        "psa/crypto.h": """
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT 0
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
int psa_crypto_init(void);
int psa_hash_setup(uint32_t *,int);
int psa_hash_update(uint32_t *,const uint8_t *,size_t);
int psa_hash_finish(uint32_t *,uint8_t *,size_t,size_t *);
int psa_hash_abort(uint32_t *);
""",
    }
    for name, text in headers.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    (tmp_path / "muse-character").mkdir()
    source = tmp_path / "initial_test.c"
    source.write_text(r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <windows.h>
#define fsync _commit
#define mkdir(path, mode) _mkdir(path)
/* Windows CRT rename refuses an existing destination; NAND/POSIX replaces it.
 * Model that contract, without treating host behavior as NAND crash evidence. */
static int replace_rename(const char *from,const char *to)
{return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING) ? 0 : -1;}
#define rename replace_rename
#endif
#include "companion_character_backup.c"
static esp_partition_t part={0xd00000,4096};
static size_t available=100000;
static int native_error,sync_error,sync_fail_at,corrupt_partial,reads,native_calls,sync_calls;
static uint8_t original[4096];
const esp_partition_t *esp_partition_find_first(int a,int b,const char *n)
{(void)a;(void)b;assert(!strcmp(n,"ui_apps"));return &part;}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t o,void *b,size_t n)
{(void)p;(void)o;(void)b;(void)n;++reads;return ESP_FAIL;}
esp_err_t mosaico_nand_store_usage(size_t *total,size_t *used)
{*total=available;*used=0;return ESP_OK;}
esp_err_t mosaico_nand_store_sync(void)
{++sync_calls;
 if(corrupt_partial && sync_calls==1){FILE *f=fopen(RESTORE_PATH ".partial","r+b");assert(f);assert(fputc(0,f)!=EOF);assert(!fclose(f));}
 return sync_error || sync_calls==sync_fail_at ? ESP_FAIL : ESP_OK;}
esp_err_t companion_character_image_validate(FILE *f,uint32_t offset,uint32_t bytes)
{assert(f && offset==0 && bytes==128);++native_calls;return native_error;}
int psa_crypto_init(void){return 0;}
int psa_hash_setup(uint32_t *op,int alg){assert(alg==1);*op=2166136261U;return 0;}
int psa_hash_update(uint32_t *op,const uint8_t *b,size_t n)
{for(size_t i=0;i<n;++i)*op=(*op^b[i])*16777619U;return 0;}
int psa_hash_finish(uint32_t *op,uint8_t *b,size_t n,size_t *out)
{assert(n==32);for(unsigned i=0;i<32;++i)b[i]=(uint8_t)(*op>>((i%4)*8));*out=32;return 0;}
int psa_hash_abort(uint32_t *op){(void)op;return 0;}
static void digest(const uint8_t *b,size_t n,uint8_t out[32])
{uint32_t op;size_t size;psa_hash_setup(&op,1);psa_hash_update(&op,b,n);psa_hash_finish(&op,out,32,&size);}
static void write_bytes(const char *path,const void *b,size_t n)
{FILE *f=fopen(path,"wb");assert(f);assert(fwrite(b,1,n,f)==n);assert(!fclose(f));}
static void record_image(void)
{backup_record_t r={.partition_bytes=4096,.partition_address=0xd00000};
 memcpy(r.magic,record_magic,8);digest(original,sizeof(original),r.sha256);
 write_bytes(INITIAL_PATH,original,sizeof(original));write_bytes(RECORD_PATH,&r,sizeof(r));}
static void seed(void)
{memset(original,0xff,sizeof(original));memset(original,0,64);
 const uint8_t magic[8]={'M','O','S','G','S','P',0,1};memcpy(original,magic,8);
 original[8]=1;original[10]=64;original[12]=64;
 digest(original+64,64,original+16);record_image();
 part.size=4096;part.address=0xd00000;available=100000;native_error=sync_error=0;
 native_calls=sync_calls=sync_fail_at=corrupt_partial=0;unlink(RESTORE_PATH);unlink(RESTORE_PATH ".partial");}
static void preserved(void)
{uint8_t b[4096];FILE *f=fopen(INITIAL_PATH,"rb");assert(f);
 assert(fread(b,1,sizeof(b),f)==sizeof(b));assert(!fclose(f));assert(!memcmp(b,original,sizeof(b)));}
int main(void)
{
 companion_character_initial_info_t info;
 assert(companion_character_backup_prepare_restore(NULL)==ESP_ERR_INVALID_ARG);
 memset(&info,0x55,sizeof(info));
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_NOT_FOUND);
 assert(!info.image_bytes && !reads && !native_calls && !sync_calls);
 seed();int result=companion_character_backup_prepare_restore(&info);
 if(result!=ESP_OK)fprintf(stderr,"prepare=%d native=%d sync=%d errno=%d\n",result,native_calls,sync_calls,errno);
 assert(result==ESP_OK);
 assert(info.image_bytes==128 && info.partition_bytes==4096 && info.partition_address==0xd00000);
 assert(native_calls==1 && sync_calls==2 && !reads);preserved();
 FILE *f=fopen(RESTORE_PATH,"rb");uint8_t b[128];assert(f);
 assert(fread(b,1,128,f)==128 && fgetc(f)==EOF && !memcmp(b,original,128));assert(!fclose(f));
 assert(companion_character_backup_prepare_restore(&info)==ESP_OK);preserved();
 seed();part.address+=4096;
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_STATE && !info.image_bytes);
 assert(!native_calls && !sync_calls);preserved();
 seed();part.size+=4096;
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_STATE);preserved();
 seed();original[200]^=1;write_bytes(INITIAL_PATH,original,sizeof(original));
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_CRC);
 assert(!native_calls && !sync_calls);preserved();
 seed();original[64]^=1;record_image(); // full-file hash passes, payload hash fails
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_CRC);
 assert(!native_calls && !sync_calls);preserved();
 seed();original[48]=1;record_image();
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_RESPONSE);preserved();
 seed();original[12]=0xff;original[15]=0xff;record_image();
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_SIZE);preserved();
 seed();native_error=ESP_ERR_INVALID_RESPONSE;
 assert(companion_character_backup_prepare_restore(&info)==native_error && !sync_calls);preserved();
 seed();available=65536;
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_NO_MEM && !sync_calls);preserved();
 seed();sync_error=ESP_FAIL;
 assert(companion_character_backup_prepare_restore(&info)==ESP_FAIL && !info.image_bytes);preserved();
 seed();sync_fail_at=2; // rename has happened: no authorization from output alone
 assert(companion_character_backup_prepare_restore(&info)==ESP_FAIL && !info.image_bytes);preserved();
 seed();corrupt_partial=1;
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_INVALID_CRC && !info.image_bytes);preserved();
 seed();write_bytes(INITIAL_PATH,original,4095);
 assert(companion_character_backup_prepare_restore(&info)==ESP_FAIL && !info.image_bytes);
 seed();unlink(RECORD_PATH);
 assert(companion_character_backup_prepare_restore(&info)==ESP_ERR_NOT_FOUND && !reads);preserved();
 assert(!reads); // restoration never invents a missing backup from active NOR
 return 0;
}
''', encoding="utf-8")
    executable = tmp_path / "initial_test.exe"
    subprocess.run([compiler, "-Wall", "-Wextra", "-Werror", "-std=c11",
                    "-I", str(tmp_path), "-I", str(main), "-I", str(component / "include"),
                    str(source), str(component / "character_journal.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=tmp_path, check=True)
