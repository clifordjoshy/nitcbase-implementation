#include "BlockBuffer.h"

#include <cstdlib>
#include <cstring>

using namespace std;

// Constructor 1
BlockBuffer::BlockBuffer(char blockType) {
  // allocate a block on the disk and a buffer in memory to hold the new block of given type using getFreeBlock function and get the return error codes if any.
  int blockNum;
  switch (blockType) {
    case 'R':
      blockNum = getFreeBlock(REC);
      break;
    case 'I':
      blockNum = getFreeBlock(IND_INTERNAL);
      break;
    case 'L':
      blockNum = getFreeBlock(IND_LEAF);
      break;
    default:
      blockNum = FAILURE;
  }

  // set the blockNum field of the object to that of the allocated block number if the method returned a valid block number,
  // otherwise set the error code returned as the block number.
  this->blockNum = blockNum;

  // The caller must check if the constructor allocatted block successfully by checking the value of block number field.
}

// Constructor 2
BlockBuffer::BlockBuffer(int blockNum) {
  // set the blockNum field of the object to input argument.
  this->blockNum = blockNum;
}

int BlockBuffer::getBlockNum() {
  // return corresponding block number
  return this->blockNum;
}

int BlockBuffer::getBlockType() {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if the call to loadBlockAndGetBufferPtr(&bufferPtr) return SUCCESS
  if (ret == SUCCESS) {
    // return the first 4 bytes of the buffer that stores the block type. (Hint: cast using int32_t)
    return (int32_t)(*bufferPtr);
  } else {
    // else load failed due to E_OUTOFBOUND, invalid block number, return the value returned by the call.
    return ret;
  }
}

int BlockBuffer::setBlockType(int blockType) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  if (ret != SUCCESS) {
    // return the value returned by the call.
    return ret;
  }

  // store the input block type in the first 4 bytes of the buffer.
  *((int32_t *)bufferPtr) = blockType;

  // update the StaticBuffer::blockAllocMap entry corresponding to the object's block number.
  StaticBuffer::blockAllocMap[this->blockNum] = (unsigned char)blockType;

  // update dirty bit by calling appropriate method of StaticBuffer class.
  // if setDirtyBit() failed
  // return the returned value from the call

  // return SUCCESS
  ret = StaticBuffer::setDirtyBit(this->blockNum);

  return ret;
}

int BlockBuffer::getHeader(struct HeadInfo *head) {
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  unsigned char *bufferPtr;
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // Use type casting here to cast the returned pointer type to the appropriate struct pointer to get the headInfo
  struct HeadInfo *bufferHeader = (struct HeadInfo *)bufferPtr;

  // copy the header of block to the memory location pointed to by the argument head.
  // not copying reserved
  head->blockType = bufferHeader->blockType;
  head->pblock = bufferHeader->pblock;
  head->lblock = bufferHeader->lblock;
  head->rblock = bufferHeader->rblock;
  head->numEntries = bufferHeader->numEntries;
  head->numAttrs = bufferHeader->numAttrs;
  head->numSlots = bufferHeader->numSlots;

  return SUCCESS;
}

int BlockBuffer::setHeader(struct HeadInfo *head) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // Use type casting here to cast the returned pointer type to the appropriate struct pointer to get the headInfo
  struct HeadInfo *bufferHeader = (struct HeadInfo *)bufferPtr;

  // copy the contents of the memory location pointed to by head to the header of block using appropriate.
  //  not copying reserved
  bufferHeader->blockType = head->blockType;
  bufferHeader->pblock = head->pblock;
  bufferHeader->lblock = head->lblock;
  bufferHeader->rblock = head->rblock;
  bufferHeader->numEntries = head->numEntries;
  bufferHeader->numAttrs = head->numAttrs;
  bufferHeader->numSlots = head->numSlots;

  // update dirty bit by calling appropriate method of StaticBuffer class.
  //  if setDirtyBit() failed, return the error code
  ret = StaticBuffer::setDirtyBit(this->blockNum);

  return ret;
}

void BlockBuffer::releaseBlock() {
  // if blockNum is INVALID_BLOCK (-1), or it is invalidated already, do nothing
  if (this->blockNum == INVALID_BLOCKNUM) {
    return;
  }
  // else
  // get the buffer number of the buffer assigned to the block using StaticBuffer::getBufferNum().
  int bufferNum = StaticBuffer::getBufferNum(this->blockNum);

  // if the buffer number is valid (!=E_BLOCKNOTINBUFFER), free the buffer by setting the free flag of its metaInfo entry to true.
  if (bufferNum != E_BLOCKNOTINBUFFER) {
    StaticBuffer::metainfo[bufferNum].free = true;
  }
  // free the block in disk by setting the data type of the entry corresponding to the block number in StaticBuffer::blockAllocMap to UNUSED_BLK.
  StaticBuffer::blockAllocMap[blockNum] = UNUSED_BLK;

  // set the object's blockNum to INVALID_BLOCK (-1)
  this->blockNum = INVALID_BLOCKNUM;
}

/* NOTE: This function will NOT check if the block already exists in disk or not,
   rather will copy whatever content is there in that disk block to the buffer.
   Only call this if the Block exists in disk already, otherwise call constructor 1 to allocate space for a new block.
   Also ensure that all getter and setter methods accessing the block's data should call the loadBlockAndGetBufferPtr().
 */
int BlockBuffer::loadBlockAndGetBufferPtr(unsigned char **buffPtr) {
  // check whether the block is already present in the buffer using StaticBuffer.getBufferNum()
  int bufferNum = StaticBuffer::getBufferNum(this->blockNum);

  // if present (!=E_BLOCKNOTINBUFFER), set the timestamp of the corresponding buffer to 0 and increment the timestamps of all other occupied buffers in the BufferMetaInfo.
  if (bufferNum != E_BLOCKNOTINBUFFER) {
    for (int bufferIndex = 0; bufferIndex < BUFFER_CAPACITY; ++bufferIndex) {
      if (!StaticBuffer::metainfo[bufferIndex].free) {
        StaticBuffer::metainfo[bufferIndex].timeStamp++;
      }
    }

    StaticBuffer::metainfo[bufferNum].timeStamp = 0;
  } else {
    /* else
      if not present, get a free buffer using StaticBuffer.getFreeBuffer()
      if the call returns E_OUTOFBOUND, return E_OUTOFBOUND here as the blockNum is invalid
      Read the block into the free buffer using readBlock()
      If the read failed, the block number is invalid return E_OUTOFBOUND;*/

    bufferNum = StaticBuffer ::getFreeBuffer(this->blockNum);
    if (bufferNum == E_OUTOFBOUND) {
      return E_OUTOFBOUND;
    }

    int readStatus = Disk::readBlock(StaticBuffer::blocks[bufferNum], this->blockNum);
    if (readStatus == E_OUTOFBOUND) {
      return E_OUTOFBOUND;
    }
  }

  // store the pointer to this buffer (blocks[bufferNum]) in *buffPtr
  *buffPtr = StaticBuffer::blocks[bufferNum];

  return SUCCESS;
}

int BlockBuffer::getFreeBlock(int blockType) {
  // iterate through the StaticBuffer.blockAllocMap and find the index of a free block in the disk.
  int freeBlock = -1;
  for (int blockIndex = 0; blockIndex < DISK_BLOCKS; ++blockIndex) {
    if (StaticBuffer::blockAllocMap[blockIndex] == UNUSED_BLK) {
      freeBlock = blockIndex;
      break;
    }
  }

  // if no block is free, return E_DISKFULL.
  if (freeBlock == -1) {
    return E_DISKFULL;
  }

  // set the object's blockNum to the block number of the free block.
  this->blockNum = freeBlock;

  //  find a free buffer using StaticBuffer.getFreeBuffer()
  StaticBuffer::getFreeBuffer(freeBlock);

  // initialize the header of the block with pblock: -1, lblock: -1, rblock: -1, numEntries: 0, numAttrs: 0 and numSlots: 0 using setHeader().
  HeadInfo headInfo;
  headInfo.pblock = -1;
  headInfo.rblock = -1;
  headInfo.lblock = -1;
  headInfo.numEntries = 0;
  headInfo.numAttrs = 0;
  headInfo.numSlots = 0;
  this->setHeader(&headInfo);

  // update the block type of the block to the input block type using setBlockType().
  this->setBlockType(blockType);

  // return block number of the free block.
  return freeBlock;
}

RecBuffer::RecBuffer(int blockNum) : BlockBuffer(blockNum) {}

RecBuffer::RecBuffer() : BlockBuffer('R') {}

int RecBuffer::getSlotMap(unsigned char *slotMap) {
  unsigned char *bufferPtr;

  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // Use type casting here to cast the returned pointer type to the appropriate struct pointer
  struct HeadInfo head;
  getHeader(&head);

  // get the number of slots in the block.
  int slotCount = head.numSlots;

  // using offset range copy the slotmap of the block to the memory pointed by the argument.
  unsigned char *slotMapInBuffer = bufferPtr + HEADER_SIZE;
  for (int i = 0; i < slotCount; ++i) {
    slotMap[i] = slotMapInBuffer[i];
  }

  return SUCCESS;
}

int RecBuffer::setSlotMap(unsigned char *slotMap) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // Use type casting here to cast the returned pointer type to the appropriate struct pointer to access headInfo
  struct HeadInfo head;
  getHeader(&head);

  // get the number of slots in the block.
  int slotCount = head.numSlots;

  // using offset range copy the slotmap from the memory pointed by the argument to that of the block.
  unsigned char *slotMapInBuffer = bufferPtr + HEADER_SIZE;
  for (int i = 0; i < slotCount; ++i) {
    slotMapInBuffer[i] = slotMap[i];
  }

  // update dirty bit.
  // if setDirtyBit failed, return the value returned by the call
  ret = StaticBuffer::setDirtyBit(this->blockNum);

  return ret;
}

int RecBuffer::getRecord(union Attribute *rec, int slotNum) {
  unsigned char *bufferPtr;

  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // Use type casting here to cast the returned pointer type to the appropriate struct pointer to access headInfo
  struct HeadInfo head;
  getHeader(&head);

  // get number of attributes in the block.
  int attrCount = head.numAttrs;

  // get the number of slots in the block.
  int slotCount = head.numSlots;

  // if input slotNum is not in the permitted range return E_OUTOFBOUND
  if (slotNum < 0 && slotNum >= slotCount) {
    return E_OUTOFBOUND;
  }

  unsigned char slotMap[slotCount];
  getSlotMap(slotMap);

  // if slot corresponding to input slotNum is free return E_FREESLOT
  if (slotMap[slotNum] == SLOT_UNOCCUPIED) {
    return E_FREESLOT;
  }

  // using offset range copy slotNumth record to the memory pointed by rec.
  int recordSize = attrCount * ATTR_SIZE;
  unsigned char *slotPointer = bufferPtr + HEADER_SIZE + slotCount + (recordSize * slotNum);
  memcpy(rec, slotPointer, recordSize);

  return SUCCESS;
}

int RecBuffer::setRecord(union Attribute *rec, int slotNum) {
  unsigned char *bufferPtr;

  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // Use type casting here to cast the returned pointer type to the appropriate struct pointer to access headInfo
  struct HeadInfo head;
  getHeader(&head);

  // get number of attributes in the block.
  int attrCount = head.numAttrs;

  // get the number of slots in the block.
  int slotCount = head.numSlots;

  // if input slotNum is not in the permitted range return E_OUTOFBOUND
  if (slotNum < 0 && slotNum >= slotCount) {
    return E_OUTOFBOUND;
  }

  // using offset range copy contents of the memory pointed by rec to slotNumth record.
  int recordSize = attrCount * ATTR_SIZE;
  unsigned char *slotPointer = bufferPtr + HEADER_SIZE + slotCount + (recordSize * slotNum);
  memcpy(slotPointer, rec, recordSize);

  // update dirty bit.
  // if setDirtyBit failed, return the value returned by the call
  ret = StaticBuffer::setDirtyBit(this->blockNum);

  return ret;
}

IndBuffer::IndBuffer(int blockNum) : BlockBuffer(blockNum) {}

IndBuffer::IndBuffer(char blockType) : BlockBuffer(blockType) {}

IndInternal::IndInternal() : IndBuffer('I') {}

IndInternal::IndInternal(int blockNum) : IndBuffer(blockNum) {}

int IndInternal::getEntry(void *ptr, int indexNum) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // if the indexNum is not in the valid range of 0-(MAX_KEYS_INTERNAL-1), return E_OUTOFBOUND.
  if (indexNum < 0 || indexNum > MAX_KEYS_INTERNAL) {
    return E_OUTOFBOUND;
  }
  // copy the indexNum'th Internalentry in block to memory ptr(ptr can be type casted appropriately if needed).
  unsigned char *entryPtr = bufferPtr + HEADER_SIZE + (indexNum * 20);
  struct InternalEntry *internalEntry = (struct InternalEntry *)ptr;
  memcpy(internalEntry, entryPtr, 4);
  memcpy(&(internalEntry->attrVal), entryPtr + 4, ATTR_SIZE);
  memcpy(&(internalEntry->rChild), entryPtr + 20, 4);

  return SUCCESS;
}

int IndInternal::setEntry(void *ptr, int indexNum) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // if the indexNum is not in the valid range of 0-(MAX_KEYS_INTERNAL-1), return E_OUTOFBOUND.
  if (indexNum < 0 || indexNum > MAX_KEYS_INTERNAL) {
    return E_OUTOFBOUND;
  }

  // copy the struct InternalEntry pointed by ptr to indexNum'th entry in block.
  unsigned char *entryPtr = bufferPtr + HEADER_SIZE + (indexNum * 20);
  struct InternalEntry *internalEntry = (struct InternalEntry *)ptr;
  memcpy(entryPtr, &(internalEntry->lChild), 4);
  memcpy(entryPtr + 4, &(internalEntry->attrVal), ATTR_SIZE);
  memcpy(entryPtr + 20, &(internalEntry->rChild), 4);

  // update dirty bit.
  // if setDirtyBit failed, return the value returned by the call
  ret = StaticBuffer::setDirtyBit(this->blockNum);

  return ret;
}

IndLeaf::IndLeaf() : IndBuffer('L') {}

IndLeaf::IndLeaf(int blockNum) : IndBuffer(blockNum) {}

int IndLeaf::getEntry(void *ptr, int indexNum) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // if the indexNum is not in the valid range of 0-(MAX_KEYS_LEAF-1), return E_OUTOFBOUND.
  if (indexNum < 0 || indexNum > MAX_KEYS_LEAF) {
    return E_OUTOFBOUND;
  }

  // copy the indexNum'th Index entry in block to memory ptr(ptr can be type casted appropriately if needed).
  unsigned char *entryPtr = bufferPtr + HEADER_SIZE + (indexNum * LEAF_ENTRY_SIZE);
  memcpy((struct Index *)ptr, entryPtr, LEAF_ENTRY_SIZE);

  return SUCCESS;
}

int IndLeaf::setEntry(void *ptr, int indexNum) {
  unsigned char *bufferPtr;
  // get the starting address of the buffer containing the block using loadBlockAndGetBufferPtr(&bufferPtr).
  int ret = loadBlockAndGetBufferPtr(&bufferPtr);

  // if loadBlockAndGetBufferPtr(&bufferPtr) != SUCCESS
  // return the value returned by the call.
  if (ret != SUCCESS) {
    return ret;
  }

  // if the indexNum is not in the valid range of 0-(MAX_KEYS_LEAF-1), return E_OUTOFBOUND.
  if (indexNum < 0 || indexNum > MAX_KEYS_LEAF) {
    return E_OUTOFBOUND;
  }

  // copy the struct Index pointed by ptr to indexNum'th entry in block.
  unsigned char *entryPtr = bufferPtr + HEADER_SIZE + (indexNum * LEAF_ENTRY_SIZE);
  memcpy(entryPtr, (struct Index *)ptr, LEAF_ENTRY_SIZE);

  // update dirty bit.
  // if setDirtyBit failed, return the value returned by the call
  ret = StaticBuffer::setDirtyBit(this->blockNum);

  return ret;
}

int compareAttrs(Attribute attr1, Attribute attr2, int attrType) {
  if (attrType == STRING) {
    return strcmp(attr1.sVal, attr2.sVal);
  } else {
    return attr1.nVal - attr2.nVal;
  }
}