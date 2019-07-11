#ifndef blu_compiler_h
#define blu_compiler_h

#include "object.h"
#include "vm.h"

bluObjFunction* bluCompile(bluVM* vm, const char* source);

void bluGrayCompilerRoots(bluVM* vm);

#endif
