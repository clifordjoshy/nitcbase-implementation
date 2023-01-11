#include "BPlusTree.h"

#include <cstring>

int BPlusTree::bPlusCreate(int relId, char attrName[ATTR_SIZE]) {
  AttrCatEntry attrCatEntry;
  int retVal = AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

  if (retVal != SUCCESS) {
    return retVal;
  }

  int rootBlockNum = attrCatEntry.rootBlock;

  if (rootBlockNum != -1) {
    // (index already exists for the attribute.)
    return SUCCESS;
  }

  /******Creeating a new B+ Tree ******/

  // get a free leaf block.
  IndLeaf rootBlockBuf;
  int rootBlock = rootBlockBuf.getBlockNum();

  if (rootBlock == E_DISKFULL) {
    return E_DISKFULL;
  }

  attrCatEntry.rootBlock = rootBlock;
  AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

  RelCatEntry relCatEntry;
  RelCacheTable::getRelCatEntry(relId, &relCatEntry);

  int block = relCatEntry.firstBlk;

  /******Traverse all the blocks in the relation and insert them one by one into the B+ Tree******/
  while (block != -1) {
    RecBuffer blockBuf(block);

    unsigned char slotMap[relCatEntry.numSlotsPerBlk];

    blockBuf.getSlotMap(slotMap);

    for (int slot = 0; slot < relCatEntry.numSlotsPerBlk; ++slot) {
      if (slotMap[slot] == SLOT_OCCUPIED) {
        union Attribute record[relCatEntry.numAttrs];
        blockBuf.getRecord(record, slot);

        RecId recId{block, slot};
        // insert the attribute value of the record corresponding to attrName using bPlusInsert.
        // will destroy the bplus tree if insert fails
        retVal = bPlusInsert(relId, attrName, record[attrCatEntry.offset], recId);

        if (retVal == E_DISKFULL) {
          // disk is full, i.e,  unable to get enough blocks to build the B+ Tree.
          return E_DISKFULL;
        }
      }
    }

    HeadInfo blockHead;
    blockBuf.getHeader(&blockHead);
    block = blockHead.rblock;
  }

  return SUCCESS;
}

int BPlusTree::bPlusInsert(int relId, char attrName[ATTR_SIZE], Attribute attrVal, RecId recId) {
  AttrCatEntry attrCatEntry;
  int retVal = AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

  if (retVal != SUCCESS) {
    return retVal;
  }

  int blockNum = attrCatEntry.rootBlock;

  if (blockNum == -1) {
    // (B+ Tree does not exist for attrName.)
    return E_NOINDEX;
  }

  /******Traverse the B+ Tree to reach the appropriate leaf where insertion can be done******/
  while (StaticBuffer::getStaticBlockType(blockNum) != IND_LEAF) {
    IndInternal indBlock(blockNum);
    HeadInfo blockHeader;
    indBlock.getHeader(&blockHeader);

    /*iterate through all the entries, to find first the entry whose attibute value >= value to be inserted.*/
    InternalEntry intIndEntry;
    int entryIndex;
    for (entryIndex = 0; entryIndex < blockHeader.numEntries; ++entryIndex) {
      indBlock.getEntry(&intIndEntry, entryIndex);
      if (compareAttrs(intIndEntry.attrVal, attrVal, attrCatEntry.attrType) > 0) {
        break;
      }
    }
    if (entryIndex == blockHeader.numEntries) {
      // update blockNum with rChild of nEntries-1'th (i.e. last) entry of the block.
      blockNum = intIndEntry.rChild;
    } else {
      // update blockNum with lChild of the found entry of the block.
      blockNum = intIndEntry.lChild;
    }
  }

  // NOTE: now blockNum is the leaf index block to which insertion of val is to be done.

  /******Insertion of entry in the appropriate leaf block******/
  IndLeaf* leafBuf = new IndLeaf(blockNum);
  HeadInfo blockHeader;
  leafBuf->getHeader(&blockHeader);

  Index indexToIns;  // the Index entry to be inserted into B+ Tree.
  indexToIns.block = recId.block;
  indexToIns.slot = recId.slot;
  indexToIns.attrVal = attrVal;

  /*iterate through all the entries in the block and copy them to the array indices.
    Also insert indexval at appropriate position in the indices array.*/

  Index indices[blockHeader.numEntries + 1];
  int arrayPos = 0, recordPos = 0;
  while (recordPos < blockHeader.numEntries) {
    Index leafEntry;
    leafBuf->getEntry(&leafEntry, recordPos);
    if (arrayPos == recordPos && compareAttrs(leafEntry.attrVal, indexToIns.attrVal, attrCatEntry.attrType) > 0) {
      indices[arrayPos] = indexToIns;
      ++arrayPos;
    }
    indices[arrayPos] = leafEntry;
    ++arrayPos;
    ++recordPos;
  }
  if (arrayPos == blockHeader.numEntries) {
    // (index was not inserted anywhere in the middle)
    indices[arrayPos] = indexToIns;
  }

  if (blockHeader.numEntries != MAX_KEYS_LEAF) {
    // (leaf block has not reached max limit.)
    // increment blockHeader.numEntries and set this as header of block using BlockBuffer::setHeader().
    blockHeader.numEntries++;
    leafBuf->setHeader(&blockHeader);

    // iterate through all the entries of indices array and populate the entries of block with them using IndLeaf::setEntry().
    for (int i = 0; i < blockHeader.numEntries; ++i) {
      leafBuf->setEntry(indices + i, i);
    }
    delete leafBuf;
    return SUCCESS;
  }

  // (leaf block is full- need a new leaf to make the entry; split the entries between the two blocks.)

  // obtain new leaf index block to be used as the right block in the splitting
  IndBuffer* rightBlk = new IndLeaf();
  // assign the existing block as the left block in the splitting.
  IndBuffer* leftBlk = leafBuf;

  int rightBlkNum = rightBlk->getBlockNum();
  int leftBlkNum = leftBlk->getBlockNum();

  if (rightBlkNum == E_DISKFULL) {
    // destroy the existing B+ tree by passing rootBlock member field to bPlusDestroy().
    bPlusDestroy(attrCatEntry.rootBlock);

    attrCatEntry.rootBlock = -1;
    AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

    delete rightBlk;
    delete leftBlk;
    return E_DISKFULL;
  }

  HeadInfo leftBlkHeader = blockHeader;
  HeadInfo rightBlkHeader;
  rightBlk->getHeader(&rightBlkHeader);

  rightBlkHeader.numEntries = (MAX_KEYS_LEAF + 1) / 2;
  rightBlkHeader.rblock = leftBlkHeader.rblock;
  rightBlkHeader.pblock = leftBlkHeader.pblock;
  rightBlkHeader.lblock = leftBlkNum;
  rightBlk->setHeader(&rightBlkHeader);

  leftBlkHeader.numEntries = (MAX_KEYS_LEAF + 1) / 2;
  leftBlkHeader.rblock = rightBlkNum;
  leftBlk->setHeader(&leftBlkHeader);

  for (int i = 0; i < leftBlkHeader.numEntries; ++i) {
    leftBlk->setEntry(indices + i, i);
    rightBlk->setEntry(indices + MIDDLE_INDEX_LEAF + i + 1, i);
  }

  delete leftBlk;
  delete rightBlk;

  /******Traverse the internal index blocks of the B+ Tree bottom up making insertions wherever required******/

  // store pblock of leftBlk in parBlkNum.
  int parBlkNum = leftBlkHeader.pblock;

  // the attribute to be inserted into the parent
  Attribute newAttrVal = indices[MIDDLE_INDEX_LEAF].attrVal;

  while (parBlkNum != -1) {
    // (while not at the root block)

    IndInternal* parBlk = new IndInternal(parBlkNum);

    HeadInfo parHeader;
    parBlk->getHeader(&parHeader);

    InternalEntry internalEntries[parHeader.numEntries + 1];
    arrayPos = 0;
    recordPos = 0;
    while (recordPos < parHeader.numEntries) {
      InternalEntry internalEntry;
      parBlk->getEntry(&internalEntry, recordPos);
      if (arrayPos == recordPos && compareAttrs(internalEntry.attrVal, newAttrVal, attrCatEntry.attrType) > 0) {
        internalEntries[arrayPos] = {leftBlkNum, newAttrVal, rightBlkNum};
        ++arrayPos;
        internalEntry.lChild = rightBlkNum;
      }
      internalEntries[arrayPos] = internalEntry;
      ++arrayPos;
      ++recordPos;
    }
    if (arrayPos == parHeader.numEntries) {
      // (index was not inserted anywhere in the middle)
      internalEntries[arrayPos] = {leftBlkNum, newAttrVal, rightBlkNum};
    }

    if (parHeader.numEntries != MAX_KEYS_INTERNAL) {
      // (parblk has not reached max limit)
      // increment parheader.numEntries and update it as header of parblk using BlockBuffer::setHeader().
      parHeader.numEntries++;
      parBlk->setHeader(&parHeader);

      // iterate through all entries in internalEntries array and populate the entries of parblk with them using IndInternal::setEntry().
      for (int i = 0; i < parHeader.numEntries; ++i) {
        parBlk->setEntry(internalEntries + i, i);
      }
      delete parBlk;

      return SUCCESS;
    }

    /*the parent internal index is full- need a new internal index to make the entry;
      split the entries between the two blocks*/

    // assign parBlk as the left block in the splitting.
    leftBlk = parBlk;
    // get a new internal block for the right block
    rightBlk = new IndInternal();

    leftBlkNum = parBlkNum;
    rightBlkNum = rightBlk->getBlockNum();

    if (rightBlkNum == E_DISKFULL) {
      // destroy the right subtree, given by rightBlkNum, build up till now that has not yet been connected to the existing B+ Tree
      bPlusDestroy(rightBlkNum);
      // destroy the existing B+ tree by passing rootBlock member field to bPlusDestroy().
      bPlusDestroy(attrCatEntry.rootBlock);

      attrCatEntry.rootBlock = -1;
      AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

      delete leftBlk;
      delete rightBlk;

      return E_DISKFULL;
    }

    leftBlkHeader = parHeader;
    leftBlkHeader.numEntries = MAX_KEYS_INTERNAL / 2;
    leftBlk->setHeader(&leftBlkHeader);

    rightBlk->getHeader(&rightBlkHeader);
    rightBlkHeader.numEntries = MAX_KEYS_INTERNAL / 2;
    rightBlk->setHeader(&rightBlkHeader);

    // middle child is going to the parent.
    for (int i = 0; i < MAX_KEYS_INTERNAL / 2; ++i) {
      // from 0 .. MAX_KEYS/2-1
      leftBlk->setEntry(internalEntries + i, i);

      // from MAX_KEYS/2 .. MAX_KEYS
      rightBlk->setEntry(internalEntries + MIDDLE_INDEX_INTERNAL + i + 1, i);
    }

    /*store the block type of a child of any entry .*/
    int type = StaticBuffer::getStaticBlockType(internalEntries[0].rChild);

    // iterate from 50 to 100. (index 50 rchild is index 51 lchild, so no need to handle that separately)
    for (int i = MAX_KEYS_INTERNAL / 2; i < MAX_KEYS_INTERNAL + 1; ++i) {
      IndBuffer* childBuff;
      // assign the rchild block of ith index in internalEntries of the appropriate type to childBuff.
      if (type == IND_LEAF)
        childBuff = new IndLeaf(internalEntries[i].rChild);
      else if (type == IND_INTERNAL)
        childBuff = new IndInternal(internalEntries[i].rChild);

      HeadInfo childBuffHead;
      childBuff->getHeader(&childBuffHead);
      childBuffHead.pblock = rightBlkNum;
      childBuff->setHeader(&childBuffHead);

      delete childBuff;
    }

    delete leftBlk;
    delete rightBlk;

    parBlkNum = leftBlkHeader.pblock;

    newAttrVal = internalEntries[MIDDLE_INDEX_INTERNAL].attrVal;
  }

  // root block has also been split.
  // Need to allot a new block which would be the root of the B+ Tree.

  IndInternal newRootBlk;
  int newRootBlkNum = newRootBlk.getBlockNum();

  if (newRootBlkNum == E_DISKFULL) {
    // destroy the right subtree, given by rightBlkNum, build up till now that has not yet been connected to the existing B+ Tree
    bPlusDestroy(rightBlkNum);
    // destroy the existing B+ tree
    bPlusDestroy(attrCatEntry.rootBlock);

    attrCatEntry.rootBlock = -1;
    AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);
    return E_DISKFULL;
  }

  HeadInfo newRootHeader;
  newRootBlk.getHeader(&newRootHeader);
  newRootHeader.numEntries = 1;
  newRootBlk.setHeader(&newRootHeader);

  InternalEntry rootEntry{.lChild = leftBlkNum, .attrVal = newAttrVal, .rChild = rightBlkNum};
  newRootBlk.setEntry(&rootEntry, 0);

  int type = StaticBuffer::getStaticBlockType(leftBlkNum);
  if (type == IND_INTERNAL) {
    leftBlk = new IndInternal(leftBlkNum);
    rightBlk = new IndInternal(rightBlkNum);
  } else {
    leftBlk = new IndLeaf(leftBlkNum);
    rightBlk = new IndLeaf(rightBlkNum);
  }

  leftBlk->getHeader(&leftBlkHeader);
  rightBlk->getHeader(&rightBlkHeader);
  leftBlkHeader.pblock = newRootBlkNum;
  rightBlkHeader.pblock = newRootBlkNum;
  leftBlk->setHeader(&leftBlkHeader);
  rightBlk->setHeader(&rightBlkHeader);

  attrCatEntry.rootBlock = newRootBlkNum;
  AttrCacheTable::setAttrCatEntry(relId, attrName, &attrCatEntry);

  delete leftBlk;
  delete rightBlk;

  return SUCCESS;
}

int BPlusTree::bPlusDestroy(int rootBlockNum) {
  if (rootBlockNum < 0 || rootBlockNum >= DISK_BLOCKS) {
    return E_OUTOFBOUND;
  }

  int type = StaticBuffer::getStaticBlockType(rootBlockNum);
  if (type == IND_LEAF) {
    IndLeaf leafBlk(rootBlockNum);
    leafBlk.releaseBlock();
    return SUCCESS;

  } else if (type == IND_INTERNAL) {
    IndInternal internalBlk(rootBlockNum);
    HeadInfo internalHead;
    internalBlk.getHeader(&internalHead);

    InternalEntry entry;

    /*iterate through all the entries of the internalBlk and destroy the lChild
    of the first entry and rChild of all entries using BPlusTree::bPlusDestroy(). */
    internalBlk.getEntry(&entry, 0);
    bPlusDestroy(entry.lChild);
    for (int i = 0; i < internalHead.numEntries; ++i) {
      bPlusDestroy(entry.rChild);
    }

    internalBlk.releaseBlock();
    return SUCCESS;

  } else {
    return E_INVALIDBLOCK;
  }
}

RecId BPlusTree::bPlusSearch(int relId, char attrName[ATTR_SIZE], Attribute attrVal, int op) {
  IndexId searchIndex;
  AttrCacheTable::getSearchIndex(relId, attrName, &searchIndex);

  AttrCacheTable::resetSearchIndex(relId, attrName);

  AttrCatEntry attrCatEntry;
  AttrCacheTable::getAttrCatEntry(relId, attrName, &attrCatEntry);

  // let block and index variables be used to locate the entry to be searched.
  int block, index;

  if (searchIndex.block == -1 || searchIndex.index == -1) {
    //(search is done for the first time)

    block = attrCatEntry.rootBlock;
    index = 0;  // start the search from the root.

    if (block == -1) {  // B+ Tree does not exist for attrName.
      return RecId{-1, -1};
    }

  } else {
    /*a valid searchIndex points to an entry in the leaf index of the attribute's B+ Tree which had
      previously satisfied the op for the given attrVal.*/

    block = searchIndex.block;
    index = searchIndex.index + 1;  // search is resumed from the next index.

    // load block into leaf using IndLeaf::IndLeaf().
    IndLeaf leaf(block);

    // let leafHead be used to hold the header of leaf.
    HeadInfo leafHead;
    leaf.getHeader(&leafHead);

    if (index >= leafHead.numEntries) {
      //(all the entries in the block has been searched; search from the beginning of the next leaf index block.)

      block = leafHead.rblock;
      index = 0;

      if (block == -1) {  // end of linked list reach - the search is done.
        return RecId{-1, -1};
      }
    }
  }

  /******Traverse through all the internal nodes according to value of AttrVal and the operator op******/

  while (StaticBuffer::getStaticBlockType(block) == IND_INTERNAL) {
    IndInternal internalBlk(block);
    HeadInfo intHead;
    InternalEntry intEntry;

    internalBlk.getHeader(&intHead);

    if (op == NE || op == LT || op == LE) {
      // get the first entry and go to the left child
      internalBlk.getEntry(&intEntry, 0);
      block = intEntry.lChild;

    } else {
      /*
        - EQ, GT and GE: move to the left child of the first entry that is
        greater than (or equal to) attrVal
      */

      bool foundEntry = false;
      for (int i = 0; i < intHead.numEntries; ++i) {
        internalBlk.getEntry(&intEntry, index);
        int cmpVal = compareAttrs(intEntry.attrVal, attrVal, attrCatEntry.attrType);
        if ((op == EQ && cmpVal >= 0) || (op == GT && cmpVal > 0) || (op == GE && cmpVal >= 0)) {
          foundEntry = true;
          break;
        }
      }

      /* if traversed all the entries without finding one that satisfies the condition,
      then move to the right child*/

      block = foundEntry ? intEntry.lChild : intEntry.rChild;
    }
  }

  // NOTE: the block is now a leaf index block.

  /******Traverse through index entries in the leaf index block from the index entry numbered as index******/
  while (block != -1) {
    IndLeaf leafBlk(block);
    HeadInfo leafHead;
    Index leafEntry;

    leafBlk.getHeader(&leafHead);

    while (index < leafHead.numEntries) {
      leafBlk.getEntry(&leafEntry, index);
      int cmpVal = compareAttrs(leafEntry.attrVal, attrVal, attrCatEntry.attrType);

      if (
          (op == EQ && cmpVal == 0) ||
          (op == LE && cmpVal <= 0) ||
          (op == LT && cmpVal < 0) ||
          (op == GT && cmpVal > 0) ||
          (op == GE && cmpVal >= 0) ||
          (op == NE && cmpVal != 0)) {
        // (entry satisfying the condition found)

        // set search index to {block, index}
        IndexId foundIndex{block, index};
        AttrCacheTable::setSearchIndex(relId, attrName, &foundIndex);

        return RecId{leafEntry.block, leafEntry.slot};

      } else if ((op == EQ || op == LE || op == LT) && cmpVal > 0) {
        /*future entries will not satisfy EQ, LE, LT since the values are
        arranged in ascending order in the leaves */

        return RecId{-1, -1};
      }

      // search next index.
      ++index;
    }

    /*only for NE do we have to check the entire linked list;
    for all the other op it is guaranteed that the block being searched will
    have an entry,if it exists, satisying that op.*/
    if (op != NE) {
      break;
    }

    // block = next block in the linked list, i.e., the rblock in the leafHead.
    block = leafHead.rblock;
    index = 0;
  }

  return RecId{-1, -1};
}
