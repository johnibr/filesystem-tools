#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include "utils.h"


extern unsigned char* disk;
   
//Inode_table is a pointer to the mmaped memory starting at the inode table, path is a string containing the absolute path
struct path_inode * traverse_path(unsigned char * inode_table, char * path) {
    int inode_idx = EXT2_ROOT_INO - 1; //Index of current inode, start at root inode (inodes start at 1)
    struct ext2_inode* inode_iterator = (struct ext2_inode *)(inode_table) + inode_idx; //Iterator, starting at the root directory

    struct ext2_dir_entry_2* dir_entry = NULL; //A directory entry pointer to examine directories
    struct path_inode *result; //Return value, a struct containing 2 inodes, one corresponding to the end of the path,
                                     //and one corresponding to the parent directory of the end

    //First, if only the root is required, return immediately

    if (!strcmp(path, "") || !strcmp(path, "/")) {
        result = malloc(sizeof(struct path_inode));
        result->inode = inode_iterator;
        result->parent = NULL;
        result->parent_num = 0;

        return result;
    }

    char * path_buffer = malloc(strlen(path) + 1); //Buffer to store the path
    
    strncpy(path_buffer, path, strlen(path) + 1); //Copy path into buffer

    char * token; //For tokenizing the path
    int block_idx = 0; //Stores the index in inode->i_block
    int total_size; //Stores the total number of bytes that this inode spans over
    int current_size = 0; //Stores the current number of bytes traversed over
    int inode_found = 0; //Stores whether or not we found the next inode

    //Malloc a path_inode for the result
    result = malloc(sizeof(struct path_inode));
    result->inode = inode_iterator;
    result->parent = NULL;

    result->inode_num = 2;
    result->parent_num = 2;

    //Copy the path into the path_buffer
    //strcpy(path, path_buffer);

    //If the string ends in a '/', remove it

    if (path_buffer[strlen(path) - 1] == '/') {
        path_buffer[strlen(path) - 1] = '\0';
    }

    //Tokenize the path
    token = strtok(path_buffer, "/");

    

    //While there is still a path left to traverse
    while (token) {
        total_size = inode_iterator->i_size;
        if (dir_entry) {
            result->parent_num = dir_entry->inode;
        }
        //Point dir_entry to the first directory entry
        dir_entry = (struct ext2_dir_entry_2*)(disk + EXT2_BLOCK_SIZE * inode_iterator->i_block[0]);
        inode_found = 0;
        while (current_size < total_size) { //While there are bytes left to read
            //Add the size of the current directory

            //If the name matches
            if (strlen(token) == dir_entry->name_len &&
                strncmp(token, dir_entry->name, dir_entry->name_len) == 0) {
                result->parent = result->inode;
                //Store the name in the result as well
                strncpy(result->name, token, MAXLINE);
                result->inode = (struct ext2_inode *)(inode_table) + (dir_entry->inode - 1);
                result->inode_num = dir_entry->inode;

                //Now check if it is a directory inode or not.
                //If it isn't a directory inode, either we're at the end of the path, or the path tried to go through a non directory.
                if (!(dir_entry->file_type == EXT2_FT_DIR)) {
                    if (strtok(NULL, "/")) {
                        free(result);
                        //If there is something after this non directory in the path, invalid path
                        return NULL;
                    }
                    //Otherwise, return the result that we have
                    return result;
                }
                inode_iterator = result->inode;
                inode_found = 1;
                break;
            }
			//Update current size
            current_size += dir_entry->rec_len;

            //If we need to move on to the next block
            if (current_size % EXT2_BLOCK_SIZE == 0) { 
                block_idx++;
                if (block_idx < 13) {
                    dir_entry = (struct ext2_dir_entry_2*)(disk + EXT2_BLOCK_SIZE * inode_iterator->i_block[block_idx]);
                } else {
                    dir_entry = (struct ext2_dir_entry_2*)(find_indirect_block((block_idx - 12), inode_iterator));
                }
            } else {
				//Otherwise, continue on to the next entry
				dir_entry = (struct ext2_dir_entry_2 *)((char*)dir_entry + dir_entry->rec_len);
			}
        }

        //If the inode wasn't found
        if (!inode_found) {
            //For use with ln, mkdir, etc., return path_node with last found inode (the parent directory) and a name value equal to first not found inode
            result->parent = result->inode;
            result->inode = NULL;
            strncpy(result->name, token, MAXLINE);
            return result;
        }

        token = strtok(NULL, "/");
    }

    //At this point, the inode representing the last part of the path should be found
    //Will only reach this point if the path ended in a directory

    return result;
}


//Returns a pointer to the nth block in the indirect block pointer of inode i

unsigned char * find_indirect_block(unsigned int n, struct ext2_inode * inode_iterator) {
    //Find the indirect pointer block (cast to int because an indirect block number is an int)
    unsigned int * indirect_block = (unsigned int *)(disk + EXT2_BLOCK_SIZE * inode_iterator->i_block[13]);
    //Find the specific pointer 
    indirect_block += n;

    //Return a pointer to the specific block
    return (disk + 1024 + EXT2_BLOCK_SIZE * (*indirect_block - 1));
}


//Finds the file name given the path, returns a pointer to the file name (does not copy the string to new space)

char * find_name(char* path) {
  int i = strlen(path) - 1;
  while (path[i] != '/') {
    i--;
  }

  // return path[i + 1];
  return &path[i + 1];
}


//Helper function to free path_node

void free_path_inode(struct path_inode * node) {
    if (node->inode) {
        free(node->inode);
    }

    if (node->parent) {
        free(node->parent);
    }
    
    free(node);
}

/* NOTE: inode is an inode number  */ 
int add_dir_entry(char *name, unsigned int inode, unsigned char file_type, struct ext2_inode *dir) {

	//Calculate the amount of space needed for the entry
	//(Rounds up to the nearest 4 bytes and adds another 8 on top of that)
	unsigned int space_needed = 0;

	//Look for space in a directory block
	unsigned int num_blocks = dir->i_size / EXT2_BLOCK_SIZE;
	int i;
	struct ext2_dir_entry_2 * dir_entry = NULL;
	for (i = 0; i < num_blocks; i++) {
		if (i < 13) {
			dir_entry = (struct ext2_dir_entry_2 *)(disk + 1024 + EXT2_BLOCK_SIZE * (dir->i_block[i] - 1));
		} else {
			dir_entry = (struct ext2_dir_entry_2*)(find_indirect_block((i - 12), dir));
		}
		
		//Go through the block looking for an empty entry (empty entries are all 0s, so the name would be empty string, name can't be empty for a directory entry)
		unsigned int current_size = 0;
		while (current_size < EXT2_BLOCK_SIZE) {

            if (strlen(name) % 4 == 0){
                space_needed = strlen(name) + 8;
            }

            else {
                space_needed = (strlen(name) - (strlen(name) % 4)) + 12;
            }
			
			
			//If there is enough space to fit the new entry there

            int temp = 0; 
            if (dir_entry->name_len % 4 == 0) {
                temp = dir_entry->name_len + 8;
            }

            else {
                temp = dir_entry->name_len - (dir_entry->name_len % 4) + 12;
            }

			if (dir_entry->rec_len >= space_needed + temp) {
                int leftover_space = dir_entry->rec_len - temp; //The amount of extra space in the entry
				//Update rec_len of the previously last entry to new value
				dir_entry->rec_len = temp;
				//Shift to the start of the new entry
				dir_entry = (struct ext2_dir_entry_2 *)((char * )(dir_entry) + temp);
				//Put in new entry (using up the remainder of the space in the block)
				dir_entry->rec_len = leftover_space;
				dir_entry->inode = inode;
				dir_entry->file_type = file_type;
				dir_entry->name_len = strlen(name);
				strncpy(dir_entry->name, name, (strlen(name) + 1));
				return 0;
			}
            current_size += dir_entry->rec_len;
            dir_entry = (struct ext2_dir_entry_2 *)((char *)(dir_entry) + dir_entry->rec_len);
		}
	}
	//If reached this point, we need to allocate a new block for the directory entry
	int idx = allocate_blocks(dir, 1);
	
	//If there is no space
	if (idx < 0) {
		return -1;
	}
	//Now find that new block
	if (idx > 12) {
		dir_entry = (struct ext2_dir_entry_2*)(find_indirect_block((idx - 12), dir));
	} else {
		dir_entry = (struct ext2_dir_entry_2 *)(disk + 1024 + EXT2_BLOCK_SIZE * (dir->i_block[idx]));
	}
	
	//Since the block is empty, just add the directory entry
	dir_entry->rec_len = EXT2_BLOCK_SIZE;
	dir_entry->inode = inode;
	dir_entry->file_type = file_type;
	dir_entry->name_len = strlen(name);
	strncpy(dir_entry->name, name, (strlen(name) + 1));
	return 0;
}


int allocate_blocks(struct ext2_inode *inode, int num_blocks) {

    /* NOTE: NEED TO TAKE CARE OF INDIRECT BLOCKS!!!! */

    /* Make sure there are enough free blocks. */
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);

    if (num_blocks > sb->s_free_blocks_count || sb->s_free_blocks_count==0) {
        return -1;
    }

    /* Find the block bitmap, then traverse it to find the first number of free blocks,
    and link them to the specified inode i_block field. */
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + EXT2_BLOCK_SIZE);
    char *block_bitmap = (char *)(disk + 1024 +  EXT2_BLOCK_SIZE * (gd->bg_block_bitmap - 1));

    /* Go through the bitmap, if an entry is 0, link it to the inode's i_block. */
    int iblock_idx = 0, dir_block_count = 0, indir_block_count = 0;
    int indir_idx = 0, indirect_flag = 0;
    int bitmap_idx, bit;
    unsigned int *indir_block;

    if (num_blocks > 12) {
        indirect_flag = 1;
        indir_block_count = num_blocks-12;
        indir_block = (unsigned int *)(disk+inode->i_block[12]*EXT2_BLOCK_SIZE);
    }
        
    for (bitmap_idx=0; bitmap_idx<16; bitmap_idx++) {
        int byte = *(block_bitmap + bitmap_idx);
        for (bit=0; bit<8; bit++) {
            
            /* Free block. Now update sb, gb and inode fields. */
            if (!((byte >> bit) & 1)) {

                /* If we need a block for an indirect flag and have collected all direct blocks. */
                if (indirect_flag && dir_block_count==12) {
                    inode->i_block[12] = bitmap_idx*8 + bit + 1;
                    sb->s_free_blocks_count--;
                    gd->bg_free_blocks_count--;

                    /* At this point we have an indirect block, now time to find direct blocks for the
                    indirect block. */
                    if (indir_block_count > 0) {
                        indir_block[indir_idx] = bitmap_idx*8 + bit + 1;
                        sb->s_free_blocks_count--;
                        gd->bg_free_blocks_count--;
                        indir_block_count--;
                        indir_idx++;
                    }

                    else {
                        /* We have allocated all direct and indirect blocks. */
                        break;
                    }
                }

                inode->i_block[iblock_idx] = bitmap_idx * 8 + bit + 1; //CHECK THIS
                dir_block_count++;
                iblock_idx++;
                sb->s_free_blocks_count--;
                gd->bg_free_blocks_count--;
                *(block_bitmap + bitmap_idx) = byte | (1 << bit);

                if (num_blocks==1) {
                    return bitmap_idx;
                }
            }
        }

    }

    return 0;
}

int write_to_blocks(struct ext2_inode *inode, FILE *source) {

    /* Calculate the filesize then the number of blocks needed. */
    int filesize = 0;
    fseek(source, 0L, SEEK_END);
    filesize = ftell(source);
    fseek(source, 0L, SEEK_SET);

    int num_blocks;
    if (filesize % EXT2_BLOCK_SIZE == 0) {
        num_blocks = filesize/EXT2_BLOCK_SIZE;
    }
    else {
        num_blocks = filesize/EXT2_BLOCK_SIZE + 1;
    }

    /* Attempt to allocate the number of blocks needed. If result is non-zero, there was
    a failure and we could not allocate blocks. */
    if (allocate_blocks(inode, num_blocks)<0) {
        fprintf(stderr, "%s\n", strerror(ENOSPC));
        return ENOSPC;
    }

    /* At this point we have allocated the blocks needed. Write the data to those blocks and
    update the inode table entry for the file. */
    int idx, bytes_count = 0;
    for (idx=0; idx<13; idx++) {
        void *block = (void *)(disk+(inode->i_block[idx]*EXT2_BLOCK_SIZE)); //Location of i_block[i]
        bytes_count += fread(block, sizeof(char), 1, source);
    }

    /* Need an indirect block if greater than 12. */
    if (num_blocks > 12) {
        void *block = (void *)(disk+(inode->i_block[12]*EXT2_BLOCK_SIZE)); //Location of i_block[12]
        for (idx=0; idx<(num_blocks-12); idx++) {
            bytes_count += fread(block, sizeof(char), 1, source);
        }
    }

    /* Make sure all bytes were written or something went wrong. */
    if (bytes_count!=filesize) {
        fprintf(stderr, "%s\n", strerror(ENOSPC));
        return ENOSPC;
    }

    /* Now update the inode and its table entry. */
    inode->i_mode = EXT2_FT_REG_FILE;
    inode->i_size = filesize;
    inode->i_blocks = num_blocks * 2; //Sectors

    return 0;
}

unsigned int allocate_inode() {

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

    /* No free inodes. */
    if (sb->s_free_inodes_count==0) {
        fprintf(stderr, "%s\n", strerror(ENOSPC));
        return ENOSPC;
    }

    /* Now go through the inode bitmap and find a free inode. */
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + EXT2_BLOCK_SIZE);
    unsigned char *inode_bitmap = disk + EXT2_BLOCK_SIZE*gd->bg_inode_bitmap;

    int bitmap_idx, bit;
    unsigned int inode;
    /* Only 32 inodes, so the bitmap is 32/8 = 4 bytes */
    for (bitmap_idx=0; bitmap_idx<4; bitmap_idx++) {
        unsigned char byte = *(inode_bitmap + bitmap_idx);
        for (bit=0; bit<8; bit++) {
            
            /* Free inode. Now update fields and return the inode.*/
            if (!((byte >> bit) & 1)) {
                //*(inode_bitmap + bitmap_idx) = *(inode_bitmap + bitmap_idx) |= (1 <<bit);
                inode = bitmap_idx * 8 + bit;
                *(inode_bitmap + bitmap_idx) = byte | (1 << bit);
                sb->s_free_inodes_count--;
                gd->bg_free_inodes_count--;
                return inode;
            }
        }
    }

    /* Returns 0 if there wasnt a free inode. */
    return 0;
}

void free_inode(struct ext2_inode *inode, unsigned int inode_num) {
    int num_blocks = inode->i_blocks/2;
    int idx, blocks, indir_flag = 0;

    /* Check if there is an indirect block. */
    if (num_blocks > 12) {
        indir_flag = 1;
        blocks = 12;
    }

    else {
        blocks = num_blocks;
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + EXT2_BLOCK_SIZE);
    unsigned char *block_bitmap = disk + 1024 + EXT2_BLOCK_SIZE*(gd->bg_block_bitmap-1);

    /* Clear all the direct blocks. */
    int bitmap_idx, bit, byte;
    for (idx=0; idx<blocks; idx++) {

        bitmap_idx = (inode->i_block[idx] - 1)/8;
        bit = inode->i_block[idx] - 1 - 8*bitmap_idx;
        byte = *(block_bitmap + bitmap_idx);
        block_bitmap[bitmap_idx] = (byte &= ~(1<<bit));
        sb->s_free_blocks_count++;
        gd->bg_free_blocks_count++;
    }

    /* Now to clear the indirect block. */
    if (indir_flag) {
        unsigned int * indir_block = (unsigned int *)(disk+inode->i_block[12]*EXT2_BLOCK_SIZE);
        for (idx=0; idx<(num_blocks-blocks); idx++) {
            bitmap_idx = (indir_block[idx] - 1)/8;
            bit = indir_block[idx] - 1 - 8*bitmap_idx;
            byte = *(block_bitmap + bitmap_idx);
            block_bitmap[bitmap_idx] = (byte &= ~(1 << bit));
            sb->s_free_blocks_count++;
            gd->bg_free_blocks_count++;
        }

    }

    /* Now clear the bitmap entry */
    unsigned char *inode_bitmap = disk + EXT2_BLOCK_SIZE*gd->bg_inode_bitmap;
    bitmap_idx = (inode_num - 1)/4;
    bit = inode_num - 1 - 4*bitmap_idx;
    // inode_bitmap[(inode_num - 1)/8] &= ~(1 << ((inode_num - 1) % 8));
    inode_bitmap[bitmap_idx] &= ~(1<<bit);
    sb->s_free_inodes_count++;
    gd->bg_free_inodes_count++;
    inode->i_dtime = time(NULL);

}

void remove_dir_entry(struct ext2_inode * inode, unsigned int inode_num) {
	
	//If it's not a directory, then just return
	if (!(inode->i_mode & EXT2_S_IFDIR)) {
		return;
	}
	//Find the entry in the directory data blocks
	int current_size = 0;
	int total_size = inode->i_size;
	int block_idx = 0;
	//If the inode doesn't contain any blocks, return
	if (!inode->i_block[0]) {
		return;
	}
	
	struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2 *)(disk + EXT2_BLOCK_SIZE * inode->i_block[0]);
	int prev_rec_len = 0; //Stores the previous rec_len so we can figure out where the previous directory entry was relative to the current one
	
	while (current_size < total_size) {
		if (dir_entry->inode == inode_num) {
			//We have found the entry to remove
			//Two cases: either it's the first entry, or it isn't
			//If it's the first entry, add the space to the second entry and shift the entry up
			//Otherwise, add the space to the entry before it
			if (current_size % EXT2_BLOCK_SIZE == 0) {
				//If the entire block just consists of this entry, make name_len 0 to signify a now empty block
				if (dir_entry->rec_len == EXT2_BLOCK_SIZE) {
					dir_entry->name_len = 0;
					return;
				}
				//Otherwise, add rec_len to the next entry and shift it up
				struct ext2_dir_entry_2* dir_entry2 = (struct ext2_dir_entry_2 *)((char*)(dir_entry) + dir_entry->rec_len);
				dir_entry2->rec_len += dir_entry->rec_len;
				memmove(dir_entry, dir_entry2, dir_entry2->rec_len);
				return;
				
			} else {
				//Otherwise, we can add rec_len to the entry before it
				int rec_len = dir_entry->rec_len;
				//Go back an entry
				dir_entry = (struct ext2_dir_entry_2 *)((char*)(dir_entry) - prev_rec_len);
				//Now add the appropriate amount of space to that entry's record length
				dir_entry->rec_len = rec_len + dir_entry->rec_len;
			}
		}
		//Updated current_size and dir_entry
		current_size += dir_entry->rec_len;
		prev_rec_len = dir_entry->rec_len;
		dir_entry = (struct ext2_dir_entry_2 *)((char*)(dir_entry) + dir_entry->rec_len);
		if (current_size % EXT2_BLOCK_SIZE == 0) {
			//Move onto the next block
			block_idx++;
            if (block_idx < 13) {
                dir_entry = (struct ext2_dir_entry_2*)(disk + EXT2_BLOCK_SIZE * inode->i_block[block_idx]);
            } else {
                dir_entry = (struct ext2_dir_entry_2*)(find_indirect_block((block_idx - 12), inode));
            }
		}
		
		
	}
	return;
}
