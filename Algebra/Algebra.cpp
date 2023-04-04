#include "Algebra.h"

#include <cstring>

bool isNumber(char *str) {
  int len;
  float ignore;
  /*
    sscanf returns the number of elements read, so if there is no float matching
    the first %f, ret will be 0, else it'll be 1

    %n gets the number of characters read. this scanf sequence will read the
    first float ignoring all the whitespace before and after. and the number of
    characters read that far will be stored in len. if len == strlen(str), then
    the string only contains a float with/without whitespace. else, there's other
    characters.
  */
  int ret = sscanf(str, "%f %n", &ignore, &len);
  return ret == 1 && len == strlen(str);
}

bool isInvalidCharacter(char character) {
  if ((character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z') ||
      (character >= 'a' && character <= 'z') || character == '-' || character == '_') {
    return false;
  }
  return true;
}

int Algebra::insert(char relName[ATTR_SIZE], int nAttrs, char record[][ATTR_SIZE]) {
  if (strcmp(relName, RELCAT_RELNAME) == 0 || strcmp(relName, ATTRCAT_RELNAME) == 0) {
    return E_NOTPERMITTED;
  }

  int relId = OpenRelTable::getRelId(relName);
  if (relId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  RelCatEntry relCatEntry;
  RelCacheTable::getRelCatEntry(relId, &relCatEntry);

  if (relCatEntry.numAttrs != nAttrs) {
    return E_NATTRMISMATCH;
  }

  Attribute recordToInsert[nAttrs];

  for (int i = 0; i < nAttrs; ++i) {
    AttrCatEntry attrCatEntry;
    // get the attribute catalog entry for the i'th attribute from the attribute cache
    AttrCacheTable::getAttrCatEntry(relId, i, &attrCatEntry);

    int type = attrCatEntry.attrType;

    if (type == NUMBER) {
      if (isNumber(record[i])) {
        // convert the char array to double and store it at recordToInsert[i].nVal
        recordToInsert[i].nVal = atof(record[i]);
      } else {
        return E_ATTRTYPEMISMATCH;
      }

    } else if (type == STRING) {
      for (int charIndex = 0; charIndex < ATTR_SIZE; ++charIndex) {
        char ch = record[i][charIndex];

        if (ch == '\0') {
          break;
        }

        if (isInvalidCharacter(ch)) {
          return E_NOTPERMITTED;
        }
      }
      strcpy(recordToInsert[i].sVal, record[i]);
    }
  }

  return BlockAccess::insert(relId, recordToInsert);
}

int Algebra::select(char srcRel[ATTR_SIZE], char targetRel[ATTR_SIZE], char attr[ATTR_SIZE], int op, char strVal[ATTR_SIZE]) {
  int srcRelId = OpenRelTable::getRelId(srcRel);
  if (srcRelId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  // get the attribute catalog entry for attr, using getAttrcatEntry() method of AttrCacheTable in cache layer.
  AttrCatEntry attrCatEntry;
  if (AttrCacheTable::getAttrCatEntry(srcRelId, attr, &attrCatEntry) != SUCCESS) {
    return E_ATTRNOTEXIST;
  }

  /*** Convert strVal (c-string) to an attribute of data type NUMBER or STRING as given in the following code. ***/
  int type = attrCatEntry.attrType;
  Attribute attrVal;
  if (type == NUMBER) {
    if (isNumber(strVal)) {
      attrVal.nVal = atof(strVal);
    } else {
      return E_ATTRTYPEMISMATCH;
    }
  } else if (type == STRING) {
    strcpy(attrVal.sVal, strVal);
  }

  /*** Creating and opening the target relation ***/
  RelCatEntry srcRelCatEntry;
  RelCacheTable::getRelCatEntry(srcRelId, &srcRelCatEntry);
  int nAttrs = srcRelCatEntry.numAttrs;

  char attrNames[nAttrs][ATTR_SIZE];
  int attrTypes[nAttrs];

  for (int i = 0; i < nAttrs; ++i) {
    AttrCatEntry acEntry;
    AttrCacheTable::getAttrCatEntry(srcRelId, i, &acEntry);
    strcpy(attrNames[i], acEntry.attrName);
    attrTypes[i] = acEntry.attrType;
  }

  // create the target relation
  int retVal = Schema::createRel(targetRel, nAttrs, attrNames, attrTypes);
  if (retVal != SUCCESS) {
    return retVal;
  }

  int targetRelId = OpenRelTable::openRel(targetRel);
  if (targetRelId < 0) {
    Schema::deleteRel(targetRel);
    // Return Error -> E_CACHEFULL, E_RELNOTEXIST (should not occur)
    return targetRelId;
  }

  /*** Selecting and inserting records into the target relation ***/
  // Before calling the search function, reset the search to start from the first hit
  Attribute record[nAttrs];
  RelCacheTable::resetSearchIndex(srcRelId);
  AttrCacheTable::resetSearchIndex(srcRelId, attr);

  while (true) {
    // For doing projection call search of Block Access layer with the following arguments:
    retVal = BlockAccess::search(srcRelId, record, attr, attrVal, op);

    if (retVal == SUCCESS) {
      retVal = BlockAccess::insert(targetRelId, record);
      if (retVal != SUCCESS) {
        OpenRelTable::closeRel(targetRelId);
        Schema::deleteRel(targetRel);
        return retVal;
      }

    } else {
      // (all records over)
      break;
    }
  }
  OpenRelTable::closeRel(targetRelId);

  return SUCCESS;
}

int Algebra::project(char srcRel[ATTR_SIZE], char targetRel[ATTR_SIZE], int tar_nAttrs, char tar_Attrs[][ATTR_SIZE]) {
  int srcRelId = OpenRelTable::getRelId(srcRel);
  if (srcRelId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  RelCatEntry relCatEntry;
  RelCacheTable::getRelCatEntry(srcRelId, &relCatEntry);
  int srcNumAttrs = relCatEntry.numAttrs;

  // let attr_offset[tar_nAttrs] be an array of type int.
  // where ith entry corresponds to the offset in the srcRel of ith attribute in the target relation.
  int attrOffsets[tar_nAttrs];

  // let attr_types[tar_nAttrs] be an array of type int.
  // where ith entry corresponds to the type ith attribute in the target relation.
  int attrTypes[tar_nAttrs];

  int retVal;

  for (int i = 0; i < tar_nAttrs; ++i) {
    AttrCatEntry acEntry;
    retVal = AttrCacheTable::getAttrCatEntry(srcRelId, tar_Attrs[i], &acEntry);
    if (retVal != SUCCESS) {
      return E_ATTRNOTEXIST;
    }
    attrOffsets[i] = acEntry.offset;
    attrTypes[i] = acEntry.attrType;
  }

  /*** Creating and opening the target relation ***/

  // Create a relation for target relation by calling createRel() method of Schema layer by providing appropriate arguments
  // if the createRel returns an error code, then return that value.
  retVal = Schema::createRel(targetRel, tar_nAttrs, tar_Attrs, attrTypes);
  if (retVal != SUCCESS) {
    return retVal;
  }

  // Open the newly created target relation by calling openRel() method of OpenRelTable and store the target relid
  // If opening fails, delete the target relation by calling deleteRel() of Schema Layer and return the error value.
  int targetRelId = OpenRelTable::openRel(targetRel);
  if (targetRelId < 0) {
    Schema::deleteRel(targetRel);
    return targetRelId;
  }

  /*** Inserting projected records into the target relation ***/
  // Before calling the search function, reset the search to start from the first hit
  Attribute record[srcNumAttrs];
  RelCacheTable::resetSearchIndex(srcRelId);

  while (true) {
    if (BlockAccess::project(srcRelId, record) == SUCCESS) {
      // record will contain the searched record
      Attribute projectedRecord[tar_nAttrs];

      for (int i = 0; i < tar_nAttrs; ++i) {
        projectedRecord[i] = record[attrOffsets[i]];
      }

      retVal = BlockAccess::insert(targetRelId, projectedRecord);

      if (retVal != SUCCESS) {
        OpenRelTable::closeRel(targetRelId);
        Schema::deleteRel(targetRel);
        return retVal;
      }
    } else {
      // no more records
      break;
    }
  }

  OpenRelTable::closeRel(targetRelId);

  return SUCCESS;
}

int Algebra::project(char srcRel[ATTR_SIZE], char targetRel[ATTR_SIZE]) {
  int srcRelId = OpenRelTable::getRelId(srcRel);
  if (srcRelId == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  RelCatEntry relCatEntry;
  RelCacheTable::getRelCatEntry(srcRelId, &relCatEntry);
  int numAttrs = relCatEntry.numAttrs;

  char attrNames[numAttrs][ATTR_SIZE];
  int attrTypes[numAttrs];

  for (int i = 0; i < numAttrs; ++i) {
    AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(srcRelId, i, &attrCatEntry);
    strcpy(attrNames[i], attrCatEntry.attrName);
    attrTypes[i] = attrCatEntry.attrType;
  }

  // Create a relation for target relation by calling createRel() method of Schema layer by providing appropriate arguments
  // if the createRel returns an error code, then return that value.
  int retVal = Schema::createRel(targetRel, numAttrs, attrNames, attrTypes);
  if (retVal != SUCCESS) {
    return retVal;
  }

  // Open the newly created target relation by calling openRel() method of OpenRelTable and store the target relid
  // If opening fails, delete the target relation by calling deleteRel() of Schema Layer and return the error value.
  int targetRelId = OpenRelTable::openRel(targetRel);
  if (targetRelId < 0) {
    Schema::deleteRel(targetRel);
    return targetRelId;
  }

  /*** Inserting projected records into the target relation ***/
  // Before calling the search function, reset the search to start from the first hit
  Attribute record[numAttrs];
  RelCacheTable::resetSearchIndex(srcRelId);

  while (true) {
    if (BlockAccess::project(srcRelId, record) == SUCCESS) {
      retVal = BlockAccess::insert(targetRelId, record);

      if (retVal != SUCCESS) {
        OpenRelTable::closeRel(targetRelId);
        Schema::deleteRel(targetRel);
        return retVal;
      }
    } else {
      // no more records
      break;
    }
  }

  OpenRelTable::closeRel(targetRelId);

  return SUCCESS;
}

int Algebra::join(char srcRelation1[ATTR_SIZE], char srcRelation2[ATTR_SIZE], char targetRelation[ATTR_SIZE], char attribute1[ATTR_SIZE], char attribute2[ATTR_SIZE]) {
  int srcRelId1 = OpenRelTable::getRelId(srcRelation1);
  int srcRelId2 = OpenRelTable::getRelId(srcRelation2);

  if (srcRelId1 == E_RELNOTOPEN || srcRelId2 == E_RELNOTOPEN) {
    return E_RELNOTOPEN;
  }

  AttrCatEntry attrCatEntry1, attrCatEntry2;

  if (AttrCacheTable::getAttrCatEntry(srcRelId1, attribute1, &attrCatEntry1) == E_ATTRNOTEXIST ||
      AttrCacheTable::getAttrCatEntry(srcRelId2, attribute2, &attrCatEntry2) == E_ATTRNOTEXIST) {
    return E_ATTRNOTEXIST;
  }

  if (attrCatEntry1.attrType != attrCatEntry2.attrType) {
    return E_ATTRTYPEMISMATCH;
  }

  RelCatEntry relCatEntry1, relCatEntry2;
  RelCacheTable::getRelCatEntry(srcRelId1, &relCatEntry1);
  RelCacheTable::getRelCatEntry(srcRelId2, &relCatEntry2);

  // let numOfAttributes1, numOfAttributes2 be the number of attributes in srcRelation1 and srcRelation2 respectively
  // (note: the number of attributes field is present in relation catalog entry)
  int numAttrs1 = relCatEntry1.numAttrs, numAttrs2 = relCatEntry2.numAttrs;

  // Next step ensures that an index exists for the second relations
  if (attrCatEntry2.rootBlock == -1) {
    if (BPlusTree::bPlusCreate(srcRelId2, attribute2) != SUCCESS) {
      return E_DISKFULL;
    }
  }

  int targetNumAttrs = numAttrs1 + numAttrs2 - 1;
  char targetAttrNames[targetNumAttrs][ATTR_SIZE];
  int targetAttrTypes[targetNumAttrs];

  // get the attributes for the target relation from both the source relation
  // use the join attribute from the srcRelation1
  for (int i = 0; i < numAttrs1; ++i) {
    AttrCatEntry acEntry;
    AttrCacheTable::getAttrCatEntry(srcRelId1, i, &acEntry);
    strcpy(targetAttrNames[i], acEntry.attrName);
    targetAttrTypes[i] = acEntry.attrType;
  }
  int targetAttrIndex = numAttrs1;
  for (int i = 0; i < numAttrs2; ++i) {
    AttrCatEntry acEntry;
    AttrCacheTable::getAttrCatEntry(srcRelId2, i, &acEntry);
    if (acEntry.offset != attrCatEntry2.offset) {
      strcpy(targetAttrNames[targetAttrIndex], acEntry.attrName);
      targetAttrTypes[targetAttrIndex] = acEntry.attrType;
      targetAttrIndex++;
    }
  }
  // check for duplicate attrs between the two relations
  for (int i = 0; i < numAttrs1; ++i) {
    for (int j = numAttrs1; j < targetNumAttrs; ++j) {
      if (strcmp(targetAttrNames[i], targetAttrNames[j]) == 0) {
        return E_DUPLICATEATTR;
      }
    }
  }

  int retVal = Schema::createRel(targetRelation, targetNumAttrs, targetAttrNames, targetAttrTypes);

  if (retVal != SUCCESS) {
    return retVal;
  }

  int targetRelId = OpenRelTable::openRel(targetRelation);

  // if openRel() fails (No free entries left in the Open Relation Table)
  if (targetRelId < 0) {
    Schema::deleteRel(targetRelation);
    return targetRelId;
  }

  Attribute record1[numAttrs1], record2[numAttrs2], targetRecord[targetNumAttrs];

  // this loop is to get every record of the srcRelation1 one by one
  while (BlockAccess::project(srcRelId1, record1) == SUCCESS) {
    // this loop is to get every record of the srcRelation2 which satisfies the following condition:
    // record1.attribute1 = record2.attribute2 (i.e. Equi-Join condition)

    RelCacheTable::resetSearchIndex(srcRelId2);
    AttrCacheTable::resetSearchIndex(srcRelId2, attribute2);

    while (BlockAccess::search(srcRelId2, record2, attribute2, record1[attrCatEntry1.offset], EQ) == SUCCESS) {
      // copy srcRelation1's and srcRelation2's attribute values(except for attribute2 in rel2) from
      // record1 and record2 to targetRecord
      // (iterate offset from 0 to numOfAttributes1-1 in record1 and 0 to numOfAttributes2-1 in record2
      for (int i = 0; i < numAttrs1; ++i) {
        targetRecord[i] = record1[i];
      }
      targetAttrIndex = numAttrs1;
      for (int i = 0; i < numAttrs2; ++i) {
        if (i != attrCatEntry2.offset) {
          targetRecord[targetAttrIndex] = record2[i];
          targetAttrIndex++;
        }
      }

      // insert the current record into the target relation by calling BlockAccess::insert()
      retVal = BlockAccess::insert(targetRelId, targetRecord);

      if (retVal == E_DISKFULL) {
        OpenRelTable::closeRel(targetRelId);
        Schema::deleteRel(targetRelation);
        return E_DISKFULL;
      }
    }
  }

  OpenRelTable::closeRel(targetRelId);
  return SUCCESS;
}