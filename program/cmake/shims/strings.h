/* Shim for platforms without <strings.h> (Windows). */
#pragma once
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif
