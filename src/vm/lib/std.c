#include "std.h"

#include "vm/lib/core/core.h"
#include "vm/lib/file/file.h"
#include "vm/lib/math/math.h"
#include "vm/lib/system/system.h"

void bluInitStd(bluVM* vm) {
	bluInitCore(vm);

	bluRegisterModule(vm, "system", bluInitSystem);
	bluRegisterModule(vm, "math", bluInitMath);
	bluRegisterModule(vm, "file", bluInitFile);
}
