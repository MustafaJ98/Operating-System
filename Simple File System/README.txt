Simple File System

Author: Mustafa Javed

NOTE: The initialization can take up to a minute
 since the sfs is about 30 MB and each file can be as big as the Inode allows.

The following parameters are used in the sfs_api.c file

A emulated disk file will be created named "myDiskFile"
The maximum filname length can 20 letters including file extension
A maximum of 100 files can be created simultaneously
A maximum of 100 files can be opened simultaneously

The file system implmentation uses:
-1 Data Block for Super Block
-4 Data Block for Free bitmap
-7 Data Blocks for 100 Inodes
-4 Data Blocks for rootDirectory Data 
-26900 Data Blocks for file/I-Node Data

The sfs_api.c passes test[0-2] and and works with fuse wrapper as well.

How to use the Make file:
-Uncomment one of the 5 SOURCES lines in Makefile to compile
-Type 'make'

A executable file with name sfs_mustafa will be created