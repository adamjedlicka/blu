#ifndef blu_memory_h
#define blu_memory_h

#include "compiler/chunk.h"
#include "object.h"
#include "table.h"

void* bluAllocate(bluVM* vm, size_t size);
void* bluReallocate(bluVM* vm, void* previous, size_t oldSize, size_t newSize);
void bluDeallocate(bluVM* vm, void* pointer, size_t size);

void bluGrayValue(bluVM* vm, bluValue value);
void bluGrayObject(bluVM* vm, bluObj* object);
void bluGrayValueBuffer(bluVM* vm, bluValueBuffer* buffer);
void bluGrayTable(bluVM* vm, bluTable* table);

void bluCollectGarbage(bluVM* vm);
void bluCollectMemory(bluVM* vm);

#endif
