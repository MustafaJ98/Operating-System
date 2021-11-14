CC=gcc
CFLAGS=-pthread

TEST1=t1
TEST2=t2
TEST3=t3
TEST4=t4
TEST5=t5

tests: sut.c test1.c test2.c test3.c test4.c test5.c

	$(CC) -o $(TEST1) $(CFLAGS) test1.c sut.c 
	$(CC) -o $(TEST2) $(CFLAGS) test2.c sut.c 
	$(CC) -o $(TEST3) $(CFLAGS) test3.c sut.c 
	$(CC) -o $(TEST4) $(CFLAGS) test4.c sut.c
	$(CC) -o $(TEST5) $(CFLAGS) test5.c sut.c