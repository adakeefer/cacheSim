#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define NUMMEMORY 65536 /* maximum number of words in memory */
#define NUMREGS 8 /* number of machine registers */
#define MAXLINELENGTH 1000

typedef struct stateStruct {
    int pc;
    int mem[NUMMEMORY];
    int reg[NUMREGS];
    int numMemory;
} stateType;

typedef struct blockStruct {
    int valid;
    int dirty;
    int tag;
    int data[256];
} block;

typedef struct cacheLineStruct {
    //LRU - keep array of counts
    int LRU[256];
    int numBlocks;
    block* blocks[256];
} set;

enum actionType
    {cacheToProcessor, processorToCache, memoryToCache, cacheToMemory,
     cacheToNowhere};

int convertNum(int);

void reorderLRU(int * LRU, int accessed, int BLOCKSPERSET);

//print state of cache
void printAction(int address, int size, enum actionType type);

int getTag(int addr, int blockOffset, int setOffset);

int getBlock(int addr, int blockOffset);

int getSet(int addr, int blockOffset, int setOffset);

int
main(int argc, char *argv[])
{
    char line[MAXLINELENGTH];
    stateType state;
    stateType *statePtr = &state;
    FILE *filePtr;
    int opCode = 0;
    int reg1 = 0;
    int reg2 = 0;
    int destReg = 0;
    int offset = 0;
    int foundHalt = 0;
    int instr = 0;
    int blockOffset, setIndexSize;
    int blockAddr, tagAddr, setAddr;
    int temp = 0;
    int compMiss = 0;
    int confMiss = 0;
    int targBlock;
    int reconstructed;
int temp1, temp2;

    //init cache
    set* cache[256];

    if (argc != 5) {
        printf("error: usage: %s <machine-code file> <blockSizeInWords> <numberOfSets> <blocksPerSet>\n", argv[0]);
        exit(1);
    }

    filePtr = fopen(argv[1], "r");
    if (filePtr == NULL) {
        printf("error: can't open file %s", argv[1]);
        perror("fopen");
        exit(1);
    }

    const int BLOCKSIZE = atoi(argv[2]);
    const int NUMSETS = atoi(argv[3]);
    const int BLOCKSPERSET = atoi(argv[4]);

    for (offset = 0; offset < 256; ++offset) {
        cache[offset] = (set *)malloc(sizeof(set));
        cache[offset]->numBlocks = 0;
        for (instr = 0; instr < 256; ++instr) {
            cache[offset]->blocks[instr] = (block *)malloc(sizeof(block));
            cache[offset]->blocks[instr]->tag = 0;
            cache[offset]->blocks[instr]->valid = 0;
            cache[offset]->blocks[instr]->dirty = 0;
            for (temp = 0; temp < 256; ++temp)
                cache[offset]->blocks[instr]->data[temp] = 0;
        }//endfor - instr
    }//endfor - offset

    offset = 0;
    instr = 0;

    /* read in the entire machine-code file into memory */
    for (state.numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL;
            state.numMemory++) {

        if (sscanf(line, "%d", state.mem+state.numMemory) != 1) {
            printf("error in reading address %d\n", state.numMemory);
            exit(1);
        }
    }

    for (temp = state.numMemory; temp < NUMMEMORY; ++temp)
        state.mem[temp] = 0;

    //init registers
    for (destReg = 0; destReg < NUMREGS; ++destReg)
        state.reg[destReg] = 0;

    foundHalt = 0;

    //tag gets set, set index gets block, block offset gets data

    blockOffset = log(BLOCKSIZE) / log(2);
    setIndexSize = log(NUMSETS) / log(2);

    //step through machine code instructions
    for (state.pc = 0; state.pc < state.numMemory; state.pc++) {
        //check for cache miss on instr load
        blockAddr = getBlock(state.pc, blockOffset);
        tagAddr = getTag(state.pc, blockOffset, setIndexSize);
        setAddr = getSet(state.pc, blockOffset, setIndexSize);

        //try to find your block
        for (temp = 0; temp < BLOCKSPERSET; ++temp) {
            if (cache[setAddr]->blocks[temp]->tag == tagAddr &&
                cache[setAddr]->blocks[temp]->valid) {
                targBlock = temp;
                break;
            }
            else if (!cache[setAddr]->blocks[temp]->valid) {
                compMiss = 1;
                targBlock = temp;
                break;
            }
        }//endfor - find block

        if (temp == BLOCKSPERSET)
            confMiss = 1;
        
        //compulsory miss////////////////////////////////////////////////////////////////
        if (compMiss) {
          for (temp = 0; temp < BLOCKSIZE; ++temp) {
            cache[setAddr]->blocks[targBlock]->data[temp] 
            = state.mem[temp + (state.pc - (state.pc % BLOCKSIZE))];
          }//endfor - temp

          printAction(state.pc - (state.pc % BLOCKSIZE), BLOCKSIZE, memoryToCache);
          cache[setAddr]->blocks[targBlock]->valid = 1;
          if (cache[setAddr]->numBlocks > 0) {
              if (cache[setAddr]->LRU[cache[setAddr]->numBlocks - 1] >= cache[setAddr]->numBlocks)
                  cache[setAddr]->LRU[cache[setAddr]->numBlocks] = cache[setAddr]->LRU[cache[setAddr]->numBlocks - 1] - 1;
              else
                  cache[setAddr]->LRU[cache[setAddr]->numBlocks] = cache[setAddr]->numBlocks;
          }//endif
          else
              cache[setAddr]->LRU[0] = 0;

          cache[setAddr]->numBlocks++;
          cache[setAddr]->blocks[targBlock]->tag = tagAddr;
          compMiss = 0;
        }//endif - compulsory miss///////////////////////////////////////////////////////

        //capacity/conflict miss////////////////////////////////////////////////////////
        else if (confMiss) {
            //find LRU block and reorder LRU
            for (temp = 0; temp < BLOCKSPERSET; ++temp) {
                if (!cache[setAddr]->LRU[temp]) {
                    targBlock = temp;
                    break;
                }//endif - found LRU
            }//endfor - find LRU block


            //if block is dirty write it to memory before evicting
            if (cache[setAddr]->blocks[targBlock]->dirty) {
                for (temp = 0; temp < BLOCKSIZE; ++temp) {
                    reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                    reconstructed = (reconstructed | setAddr) << blockOffset;
                    state.mem[temp + (reconstructed - (reconstructed % BLOCKSIZE))] = 
                    cache[setAddr]->blocks[targBlock]->data[temp];
                }//endfor
                
                cache[setAddr]->blocks[targBlock]->dirty = 0;

                //reconstruct address to print
                reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                reconstructed = (reconstructed | setAddr) << blockOffset;
                printAction(reconstructed, BLOCKSIZE, cacheToMemory);
            }//endif - dirty bit

            //log eviction of clean block
            else {
                //reconstruct address to print
                reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                reconstructed = (reconstructed | setAddr) << blockOffset;
                printAction(reconstructed, BLOCKSIZE, cacheToNowhere);
            }//endelse

            //now load new block from memory
            for (temp = 0; temp < BLOCKSIZE; ++temp) {
                cache[setAddr]->blocks[targBlock]->data[temp] 
                = state.mem[temp + (state.pc - (state.pc % BLOCKSIZE))];
            }//endfor - temp

            cache[setAddr]->blocks[targBlock]->tag = tagAddr;

            printAction(state.pc - (state.pc % BLOCKSIZE), BLOCKSIZE, memoryToCache);
            confMiss = 0;
        }//endelif - cap/comp miss////////////////////////////////////////////////////////

        //cache hit/ read new instr if recovering from conflict/cap/compuls misses
        instr = cache[setAddr]->blocks[targBlock]->data[blockAddr];
        printAction(state.pc, 1, cacheToProcessor);

        reorderLRU(cache[setAddr]->LRU, targBlock, cache[setAddr]->numBlocks);



        opCode = instr >> 22;

        /*
        decode for reg1 and reg2
        register positioning universal except for halt and noop
        in which case the regs will simply be zero
        */
        reg1 = (instr >> 19) & 7;
        reg2 = (instr >> 16) & 7;

        switch(opCode) {
            //add
            case 0:
                //decode
                destReg = instr & 7;
                //operation
                state.reg[destReg] = state.reg[reg1] + state.reg[reg2];
                break;
            //nor
            case 1:
                destReg = instr & 7;
                state.reg[destReg] = ~(state.reg[reg1] | state.reg[reg2]);
                break;
            //lw
            case 2:
                offset = convertNum(instr & 0xffff) + state.reg[reg1]; 

                blockAddr = getBlock(offset, blockOffset);
                tagAddr = getTag(offset, blockOffset, setIndexSize);
                setAddr = getSet(offset, blockOffset, setIndexSize);

                //try to find your block
                for (temp = 0; temp < BLOCKSPERSET; ++temp) {
                    if (cache[setAddr]->blocks[temp]->tag == tagAddr &&
                        cache[setAddr]->blocks[temp]->valid) {
                        targBlock = temp;
                        break;
                    }
                    else if (!cache[setAddr]->blocks[temp]->valid) {
                        compMiss = 1;
                        targBlock = temp;
                        break;
                    }
                }//endfor - find block

                if (temp == BLOCKSPERSET)
                    confMiss = 1;
                
                //compulsory miss////////////////////////////////////////////////////////////////
                if (compMiss) {
                  for (temp = 0; temp < BLOCKSIZE; ++temp) {
                    cache[setAddr]->blocks[targBlock]->data[temp] 
                    = state.mem[temp + (offset - (offset % BLOCKSIZE))];
                  }//endfor - offset

                  printAction(offset - (offset % BLOCKSIZE), BLOCKSIZE, memoryToCache);
                  cache[setAddr]->blocks[targBlock]->valid = 1;
                  cache[setAddr]->LRU[cache[setAddr]->numBlocks] = cache[setAddr]->numBlocks;
                  cache[setAddr]->numBlocks++;
                  cache[setAddr]->blocks[targBlock]->tag = tagAddr;
                  compMiss = 0;
                }//endif - compulsory miss///////////////////////////////////////////////////////

                //capacity/conflict miss////////////////////////////////////////////////////////
                else if (confMiss) {
                    //find LRU block and reorder LRU
                    for (temp = 0; temp < BLOCKSPERSET; ++temp) {
                        if (!cache[setAddr]->LRU[temp]) {
                            targBlock = temp;
                            break;
                        }//endif - found LRU
                    }//endfor - find LRU block


                    //if block is dirty write it to memory before evicting
                    if (cache[setAddr]->blocks[targBlock]->dirty) {
                        for (temp = 0; temp < BLOCKSIZE; ++temp) {
                            reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                            reconstructed = (reconstructed | setAddr) << blockOffset;
                            state.mem[temp + (reconstructed - (reconstructed % BLOCKSIZE))] =
                            cache[setAddr]->blocks[targBlock]->data[temp];
                        }//endfor
                        
                        cache[setAddr]->blocks[targBlock]->dirty = 0;

                        //reconstruct address to print
                        reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                        reconstructed = (reconstructed | setAddr) << blockOffset;
                        printAction(reconstructed, BLOCKSIZE, cacheToMemory);
                    }//endif - dirty bit

                    //log eviction of clean block
                    else {
                        //reconstruct address to print
                        reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                        reconstructed = (reconstructed | setAddr) << blockOffset;
                        printAction(reconstructed, BLOCKSIZE, cacheToNowhere);
                    }//endelse

                    //now load new block from memory
                    for (temp = 0; temp < BLOCKSIZE; ++temp) {
                 	temp1 = cache[setAddr]->blocks[targBlock]->data[temp]; 
                        temp2 = state.mem[temp + (offset - (offset % BLOCKSIZE))];
			cache[setAddr]->blocks[targBlock]->data[temp] = 
			state.mem[temp + (offset - (offset % BLOCKSIZE))];
                    }//endfor - offset

                    cache[setAddr]->blocks[targBlock]->tag = tagAddr;

                    printAction(offset - (offset % BLOCKSIZE), BLOCKSIZE, memoryToCache);
                    confMiss = 0;
                }//endelif - cap/comp miss////////////////////////////////////////////////////////

                //cache hit/ read new instr if recovering from conflict/cap/compuls misses
                state.reg[reg2] = cache[setAddr]->blocks[targBlock]->data[blockAddr];
                printAction(offset, 1, cacheToProcessor);

                reorderLRU(cache[setAddr]->LRU, targBlock, cache[setAddr]->numBlocks);

                break;
            //sw
            case 3:
                offset = convertNum(instr & 0xffff) + state.reg[reg1];

                blockAddr = getBlock(offset, blockOffset);
                tagAddr = getTag(offset, blockOffset, setIndexSize);
                setAddr = getSet(offset, blockOffset, setIndexSize);

                //try to find your block
                for (temp = 0; temp < BLOCKSPERSET; ++temp) {
                    if (cache[setAddr]->blocks[temp]->tag == tagAddr &&
                        cache[setAddr]->blocks[temp]->valid) {
                        targBlock = temp;
                        break;
                    }
                    else if (!cache[setAddr]->blocks[temp]->valid) {
                        compMiss = 1;
                        targBlock = temp;
                        break;
                    }
                }//endfor - find block

                if (temp == BLOCKSPERSET)
                    confMiss = 1;
               
                //compulsory miss////////////////////////////////////////////////////////////////
                if (compMiss) {
                  for (temp = 0; temp < BLOCKSIZE; ++temp) {
                    cache[setAddr]->blocks[targBlock]->data[temp] 
                    = state.mem[temp + (offset - (offset % BLOCKSIZE))];
                  }//endfor - offset

                  printAction(offset - (offset % BLOCKSIZE), BLOCKSIZE, memoryToCache);
                  cache[setAddr]->blocks[targBlock]->valid = 1;
                  cache[setAddr]->LRU[cache[setAddr]->numBlocks] = cache[setAddr]->numBlocks;
                  cache[setAddr]->numBlocks++;
                  cache[setAddr]->blocks[targBlock]->tag = tagAddr;
                  compMiss = 0;
                }//endif - compulsory miss///////////////////////////////////////////////////////

                //capacity/conflict miss////////////////////////////////////////////////////////
                else if (confMiss) {
                    //find LRU block and reorder LRU
                    for (temp = 0; temp < BLOCKSPERSET; ++temp) {
                        if (!cache[setAddr]->LRU[temp]) {
                            targBlock = temp;
                            break;
                        }//endif - found LRU
                    }//endfor - find LRU block


                    //if block is dirty write it to memory before evicting
                    if (cache[setAddr]->blocks[targBlock]->dirty) {
                        for (temp = 0; temp < BLOCKSIZE; ++temp) {
                            reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                            reconstructed = (reconstructed | setAddr) << blockOffset;
                            state.mem[temp + (reconstructed - (reconstructed % BLOCKSIZE))] =
                            cache[setAddr]->blocks[targBlock]->data[temp];
                        }//endfor
                        
                        cache[setAddr]->blocks[targBlock]->dirty = 0;

                        //reconstruct address to print
                        reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                        reconstructed = (reconstructed | setAddr) << blockOffset;
                        printAction(reconstructed, BLOCKSIZE, cacheToMemory);
                    }//endif - dirty bit

                    //log eviction of clean block
                    else {
                        //reconstruct address to print
                        reconstructed = cache[setAddr]->blocks[targBlock]->tag << setIndexSize;
                        reconstructed = (reconstructed | setAddr) << blockOffset;
                        printAction(reconstructed, BLOCKSIZE, cacheToNowhere);
                    }//endelse

                    //now load new block from memory
                    for (temp = 0; temp < BLOCKSIZE; ++temp) {
                        cache[setAddr]->blocks[targBlock]->data[temp] 
                        = state.mem[temp + (offset - (offset % BLOCKSIZE))];
                    }//endfor - offset

                    cache[setAddr]->blocks[targBlock]->tag = tagAddr;

                    printAction(offset - (offset % BLOCKSIZE), BLOCKSIZE, memoryToCache);
                    confMiss = 0;
                }//endelif - cap/comp miss////////////////////////////////////////////////////////

                //cache hit/ read new instr if recovering from conflict/cap/compuls misses
                cache[setAddr]->blocks[targBlock]->data[blockAddr] = state.reg[reg2];
                cache[setAddr]->blocks[targBlock]->dirty = 1;
                printAction(offset, 1, processorToCache);

                reorderLRU(cache[setAddr]->LRU, targBlock, cache[setAddr]->numBlocks);
                break;
            //beq
            case 4:
                if (state.reg[reg1] == state.reg[reg2]) {
                    offset = convertNum(instr & 0xffff);
                    state.pc = state.pc + offset;
                }
                break;
            //jalr
            case 5:
                state.reg[reg2] = state.pc + 1;
                state.pc = state.reg[reg1] - 1;
                break;
            //halt
            case 6:
                foundHalt = 1;
                state.pc++;
                break;
            //noop
            default:
                break;
        }//end - switch

        if (foundHalt != 0)
            break;

        instr = 0;

    }//endfor - pc


    for (offset = 0; offset < 256; ++offset) {
        for (instr = 0; instr < 256; ++instr) {
            free(cache[offset]->blocks[instr]);
        }//endfor - instr
        free(cache[offset]);
    }//endfor - offset

    return(0);

}//end - main()


int convertNum(int num) {
        /* convert a 16-bit number into a 32-bit Linux integer */
        if (num & (1<<15) ) {
            num -= (1<<16);
        }
        return(num);
    }


void reorderLRU(int * LRU, int accessed, int BLOCKSPERSET) {
    int val = LRU[accessed];
    int temp = 0;
    LRU[accessed] = BLOCKSPERSET - 1;
    for (temp = 0; temp < BLOCKSPERSET; ++temp) {
        if (temp != accessed && LRU[temp] > val)
            LRU[temp]--;
    }
}

int getTag(int addr, int blockOffset, int setOffset) {
    return addr >> (blockOffset + setOffset);
}

int getBlock(int addr, int blockOffset) {
    return addr & ((1 << blockOffset) - 1);
}

int getSet(int addr, int blockOffset, int setOffset) {
    int cover = ((1 << setOffset) - 1) << blockOffset;
    return (addr & cover) >> blockOffset;
}

void
printAction(int address, int size, enum actionType type)
{
    printf("@@@ transferring word [%d-%d] ", address, address + size - 1);
    if (type == cacheToProcessor) {
        printf("from the cache to the processor\n");
    } else if (type == processorToCache) {
        printf("from the processor to the cache\n");
    } else if (type == memoryToCache) {
        printf("from the memory to the cache\n");
    } else if (type == cacheToMemory) {
        printf("from the cache to the memory\n");
    } else if (type == cacheToNowhere) {
        printf("from the cache to nowhere\n");
    }
}
