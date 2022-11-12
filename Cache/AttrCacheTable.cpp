#include "AttrCacheTable.h"

#include <cstring>

AttrCacheEntry* AttrCacheTable::attrCache[MAX_OPEN];

int AttrCacheTable::getAttrCatEntry(int relId, char attrName[ATTR_SIZE], AttrCatEntry* attrCatBuf) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (strcmp(entry->attrCatEntry.attrName, attrName) == 0) {
      *attrCatBuf = entry->attrCatEntry;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::getAttrCatEntry(int relId, int attrOffset, AttrCatEntry* attrCatBuf) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (entry->attrCatEntry.offset == attrOffset) {
      *attrCatBuf = entry->attrCatEntry;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::setAttrCatEntry(int relId, char attrName[ATTR_SIZE], AttrCatEntry* attrCatBuf) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (strcmp(entry->attrCatEntry.attrName, attrName) == 0) {
      entry->attrCatEntry = *attrCatBuf;
      entry->dirty = true;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::setAttrCatEntry(int relId, int attrOffset, AttrCatEntry* attrCatBuf) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (entry->attrCatEntry.offset == attrOffset) {
      entry->attrCatEntry = *attrCatBuf;
      entry->dirty = true;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::getSearchIndex(int relId, char attrName[ATTR_SIZE], IndexId* searchIndex) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (strcmp(entry->attrCatEntry.attrName, attrName) == 0) {
      *searchIndex = entry->searchIndex;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::getSearchIndex(int relId, int attrOffset, IndexId* searchIndex) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (entry->attrCatEntry.offset == attrOffset) {
      *searchIndex = entry->searchIndex;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::setSearchIndex(int relId, char attrName[ATTR_SIZE], IndexId* searchIndex) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (strcmp(entry->attrCatEntry.attrName, attrName) == 0) {
      entry->searchIndex = *searchIndex;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::setSearchIndex(int relId, int attrOffset, IndexId* searchIndex) {
  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (attrCache[relId] == nullptr) {
    return E_RELNOTOPEN;
  }

  for (AttrCacheEntry* entry = attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (entry->attrCatEntry.offset == attrOffset) {
      entry->searchIndex = *searchIndex;
      return SUCCESS;
    }
  }
  return E_ATTRNOTEXIST;
}

int AttrCacheTable::resetSearchIndex(int relId, char attrName[ATTR_SIZE]) {
  IndexId resetIndex{-1, -1};
  return setSearchIndex(relId, attrName, &resetIndex);
}

int AttrCacheTable::resetSearchIndex(int relId, int attrOffset) {
  IndexId resetIndex{-1, -1};
  return setSearchIndex(relId, attrOffset, &resetIndex);
}

void AttrCacheTable::recordToAttrCacheEntry(union Attribute record[ATTRCAT_NO_ATTRS], AttrCacheEntry* attrCacheEntry) {
  strcpy(attrCacheEntry->attrCatEntry.relName, record[ATTRCAT_REL_NAME_INDEX].sVal);
  strcpy(attrCacheEntry->attrCatEntry.attrName, record[ATTRCAT_ATTR_NAME_INDEX].sVal);
  attrCacheEntry->attrCatEntry.attrType = (int)record[ATTRCAT_ATTR_TYPE_INDEX].nVal;
  attrCacheEntry->attrCatEntry.primaryFlag = (int)record[ATTRCAT_PRIMARY_FLAG_INDEX].nVal;
  attrCacheEntry->attrCatEntry.rootBlock = (int)record[ATTRCAT_ROOT_BLOCK_INDEX].nVal;
  attrCacheEntry->attrCatEntry.offset = (int)record[ATTRCAT_OFFSET_INDEX].nVal;

  // The dirty, recId, searchIndex, and next fields are initialized with default values of:
  // false, {-1, -1}, {-1, -1}, and NULL, respectively.
  attrCacheEntry->dirty = false;
  attrCacheEntry->recId = {-1, -1};
  attrCacheEntry->searchIndex = {-1, -1};
  attrCacheEntry->next = nullptr;
}

void AttrCacheTable::attrCacheEntryToRecord(union Attribute record[ATTRCAT_NO_ATTRS], AttrCacheEntry* attrCacheEntry) {
  strcpy(record[ATTRCAT_REL_NAME_INDEX].sVal, attrCacheEntry->attrCatEntry.relName);
  strcpy(record[ATTRCAT_ATTR_NAME_INDEX].sVal, attrCacheEntry->attrCatEntry.attrName);
  record[ATTRCAT_ATTR_TYPE_INDEX].nVal = attrCacheEntry->attrCatEntry.attrType;
  record[ATTRCAT_PRIMARY_FLAG_INDEX].nVal = attrCacheEntry->attrCatEntry.primaryFlag;
  record[ATTRCAT_ROOT_BLOCK_INDEX].nVal = attrCacheEntry->attrCatEntry.rootBlock;
  record[ATTRCAT_OFFSET_INDEX].nVal = attrCacheEntry->attrCatEntry.offset;
}
