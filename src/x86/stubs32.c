// Parts of the 64-bit DLL that the 32-bit build does not have yet; each stub is replaced by the real thing as
// that part is ported (docs/32bit-plan.md), and its source file moves into the x86 line of build.sh.
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "scrdebug.h"
