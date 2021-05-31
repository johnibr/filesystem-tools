all : ext2_ls ext2_cp ext2_mkdir ext2_ln ext2_rm 

ext2_ls : ext2_ls.o utils.o
	gcc -Wall -g -o ext2_ls ext2_ls.o utils.o

ext2_cp : ext2_cp.o utils.o
	gcc -Wall -g -o ext2_cp ext2_cp.o utils.o

ext2_mkdir : ext2_mkdir.o utils.o
	gcc -Wall -g -o ext2_mkdir ext2_mkdir.o utils.o

ext2_ln : ext2_ln.o utils.o
	gcc -Wall -g -o ext2_ln ext2_ln.o utils.o

ext2_rm : ext2_rm.o utils.o
	gcc -Wall -g -o ext2_rm ext2_rm.o utils.o

%.o : %.c 
	gcc -Wall -g -c $<

clean:
	rm -f ext2_ls ext2_cp ext2_mkdir ext2_ln ext2_rm *.o *~
