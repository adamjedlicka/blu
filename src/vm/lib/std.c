#include "std.h"

#include "vm/lib/core/core.h"
#include "vm/lib/system/system.h"

void bluInitStd(bluVM* vm) {
	bluInitCore(vm);
	bluInitSystem(vm);
}
