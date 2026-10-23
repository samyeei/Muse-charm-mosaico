#include "companion_character_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    character_command_t command;
    char message[4096];
    strcpy(message, "Hello. [CHARACTER_PACK job=\"cpack-001\"] Wait a moment.");
    assert(companion_character_extract(message, &command) == CHARACTER_PACK);
    assert(strcmp(command.job, "cpack-001") == 0);
    assert(strcmp(message, "Hello.  Wait a moment.") == 0);
    strcpy(message, "[CHARACTER_STATUS job=\"cpack-001\" status=\"building\"]");
    assert(companion_character_extract(message, &command) == CHARACTER_STATUS);
    assert(command.status == CHARACTER_BUILDING && message[0] == 0);
    strcpy(message, "[CHARACTER_STATUS job=\"cpack-001\" status=\"building\" error=\"\"]");
    assert(companion_character_extract(message, &command) == CHARACTER_STATUS);
    assert(command.status == CHARACTER_BUILDING && message[0] == 0);
    strcpy(message, "[CHARACTER_STATUS job=\"cpack-001\" status=\"building\" error=\"generation failed\"]");
    assert(companion_character_extract(message, &command) == CHARACTER_INVALID);
    strcpy(message, "[CHARACTER_STATUS job=\"cpack-001\" status=\"failed\" error=\"unknown job\"]");
    assert(companion_character_extract(message, &command) == CHARACTER_STATUS);
    assert(command.status == CHARACTER_FAILED);
    const char *ready = "[CHARACTER_STATUS job=\"cpack-001\" status=\"ready\" pack_url=\"https://muse.ai/files/a.cpack\" sha256=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" size=\"2480324\" expires_at=\"2026-10-07T05:19:17Z\" character_version=\"guinea-v4\" compat_version=\"compat-1\" error=\"\"]";
    strcpy(message, ready);
    assert(companion_character_extract(message, &command) == CHARACTER_STATUS);
    assert(command.status == CHARACTER_READY && command.size == 2480324);
    assert(!companion_character_url_allowed("https://muse.ai.evil.test/files/a"));
    assert(!companion_character_url_allowed("https://muse.ai@evil.test/files/a"));
    assert(!companion_character_url_allowed("http://muse.ai/files/a"));
    assert(!companion_character_url_allowed("https://muse.ai:443/files/a"));
    assert(!companion_character_url_allowed("https://muse.ai/files/a#b"));
    strcpy(message, "OK [CHARACTER_PACK job=\"a\" job=\"b\"] done");
    assert(companion_character_extract(message, &command) == CHARACTER_INVALID);
    assert(strcmp(message, "OK  done") == 0);
    strcpy(message, "[CHARACTER_PACK job=\"a\"][CHARACTER_PACK job=\"b\"]");
    assert(companion_character_extract(message, &command) == CHARACTER_INVALID && message[0] == 0);
    strcpy(message, "OK [CHARACTER_PACK job=\"a\"");
    assert(companion_character_extract(message, &command) == CHARACTER_INVALID && strcmp(message, "OK ") == 0);
    strcpy(message, ready);
    char *date = strstr(message, "2026-10-07");
    memcpy(date, "2026-02-30", 10);
    assert(companion_character_extract(message, &command) == CHARACTER_INVALID);
    strcpy(message, "[CHARACTER_STATUS job=\"a\" status=\"building\" pack_url=\"https://muse.ai/a\"]");
    assert(companion_character_extract(message, &command) == CHARACTER_INVALID);
    strcpy(message, "Ordinary text remains unchanged.");
    assert(companion_character_extract(message, &command) == CHARACTER_NONE);
    assert(strcmp(message, "Ordinary text remains unchanged.") == 0);
    puts("character protocol boundary tests PASS");
    return 0;
}
