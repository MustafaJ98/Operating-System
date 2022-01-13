/*
    Simple File System
	Author: Mustafa Javed
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include "disk_emu.h"
#include "sfs_api.h"

#define DEBUG 0 // set to 1 to print debugging statements

#define DiskFileName "myDiskFile" //name of the emulated disk file created
#define MAX_FILENAME_LENGTH 20    // maximum length of the file name
#define MAX_NUMBER_OF_FILES 100   //Maximum Number of files created
#define BLOCK_SIZE 1024           // each data block is 1KB
#define INODE_SIZE 64             // an I-Node size id 64 Bytes = 56 data + 8 padding
#define NUM_BLOCKS 30000          // Total number of 1KB block on disk

// Disk file is an array of blocks
#define SUPER_BLOCK_INDEX 0                 // Block 0 stores the super block
#define FREE_BIT_MAP_ADDRESS 1              // Block 1 is the start of freebitmap on disk 
#define NUMBER_OF_BLOCK_FOR_FREEBIT_MAP 4   // Block 1 to 4 are used to store free-bit map
#define START_OF_INODE 5                    // Block 5 is the start of Inodes on disk
#define NUMBER_OF_DATABLOCK_FOR_INODES 7    // Block 5 to 11 are used to store I-Nodes
#define ROOT_DIRECTORY_DATABLOCKS 12        // Block 12-15 are used to store directory
#define START_OF_DATABLOCKS 16              // Block 16 onwards will be used to store file data
#define NUMBER_OF_DATABLOCKS_PER_FILE 268   // each file can take a maximum of 268 blocks = 12 direct pointer + 256 indirect pointers
#define MAXIMUM_OPEN_FILES 100              // Max entires in the File Descriptor table

#define MIN(a, b) (((a) < (b)) ? (a) : (b)) // a simple function to find min of two numbers

typedef struct __FCB         // structure for a file control block
{                            // we implement a I-node structure for out FCB
    int size;                // file size in bytes
    int direct_pointers[12]; // array of 12 direct data block pointer
    int indirect_pointer;    // 1 indirect pointer, will point to a data block that cointains indrect pointers
} I_Node;

typedef struct __directory_entry // structure for a entry in our root Directory
{
    char filename[MAX_FILENAME_LENGTH + 1]; // name of the file
    int i_node;                             // Index of the Inode that belongs to this file, an -1 indicates that entry is empty
    int active;                             // boolean to track if entry is removed and empty
} Directory_Entry;

typedef struct __FileDescriptorEntry //data type for file descriptors
{
    int inode;     // Index of the Inode that belongs to this file descirptor, an -1 here indicates that entry is empty
    int rwPointer; // read write pointer of this file descriptor
} FileDescriptorEntry;

typedef struct __superblock //structure of the super block
{
    int MAGIC;
    int BlockSize;
    int FileSystemSize; //# of blocks
    int InodeTableLen;  //# of blocks
    int *rootDirectory; // Inode Number
} SuperBlock;

//In memory cache structures
SuperBlock *superblock;
Directory_Entry rootDirectory[MAX_NUMBER_OF_FILES];          //Array represents Root Directory in sfs
I_Node *I_Node_Table[MAX_NUMBER_OF_FILES];                   // This array represents the Inodes in the sfs
FileDescriptorEntry FileDescriptorTable[MAXIMUM_OPEN_FILES]; //This array contains all the file descriptors
int directoryTracker;                                        // used in get next filename
char FreeBitMap[4 * BLOCK_SIZE];                             // Array represents freeBitMap, 1 is free, 0 is taken



// --------------------FreeBitMap helper functions----------------------//

/**
 * @brief find the first free datablock (bit 1) in the freebitmap, set it to taken (bit 0)
 * 
 * @return int return the position of the free bit
 */
int find_And_Set_Free_Datablock()
{
    for (int i = 0; i < NUMBER_OF_BLOCK_FOR_FREEBIT_MAP * BLOCK_SIZE * 8; i++)  //loop over bits of freebitmap
    {

        if ((FreeBitMap[i / 8] & (1 << (i % 8))))   //bit is 1 i.e. free
        {                                         
            FreeBitMap[i / 8] &= ~(1 << (i % 8)); // Set the bit slot in freebitmap to 0 i.e. taken

            if (write_blocks(FREE_BIT_MAP_ADDRESS, NUMBER_OF_BLOCK_FOR_FREEBIT_MAP, FreeBitMap) < 0)    //flash freebitmap to disk
            {
                printf("Could not create free bit map \n");
                exit(-1);
            }
            return i;
        }
    }

    return -1;  // if no free bit, return -1
}

/**
 * @brief set a datablock as taken in freebitmap
 * 
 * @param pos index of the datablock to be marked as taken 
 */
void mark_datablock_as_taken(int pos)
{
    FreeBitMap[pos / 8] &= ~(1 << (pos % 8)); // Set the bitpos slot in freebitmap to 0 i.e. taken

    if (write_blocks(FREE_BIT_MAP_ADDRESS, NUMBER_OF_BLOCK_FOR_FREEBIT_MAP, FreeBitMap) < 0) //flash freebitmap to disk
    {
        printf("Could not create free bit map \n");
        exit(-1);
    }
}

/**
 * @brief set a datablock as free in freebitmap
 * 
 * @param pos index of the datablock to be marked as free 
 */
void mark_datablock_as_free(int pos)
{
    FreeBitMap[pos / 8] |= 1 << (pos % 8); // Set the bitpos slot in freebitmap to 1 i.e. free

    if (write_blocks(FREE_BIT_MAP_ADDRESS, NUMBER_OF_BLOCK_FOR_FREEBIT_MAP, FreeBitMap) < 0) //flash freebitmap to disk
    {
        printf("Could not create free bit map \n");
        exit(-1);
    }
}

/**
 * @brief initialize the freebitmap by setting all datablocks as free
 * except the first 15 metadata blocks
 * 
 */

void init_freeBitMap()
{
    for (int i = 0; i < NUMBER_OF_BLOCK_FOR_FREEBIT_MAP * BLOCK_SIZE; i++)
    {                                      // set all block free
        FreeBitMap[i] |= ~0; // Set the bit slot in freebitmap to 1 i.e. free
    }
    for (int i = 0; i < START_OF_DATABLOCKS; i++)
    {
        FreeBitMap[i / 8] &= ~(1 << (i % 8));   // set inital 16 block as taken for Inodes, directory, etc
    }

    if (write_blocks(FREE_BIT_MAP_ADDRESS, NUMBER_OF_BLOCK_FOR_FREEBIT_MAP, FreeBitMap) < 0) //flash freebitmap to disk
    {
        printf("Could not create free bit map \n");
        exit(-1);
    }
}

/**
 * @brief initialize the freebitmap from existing disk image
 */

void init_freeBitMap_from_Disk()
{

    if (read_blocks(FREE_BIT_MAP_ADDRESS, NUMBER_OF_BLOCK_FOR_FREEBIT_MAP, FreeBitMap) < 0) //load freebitmap to disk
    {
        printf("Could not create free bit map from disk \n");
        exit(-1);
    }
}
// --------------------------------------------------------------//

// --------------------Inode helper functions----------------------//


/**
 * @brief Create a I Node object, initializes the size, direct pointers and indirect pointers.
 All inode datablock pointer are set to -1 to indicate they are not set
 * 
 * @param InodeNumber index of Inode
 * @return I_Node* Returns a pointer to the created I-node instance.
 Instance must be freed to manage memory 
 */
I_Node *create_I_Node(int InodeNumber)
{
    int blockNumber = START_OF_INODE + (InodeNumber / (BLOCK_SIZE / INODE_SIZE));     // calculate Inode block number based on Inode index
    char InodeBlock[BLOCK_SIZE];
    read_blocks(blockNumber, 1, InodeBlock);    // read existing disk data

    I_Node *new_inode = (I_Node *)malloc(sizeof(I_Node));       //create a new Inode object
    new_inode->size = 0;                                        //set size to 0

    for (int i = 0; i < 12; i++)    
    {
        new_inode->direct_pointers[i] = -1;     //set all direct pointer to -1
    }
    new_inode->indirect_pointer = -1;       //set indirect pointer to -1

    char InodeData[INODE_SIZE] = {'\0'};
    memcpy(InodeData, new_inode, sizeof(I_Node));
    memcpy(InodeBlock + (INODE_SIZE * (InodeNumber % 16)), InodeData, INODE_SIZE);

    if (write_blocks(blockNumber, 1, InodeBlock) < 0)       //flash to disk
    {
        printf("Could not create I-node \n");
        exit(-1);
    }

    if (DEBUG) printf(" Inode %d has direct block 0 at %d \n", InodeNumber, new_inode->direct_pointers[0]);
    return new_inode;
}

/**
 * @brief This method creates 100 I_nodes and puts them in the I Node table in memory
 *  useful when initializing the file system with mksfs(1)
 * 
 */
void init_I_Nodes()
{
    for (int i = 0; i < MAX_NUMBER_OF_FILES; i++)
    {
        I_Node_Table[i] = create_I_Node(i); 
    }
}

/**
 * @brief  This methods creates an Inode table from an existing disk image
 *  when mksfs(0) is used to create the sfs from a existing disk file
 * 
 */
void init_I_Node_Table_From_Disk()
{

    for (int InodeNumber = 0; InodeNumber < MAX_NUMBER_OF_FILES; InodeNumber++)
    {
        int blockNumber = START_OF_INODE + (InodeNumber / (BLOCK_SIZE / INODE_SIZE));
        char InodeBlock[BLOCK_SIZE];
        read_blocks(blockNumber, 1, InodeBlock);

        I_Node *new_inode = (I_Node *)malloc(sizeof(I_Node));

        char InodeData[INODE_SIZE] = {'\0'};
        memcpy(InodeData, InodeBlock + (INODE_SIZE * (InodeNumber % 16)), INODE_SIZE);
        memcpy(new_inode, InodeData, sizeof(I_Node));

        I_Node_Table[InodeNumber] = new_inode;
        if (DEBUG)
            printf(" Inode %d has direct block 0 at %d \n", InodeNumber, new_inode->direct_pointers[0]);
    }
}

/**
 * @brief flashes an Inode with the index InodeNumber from the in-memory I-Node Table to the disk image file
 * 
 * @param InodeNumber 
 */
void flash_I_Node_to_Disk(int InodeNumber)
{

    int blockNumber = START_OF_INODE + (InodeNumber / (BLOCK_SIZE / INODE_SIZE));   //calculate the block of Inode
    char InodeBlock[BLOCK_SIZE];
    read_blocks(blockNumber, 1, InodeBlock);

    I_Node *new_inode = I_Node_Table[InodeNumber];

    char InodeData[INODE_SIZE] = {'\0'};
    memcpy(InodeData, new_inode, sizeof(I_Node));
    memcpy(InodeBlock + (INODE_SIZE * (InodeNumber % 16)), InodeData, INODE_SIZE);

    if (write_blocks(blockNumber, 1, InodeBlock) < 0)       //flash the inode to disk
    {
        printf("Could not create I-node \n");
        exit(-1);
    }
}

/**
 * @brief assign datablocks to an Inode 
 * 
 * @param InodeNumber Inode index
 * @param NumberOfDataBlocks number of datablocks to assign
 */
void add_DataBlock_To_Inode(int InodeNumber, int NumberOfDataBlocks)
{
    I_Node *inode = I_Node_Table[InodeNumber];  //get the inode

    int start_block = (inode->size / BLOCK_SIZE) + 1;       // start adding from this block
    int end_block = start_block + NumberOfDataBlocks-1;     // till this block

    if ((start_block <= 12) && (end_block >=12))
    {
        inode->indirect_pointer = find_And_Set_Free_Datablock();    // see if we need a indirect pointer block
    }

    int IndirectPointersDataBlock[256]; // num of pointers in a indrect pointer block = BLOCK_SIZE/sizeof(int) = 256

    if (inode->indirect_pointer > 0)
    {
        if (read_blocks(inode->indirect_pointer, 1, IndirectPointersDataBlock) < 0)
        {
            printf("Could not read I-node indirect pointer\n");
            exit(-1);
        }
    }

    for (int i = start_block; i <= end_block; i++)
    {
        int free_datablock = find_And_Set_Free_Datablock();

        printf("Giving Inode %d datablock %d \n", InodeNumber,free_datablock);

        if (i >= 12) // add to indirect pointer data blocks
        {
            IndirectPointersDataBlock[i - 12] = free_datablock;
        }
        else // add to direct pointer data blocks
        {
            inode->direct_pointers[i] = free_datablock;
        }
    }

    if (inode->indirect_pointer > 0)
    {
        if (write_blocks(inode->indirect_pointer, 1, IndirectPointersDataBlock) < 0)    //flash changes to disk
        {
            printf("Could not create I-node indirect block \n");
            exit(-1);
        }
    }
}

/**
 * @brief clear an inode by freeing all datablocks taken by inode and setting pointers to -1
 * 
 * @param Inode_Number 
 */

void reset_Inode(int Inode_Number){
    I_Node *inode = I_Node_Table[Inode_Number];
    for (int i = 0; i < 12; i++)
    {
        if (inode->direct_pointers[i] > 0) mark_datablock_as_free(inode->direct_pointers[i]);
        inode->direct_pointers[i] = -1;
    }

    if (inode->indirect_pointer > 0){
        int IndirectPointersDataBlock[256]; // num of pointers in a indrect pointer block = BLOCK_SIZE/sizeof(int) = 256
        if (read_blocks(inode->indirect_pointer, 1, IndirectPointersDataBlock) < 0) //free indirect pointers
        {
            printf("Could not read I-node indirect pointer\n");
            exit(-1);
        }

        for (int i = 0; i < 256; i++){
            if (IndirectPointersDataBlock[i] > 0) {
                mark_datablock_as_free(IndirectPointersDataBlock[i]);   //free direct pointers
            }
        }
        mark_datablock_as_free(inode->indirect_pointer);
        if (write_blocks(FREE_BIT_MAP_ADDRESS, NUMBER_OF_BLOCK_FOR_FREEBIT_MAP, FreeBitMap) < 0)
        {
            printf("Could not create free bit map \n");
            exit(-1);
        }
    } 
    inode->indirect_pointer = -1;

    flash_I_Node_to_Disk(Inode_Number);

}
// --------------------------------------------------------------//

// --------------------Directory helper functions----------------------//

// This method creates 100 directory entires with empty names and puts them in the rootDirectory array in memory
// useful when initializing the file system with mksfs(1)
// it also flashes the rootDirectory to the disk file image
void init_directory()
{
    for (int i = 0; i < MAX_NUMBER_OF_FILES; i++)
    {
        rootDirectory[i].active = 0;
        memset(rootDirectory[i].filename, '\0', MAX_FILENAME_LENGTH + 1);
        rootDirectory[i].i_node = i;
    }

    char DirectoryOnDisk[BLOCK_SIZE * 4] = {0}; //directory will take ~4 blocks
    memcpy(DirectoryOnDisk, rootDirectory, MAX_NUMBER_OF_FILES * sizeof(Directory_Entry));
    write_blocks(ROOT_DIRECTORY_DATABLOCKS, 4, DirectoryOnDisk);
}

// loop thorugh the directory to find a file with the filename set to name argument
// reutrn the index of the file in the rootDirectory array if found
// else return -1
int findInDirectory(const char *name)
{
    for (int i = 0; i < MAX_NUMBER_OF_FILES; i++)
    {
        if (rootDirectory[i].active == 1)
        {
            if (strcmp(rootDirectory[i].filename, name) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

// loop thorugh the directory to find the first empty slot
// reutrn the index of the empty slot if found
// else return -1
int getEmptyDirectorySlot()
{
    for (int i = 0; i < MAX_NUMBER_OF_FILES; i++)
    {
        if (rootDirectory[i].active == 0)
        {
            return i;
        }
    }
    printf("Max File Number Reached \n");
    return -1;
}

// --------------------------------------------------------------//

// --------------------FDT helper functions----------------------//

// This method creates 100 entries and puts them in the file descriptor table in memory
// useful when initializing the file system
// Note file descriptor table is an im-memory struct only and never flashed to disk

void init_FileDescriptorTable()
{
    for (int i = 0; i < MAXIMUM_OPEN_FILES; i++)
    {
        FileDescriptorTable[i].rwPointer = 0;
        FileDescriptorTable[i].inode = -1;
    }
}

// loop thorugh the FileDescriptor Table to find the first empty slot
// reutrn the index of the empty slot if found
// else return -1
int findEmptyFDTslot()
{
    for (int i = 0; i < MAXIMUM_OPEN_FILES; i++)
    {
        if (FileDescriptorTable[i].inode == -1)
        {
            return i;
        }
    }
    printf("Maximum file opended, can't open anymore files \n");
    return -1;
}

// --------------------------------------------------------------//

/*
    mksfs(int) is used to initialize the file system
    the int parameter of 1 indicates if the file system should be created from scrach or
    0 indicates that a file system exists on the disk and should be used to initialze the file system
*/
void mksfs(int fresh)
{

    superblock = (SuperBlock *)malloc(sizeof(SuperBlock)); //create new sfs structure
    char superBlockData[BLOCK_SIZE] = {0};
    directoryTracker = 0;

    if (fresh)
    { //fresh sfs

        printf("Creating New File System... \n");

        if (init_fresh_disk(DiskFileName, BLOCK_SIZE, NUM_BLOCKS) == -1) // create a fresh disk image file
        {
            printf("Error initializing new disk file image \n");
            exit(-1);
        }
        // 1) initialize super block and flash it to disk
        superblock->MAGIC = 0xACBD0005;
        superblock->BlockSize = BLOCK_SIZE;                  // block size
        superblock->FileSystemSize = NUM_BLOCKS;             //(# of total blks)
        superblock->InodeTableLen = MAX_NUMBER_OF_FILES + 1; // i-Node Table Length (# blks) = 1 root dir + max file #
        superblock->rootDirectory = 0;                       //(i-Node #)

        memcpy(superBlockData, superblock, sizeof(SuperBlock));
        if (write_blocks(0, 1, superBlockData) < 0)
        {
            printf("Could not write to new File system \n");
            exit(-1);
        }

        init_directory();           // 2) Initialize the root Directory and flash it to disk using helper methods
        init_FileDescriptorTable(); // 3) Initialize the File Descriptor Table
        init_freeBitMap();          // 4) Initialize the free bit map
        init_I_Nodes();             // 5) Initialize the I node Table and flash it to disk using helper methods
        

        printf("File System Initialization Complete \n");
    }

    else //sfs should already exist, read from file system
    {
        printf("Reading Existing File System... \n");

        if (init_disk(DiskFileName, BLOCK_SIZE, NUM_BLOCKS) == -1) //read an existing disk image file
        {
            printf("Error initializing existing disk file image \n");
            exit(-1);
        }
        // 0th datablock should have the super block
        if (read_blocks(0, 1, superBlockData) < 0) //  1) initialize super block from the disk
        {
            printf("Could not read from existing File system \n");
            exit(-1);
        }

        memcpy(superblock, superBlockData, sizeof(SuperBlock));

        char DirectoryOnDisk[BLOCK_SIZE * 4] = {0};                         //directory will take ~4 blocks
        if (read_blocks(ROOT_DIRECTORY_DATABLOCKS, 4, DirectoryOnDisk) < 0) // 2-5 datablock should have the directory entries
        {
            printf("Could not read from existing File system \n");
            exit(-1);
        } // 2) Initialize the root Directory from the disk
        memcpy(rootDirectory, DirectoryOnDisk, MAX_NUMBER_OF_FILES * sizeof(Directory_Entry));

        init_FileDescriptorTable();    // 3) Initialize the File Descriptor Table
        init_freeBitMap_from_Disk();   // 4) Initialize the freebit map from disk
        init_I_Node_Table_From_Disk(); // 5) Initialize the I node Table and reading from the disk
        

        printf("File System Initialization Complete \n");
    }
}

/**
 * @brief opens a file with name given, If file doesnt exist in directory, make a file with the name
 * 
 * @param name name of the file
 * @return int return the file descriptor if file open is successful, return -1 on error
 */
int sfs_fopen(char *name)
{

    if (strlen(name) > MAX_FILENAME_LENGTH + 1)
    {
        return -1;
    }

    //go thorugh directory and check if already open

    int directoryIndex = findInDirectory(name);

    if (directoryIndex != -1) //file is already in directory
    {
        for (int i = 0; i < MAXIMUM_OPEN_FILES; i++) //check if already open in FileDirectoryTable
        {
            if (rootDirectory[directoryIndex].i_node == FileDescriptorTable[i].inode)
            {
                printf("File is already open!\n");
                return -1;
            }
        }
    }
    else
    {                                             //File is not in directory
        directoryIndex = getEmptyDirectorySlot(); //make new directory entry
        if (directoryIndex == -1)
        {
            return -1;
        }
        else
        {
            rootDirectory[directoryIndex].active = 1;
            strcpy(rootDirectory[directoryIndex].filename, name);
            //flash directory update to disk
            char DirectoryOnDisk[BLOCK_SIZE * 4] = {0}; //directory will take ~4 blocks
            memcpy(DirectoryOnDisk, rootDirectory, MAX_NUMBER_OF_FILES * sizeof(Directory_Entry));
            write_blocks(ROOT_DIRECTORY_DATABLOCKS, 4, DirectoryOnDisk);

            I_Node_Table[rootDirectory[directoryIndex].i_node]->size = 0;   //set size to 0
            I_Node_Table[rootDirectory[directoryIndex].i_node]->direct_pointers[0] = find_And_Set_Free_Datablock(); // assign one datablock
            flash_I_Node_to_Disk(rootDirectory[directoryIndex].i_node); // flash Inode table
        }
    }
    //make fd entry
    int fd = findEmptyFDTslot();
    if (fd != -1)
    {
        FileDescriptorTable[fd].inode = rootDirectory[directoryIndex].i_node;
        FileDescriptorTable[fd].rwPointer = I_Node_Table[rootDirectory[directoryIndex].i_node]->size;
        if (DEBUG)
            printf("File %s size at opening is %d \n", rootDirectory[directoryIndex].filename, FileDescriptorTable[fd].rwPointer);
    }

    return fd;
}

/**
 * @brief close the file with the given file descriptor
 * 
 * @param fd   file descriptor of the file to close
 * @return int returns 0 if successful, else returns -1
 */
int sfs_fclose(int fd)
{
    if (fd < 0 || fd >= MAXIMUM_OPEN_FILES) //check if file descrioptor is with in limits
    {
        printf("invalid file descriptor \n");
        return -1;
    }

    if (FileDescriptorTable[fd].inode == -1) //check if file descriptor is open
    {
        printf("No such file open\n");
        return -1;
    }

    FileDescriptorTable[fd].inode = -1;    //closing file descriptor by setting value to -1
    FileDescriptorTable[fd].rwPointer = 0; // -1 allows spot to be taken by next opening file

    return 0;
}

/**
 * @brief removes a file from directory with the given name if it exists
 * 
 * @param name name of the file to remove
 * @return int 1 if successful, -1 if error
 */

int sfs_remove(char *name)
{
    int directoryIndex = findInDirectory(name);

    if (directoryIndex == -1)
    { //file is not in directory

        printf("No such file exists \n");
        return -1;
    }

    else //File is in directory
    {
        for (int i = 0; i < MAXIMUM_OPEN_FILES; i++) //go thorugh FDT and check if already open
        {
            if (rootDirectory[directoryIndex].i_node == FileDescriptorTable[i].inode)
            { // if file open, close file
                sfs_fclose(i);
                break;
            }
        }
        
        reset_Inode(rootDirectory[directoryIndex].i_node);
        rootDirectory[directoryIndex].active = 0;
        memset(rootDirectory[directoryIndex].filename, '\0', MAX_FILENAME_LENGTH + 1);
        //flash changes in Directory to disk
        char DirectoryOnDisk[BLOCK_SIZE * 4] = {0}; //directory will take ~4 blocks
        memcpy(DirectoryOnDisk, rootDirectory, MAX_NUMBER_OF_FILES * sizeof(Directory_Entry));
        write_blocks(ROOT_DIRECTORY_DATABLOCKS, 4, DirectoryOnDisk);
    }

    return 1;
}

/**
 * @brief write data from a buffer to file
 * 
 * @param fd file descriptor of the file in which data will be written, file must be opened as a pre-req
 * @param buf pointer to the buffer that contains the data to be written
 * @param length length of data to copied from buf to file in bytes, must of less than or euqal to size of buf
 * 
 * @return int length of data written in bytes or -1 if error   
 */
int sfs_fwrite(int fd, const char *buf, int length)
{
    //validate args
    if (fd < 0 || fd >= MAXIMUM_OPEN_FILES)
    {
        printf("invalid file descriptor \n");
        return -1;
    }

    if (FileDescriptorTable[fd].inode == -1)
    {
        printf("No such file open\n");
        return -1;
    }

    int Inode_number = FileDescriptorTable[fd].inode;  //get file inode number
    int rwpointer = FileDescriptorTable[fd].rwPointer; //get file rw pointer

    I_Node *inode = I_Node_Table[Inode_number]; //get file inode

    if (length > ( NUMBER_OF_DATABLOCKS_PER_FILE  * BLOCK_SIZE) - inode->size) //buffer will exceed max file size
    {
        printf("Buffer exceed Max File Size \n");
        return -1;
    }

    int startBlock = rwpointer / BLOCK_SIZE;          // write start block
    int endBlock = (rwpointer + length) / BLOCK_SIZE; // write end block

    if ((rwpointer + length) > inode->size) {
        int additionalDatablocksNeeded = 1+( (rwpointer + length) - inode->size )/BLOCK_SIZE;
        add_DataBlock_To_Inode(Inode_number,additionalDatablocksNeeded);
    }
    // if (endBlock > startBlock) {
    //     add_DataBlock_To_Inode(Inode_number,endBlock-startBlock);
    // }

    int IndirectPointersDataBlock[256]; // num of pointers in a indrect pointer block = BLOCK_SIZE/sizeof(int) = 256

    if (inode->indirect_pointer > 0)
    {
        if (read_blocks(inode->indirect_pointer, 1, IndirectPointersDataBlock) < 0)
        {
        printf("Could not read I-node indirect pointer\n");
        exit(-1);
        }
    }
    
    char temp[BLOCK_SIZE]; // temp buffer is used to read the block, edit data in it and write back the block
    int buffer_index = 0;  // this will keep track of how much data has been added to file

    for (int i = startBlock; i <= endBlock; i++) //loop over blocks to write
    {

        if (i >= 12) // if add to indirect pointer data blocks
        {
            read_blocks(IndirectPointersDataBlock[i - 12], 1, temp); //read block so we dont overwrite existing data

            int minOfTwo = MIN(length - buffer_index, BLOCK_SIZE - rwpointer % BLOCK_SIZE); // see what is minimum, reamining block space or data left to write?
            memcpy(temp + rwpointer % BLOCK_SIZE, buf + buffer_index, minOfTwo);            // data to copy to the buffer will be less of two
            rwpointer += minOfTwo;                                                          // move the rw pointer by amount written to file
            buffer_index += minOfTwo;                                                       //keep track of how much data written

            write_blocks(IndirectPointersDataBlock[i - 12], 1, temp); //write the block back to file
        }
        else // if add to direct pointer data blocks
        {

            read_blocks(inode->direct_pointers[i], 1, temp); //read block so we dont overwrite existing data

            int minOfTwo = MIN(length - buffer_index, BLOCK_SIZE - rwpointer % BLOCK_SIZE); // see what is minimum, reamining block space or data left to write?
            memcpy(temp + rwpointer % BLOCK_SIZE, buf + buffer_index, minOfTwo);            // data to copy to the buffer will be less of two
            rwpointer += minOfTwo;                                                          // move the rw pointer by amount written to file
            buffer_index += minOfTwo;                                                       //keep track of how much data written

            write_blocks(inode->direct_pointers[i], 1, temp); //write the block back to file
        }
    }

    FileDescriptorTable[fd].rwPointer = rwpointer; //edit the rw pointer in FDT
    inode->size = rwpointer;                       // change size in Inode
    if (DEBUG)
        printf("inode size after write: %d \n", inode->size);
    flash_I_Node_to_Disk(Inode_number); // flash the change in Inode to disk

    return buffer_index; //return num of bytes written to disk
}

/**
 * @brief read from a file to buffer, file must be open before read
 * 
 * @param fd file descriptor of the file 
 * @param buf pointer to the buffer in which data will be read to
 * @param length length of data to read in bytes
 * @return int num of bytes read or -1 if error
 */
int sfs_fread(int fd, char *buf, int length)
{
    if (DEBUG) printf("Read called\n");
    //validate args
    if (fd < 0 || fd >= MAXIMUM_OPEN_FILES) // file descriptor doesn't exist
    {
        printf("invalid file descriptor \n");
        return -1;
    }

    if (FileDescriptorTable[fd].inode == -1) // file is not open
    {
        printf("No such file open\n");
        return -1;
    }

    int Inode_number = FileDescriptorTable[fd].inode;  //get file inode number
    int rwpointer = FileDescriptorTable[fd].rwPointer; //get file rw pointer

    if (DEBUG) printf("Inode number %d\n", Inode_number);
    
    I_Node *inode = I_Node_Table[Inode_number]; //get file inode

    if (length > inode->size - rwpointer)
    {                                     // if reading beyond max length
        length = inode->size - rwpointer; //return till end of buffer
    }

    int startBlock = rwpointer / BLOCK_SIZE;          // read start block
    int endBlock = (rwpointer + length) / BLOCK_SIZE; // // read end block

    int IndirectPointersDataBlock[256]; // num of pointers in a indrect pointer block = BLOCK_SIZE/sizeof(int) = 256

    if (inode->indirect_pointer > 0) {
        if (read_blocks(inode->indirect_pointer, 1, IndirectPointersDataBlock) < 0) //get indirect pointers
        {
        printf("Could not read I-node indirect pointer \n");
        exit(-1);
        }
    }
    
    char temp[BLOCK_SIZE]; // temp buffer is used to read the block
    int buffer_index = 0;  // this will keep track of how much data has been read

    for (int i = startBlock; i <= endBlock; i++) // loop over blocks to read
    {

        if (i >= 12) // if read from indirect pointer data blocks
        {
            read_blocks(IndirectPointersDataBlock[i - 12], 1, temp); //read block

            int minOfTwo = MIN(length - buffer_index, BLOCK_SIZE - rwpointer % BLOCK_SIZE); // see what is minimum, reamining block space or data left to read?
            memcpy(buf + buffer_index, temp + rwpointer % BLOCK_SIZE, minOfTwo);            // copy data, data to copy to the buffer will be less of two
            rwpointer += minOfTwo;                                                          // move the rw pointer by amount read from file
            buffer_index += minOfTwo;                                                       //keep track of how much data read
        }
        else
        {
            read_blocks(inode->direct_pointers[i], 1, temp); //read block

            int minOfTwo = MIN(length - buffer_index, BLOCK_SIZE - rwpointer % BLOCK_SIZE); // see what is minimum, reamining block space or data left to read?
            memcpy(buf + buffer_index, temp + rwpointer % BLOCK_SIZE, minOfTwo);            // copy data, data to copy to the buffer will be less of two
            rwpointer += minOfTwo;                                                          // move the rw pointer by amount read from file
            buffer_index += minOfTwo;                                                       //keep track of how much data read
        }
    }

    FileDescriptorTable[fd].rwPointer = rwpointer; //edit the rw pointer in FDT

    if (DEBUG) printf("File size at read is %d \n", buffer_index);
    
    return buffer_index; //return num of bytes read from disk
}

/**
 * @brief move the read/write pointer to a locatation
 * 
 * @param fd file descriptor whose pointer will be moved
 * @param loc locaation where the pointer will be moved, must be greater than 0 and less than file size
 * @return int 1 on sucess, -1 on error
 */
int sfs_fseek(int fd, int loc)
{
    //validate args
    if (fd < 0 || fd >= MAXIMUM_OPEN_FILES) // FD is out of range
    {
        printf("invalid file descriptor \n");
        return -1;
    }

    if (FileDescriptorTable[fd].inode == -1) // file is not open
    {
        printf("No such file open\n");
        return -1;
    }

    FileDescriptorTable[fd].rwPointer = loc; //move the fd pointer in memory, no need to flash to disk
    return 1;
}

/**
 * @brief return the filename of the next file in directory
 * 
 * @param fname // name is written to this buffer
 * @return int // return 1 on success, 0 in error
 */

int sfs_getnextfilename(char *fname)
{
    while (1) // go over all entires in directory
    {

        if (directoryTracker >= MAX_NUMBER_OF_FILES - 1) // loop over since directory finished
        {
            directoryTracker = 0;
            return 0;
        }
        if (rootDirectory[directoryTracker].active == 1) // entires represents a file
        {
            strcpy(fname, rootDirectory[directoryTracker].filename); // copy file name to buffer
            directoryTracker++;
            return 1;
        }

        directoryTracker++;
    }
}

/**
 * @brief returns the size of the file in bytes
 * 
 * @param path path location and file name (relative to root directory)
 * @return int return file size or -1 if error
 */

int sfs_getfilesize(const char *path)
{
    int directoryIndex = findInDirectory(path);

    if (directoryIndex == -1)
    { //file is not in directory
        printf("No such file exists \n");
        return -1;
    }

    else //File is in directory
    {
        return I_Node_Table[rootDirectory[directoryIndex].i_node]->size; //get size of file from Inode
    }
}

/**
 * @brief Print all files in directory
 * not required by sfs API, used for debugging
 * 
 */

void printDir()
{
    for (int i = 0; i < MAX_NUMBER_OF_FILES; i++)
    {
        if (rootDirectory[i].active == 1)
        {
            printf("Dir item %d : %s of size %d\n", i, rootDirectory[i].filename, I_Node_Table[rootDirectory[i].i_node]->size);
        }
    }
}