/* project_check.hpp - internal: ce_project_validate with the settings the
 * machine will run with. Not part of the ABI. */

#pragma once

#include "chimera/engine.h"
#include "../../extern/cjson/cJSON.h"

/* ce_project_validate, with the slots' exposedWhen conditions read against
 * these settings (a JSON object) rather than the project's own; NULL reads the
 * project's own. ce_session_open_project passes the package's defaults with
 * the project's settings on top, so a project that leaves a setting out is
 * judged by the value the machine will boot with. */
extern "C" int32_t ceProjectValidate(const ce_project *p, const char *slots_json, uint64_t slots_len,
	const cJSON *settings, const char **error_out);
