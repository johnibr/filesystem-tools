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

	/* Some quick error checking. */
	if (argc != 4) {
		fprintf(stderr, "Usage: ext2_cp <image file name> <file path> <abs path>\n");
		exit(1);
	}

	/* Args. */
	char * image = argv[1];
	char * file_path = argv[2];
	char * dest_path = argv[3];


	int fd = open(image, O_RDWR);

	if (fd < 0) {
		perror("open");
		exit(1);
	}

	/* MMAP the disk. */
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  	if (disk == MAP_FAILED) {
    	perror("mmap");
    	exit(1);
  	}

  	/* First, need to check whether the file and destination exist or not. */
  	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + EXT2_BLOCK_SIZE);

  	void *inode_table = (void *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_table);

  	// struct path_inode * source = (struct path_inode *)traverse_path(inode_table, file_path);
  	struct path_inode * dest = (struct path_inode *)traverse_path(inode_table, dest_path);

  	/* Check if file and destination exists. */
  	if (!dest) {
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}

  	/* Check if destination is actually a directory. */
  	struct ext2_inode *inode = dest->inode;
  	if (!(inode->i_mode & EXT2_S_IFDIR)) {
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}

  	/* At this point, the file and destination both exist. Now time to actually copy the
  	file. */

    printf("%s\n", file_path);

  	FILE *f = fopen(file_path, "r");
  	if (!f) {
  		fprintf(stderr, "%s\n", strerror(ENOENT));
  		return ENOENT;
  	}

    // printf("REACHED\n");

  	int filesize, num_blocks;
  	fseek(f, 0L, SEEK_END);
  	filesize = ftell(f);
  	fseek(f, 0L, SEEK_SET);

    if (filesize % EXT2_BLOCK_SIZE == 0) {
        num_blocks = filesize/EXT2_BLOCK_SIZE;
    }
    else {
        num_blocks = filesize/EXT2_BLOCK_SIZE + 1;
    }

    unsigned int node_addr = allocate_inode();
    if (node_addr==ENOSPC) {
      //printf("Couldnt get an inode.\n");
    	fclose(f);
    	fprintf(stderr, "%s\n", strerror(ENOSPC));
    	return ENOSPC;
    }

    struct ext2_inode *node = (struct ext2_inode *)(inode_table + node_addr - 1);
    /* Try to allocate blocks. */
    int val;
    if ((val = allocate_blocks(node, num_blocks))<0) {
      // printf("Couldnt get any blocks.\n");
    	fclose(f);
      fprintf(stderr, "%s\n", strerror(ENOSPC));
      return ENOSPC;
    }

    /* Update the node's fields. */
    node->i_mode = EXT2_FT_REG_FILE;
    node->i_size = filesize;
    node->i_blocks = node->i_size/EXT2_BLOCK_SIZE; //Sectors.
    node->i_links_count = 1;
    node->i_ctime = time(NULL);

    // printf("time to write to blocks\n");

    /* At this point we have allocated an inode and blocks to copy over the data. Time to write
    the data to the blocks. */
    if (write_to_blocks(node, f)!=0) {
      // printf("could not write to blocks\n");
    	fclose(f);
    	fprintf(stderr, "%s\n", strerror(ENOSPC));
    	return ENOSPC;
    }

    // printf("wrote to blocks\n");
    /* Time to add the directory entry of this inode. */
    char *filename = find_name(file_path);

    // printf("time to write the dir entry\n");

    if (add_dir_entry(filename, node_addr+1, EXT2_FT_REG_FILE, dest->inode)< 0) {
      // printf("couldn't add dir entry");
    	fclose(f);
    	fprintf(stderr, "%s\n", strerror(ENOSPC));
    	return ENOSPC;
    }

    fclose(f);

	return 0;	
}

