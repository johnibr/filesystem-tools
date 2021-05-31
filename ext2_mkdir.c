#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <stdbool.h>
#include <string.h>
#include "utils.h"

unsigned char *disk;


int main(int argc, char **argv) {

	if (argc != 3) {
    	fprintf(stderr, "Usage: ext2_mkdir <image file name> <abs path>\n");
    	exit(1);
  }


  char * path = argv[2];
	//Check to make sure name isn't empty
  if (!strcmp(find_name(path), "")) {
    fprintf(stderr, "Error: Name cannot be empty");
    exit(1);
  }

  int fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    perror("open");
    exit(1);
  }
	int v = 0; //To check return values
	
	//Mmap disk
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
  	perror("mmap");
  	exit(1);
	}
	//Find group descriptor and inode table
	struct ext2_group_desc* gd = (struct ext2_group_desc*)(disk + 1024 + EXT2_BLOCK_SIZE);
	unsigned char* inode_table = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

	struct path_inode * result_inode = traverse_path(inode_table, path);
	//If somewhere along the path doesn't exist (except the final directory), checked by name field in struct
	if (!result_inode || strcmp(result_inode->name, find_name(path))) {
		fprintf(stderr, "%s\n", strerror(ENOENT));
		return ENOENT;
	}

	//If the final directory exists
	if (result_inode->inode) {
		fprintf(stderr, "%s\n", strerror(EEXIST));
		return EEXIST;
	}

	//Allocate a new inode
	int inode_idx = allocate_inode();
	if (inode_idx==ENOSPC) {
		fprintf(stderr, "%s\n", strerror(ENOSPC));
		return ENOSPC;
	}

	//Find this inode
	struct ext2_inode * inode = (struct ext2_inode *)(inode_table) + inode_idx;

	//Modify it appropriately
	inode->i_mode = EXT2_S_IFDIR;
	inode->i_size = EXT2_BLOCK_SIZE;
	inode->i_links_count = 2;
	inode->i_blocks = 2;
	inode->i_block[0] = 0;
	inode->i_ctime = time(NULL);
	//Allocate a block for this inode
	v = allocate_blocks(inode, 1);

	if (v < 0) {
		//No space to allocate block
		fprintf(stderr, "%s\n", strerror(ENOSPC));
		return ENOSPC;
	}

	//Write to the directory the appropriate entries (. and ..)
	//(Doing this manually for . because the block is not formatted to accept directory entries yet)
  struct ext2_dir_entry_2* new_dir_entry = (struct ext2_dir_entry_2 *)(disk + 1024 + EXT2_BLOCK_SIZE * (inode->i_block[0] - 1));
  new_dir_entry ->inode = inode_idx + 1;
  new_dir_entry ->rec_len = EXT2_BLOCK_SIZE;
  new_dir_entry ->name_len = 1;
  new_dir_entry ->file_type = EXT2_FT_DIR;
  new_dir_entry ->name[0] = '.';

  //Now add .. by calling helper function

  v = add_dir_entry("..", result_inode->parent_num, EXT2_FT_DIR, inode);

  //Make a new directory entry in the parent directory
	v = add_dir_entry(find_name(path), (inode_idx + 1), EXT2_FT_DIR, result_inode->parent);

	if (v < 0) {
		//No space to add the directory entry
		fprintf(stderr, "%s\n", strerror(ENOSPC));
		return ENOSPC;
	}

	gd->bg_used_dirs_count++;
	return 0;

}
