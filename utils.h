#include <errno.h>
#include "ext2.h"
#include <stdio.h>
#include <time.h>

//Define return values for utility functions

#define SUCCESS 0
#define ERROR -1

//Maximum line length for a name
#define MAXLINE 256

//Path_inode contains both the inode at the end of the path and its parent (for things like rm)
//Also includes the name used to find the inode
struct path_inode {
	struct ext2_inode * inode;
	unsigned int inode_num; //For use with ext2_ln.
	char name[MAXLINE];
	struct ext2_inode * parent;
	unsigned int parent_num;
	
};

//Returns a pointer to the inode found after traversing the path
//Will return NULL if the specified path does not lead to a file/directory
//To check if it's a directory, check the inode mode value in the struct
struct path_inode * traverse_path(unsigned char * inode_table, char * path);

//Returns a pointer to the nth block in the indirect block pointer of inode i
unsigned char * find_indirect_block(unsigned int n, struct ext2_inode * i);

//Finds the file name given the path, returns a pointer to the file name (does not copy the string to new space)
char * find_name(char* path);

//Helper function to free path_node
void free_path_inode(struct path_inode * node);

//Adds a directory entry to a directory.
int add_dir_entry(char *name, unsigned int inode, unsigned char file_type, struct ext2_inode *dir);

//Add an inode to the table and link it to the directory.
unsigned int allocate_inode();

//Allocate a block.
int allocate_blocks(struct ext2_inode *inode, int num_blocks);

//Write to a block.
int write_to_blocks(struct ext2_inode *inode, FILE *source);

//Free an inode.
void free_inode(struct ext2_inode *inode, unsigned int inode_num);

//Remove an entry.
void remove_dir_entry(struct ext2_inode * inode, unsigned int inode_num);
