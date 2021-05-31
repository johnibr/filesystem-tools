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
    	fprintf(stderr, "Usage: ext2_rm <image file name> <target path>\n");
    	exit(1);
  	}

  	//Open image file
  	int fd = open(argv[1], O_RDWR);
  	if (fd < 0) {
  		perror("open");
  		exit(1);
  	}
  	//Mmap disk
  	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  	if (disk == MAP_FAILED) {
    	perror("mmap");
    	exit(1);
  	}

	//Find the relevant inode to remove, along with the parent directory
  	struct ext2_group_desc* gd = (struct ext2_group_desc*)(disk + 1024 + EXT2_BLOCK_SIZE);
  	unsigned char* inode_table = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

  	struct path_inode * result_inode = traverse_path(inode_table, argv[2]);

  	//If the inode doesn't exist or is a directory
  	if (!result_inode || !result_inode->inode || strcmp(result_inode->name, find_name(argv[2]))) {
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}

  	//If it's a directory
  	if (result_inode->inode->i_mode & EXT2_S_IFDIR) {
  		fprintf(stderr, "%s\n", strerror(EISDIR));
  		return EISDIR;
  	}

  	//Otherwise, remove the directory entry in the parent directory
  	remove_dir_entry(result_inode->parent, result_inode->inode_num);
  	
  	//Decrease link count by 1
  	result_inode->inode->i_links_count--;

  	//If this results in a link count of 0, then remove the inode and the blocks associated with it
  	if (!result_inode->inode->i_links_count) {
  		free_inode(result_inode->inode, result_inode->inode_num);
  	}

  	return 0;
}