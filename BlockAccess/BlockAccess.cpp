#include "BlockAccess.h"

#include <cstring>

RecId BlockAccess::linearSearch(int relId, char attrName[ATTR_SIZE], union Attribute attrVal, int op) {
  struct RecId prevRecId;
  RelCacheTable::getSearchIndex(relId, &prevRecId);

  // let block and slot denote the record id of the record being currently checked
  int block, slot;

  // if the current search index record is invalid(i.e. both block and slot = -1)
  if (prevRecId.block == -1 && prevRecId.slot == -1) {
    // (no hits from previous search; search should start from the first record itself)
    struct RelCatEntry relCatEntry;
    RelCacheTable::getRelCatEntry(relId, &relCatEntry);
    // get the first record block of the relation from the relation cache
    block = relCatEntry.firstBlk;
    slot = 0;
  } else {
    // (there is a hit from previous search; search should start from the record next to the search index record)
    block = prevRecId.block;
    slot = prevRecId.slot + 1;
  }

  // The following code searches for the next record in the relation that satisfies the given condition
  // Start from the record id (block, slot) and iterate over the remaining records of the relation
  while (block != -1) {
    RecBuffer blockBuf = RecBuffer(block);

    HeadInfo blockHead;
    blockBuf.getHeader(&blockHead);
    Attribute record[blockHead.numAttrs];
    blockBuf.getRecord(record, slot);
    unsigned char slotMap[blockHead.numSlots];
    blockBuf.getSlotMap(slotMap);

    if (slot >= blockHead.numSlots) {
      // (no more slots in this block)
      block = blockHead.rblock;
      slot = 0;
      continue;
    }

    if (slotMap[slot] == SLOT_UNOCCUPIED) {
      ++slot;
      continue;
    }

    struct AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

    // attribute value in the record
    Attribute recAttrValue = record[attrCatEntry.offset];

    int cmpVal = compareAttrs(recAttrValue, attrVal, attrCatEntry.attrType);

    if (
        (op == NE && cmpVal != 0) ||
        (op == LT && cmpVal < 0) ||
        (op == LE && cmpVal <= 0) ||
        (op == EQ && cmpVal == 0) ||
        (op == GT && cmpVal > 0) ||
        (op == GE && cmpVal >= 0)) {
      // record satisfying the condition is found
      RecId curRecId{block, slot};

      // set the search index in the relation cache as the rec id of the record that satisfies the given condition
      RelCacheTable::setSearchIndex(relId, &curRecId);
      return curRecId;
    }

    ++slot;
  }

  // no record in the relation with Id relid satisfies the given condition
  return RecId{-1, -1};
}

int BlockAccess::search(int relId, Attribute *record, char attrName[ATTR_SIZE], Attribute attrVal, int op) {
  // stores the searched record
  RecId recId;

  AttrCatEntry attrCatEntry;
  AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

  if (attrCatEntry.rootBlock == -1) {
    // (no index on attribute)
    recId = linearSearch(relId, attrName, attrVal, op);
  } else {
    // (index exists for the attribute)
    recId = BPlusTree::bPlusSearch(relId, attrName, attrVal, op);
  }

  // if it fails to find a record satisfying the given condition
  if (recId.block == -1 && recId.slot == -1) {
    return E_NOTFOUND;
  }

  RecBuffer recBuffer(recId.block);
  recBuffer.getRecord(record, recId.slot);

  return SUCCESS;
}

int BlockAccess::insert(int relId, union Attribute *record) {
  RelCatEntry relCatEntry;
  RelCacheTable::getRelCatEntry(relId, &relCatEntry);

  // the record id of the slot where the new record will be inserted
  RecId newRecId = {-1, -1};

  int numSlots = relCatEntry.numSlotsPerBlk, numAttrs = relCatEntry.numAttrs;

  // let prevBlockNum denote the block number of the last element in the linked list = -1;
  // let blockNum denote the first record block of the relation
  int prevBlockNum = -1;
  int blockNum = relCatEntry.firstBlk;

  /*
      Traversing the linked list of existing record blocks of the relation
      until a free slot is found OR
      until the end of the list is reached
  */
  while (blockNum != -1) {
    RecBuffer blockBuffer(blockNum);
    HeadInfo blockHeader;
    unsigned char blockSlotMap[numSlots];
    blockBuffer.getHeader(&blockHeader);
    blockBuffer.getSlotMap(blockSlotMap);

    // search for free slot in the block 'blockNum' and store it's record id
    for (int i = 0; i < numSlots; ++i) {
      if (blockSlotMap[i] == SLOT_UNOCCUPIED) {
        newRecId.block = blockNum;
        newRecId.slot = i;
        break;
      }
    }

    // if empty slot found, then break
    if (newRecId.block != -1 && newRecId.slot != -1) {
      break;
    }

    prevBlockNum = blockNum;
    blockNum = blockHeader.rblock;
  }

  //  if no free slot is found in existing record blocks
  if (newRecId.block == -1 && newRecId.slot == -1) {
    // if relation is RELCAT, do not allocate any more blocks
    if (relId == RELCAT_RELID) {
      return E_MAXRELATIONS;
    }

    // get a new record block by calling RecBuffer Constructor for new block
    RecBuffer newBlock;

    if (newBlock.getBlockNum() == E_DISKFULL) {
      return E_DISKFULL;
    }

    newRecId.block = newBlock.getBlockNum();
    newRecId.slot = 0;

    // set the header of the new record block such that it links with existing record blocks of the relation
    HeadInfo newBlockHead;
    newBlockHead.blockType = REC;
    newBlockHead.pblock = -1;
    newBlockHead.lblock = prevBlockNum;
    newBlockHead.rblock = -1;
    newBlockHead.numEntries = 0;
    newBlockHead.numSlots = numSlots;
    newBlockHead.numAttrs = numAttrs;
    newBlock.setHeader(&newBlockHead);

    // set block's slot map with all slots marked as free
    unsigned char newBlockSlotMap[numSlots];
    for (int i = 0; i < numSlots; ++i) {
      newBlockSlotMap[i] = SLOT_UNOCCUPIED;
    }
    newBlock.setSlotMap(newBlockSlotMap);

    if (prevBlockNum != -1) {
      RecBuffer prevBlock(prevBlockNum);
      HeadInfo prevBlockHeader;
      prevBlock.getHeader(&prevBlockHeader);
      prevBlockHeader.rblock = newRecId.block;
      prevBlock.setHeader(&prevBlockHeader);

    } else {
      // update first block field in the relation catalogue entry to the new block
      relCatEntry.firstBlk = newRecId.block;
    }

    // update last block field in the relation catalogue entry to the new block
    relCatEntry.lastBlk = newRecId.block;
  }

  // create a RecBuffer object for rec_id.block(use constructor for existing block)
  RecBuffer blockBuf(newRecId.block);
  // insert the record into rec_id'th slot by calling RecBuffer::setRecord() function)
  blockBuf.setRecord(record, newRecId.slot);

  // update the slot map of the block by marking entry of the slot to which record was inserted as occupied)
  unsigned char blockSlotMap[numSlots];
  blockBuf.getSlotMap(blockSlotMap);
  blockSlotMap[newRecId.slot] = SLOT_OCCUPIED;
  blockBuf.setSlotMap(blockSlotMap);

  // increment the num_entries field in the header of the block (to which record was inserted)
  HeadInfo blockHeader;
  blockBuf.getHeader(&blockHeader);
  blockHeader.numEntries++;
  blockBuf.setHeader(&blockHeader);

  // Increment the number of records field in the relation cache entry for the relation.
  relCatEntry.numRecs++;
  RelCacheTable::setRelCatEntry(relId, &relCatEntry);

  //  B+ tree insertions
  int flag = SUCCESS;
  // Iterate over all the attributes of the relation
  for (int attrOffset = 0; attrOffset < numAttrs; ++attrOffset) {
    // get the attribute catalog entry for the attribute from the attribute cache
    AttrCatEntry attrCatEntry;
    AttrCacheTable::getAttrCatEntry(relId, attrOffset, &attrCatEntry);

    // if index exists for the attribute(i.e. rootBlock != -1)
    if (attrCatEntry.rootBlock != -1) {
      int retVal = BPlusTree::bPlusInsert(relId, attrCatEntry.attrName, record[attrOffset], newRecId);
      if (retVal == E_DISKFULL) {
        flag = E_INDEX_BLOCKS_RELEASED;
      }
    }
  }

  return flag;
}

int BlockAccess::renameRelation(char oldName[ATTR_SIZE], char newName[ATTR_SIZE]) {
  RecId relCatRecId;

  RelCacheTable::resetSearchIndex(RELCAT_RELID);

  // search for the relation with name newName in relation catalog using linearSearch()
  Attribute newRelationName;
  strcpy(newRelationName.sVal, newName);

  char relCatAttrRelname[] = RELCAT_ATTR_RELNAME;
  relCatRecId = linearSearch(RELCAT_RELID, relCatAttrRelname, newRelationName, EQ);

  // If relation with name newName already exits
  if (relCatRecId.block != -1 || relCatRecId.slot != -1) {
    return E_RELEXIST;
  }

  RelCacheTable::resetSearchIndex(RELCAT_RELID);
  Attribute oldRelationName;
  strcpy(oldRelationName.sVal, oldName);
  relCatRecId = linearSearch(RELCAT_RELID, relCatAttrRelname, oldRelationName, EQ);

  // If relation with name relName does not exist
  if (relCatRecId.block == -1 || relCatRecId.slot == -1) {
    return E_RELNOTEXIST;
  }

  // get the relation catalog record from the relation catalog (recid of the relation catalog record = relcat_recid)
  RecBuffer relCatBuffer(RELCAT_BLOCK);
  Attribute relCatRecord[RELCAT_NO_ATTRS];
  relCatBuffer.getRecord(relCatRecord, relCatRecId.slot);

  // update the relation catalog record in the relation catalog with relation name newName
  strcpy(relCatRecord[RELCAT_REL_NAME_INDEX].sVal, newName);
  relCatBuffer.setRecord(relCatRecord, relCatRecId.slot);

  /*
      update all the attribute catalog entries in the attribute catalog corresponding to the
      relation with relation name oldName to the relation name newName
  */

  RelCacheTable::resetSearchIndex(ATTRCAT_RELID);
  char attrCatAttrRelname[] = ATTRCAT_ATTR_RELNAME;
  for (int i = 0; i < (int)relCatRecord[RELCAT_NO_ATTRIBUTES_INDEX].nVal; ++i) {
    // search for the relation with name oldName in relation catalog
    RecId attrCatRecId = linearSearch(ATTRCAT_RELID, attrCatAttrRelname, oldRelationName, EQ);

    Attribute attrCatRecord[ATTRCAT_NO_ATTRS];
    RecBuffer attrCatRecBuffer(attrCatRecId.block);
    attrCatRecBuffer.getRecord(attrCatRecord, attrCatRecId.slot);

    // update the attribute catalog relCatRecord in the attribute catalog with relation name newName
    strcpy(attrCatRecord[ATTRCAT_REL_NAME_INDEX].sVal, newName);
    attrCatRecBuffer.setRecord(attrCatRecord, attrCatRecId.slot);
  }

  return SUCCESS;
}

int BlockAccess::renameAttribute(char relName[ATTR_SIZE], char oldName[ATTR_SIZE], char newName[ATTR_SIZE]) {
  Attribute relNameAttr;
  strcpy(relNameAttr.sVal, relName);

  // Search for the relation with name relName in relation catalog using Linear Search
  RelCacheTable::resetSearchIndex(RELCAT_RELID);
  char relCatAttrRelname[] = RELCAT_ATTR_RELNAME;
  RecId relCatRecId = linearSearch(RELCAT_RELID, relCatAttrRelname, relNameAttr, EQ);

  if (relCatRecId.block == -1 || relCatRecId.slot == -1) {
    return E_RELNOTEXIST;
  }

  // Iterating over all Attribute Catalog Entry record corresponding to relation to find the required attribute
  RelCacheTable::resetSearchIndex(ATTRCAT_RELID);

  RecId attrToRenameRecId{-1, -1};
  Attribute attrCatEntryRecord[ATTRCAT_NO_ATTRS];

  char attrCatAttrRelName[] = ATTRCAT_ATTR_RELNAME;

  while (true) {
    RecId attrCatRecId = linearSearch(ATTRCAT_RELID, attrCatAttrRelName, relNameAttr, EQ);

    if (attrCatRecId.block == -1 || attrCatRecId.slot == -1) {
      break;
    }

    // Get the attribute catalog record from the attribute catalog
    RecBuffer attrCatBuffer(attrCatRecId.block);
    attrCatBuffer.getRecord(attrCatEntryRecord, attrCatRecId.slot);

    if (strcmp(attrCatEntryRecord[ATTRCAT_ATTR_NAME_INDEX].sVal, oldName) == 0) {
      attrToRenameRecId = attrCatRecId;
    }
    if (strcmp(attrCatEntryRecord[ATTRCAT_ATTR_NAME_INDEX].sVal, newName) == 0) {
      return E_ATTREXIST;
    }
  }

  if (attrToRenameRecId.block == -1 || attrToRenameRecId.slot == -1) {
    return E_ATTRNOTEXIST;
  }

  // Update the entry corresponding to the attribute in the Attribute Catalog Relation.
  RecBuffer attrCatBuffer(attrToRenameRecId.block);
  attrCatBuffer.getRecord(attrCatEntryRecord, attrToRenameRecId.slot);
  strcpy(attrCatEntryRecord[ATTRCAT_ATTR_NAME_INDEX].sVal, newName);
  attrCatBuffer.setRecord(attrCatEntryRecord, attrToRenameRecId.slot);

  return SUCCESS;
}

int BlockAccess::deleteRelation(char relName[ATTR_SIZE]) {
  // Search for the relation with name relName in relation catalog using Linear Search
  Attribute relNameAttr;
  strcpy(relNameAttr.sVal, relName);

  RelCacheTable::resetSearchIndex(RELCAT_RELID);
  char relCatAttrRelname[] = RELCAT_ATTR_RELNAME;
  RecId relCatRecId = linearSearch(RELCAT_RELID, relCatAttrRelname, relNameAttr, EQ);

  if (relCatRecId.block == -1 || relCatRecId.slot == -1) {
    return E_RELNOTEXIST;
  }

  RecBuffer relCatRec(RELCAT_BLOCK);
  Attribute relCatEntryRecord[RELCAT_NO_ATTRS];
  relCatRec.getRecord(relCatEntryRecord, relCatRecId.slot);

  int firstBlock = (int)relCatEntryRecord[RELCAT_FIRST_BLOCK_INDEX].nVal;

  // Delete all the record blocks of the relation
  int block = firstBlock;
  while (block != -1) {
    RecBuffer blockBuf(block);
    HeadInfo blockHead;
    blockBuf.getHeader(&blockHead);
    blockBuf.releaseBlock();
    block = blockHead.rblock;
  }

  /*** Deleting attribute catalog entries corresponding the relation and index blocks corresponding to the relation with relName on its attributes ***/
  RelCacheTable::resetSearchIndex(ATTRCAT_RELID);

  char attrCatAttrRelname[] = ATTRCAT_ATTR_RELNAME;
  int numberOfAttributesDeleted = 0;
  while (true) {
    // search for all the attributes corresponding to the relation with relName in attribute catalog
    RecId attrCatRecId = linearSearch(ATTRCAT_RELID, attrCatAttrRelname, relNameAttr, EQ);
    if (attrCatRecId.block == -1 || attrCatRecId.slot == -1) {
      break;
    }

    numberOfAttributesDeleted++;

    RecBuffer attrCatRec(attrCatRecId.block);
    HeadInfo attrCatBlockHead;
    attrCatRec.getHeader(&attrCatBlockHead);

    // get the rootBlock from attribute catalog. This will be used later to delete any indexes if it exists
    Attribute attrCatEntryRecord[ATTRCAT_NO_ATTRS];
    attrCatRec.getRecord(attrCatEntryRecord, attrCatRecId.slot);
    int rootBlock = (int)attrCatEntryRecord[ATTRCAT_ROOT_BLOCK_INDEX].nVal;

    // Update the Slotmap for the block by indicating the slot as free
    unsigned char blockSlotMap[attrCatBlockHead.numSlots];
    attrCatRec.getSlotMap(blockSlotMap);
    blockSlotMap[attrCatRecId.slot] = SLOT_UNOCCUPIED;
    attrCatRec.setSlotMap(blockSlotMap);

    // Adjust the number of entries in the block (decrease by 1) corresponding to the attribute catalog entry
    attrCatBlockHead.numEntries--;
    attrCatRec.setHeader(&attrCatBlockHead);

    // If number of entries become 0, releaseBlock is called after fixing the Linked List.
    if (attrCatBlockHead.numEntries == 0) {
      /* Standard Linked List Delete for a Block */
      // Get the header of the left block and set it's rblock to the block's rblock
      // (no need to check for empty lblock since attr catalog will never be empty)
      RecBuffer leftBlockRec(attrCatBlockHead.lblock);
      HeadInfo leftBlockHead;
      leftBlockRec.getHeader(&leftBlockHead);
      leftBlockHead.rblock = attrCatBlockHead.rblock;
      leftBlockRec.setHeader(&leftBlockHead);

      if (attrCatBlockHead.rblock != -1) {
        // Get the header of the right block and set it's lblock to the block's lblock
        RecBuffer rightBlockRec(attrCatBlockHead.rblock);
        HeadInfo rightBlockHead;
        rightBlockRec.getHeader(&rightBlockHead);
        rightBlockHead.lblock = attrCatBlockHead.lblock;
        rightBlockRec.setHeader(&rightBlockHead);
      } else {
        // the block being released is the "Last Block" of the relation.
        // Update the Relation Catalog entry's LastBlock field for this relation with the block number of the previous block.
        relCatEntryRecord[RELCAT_LAST_BLOCK_INDEX].nVal = attrCatBlockHead.lblock;
        relCatRec.setRecord(relCatEntryRecord, relCatRecId.slot);
      }

      attrCatRec.releaseBlock();
    }

    // if index exists for the attribute (rootBlock != -1), call bplus destroy
    if (rootBlock != -1) {
      BPlusTree::bPlusDestroy(rootBlock);
    }
  }

  /*** Delete the relation catalog entry corresponding to the relation from relation catalog ***/
  // Fetch the header of Relcat block
  HeadInfo relCatHead;
  relCatRec.getHeader(&relCatHead);
  relCatHead.numEntries--;
  relCatRec.setHeader(&relCatHead);

  // Get the slotmap in relation catalog, update it by marking the slot as free(use SLOT_UNOCCUPIED) and set it back.
  unsigned char relCatSlotMap[relCatHead.numSlots];
  relCatRec.getSlotMap(relCatSlotMap);
  relCatSlotMap[relCatRecId.slot] = SLOT_UNOCCUPIED;
  relCatRec.setSlotMap(relCatSlotMap);

  /*** Updating the Relation Cache Table ***/
  /** Update relation catalog record entry (number of records in relation catalog is decreased by 1) **/
  RelCatEntry relCatInRelCache;
  RelCacheTable::getRelCatEntry(RELCAT_RELID, &relCatInRelCache);
  relCatInRelCache.numRecs--;
  RelCacheTable::setRelCatEntry(RELCAT_RELID, &relCatInRelCache);

  /** Update attribute catalog entry (number of records in attribute catalog is decreased by numberOfAttributesDeleted) **/
  RelCatEntry attrCatInRelCache;
  RelCacheTable::getRelCatEntry(ATTRCAT_RELID, &attrCatInRelCache);
  attrCatInRelCache.numRecs -= numberOfAttributesDeleted;
  RelCacheTable::setRelCatEntry(ATTRCAT_RELID, &attrCatInRelCache);

  return SUCCESS;
}

int BlockAccess::project(int relId, Attribute *record) {
  struct RecId prevRecId;
  RelCacheTable::getSearchIndex(relId, &prevRecId);

  // let block and slot denote the record id of the record being currently checked
  int block, slot;

  // if the current search index record is invalid(i.e. both block and slot = -1)
  if (prevRecId.block == -1 && prevRecId.slot == -1) {
    // (new project operation)
    struct RelCatEntry relCatEntry;
    RelCacheTable::getRelCatEntry(relId, &relCatEntry);
    // get the first record block of the relation from the relation cache
    block = relCatEntry.firstBlk;
    slot = 0;
  } else {
    // (a project operation is already in progress)
    block = prevRecId.block;
    slot = prevRecId.slot + 1;
  }

  while (block != -1) {
    RecBuffer blockBuf = RecBuffer(block);

    HeadInfo blockHead;
    blockBuf.getHeader(&blockHead);
    unsigned char slotMap[blockHead.numSlots];
    blockBuf.getSlotMap(slotMap);

    if (slot >= blockHead.numSlots) {
      // (no more slots in this block)
      block = blockHead.rblock;
      slot = 0;
    } else if (slotMap[slot] == SLOT_UNOCCUPIED) {
      ++slot;
    } else {
      break;
    }
  }

  // all records exhausted
  if (block == -1) {
    return E_NOTFOUND;
  }

  RecId nextRecId{block, slot};

  // set the search index in the relation cache to the recId of the next record
  RelCacheTable::setSearchIndex(relId, &nextRecId);

  RecBuffer recBuffer(block);
  recBuffer.getRecord(record, slot);

  return SUCCESS;
}