#include "Schema.h"

#include <cmath>
#include <cstring>

int Schema::createRel(char relName[], int numOfAttributes, char attrNames[][ATTR_SIZE], int attrTypes[]) {
  Attribute relNameAttr;
  strcpy(relNameAttr.sVal, relName);

  RecId targetRelId;

  Attribute relCatRecord[RELCAT_NO_ATTRS];

  RelCacheTable::resetSearchIndex(RELCAT_RELID);
  int retVal;
  char relCatAttrRelname[] = RELCAT_ATTR_RELNAME;
  retVal = BlockAccess::search(RELCAT_RELID, relCatRecord, relCatAttrRelname, relNameAttr, EQ);
  // relation already exists
  if (retVal == SUCCESS) {
    return E_RELEXIST;
  }

  // if any attribute have the same names , return E_DUPLICATEATTR
  for (int i = 0; i < numOfAttributes; ++i) {
    for (int j = i + 1; j < numOfAttributes; ++j) {
      if (strcmp(attrNames[i], attrNames[j]) == 0) {
        return E_DUPLICATEATTR;
      }
    }
  }

  // insert new relation to relation catalog
  strcpy(relCatRecord[RELCAT_REL_NAME_INDEX].sVal, relName);
  relCatRecord[RELCAT_NO_ATTRIBUTES_INDEX].nVal = numOfAttributes;
  relCatRecord[RELCAT_NO_RECORDS_INDEX].nVal = 0;
  relCatRecord[RELCAT_FIRST_BLOCK_INDEX].nVal = -1;
  relCatRecord[RELCAT_LAST_BLOCK_INDEX].nVal = -1;
  relCatRecord[RELCAT_NO_SLOTS_PER_BLOCK_INDEX].nVal = floor((2016 / (16 * numOfAttributes + 1)));

  retVal = BlockAccess::insert(RELCAT_RELID, relCatRecord);
  // BlockAccess::insert can return E_INDEX_BLOCKS_RELEASED or E_DISK_FULL
  // the relation catalog does not have indexed blocks, so only disk full is handled
  if (retVal != SUCCESS) {
    return retVal;
  }

  // insert attributes of new relation into the attribute catalog
  for (int i = 0; i < numOfAttributes; ++i) {
    // let Attribute attrCatRecord[6] be the record in attribute catalog corresponding to i'th Attribute)
    Attribute attrCatRecord[ATTRCAT_NO_ATTRS];

    strcpy(attrCatRecord[ATTRCAT_REL_NAME_INDEX].sVal, relName);
    strcpy(attrCatRecord[ATTRCAT_ATTR_NAME_INDEX].sVal, attrNames[i]);
    attrCatRecord[ATTRCAT_ATTR_TYPE_INDEX].nVal = attrTypes[i];
    attrCatRecord[ATTRCAT_PRIMARY_FLAG_INDEX].nVal = -1;
    attrCatRecord[ATTRCAT_ROOT_BLOCK_INDEX].nVal = -1;
    attrCatRecord[ATTRCAT_OFFSET_INDEX].nVal = i;

    retVal = BlockAccess::insert(ATTRCAT_RELID, attrCatRecord);
    if (retVal != SUCCESS) {
      deleteRel(relName);
      return retVal;
    }
  }

  return SUCCESS;
}

int Schema::deleteRel(char relName[ATTR_SIZE]) {
  if (strcmp(relName, RELCAT_RELNAME) == 0 || strcmp(relName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(relName);

  // if relation is opened in open relation table
  if (relId != E_RELNOTOPEN) {
    return E_RELOPEN;
  }

  return BlockAccess::deleteRelation(relName);

  // Errors from deleteRelation -> E_RELNOTEXIST
  //  AS OF NOW, It can return E_OUT_OF_BOUND from loadBlockAndGetBufferPtr call,
  //  but if done properly we will not reach this point
  //  this comes up only when BlockBuffer(or RecBuffer) was initialized with an Invalid Block Number
}

int Schema::renameRel(char oldRelName[ATTR_SIZE], char newRelName[ATTR_SIZE]) {
  if (strcmp(oldRelName, RELCAT_RELNAME) == 0 || strcmp(oldRelName, ATTRCAT_RELNAME) == 0 ||
      strcmp(newRelName, RELCAT_RELNAME) == 0 || strcmp(newRelName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(oldRelName);

  // if relation is opened in open relation table
  if (relId != E_RELNOTOPEN) {
    return E_RELOPEN;
  }

  return BlockAccess::renameRelation(oldRelName, newRelName);
}

int Schema::renameAttr(char relName[ATTR_SIZE], char oldAttrName[ATTR_SIZE], char newAttrName[ATTR_SIZE]) {
  if (strcmp(relName, RELCAT_RELNAME) == 0 || strcmp(relName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(relName);

  // if relation is opened in open relation table
  if (relId != E_RELNOTOPEN) {
    return E_RELOPEN;
  }

  return BlockAccess::renameAttribute(relName, oldAttrName, newAttrName);
}

int Schema::openRel(char relName[ATTR_SIZE]) {
  int relId = OpenRelTable::openRel(relName);
  if (relId >= 0) {
    return SUCCESS;
  }
  return relId;
}

int Schema::closeRel(char relName[ATTR_SIZE]) {
  if (strcmp(relName, RELCAT_RELNAME) == 0 || strcmp(relName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(relName);

  // if relation is not opened in open relation table
  if (relId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  return OpenRelTable::closeRel(relId);
}

int Schema::createIndex(char relName[ATTR_SIZE], char attrName[ATTR_SIZE]) {
  if (strcmp(relName, RELCAT_RELNAME) == 0 || strcmp(relName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(relName);

  // if relation is not opened in open relation table
  if (relId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  return BPlusTree::bPlusCreate(relId, attrName);
}

int Schema::dropIndex(char relName[ATTR_SIZE], char attrName[ATTR_SIZE]) {
  if (strcmp(relName, RELCAT_RELNAME) == 0 || strcmp(relName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(relName);

  // if relation is opened in open relation table
  if (relId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  // find the root block of the bplus tree
  AttrCatEntry attrCatEntry;
  int ret = AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);
  if (ret != SUCCESS) {
    return ret;
  }

  int rootBlock = attrCatEntry.rootBlock;

  if (rootBlock == -1) {
    return E_NOINDEX;
  }

  BPlusTree::bPlusDestroy(rootBlock);

  attrCatEntry.rootBlock = -1;
  AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

  return SUCCESS;
}