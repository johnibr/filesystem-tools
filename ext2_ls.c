#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

//#include <stdbool.h>
#include <string.h>
#include "utils.h"

unsigned char *disk;


int main(int argc, char **argv) {
	int a_flag = 0; //For optarg a

	//Mandatory arguments
	char * path = NULL;
	char * file_name = NULL;

  //Check if it involves the optional flag
  if (argc == 4) {
    if (!strcmp(argv[2], "-a")) {
      a_flag = 1;
      file_name = argv[1];
      path = argv[3];
    } else {
      fprintf(stderr, "Usage: ext2_ls <image file name> [-a] <abs path>\n");
      exit(1);
    }
  } else if (argc == 3) {
    file_name = argv[1];
    path = argv[2];

  } else {
    fprintf(stderr, "Usage: ext2_ls <image file name> [-a] <abs path>\n");
    exit(1);
  }

  	//Open image file
  	int fd = open(file_name, O_RDWR);

    if (fd < 0) {
      perror("open");
      exit(1);
    }

  	//Mmap disk
  	disk = mmap(NULL, 128 * 1024, PROT_READ, MAP_SHARED, fd, 0);
  	if (disk == MAP_FAILED) {
    	perror("mmap");
    	exit(1);
  	}
  	//Find group descriptor and inode table
  	struct ext2_group_desc* gd = (struct ext2_group_desc*)(disk + 1024 + EXT2_BLOCK_SIZE);
  	void* inode_table = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

  	struct path_inode * result_inode = traverse_path(inode_table, path);

  	if (!result_inode || !result_inode->inode) {
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}
  	//Do not care about parent inode in this case
  	struct ext2_inode * inode = result_inode->inode;

  	//If it is a directory
  	if (inode->i_mode & EXT2_S_IFDIR) {
  		int initial_entry = 0; //0 if we want . and .., 1 otherwise
  		//If a_flag is not set
  		if (!a_flag) {
  			initial_entry = 2;
  		}

  		//Find the correct data blocks
  		int block_idx = 0;
  		int current_size = 0;
  		int total_size = inode->i_size;
  		int i; //Iterator variable
      char *name;

  		struct ext2_dir_entry_2 * dir_entry = (struct ext2_dir_entry_2*)(disk + EXT2_BLOCK_SIZE * inode->i_block[0]);
  		if (initial_entry) {
  			//The two entries . and ..
  			current_size += dir_entry->rec_len;
  			dir_entry = (struct ext2_dir_entry_2 *)((char *)dir_entry + dir_entry->rec_len);

  			//. should not use an entire block, but just in case
  			if (current_size % EXT2_BLOCK_SIZE == 0) {
  				block_idx++;
  				dir_entry = (struct ext2_dir_entry_2*)(disk + EXT2_BLOCK_SIZE * inode->i_block[block_idx]);
  			}

  			current_size += dir_entry->rec_len;
  			dir_entry = (struct ext2_dir_entry_2 *)((char *)dir_entry + dir_entry->rec_len);
  		}
  		while (current_size < total_size) {
    		current_size += dir_entry->rec_len;
    		name = dir_entry->name;

    		for (i = 0; i < dir_entry->name_len; i++, name++) {
      			printf("%c", *name);
    		}
    		if (dir_entry->name_len) {
    			  //If name_len is 0, then this block was empty, and therefore does not need a newline character
      			printf("\n");
    		}
		    dir_entry = (struct ext2_dir_entry_2 *)((char *)dir_entry + dir_entry->rec_len);

  			//If necessary, go to next block
  			if (current_size % EXT2_BLOCK_SIZE == 0) {
  				block_idx++;
  				if (block_idx < 13) {
                    dir_entry = (struct ext2_dir_entry_2*)(disk + EXT2_BLOCK_SIZE * inode->i_block[block_idx]);
                } else {
                    dir_entry = (struct ext2_dir_entry_2*)(find_indirect_block((block_idx - 12), inode));
                }
  			}
  		}


  	} else { //Otherwise, was link or file
  		//Just print name of the link or file, as obtained from the path_inode
  		printf("%s\n", result_inode->name);
  	}

    free(result_inode);
  	return 0;
}
