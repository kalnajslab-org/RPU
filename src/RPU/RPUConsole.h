/*
 * RPUConsole.h
 *
 * USB serial console input and command dispatch for the RPU.
 *
 * consoleRead() accumulates characters into a line buffer, tokenizes each
 * completed line on spaces and commas (converting to lowercase), and
 * dispatches on the first token.  Supported commands: h (help), m (MEASURE),
 * s (STANDBY), c <s> (console interval), r <s> (report interval).
 *
 * Interval changes are applied via RPUStatus setters so no interval state
 * is owned or passed by the caller.
 */
#pragma once
#include "RPUTypes.h"

void consoleRead(RPUState& rpu_state);
