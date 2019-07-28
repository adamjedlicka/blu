#include "std.h"

#include "vm/lib/core/core.h"
#include "vm/lib/math/math.h"
#include "vm/lib/system/system.h"

void bluInitStd(bluVM* vm) {
	bluInitCore(vm);
	bluInitMath(vm);
	bluInitSystem(vm);
}
