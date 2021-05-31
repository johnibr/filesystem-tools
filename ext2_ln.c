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

	int v = 0; //Variable for checking return values
	int s_flag = 0; //For optarg s

	//Mandatory arguments
	char * src_path = NULL;
	char * target_path = NULL;
	char * file_name = NULL;

  if (argc == 5) {
    if (!strcmp(argv[2], "-s")) {
      s_flag = 1;
      file_name = argv[1];
      src_path = argv[3];
      target_path = argv[4];
    } else {
      fprintf(stderr, "Usage: ext2_ln <image file name> [-s] <source path> <target path>\n");
      exit(1);
    }
  } else if (argc == 4) {
    file_name = argv[1];
    src_path = argv[2];
    target_path = argv[3];

  } else {
    fprintf(stderr, "Usage: ext2_ln <image file name> [-s] <source path> <target path>\n");
    exit(1);
  }


  //Open image file
  int fd = open(file_name, O_RDWR);

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
	struct ext2_group_desc* gd = (struct ext2_group_desc*)(disk + 1024 + EXT2_BLOCK_SIZE);
  	void* inode_table = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

  	//Find the inodes representing the parent directory of target_path and src_path, starting with target_path

  	struct path_inode * target = traverse_path(inode_table, target_path);

  	//Check target

  	//If target cannot be found, or the name of the last directory reached does not match (see how traverse_path assigns name)
  	if (!target || strcmp(target->name, find_name(target_path))) {
  		if (target)
  			free(target);
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}

  	//If target exists (the file, not the parent directory)
  	if (target->inode) {
  		free(target);

  		fprintf(stderr, "%s\n", strerror(EEXIST));
  		return EEXIST;
  	}

  	//Now check source
  	struct path_inode * source = traverse_path(inode_table, src_path);

  	//If source file does not exist
  	if (!source || !source->inode) {
  		if (source) {
  			free(source);
  			free(target);
  		}
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}

  	//If the source "file" is actually a directory
  	if (source->inode->i_mode & EXT2_S_IFDIR) {
  		free(source);
  		free(target);

  		fprintf(stderr, "%s\n", strerror(EISDIR));
  		return EISDIR;
  	}

  	if (s_flag) {
  		//Symbolic link

  		//Only implementing slow symbolic links, write the path as a string in the block

  		//First allocate inode and set appropriate values
  		int inode_idx = allocate_inode();
      	if (inode_idx==ENOSPC) {
        fprintf(stderr, "%s\n", strerror(ENOSPC));
        return ENOSPC;
      }

  		//Find this inode
  		struct ext2_inode * inode = inode_table + inode_idx;

  		//Initialize it appropriately
  		inode->i_mode = EXT2_S_IFLNK;
  		inode->i_size = EXT2_BLOCK_SIZE;
  		inode->i_links_count = 1;
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

  		//Find the newly allocated block
  		unsigned char * block = disk + EXT2_BLOCK_SIZE * (v + 1);

  		//Write the path to the block
  		strncpy((char *)(block), target_path, strlen(target_path) + 1);

  		//Now add this inode to target's parent directory

		  v = add_dir_entry(find_name(target_path), inode_idx + 1, EXT2_FT_SYMLINK, target->parent);
		  if (v < 0) {
			 fprintf(stderr, "%s\n", strerror(ENOSPC));
			 return ENOSPC;
		  }

  	} else {
  		//Hard link

  		//All that is needed is to add a new directory entry pointing to the relevant inode
      	//and increase link count by 1
		  v = add_dir_entry(find_name(target_path), source->inode_num, EXT2_FT_REG_FILE, target->parent);
		  source->inode->i_links_count++;
		  if (v < 0) {
			 fprintf(stderr, "%s\n", strerror(ENOSPC));
			 return ENOSPC;
		  }

  	}

  	return 0;
}