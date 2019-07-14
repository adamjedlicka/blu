#ifndef blu_table_h
#define blu_table_h

#include "blu.h"
#include "object.h"
#include "value.h"

typedef struct {
	bluObjString* key;
	bluValue value;
} bluEntry;

typedef struct {
	uint32_t count;
	int32_t capacityMask;
	bluEntry* entries;
} bluTable;

void bluTableInit(bluVM* vm, bluTable* table);
void bluTableFree(bluVM* vm, bluTable* table);

bool bluTableGet(bluVM* vm, bluTable* table, bluObjString* key, bluValue* value);
bool bluTableSet(bluVM* vm, bluTable* table, bluObjString* key, bluValue value);
bool bluTableDelete(bluVM* vm, bluTable* table, bluObjString* key);
void bluTableAddAll(bluVM* vm, bluTable* from, bluTable* to);
bluObjString* bluTableFindString(bluVM* vm, bluTable* table, const char* chars, uint32_t length, uint32_t hash);

#endif