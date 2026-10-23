// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef struct companion_bridge *companion_bridge_handle_t;
esp_err_t companion_bridge_create(companion_bridge_handle_t *out);
esp_err_t companion_bridge_start(companion_bridge_handle_t bridge);
/* These functions run only on the existing Muse voice worker. */
const char *companion_bridge_take(companion_bridge_handle_t bridge, bool ready, bool idle);
bool companion_bridge_cancelled(companion_bridge_handle_t bridge);
void companion_bridge_finish(companion_bridge_handle_t bridge, const char *reply, const char *error);
void companion_bridge_interrupt(companion_bridge_handle_t bridge);
