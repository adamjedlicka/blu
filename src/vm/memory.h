#ifndef blu_memory_h
#define blu_memory_h

#include "object.h"
#include "vm.h"

#define ALLOCATE(vm, type, count) (type*)bluReallocate(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, pointer) bluReallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(vm, previous, type, oldCount, count)                                                                \
	(type*)bluReallocate(vm, previous, sizeof(type) * (oldCount), sizeof(type) * (count))

#define FREE_ARRAY(vm, type, pointer, oldCount) bluReallocate(vm, pointer, sizeof(type) * (oldCount), 0)

void* bluReallocate(bluVM* vm, void* previous, size_t oldSize, size_t newSize);

void bluGrayObject(bluVM* vm, bluObj* object);
void bluGrayValue(bluVM* vm, bluValue value);
void bluCollectGarbage(bluVM* vm);

void bluFreeObjects(bluVM* vm);

#endif
