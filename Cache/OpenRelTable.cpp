#include "OpenRelTable.h"

#include <cstring>

OpenRelTableMetaInfo OpenRelTable::tableMetaInfo[MAX_OPEN];

OpenRelTable::OpenRelTable() {
  // initialize tableMetaInfo of all the entries of the Open Relation Table with free as true and relName as an empty string.
  for (int i = 0; i < MAX_OPEN; ++i) {
    tableMetaInfo[i].free = true;
    tableMetaInfo[i].relName[0] = '\0';
    AttrCacheTable::attrCache[i] = nullptr;
  }

  /************ Setting up Relation Catalog relation in the cache ************/

  /**** setting up Relation Catalog relation in the Relation Cache Table ****/
  RecBuffer relCatBlock(RELCAT_BLOCK);

  union Attribute relCatRecInRelCat[RELCAT_NO_ATTRS];
  relCatBlock.getRecord(relCatRecInRelCat, RELCAT_SLOTNUM_FOR_RELCAT);

  struct RelCacheEntry relCacheEntry;
  RelCacheTable::recordToRelCatEntry(relCatRecInRelCat, &relCacheEntry.relCatEntry);
  relCacheEntry.recId.block = RELCAT_BLOCK;
  relCacheEntry.recId.slot = RELCAT_SLOTNUM_FOR_RELCAT;

  RelCacheTable::relCache[RELCAT_RELID] = (struct RelCacheEntry*)malloc(sizeof(RelCacheEntry));
  *(RelCacheTable::relCache[RELCAT_RELID]) = relCacheEntry;

  /**** setting up Relation Catalog relation in the Attribute Cache Table ****/
  RecBuffer attrCatBlock(ATTRCAT_BLOCK);

  struct AttrCacheEntry* relCatAttrCache = (AttrCacheEntry*)malloc(RELCAT_NO_ATTRS * sizeof(AttrCacheEntry));
  for (int i = 0; i < RELCAT_NO_ATTRS; ++i) {
    Attribute relCatRecInAttrCat[ATTRCAT_NO_ATTRS];
    attrCatBlock.getRecord(relCatRecInAttrCat, i);
    struct AttrCacheEntry* attrCacheEntry = relCatAttrCache + i;
    AttrCacheTable::recordToAttrCatEntry(relCatRecInAttrCat, &attrCacheEntry->attrCatEntry);
    attrCacheEntry->recId.block = ATTRCAT_BLOCK;
    attrCacheEntry->recId.slot = i;
    attrCacheEntry->next = relCatAttrCache + i + 1;
  }
  relCatAttrCache[RELCAT_NO_ATTRS - 1].next = nullptr;

  AttrCacheTable::attrCache[RELCAT_RELID] = relCatAttrCache;

  /**** setting up Relation Catalog relation in the Open Relation Table ****/
  OpenRelTable::tableMetaInfo[RELCAT_RELID].free = false;
  strcpy(OpenRelTable::tableMetaInfo[RELCAT_RELID].relName, RELCAT_RELNAME);

  /************ Setting up Attribute Catalog relation in the cache ************/

  /**** setting up Attribute Catalog relation in the Relation Cache Table ****/
  union Attribute attrCatRecInRelCat[RELCAT_NO_ATTRS];
  relCatBlock.getRecord(attrCatRecInRelCat, RELCAT_SLOTNUM_FOR_ATTRCAT);

  RelCacheTable::recordToRelCatEntry(attrCatRecInRelCat, &relCacheEntry.relCatEntry);
  relCacheEntry.recId.block = RELCAT_BLOCK;
  relCacheEntry.recId.slot = RELCAT_SLOTNUM_FOR_ATTRCAT;

  RelCacheTable::relCache[ATTRCAT_RELID] = (struct RelCacheEntry*)malloc(sizeof(RelCacheEntry));
  *(RelCacheTable::relCache[ATTRCAT_RELID]) = relCacheEntry;

  /**** setting up Attribute Catalog relation in the Attribute Cache Table ****/
  struct AttrCacheEntry* attrCatAttrCache = (AttrCacheEntry*)malloc(ATTRCAT_NO_ATTRS * sizeof(AttrCacheEntry));
  for (int i = 0; i < ATTRCAT_NO_ATTRS; ++i) {
    Attribute attrCatRecInAttrCat[ATTRCAT_NO_ATTRS];
    attrCatBlock.getRecord(attrCatRecInAttrCat, RELCAT_NO_ATTRS + i);
    struct AttrCacheEntry* attrCacheEntry = attrCatAttrCache + i;
    AttrCacheTable::recordToAttrCatEntry(attrCatRecInAttrCat, &attrCacheEntry->attrCatEntry);
    attrCacheEntry->recId.block = ATTRCAT_BLOCK;
    attrCacheEntry->recId.slot = RELCAT_NO_ATTRS + i;
    attrCacheEntry->next = attrCatAttrCache + i + 1;
  }
  attrCatAttrCache[ATTRCAT_NO_ATTRS - 1].next = nullptr;

  AttrCacheTable::attrCache[ATTRCAT_RELID] = attrCatAttrCache;

  /**** setting up Attribute Catalog relation in the Open Relation Table ****/
  OpenRelTable::tableMetaInfo[ATTRCAT_RELID].free = false;
  strcpy(OpenRelTable::tableMetaInfo[ATTRCAT_RELID].relName, ATTRCAT_RELNAME);
}

OpenRelTable::~OpenRelTable() {
  // close all open relations. 0 and 1 are relation and attr catalog
  for (int i = 2; i < MAX_OPEN; ++i) {
    if (!tableMetaInfo[i].free) {
      OpenRelTable::closeRel(i);
    }
  }

  /************ Closing Attribute Catalog relation in the cache ************/

  /****** releasing the entry corresponding to Attribute Catalog relation from Relation Cache Table ******/
  if (RelCacheTable::relCache[ATTRCAT_RELID]->dirty) {
    Attribute attrCatRecInRelCache[RELCAT_NO_ATTRS];
    RelCacheTable::relCatEntryToRecord(&RelCacheTable::relCache[ATTRCAT_RELID]->relCatEntry, attrCatRecInRelCache);

    RecBuffer(RELCAT_BLOCK).setRecord(attrCatRecInRelCache, RELCAT_SLOTNUM_FOR_ATTRCAT);
  }

  /****** releasing the entry corresponding to Attribute Catalog relation from Attribute Cache Table ******/
  RecBuffer attrCatBlock(ATTRCAT_BLOCK);

  for (AttrCacheEntry* entry = AttrCacheTable::attrCache[ATTRCAT_RELID]; entry != nullptr; entry = entry->next) {
    if (entry->dirty) {
      Attribute attrCatRecInAttrCache[ATTRCAT_NO_ATTRS];
      AttrCacheTable::attrCatEntryToRecord(&entry->attrCatEntry, attrCatRecInAttrCache);
      attrCatBlock.setRecord(attrCatRecInAttrCache, entry->recId.slot);
    }
  }

  // the entire linked list was allocated as a single block
  free(AttrCacheTable::attrCache[ATTRCAT_RELID]);

  /****** updating metadata corresponding to Attribute Catalog relation in the Open Relation Table ******/
  OpenRelTable::tableMetaInfo[ATTRCAT_RELID].free = true;

  /************ Closing Relation Catalog relation in the cache ************/

  /****** releasing the entry corresponding to Relation Catalog relation from Relation Cache Table ******/
  if (AttrCacheTable::attrCache[RELCAT_RELID]->dirty) {
    Attribute relCatRecInRelCache[RELCAT_NO_ATTRS];
    RelCacheTable::relCatEntryToRecord(&RelCacheTable::relCache[RELCAT_RELID]->relCatEntry, relCatRecInRelCache);

    RecBuffer(RELCAT_BLOCK).setRecord(relCatRecInRelCache, RELCAT_SLOTNUM_FOR_RELCAT);
  }

  /****** releasing the entry corresponding to Relation Catalog relation from Attribute Cache Table ******/
  for (AttrCacheEntry* entry = AttrCacheTable::attrCache[RELCAT_RELID]; entry != nullptr; entry = entry->next) {
    if (entry->dirty) {
      Attribute relCatRecInAttrCache[ATTRCAT_NO_ATTRS];
      AttrCacheTable::attrCatEntryToRecord(&entry->attrCatEntry, relCatRecInAttrCache);
      attrCatBlock.setRecord(relCatRecInAttrCache, entry->recId.slot);
    }
  }
  free(AttrCacheTable::attrCache[RELCAT_RELID]);

  /****** updating metadata corresponding to Relation Catalog relation in the Open Relation Table ******/
  OpenRelTable::tableMetaInfo[RELCAT_RELID].free = true;
}

int OpenRelTable::getRelId(char relName[ATTR_SIZE]) {
  /* traverse through the tableMetaInfo array,
      find the entry in the Open Relation Table corresponding to relName.*/

  // if found return the relation id, else indicate that the relation do not have an entry in the Open Relation Table.
  for (int i = 0; i < MAX_OPEN; ++i) {
    if (!OpenRelTable::tableMetaInfo[i].free && strcmp(OpenRelTable::tableMetaInfo[i].relName, relName) == 0) {
      return i;
    }
  }

  return E_RELNOTOPEN;
}

int OpenRelTable::openRel(char relName[ATTR_SIZE]) {
  int existingRelId;
  if ((existingRelId = OpenRelTable::getRelId(relName)) != E_RELNOTOPEN) {
    return existingRelId;
  }

  int freeSlot;
  if ((freeSlot = OpenRelTable::getFreeOpenRelTableEntry()) == E_CACHEFULL) {
    return E_CACHEFULL;
  }

  int& relId = freeSlot;

  /****** Setting up Relation Cache entry for the relation ******/

  /* search for the entry with relation name, relName, in the Relation Catalog using linear_search() of the Block Access Layer.
     care should be taken to reset the searchIndex of the relation, RELCAT_RELID, corresponding to
     Relation Catalog before calling linear_search().*/

  struct RecId relCatRecId;  // store the record id of the relation, relName, in the Relation Catalog.
  RelCacheTable::resetSearchIndex(RELCAT_RELID);

  union Attribute attrRelName;
  strcpy(attrRelName.sVal, relName);
  char relCatRelNameAttr[] = RELCAT_ATTR_RELNAME;
  relCatRecId = BlockAccess::linearSearch(RELCAT_RELID, relCatRelNameAttr, attrRelName, EQ);

  if (relCatRecId.block == -1 && relCatRecId.slot == -1) {
    return E_RELNOTEXIST;
  }

  /* read the record entry corresponding to relcatRecId and create a Relation Cache entry on it
     using RecBuffer::getRecord() and RelCacheTable::recordToRelCatEntry().
     update the recId field of this Relation Cache entry to relcatRecId.
     use the Relation Cache entry to set the relIdth entry of the RelCacheTable.*/

  RecBuffer relCatBlock(RELCAT_BLOCK);
  union Attribute relCatRecForRel[RELCAT_NO_ATTRS];
  relCatBlock.getRecord(relCatRecForRel, relCatRecId.slot);
  struct RelCacheEntry* relCacheEntryForRel = (RelCacheEntry*)malloc(sizeof(RelCacheEntry));
  RelCacheTable::recordToRelCatEntry(relCatRecForRel, &relCacheEntryForRel->relCatEntry);
  relCacheEntryForRel->recId = relCatRecId;

  RelCacheTable::relCache[relId] = relCacheEntryForRel;

  /****** Setting up Attribute Cache entry for the relation ******/

  /* iterate over all the entries in the Attribute Catalog corresponding to each attribute of
     the relation, relName by multiple calls of linear_search() of the Block Access Layer.
     care should be taken to reset the searchIndex of the relation, ATTRCAT_RELID, corresponding to
     Attribute Catalog before the first call to linear_search().
  {
                let attrcatRecId store a valid record id an entry of the relation, relName,
         in the Attribute Catalog.
        read the record entry corresponding to attrcatRecId and create an Attribute Cache entry on it
         using RecBuffer::getRecord() and AttrCacheTable::recordToAttrCatEntry().
         update the recId field of this Attribute Cache entry to attrcatRecId.
         add the Attribute Cache entry to the linked list of listHead .
  }*/

  RecBuffer attrCatBlock(ATTRCAT_BLOCK);

  RelCacheTable::resetSearchIndex(ATTRCAT_RELID);
  int relNumAttrs = relCacheEntryForRel->relCatEntry.numAttrs;

  struct AttrCacheEntry* attrCacheEntryForRel = (AttrCacheEntry*)malloc(relNumAttrs * sizeof(AttrCacheEntry));

  for (int i = 0; i < relNumAttrs; ++i) {
    Attribute attrRelName;
    strcpy(attrRelName.sVal, relName);
    char attrCatRelNameAttr[] = ATTRCAT_ATTR_RELNAME;
    struct RecId attrRecId = BlockAccess::linearSearch(ATTRCAT_RELID, attrCatRelNameAttr, attrRelName, EQ);

    union Attribute attrCatRecForRelAttr[ATTRCAT_NO_ATTRS];
    RecBuffer(attrRecId.block).getRecord(attrCatRecForRelAttr, attrRecId.slot);
    AttrCacheEntry* entry = attrCacheEntryForRel + i;
    AttrCacheTable::recordToAttrCatEntry(attrCatRecForRelAttr, &entry->attrCatEntry);
    entry->next = entry + 1;
    entry->recId = attrRecId;
  }
  attrCacheEntryForRel[relNumAttrs - 1].next = nullptr;
  AttrCacheTable::attrCache[relId] = attrCacheEntryForRel;

  /****** Setting up metadata in the Open Relation Table for the relation******/

  tableMetaInfo[relId].free = false;
  strcpy(tableMetaInfo[relId].relName, relName);

  return relId;
}

int OpenRelTable::closeRel(int relId) {
  if (relId == RELCAT_RELID || relId == ATTRCAT_RELID) {
    return E_NOTPERMITTED;
  }

  if (relId < 0 || relId >= MAX_OPEN) {
    return E_OUTOFBOUND;
  }

  if (OpenRelTable::tableMetaInfo[relId].free) {
    return E_RELNOTOPEN;
  }

  if (RelCacheTable::relCache[relId]->dirty) {
    union Attribute relCatEntryForRel[RELCAT_NO_ATTRS];
    RelCacheEntry* entry = RelCacheTable::relCache[relId];
    RelCacheTable::relCatEntryToRecord(&entry->relCatEntry, relCatEntryForRel);
    RecBuffer(RELCAT_BLOCK).setRecord(relCatEntryForRel, entry->recId.slot);
  }

  free(RelCacheTable::relCache[relId]);
  RelCacheTable::relCache[relId] = nullptr;

  /****** Releasing the Attribute Cache entry of the relation ******/

  for (AttrCacheEntry* entry = AttrCacheTable::attrCache[relId]; entry != nullptr; entry = entry->next) {
    if (entry->dirty) {
      union Attribute attrCatEntryForRel[ATTRCAT_NO_ATTRS];
      AttrCacheTable::attrCatEntryToRecord(&entry->attrCatEntry, attrCatEntryForRel);

      // use the slot from recid since attr catalog is in multiple blocks
      RecBuffer(entry->recId.block).setRecord(attrCatEntryForRel, entry->recId.slot);
    }
  }

  free(AttrCacheTable::attrCache[relId]);
  AttrCacheTable::attrCache[relId] = nullptr;

  /****** Updating metadata in the Open Relation Table of the relation  ******/

  OpenRelTable::tableMetaInfo[relId].free = true;

  return SUCCESS;
}

int OpenRelTable::getFreeOpenRelTableEntry() {
  for (int i = 0; i < MAX_OPEN; i++) {
    if (tableMetaInfo[i].free)
      return i;
  }
  return E_CACHEFULL;
}
