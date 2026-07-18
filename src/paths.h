#ifndef HDDSYNTH_PATHS_H
#define HDDSYNTH_PATHS_H

#include <stddef.h>

// Directory the running .exe lives in (trailing backslash included).
// Resolved once via GetModuleFileNameA rather than relying on the
// current working directory -- CWD only happens to match the exe's
// folder when launched by double-clicking in Explorer; a shortcut with
// a different "Start in" folder, or a Start Menu entry, would break any
// path built from a bare relative string.
void GetExeDir(char *buf, size_t bufSize);

// Combines the exe directory with a relative path (e.g. "samples" or
// "hddsynth.ini") into an absolute path.
void BuildExePath(char *out, size_t outSize, const char *relative);

#endif
