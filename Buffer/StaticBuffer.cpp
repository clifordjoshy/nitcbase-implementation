#include "StaticBuffer.h"

#include "../Disk_Class/Disk.h"
#include "../define/constants.h"
#include "StaticBuffer.h"

// declare static members
unsigned char StaticBuffer::blocks[BUFFER_CAPACITY][BLOCK_SIZE];
struct BufferMetaInfo StaticBuffer::metainfo[BUFFER_CAPACITY];
unsigned char StaticBuffer::blockAllocMap[DISK_BLOCKS];

StaticBuffer::StaticBuffer() {
  // copy blockAllocMap blocks from disk to buffer (using readblock() of disk)
  // blocks 0 to 3
  for (int blockNum = 0; blockNum < BLOCK_ALLOCATION_MAP_SIZE; ++blockNum) {
    int offset = BLOCK_SIZE * blockNum;
    Disk::readBlock(blockAllocMap + offset, blockNum);
  }

  // initialise metainfo of all the buffer blocks with dirty:false, free:true, timestamp:-1 and blockNum:-1
  for (int bufferIndex = 0; bufferIndex < BUFFER_CAPACITY; ++bufferIndex) {
    metainfo[bufferIndex].dirty = false;
    metainfo[bufferIndex].free = true;
    metainfo[bufferIndex].timeStamp = -1;
    metainfo[bufferIndex].blockNum = -1;
  }
}

StaticBuffer::~StaticBuffer() {
  // copy blockAllocMap blocks from buffer to disk(using writeblock() of disk)
  for (int blockNum = 0; blockNum < BLOCK_ALLOCATION_MAP_SIZE; ++blockNum) {
    int offset = BLOCK_SIZE * blockNum;
    Disk::writeBlock(blockAllocMap + offset, blockNum);
  }

  /*iterate through all the buffer blocks,
          write back blocks with meta info as free:false,dirty:true (using writeblock() of disk)*/
  for (int bufferIndex = 0; bufferIndex < BUFFER_CAPACITY; ++bufferIndex) {
    if (metainfo[bufferIndex].free == false && metainfo[bufferIndex].dirty == true) {
      Disk::writeBlock(blocks[bufferIndex], metainfo[bufferIndex].blockNum);
    }
  }
}

int StaticBuffer::getFreeBuffer(int blockNum) {
  // Check if blockNum is valid (non zero and less than number of disk blocks)
  // and return E_OUTOFBOUND if not valid.
  if (blockNum < 0 || blockNum > DISK_BLOCKS) {
    return E_OUTOFBOUND;
  }

  int allocatedBuffer = -1, maxTimestamp = -1, maxTimestampBuffer = -1;

  // increase the time stamps in metainfo of all the occupied buffers.
  // if free buffer is available, bufferIndex is the index of the free buffer.
  // if free buffer is not available, replace the buffer with the largest timestamp and set it as bufferIndex.

  for (int bufferIndex = 0; bufferIndex < BUFFER_CAPACITY; ++bufferIndex) {
    if (metainfo[bufferIndex].free) {
      allocatedBuffer = bufferIndex;
    } else {
      if (metainfo[bufferIndex].timeStamp > maxTimestamp) {
        maxTimestampBuffer = bufferIndex;
        maxTimestamp = metainfo[bufferIndex].timeStamp;
      }
      metainfo[bufferIndex].timeStamp++;
    }
  }

  if (allocatedBuffer == -1) {
    // if the buffer is dirty, then write it to the disk.
    if (metainfo[maxTimestampBuffer].dirty) {
      Disk::writeBlock(blocks[maxTimestampBuffer], metainfo[maxTimestampBuffer].blockNum);
    }

    allocatedBuffer = maxTimestampBuffer;
  }

  // update the metainfo array corresponding to the buffer index.
  metainfo[allocatedBuffer].dirty = false;
  metainfo[allocatedBuffer].free = false;
  metainfo[allocatedBuffer].timeStamp = 0;
  metainfo[allocatedBuffer].blockNum = blockNum;

  // return the buffer index
  return allocatedBuffer;
}

int StaticBuffer::getBufferNum(int blockNum) {
  // Check if blockNum is valid (non zero and less than number of disk blocks)
  // and return E_OUTOFBOUND if not valid.
  if (blockNum < 0 || blockNum > DISK_BLOCKS) {
    return E_OUTOFBOUND;
  }

  // traverse through the metainfo array, find the buffer index of the buffer to which the block is loaded.
  // if found return buffer index, else indicate failure by returning E_BLOCKNOTINBUFFER
  for (int bufferIndex = 0; bufferIndex < BUFFER_CAPACITY; ++bufferIndex) {
    if (metainfo[bufferIndex].blockNum == blockNum) {
      return bufferIndex;
    }
  }
  return E_BLOCKNOTINBUFFER;
}

int StaticBuffer::getStaticBlockType(int blockNum) {
  // Check if blockNum is valid (non zero and less than number of disk blocks)
  // and return E_OUTOFBOUND if not valid.
  if (blockNum < 0 || blockNum > DISK_BLOCKS) {
    return E_OUTOFBOUND;
  }

  // Access the entry in block allocation map corresponding to the blockNum argument
  // and return the block type after type casting to integer.
  return blockAllocMap[blockNum];
}

int StaticBuffer::setDirtyBit(int blockNum) {
  // find the buffer index corresponding to the block using the getBufferNum().
  int bufferNum = getBufferNum(blockNum);

  // set the dirty bit of that buffer in the metaInfo to true.
  if (bufferNum >= 0) {
    metainfo[bufferNum].dirty = true;
    return SUCCESS;
  }

  // return error
  return bufferNum;
}
