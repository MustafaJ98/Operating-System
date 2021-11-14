User Level Thread Library

Author: Mustafa Javed

To compile a foo.c file that uses the thread library:
gcc -o foo foo.c sut.c -pthread

To compile test1.c-test5.c files:
-ensure test1.c-test5.c, sut.c, sut.h, queue.h and Makefile are in the same folder.
-type "make"

To change the number of C_EXEC kernel level thread:
-change NUM_C_EXEC in sut.c

Provides user level threading using two kernel level pthreads: One to run computational task, second to run IO tasks.

Following commands are implemented in the library.

bool sut_create(sut_task_f fn)                : create a user-level thread which takes a C function as its sole argument. The iask is added to task queue.<br/>
void sut_yield()                              : puase the execution of the therad by giving control to executioner which runs the next task in task queue.<br/>
void sut_exit()                               : remove the user-level thread from task queue so it is no longer executed.<br/>
void sut_shutdown()                           : waits for all user-level threads to finish and then exits. <br/>
int sut_open(char *fname)                     : open a file with name fname. This is done in a seperate kernel level thread to avoid task blocking.<br/>
char *sut_read(int fd, char *buf, int size)   : read size bytes of data from the file fd to the buffer buf<br/>
void sut_write(int fd, char *buf, int size)   : write size bytes of data to the file fd from the buffer buf<br/>
void sut_close(int fd)                        : close the file descriptor fd<br/>
