#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include "ext2_fs.h"
#include "read_ext2.h"

int array_index = 0;
int isize = 0;
//char *fnames[120] = {NULL};

//Writes a single block to the output file
int blkhandl(int fd, int wfd, int blockaddr) {
  //Initializes a buffer for the block
  char buf[block_size];
  //Goes to the block address
  lseek(fd, blockaddr, SEEK_SET);
  //If the block is at the end of a data stream then it could have padding at the
  //end, this check ensures that is accounted for.
  uint len;
  if ((uint)isize < block_size) {
    len = isize;
    isize = 0;
  } else {
    len = block_size;
    isize = isize - block_size;
  }
  //Block data read into the buffer and written to the file
  read(fd, buf, len);
  write(wfd, buf, len);
  return 0;
}

//Traverse an indirect block and writes all of its data
int indhandl(int fd, int wfd, int blockaddr) {
  //Initializes buffer for all 256 blocks
  int blocks[256];
  //Goes to the first block and reads it into the array
  lseek(fd, blockaddr, SEEK_SET);
  read(fd, blocks, 1024);
  //For each block it calls the block handler
  for (int i = 0; i < 256; i++) {
    if (isize <= 0) {
      return 0;
    }
    blkhandl(fd, wfd, BLOCK_OFFSET(blocks[i]));
  }
  return 0;
}

int main(int argc, char **argv) {
	if (argc != 3) {
		printf("expected usage: ./runscan inputfile outputfile\n");
		exit(0);
	}

	int fd = open(argv[1], O_RDONLY);    /* open disk image */
        
	/////creates output directory, exits if exsists//////
	if(opendir(argv[2]) == NULL) {		
		if (mkdir(argv[2], 0777) != 0) {
			printf("directory not created\n");
			exit(0);
		}
	} else {
		printf("directory already exists\n");
		exit(0);
	}
	/////////////////////////////////////////////////////

	int inode_nums[120];
	for (int n = 0; n < 120; n++) {
	  inode_nums[n] = -1;
	}
	//char *names[120];
	//Initialize the reading of the filesystem and create the two structs for traversal of it
	ext2_read_init(fd);
	struct ext2_super_block super;
	struct ext2_group_desc group;

      //Loops through every block group
      for (uint g = 0; g < num_groups; g++) {
	//Read the superblock and group descriptor of the current group
	read_super_block(fd, g, &super);
	read_group_desc(fd, g, &group);
	//iterate through the current block group
	off_t start_inode_table = locate_inode_table(g, &group);
	
        //Loops through every inode within the block group	
	for (unsigned int i = 0; i < inodes_per_group; i++) {
	    //Malloc the current inode and read it's data
            struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
            read_inode(fd, g, start_inode_table, i, inode);

     	    if(S_ISREG(inode->i_mode)) { //If the inode is a regular file
		char buffer[1024]; 
		lseek(fd,BLOCK_OFFSET(inode->i_block[0]), SEEK_SET); //not working for double indirect i think.
		read(fd,&buffer,1024);

		//Checks if an inode represents a jpg file by checking for the "magic words"
		int is_jpg = 0;
		if (buffer[0] == (char)0xff && buffer[1] == (char)0xd8 && buffer[2] == (char)0xff &&
    		   (buffer[3] == (char)0xe0 || buffer[3] == (char)0xe1 || buffer[3] == (char)0xe8)) {
	 	      is_jpg = 1;
		}
			
		if(is_jpg) {
		  //Make output file named after inode number
		  //Copy out all direct blocks
		  //If indirect block, traverse and copy out all of its blocks
		  //If double indirect block, traverse and copy out all of its blocks
		  char fname[120], directory[120];

		  //Creates the new file name by combining the input directory argument and the
		  //filename from the spec and inode number
		  strcpy(directory, argv[2]);
		  strcat(directory, "/");
                  snprintf(fname, sizeof(fname), "file-%d.jpg", i);
                  strcat(directory, fname);
		  //fnames[array_index] = directory;
                  int wfd = open(directory, O_WRONLY | O_CREAT, 0666);

		  //Stores the size of the inode as a variable to be manipulated, allowing for 
		  //accurate copying out of the data.
		  isize = inode->i_size;
		  for(unsigned int j=0; j<EXT2_TIND_BLOCK; j++) {
		    if (isize <= 0) { //If size (decremented by block handler) is too low, break the loop
		      break;
		    } else if(j<EXT2_NDIR_BLOCKS) { //If a direct block, call block handler
			blkhandl(fd, wfd, BLOCK_OFFSET(inode->i_block[j]));	
		    } else if (j==EXT2_IND_BLOCK) { //If a single indirect block, call indirect block handler
			indhandl(fd, wfd, BLOCK_OFFSET(inode->i_block[j]));
		    } else if (j==EXT2_DIND_BLOCK) { //If a double indirect block, call indirect block handler <=256 times
			int indirects[256]; //Makes 256 buffers for each single indirect block
			//Seeks to the first indirect block and reads it in
			lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]), SEEK_SET);
			read(fd, indirects, 1024);
			//For each block, until size is too small, call the indirect block handler
			for (int k = 0; k < 256; k++) {
			  if (isize <= 0) {
			    break;
			  }
			  indhandl(fd, wfd, BLOCK_OFFSET(indirects[k]));
			}
		    }
		  }
		   //Close the write file and save the inode number for filename search later 
		   close(wfd);
		   inode_nums[array_index] = (g*inodes_per_group)+i;
		   array_index = array_index + 1;

		} //Closes isjpg
	     } //Closes isreg
	    free(inode);
	  } //Closes inode traversal loop
        } //Closes block group traversal loop
    //Read through the entire file system again, this time looking for file names
    //and copying the files of any already created jpg files.
    //Look through all the groups again
    for (uint g = 0; g < num_groups; g++) {
      read_super_block(fd, g, &super);
      read_group_desc(fd, g, &group);

      off_t start_inode_table = locate_inode_table(g, &group);
      //Look through all the inodes in each group
      for (unsigned int i = 0; i < inodes_per_group; i++) {
        struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
        read_inode(fd, g, start_inode_table, i, inode);

	//We only care about directories this time around
	if(S_ISDIR(inode->i_mode)) {
	  //Sets all the arrays we need then reads the directory main block
	  //into the buffer
	  int nmlen = EXT2_NAME_LEN*4;
	  char buffer[1024], name[nmlen];
	  lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
          read(fd, buffer, block_size);

	  //Using cur_direc we traverse through the directory block
	  void* cur_direc = (void*)buffer;
	  while(cur_direc < (void*)buffer + block_size) {
	    struct ext2_dir_entry_2* block = (struct ext2_dir_entry_2*)cur_direc;
	    //Use traverse to traverse through the inodes of the current directory entry
	    void* traverse = cur_direc;
	    while(traverse < cur_direc + block->rec_len) {
	      //Create a struct of traverse which allows access to variaables
	      struct ext2_dir_entry_2* direc = (struct ext2_dir_entry_2*)traverse;
	      int inode_num = direc->inode;
	      int moveval = 8 + (direc->name_len + 1);
	      int align = 4 - (moveval % 4) + moveval;
	      //For each inode check its name against all the saved names, if a match
	      //occurs then create a copy file as show in the spec.
	      for (int k = 0; k < array_index; k++) {
	        if (inode_nums[k] == -1) {
		  continue;
		} else if (inode_nums[k] == inode_num) {
		  //Initializes all the arrays we're using
		  char old[nmlen], new[nmlen], path[120], buf[1024];
		  //char new[nmlen], buf[1024];
		  memset(name, 0, nmlen);
		  memset(new, 0, nmlen);

		  //Finds and opens the old file made in the first half of the program
		  strncpy(name, direc->name, direc->name_len);
                  strcat(name,"\0");
		  //Take sthe filepath
		  strcpy(path, argv[2]);
		  strcat(new, argv[2]);
		  strcat(path, "/");
		  strcat(new, "/");
		  //Combines the name and the filepath to get access to the original file
                  snprintf(old, sizeof(name), "file-%d.jpg", inode_nums[k]);
		  strcat(path, old);
                  int fd = open(path, O_RDONLY);

		  //Creates the copy file of the old file
                  strcat(new,name);
                  int cpyfd = open(new, O_WRONLY | O_CREAT, 0666);

                  //Copies the contents of the old file to the copy file
		  int ret = read(fd, &buf, 1024);
                  while(ret > 0) {
                    if(write(cpyfd, &buf, ret) < 0) {
		      printf("negative value in write\n");
		    }
                    ret = read(fd, &buf, 1024);
                  }
                  write(cpyfd,&buf,ret);

		  //Cleanup, closes both the original file and the copy file. Also sets the
		  //spot the inode number held in the array to -1 so it can be skipped on
		  //further loops
		  inode_nums[k] = -1;
		  close(fd);
                  close(cpyfd);
                  break;
		}
	      } //Closes inode_nums array traversal (for loop)
	      //Increments the value of traverse so it may look at the next inode
              if(moveval % 4 != 0) { //Checks for page alignment, if not makes sure the next jump is aligned
                traverse = traverse + align;
	      } else {
                traverse = traverse + moveval;
	      }
	    } //Closes While loop
	    //Increments the value of cur_direct so it may look at the next directory entry
	    cur_direc = cur_direc + block->rec_len;
	  } //Closes while loop
	}
	free(inode);
      }
    }
  close(fd);
} //Closes main
