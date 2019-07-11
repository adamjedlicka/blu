#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void bluInitTable(bluVM* vm, bluTable* table) {
	table->count = 0;
	table->capacityMask = -1;
	table->entries = NULL;
}

void bluFreeTable(bluVM* vm, bluTable* table) {
	FREE_ARRAY(vm, bluEntry, table->entries, table->capacityMask + 1);
}

static bluEntry* findEntry(bluEntry* entries, int capacityMask, bluObjString* key) {
	uint32_t index = key->hash & capacityMask;
	bluEntry* tombstone = NULL;

	for (;;) {
		bluEntry* entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				// Empty entry.
				return tombstone != NULL ? tombstone : entry;
			} else {
				// We found a tombstone.
				if (tombstone == NULL) tombstone = entry;
			}
		} else if (entry->key == key) {
			// We found the key.
			return entry;
		}

		index = (index + 1) & capacityMask;
	}
}

bool bluTableGet(bluVM* vm, bluTable* table, bluObjString* key, bluValue* value) {
	if (table->entries == NULL) return false;

	bluEntry* entry = findEntry(table->entries, table->capacityMask, key);
	if (entry->key == NULL) return false;

	*value = entry->value;
	return true;
}

static void adjustCapacity(bluVM* vm, bluTable* table, int capacityMask) {
	bluEntry* entries = ALLOCATE(vm, bluEntry, capacityMask + 1);

	for (int i = 0; i <= capacityMask; i++) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}

	table->count = 0;

	for (int i = 0; i <= table->capacityMask; i++) {
		bluEntry* entry = &table->entries[i];
		if (entry->key == NULL) continue;

		bluEntry* dest = findEntry(entries, capacityMask, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	FREE_ARRAY(vm, bluEntry, table->entries, table->capacityMask + 1);

	table->entries = entries;
	table->capacityMask = capacityMask;
}

bool bluTableSet(bluVM* vm, bluTable* table, bluObjString* key, bluValue value) {
	if (table->count + 1 > (table->capacityMask + 1) * TABLE_MAX_LOAD) {
		// Figure out the new table size.
		int capacityMask = GROW_CAPACITY(table->capacityMask + 1) - 1;
		adjustCapacity(vm, table, capacityMask);
	}

	bluEntry* entry = findEntry(table->entries, table->capacityMask, key);

	bool isNewKey = entry->key == NULL;
	// Increase count only if we are inserting into empty bucket, not when replacing tombstone
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool bluTableDelete(bluVM* vm, bluTable* table, bluObjString* key) {
	if (table->count == 0) return false;

	// Find the entry.
	bluEntry* entry = findEntry(table->entries, table->capacityMask, key);
	if (entry->key == NULL) return false;

	// Place a tombstone in the entry.
	entry->key = NULL;
	entry->value = BOOL_VAL(true);

	return true;
}

void bluTableAddAll(bluVM* vm, bluTable* from, bluTable* to) {
	for (int i = 0; i <= from->capacityMask; i++) {
		bluEntry* entry = &from->entries[i];
		if (entry->key != NULL) {
			bluTableSet(vm, to, entry->key, entry->value);
		}
	}
}

// Function tableGet uses findEntry which compares keys with double equals, so if they are in the same place in memory.
// For string interning we want to compare keys fully with memcmp.
bluObjString* bluTableFindString(bluVM* vm, bluTable* table, const char* chars, int length, uint32_t hash) {
	// If the table is empty, we definitely won't find it.
	if (table->entries == NULL) return NULL;

	uint32_t index = hash & table->capacityMask;

	for (;;) {
		bluEntry* entry = &table->entries[index];

		if (entry->key == NULL) {
			// Stop if we find an empty non-tombstone entry.
			if (IS_NIL(entry->value)) return NULL;
		} else if (entry->key->length == length && entry->key->hash == hash &&
				   memcmp(entry->key->chars, chars, length) == 0) {
			// We found it.
			return entry->key;
		}

		// Try the next slot.
		index = (index + 1) & table->capacityMask;
	}
}

void bluTableRemoveWhite(bluVM* vm, bluTable* table) {
	for (int i = 0; i <= table->capacityMask; i++) {
		bluEntry* entry = &table->entries[i];
		if (entry->key != NULL && !entry->key->obj.isDark) {
			bluTableDelete(vm, table, entry->key);
		}
	}
}

void bluGrayTable(bluVM* vm, bluTable* table) {
	for (int i = 0; i <= table->capacityMask; i++) {
		bluEntry* entry = &table->entries[i];
		bluGrayObject(vm, (bluObj*)entry->key);
		bluGrayValue(vm, entry->value);
	}
}
