/*
	ECSE427: Operating System
	Assignment 2: Simple Thread Scheduler
	Author: Mustafa Javed - 260808710
 
 Credits: Parts of this code is given by Prof Maheswaran for ECSE 427 and due permission to reuse the code
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ucontext.h>
#include <pthread.h>
#include <string.h>

#include "queue.h"
#include "sut.h"

#define MAX_THREADS 30
#define THREAD_STACK_SIZE 64 * 1024 // 16 KB stack for each thread

#define NUM_C_EXEC 1        // number of CXEC kernel level threads, accepted value 1 or 2

#define DEBUG false         // use this to print debugging statements

typedef struct __sut_task       //structure of TCB
{
    int taskid;                 // unique if of task
    char *taskstack;            // ptr to stack of task
    void *taskfunc;             // ptr to function run by the task 
    ucontext_t taskcontext;     // context of task
} sut_task;

int numthreads_created; // keep track of current number of user threads
int numthreads_exited;  // keep track of exited number of user threads

struct queue task_ready_queue, wait_queue; //queues for tasks in ready and IO queue
struct queue C2I_msg_queue, I2C_msg_queue; //msg queues to communicate bwteen C-Exec thread and I-Exec thread

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  // mutex lock for synchronizing queue access

pthread_t C_EXEC_1, C_EXEC_2, I_EXEC;          //kernel level threads
ucontext_t c_exec_1_context, c_exec_2_context; //context of executors


bool shutdown_called;   // flag that is set to true if shutdown is called

int IO_operation; // where 0 = default, 1 = open, 2 = write, 3 = read, 4 = close

struct queue_entry *cur_task_exec1;     //current task running in C_EXEC_1
struct queue_entry *cur_task_exec2;     //current task running in C_EXEC_2

/** 
 * checks task ready queue constantly and if there is a task in the queue
 * change context to execute the task

*/
void *c_executor()
{   
   if (DEBUG) printf(" pthiread self is: %lu \n", pthread_self());  

    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)   //check which thread is running
    {
        getcontext(&c_exec_1_context);                  //get initial context for the thread
    }

    if (NUM_C_EXEC == 2)                            //if 2 kernel level threads run C_EXEC
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)   //check which thread is running
        {
            getcontext(&c_exec_2_context);                  //get initial context for the threa
        }
    }

    while (1)
    {
        pthread_mutex_lock(&mutex);
        struct queue_entry *queue_head = queue_pop_head(&task_ready_queue); //check if ready queue has a task
        pthread_mutex_unlock(&mutex);
        
        if (queue_head)         //ready queue has a task
        {

            if (DEBUG) printf("Thread ID: %lu \n", pthread_self());     //used for debugging

            sut_task *new_task = (sut_task *)queue_head->data;      
            if (pthread_equal(C_EXEC_1, pthread_self()) != 0)               
            {                                                               
                cur_task_exec1 = queue_head;
                swapcontext(&c_exec_1_context, &(new_task->taskcontext));       //swap context to that of task and save context of C_EXEC_1
            }

            if (NUM_C_EXEC == 2)
            {
                if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
                {
                    cur_task_exec2 = queue_head;
                    swapcontext(&c_exec_2_context, &(new_task->taskcontext));      //swap context to that of task and save context of C_EXEC_2
                }
            }
        }
        else
        {
            if ( (numthreads_exited == numthreads_created) && shutdown_called)
            {
                printf("Exiting C-EXEC \n");
                return NULL; // all tasks finished, ready list is empty, exit
            }

            usleep(100 * 1000); // no task in ready queue, go to sleep for 100 us
        }
    }
}

/** 
 * checks wait queue constantly and if there is a IO task in the queue
 * change context to execute the IO task
 * 
 *   Parameter from C_EXEC threads are passed to I_EXEC thread using C2I_msg_queue queue
 *    - 1st parameter in the list indicate what IO operation is requested
 *    - where 0 = default, 1 = open, 2 = write, 3 = read, 4 = close
 *    - reamining parameters are operation dependent
 *       
 *   Parameter from I_EXEC threads are returned to C_EXEC thread using I2C_msg_queue queue
 * 
*/
void *i_executor()
{
    while (1)
    {
        pthread_mutex_lock(&mutex);
        struct queue_entry *queue_head = queue_pop_head(&wait_queue);   //check if there is a task in wait queue
        pthread_mutex_unlock(&mutex);
        
        if (queue_head)             // there is a task in wait queue
        {

            sut_task *new_task = (sut_task *)queue_head->data;

            pthread_mutex_lock(&mutex);
            struct queue_entry *IO_entry = queue_pop_head(&C2I_msg_queue);      // decode first parameter to see what IO operation is requested
            pthread_mutex_unlock(&mutex);

            IO_operation = *(int *)(IO_entry->data);
            free(IO_entry);

            if (IO_operation == 1) //open command
            { 
                if (DEBUG) printf("open called \n");
                
                pthread_mutex_lock(&mutex);
                struct queue_entry *msgC2I = queue_pop_head(&C2I_msg_queue);        // get filename from C2I_msg_queue
                pthread_mutex_unlock(&mutex);

                int f = open((msgC2I->data), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IXUSR);    //open the file 
                
                if (DEBUG) printf("int f is = %d\n",f);

                if (f == -1) {
                    printf("Error opening file\n Exiting\n");
                    exit(-1);
                }

                struct queue_entry *msgI2C = queue_new_node(&f);        // return FD using I2C_msg_queue
                pthread_mutex_lock(&mutex);
                queue_insert_tail(&I2C_msg_queue, msgI2C);
                pthread_mutex_unlock(&mutex);
                free(msgC2I);

                IO_operation = 0;        //clear IO_operation
            }
            else if (IO_operation == 2) //write command
            {
                int fd;
                char *buf;
                int count;
                
                if (DEBUG) printf("write called \n");
                
                struct queue_entry *msgC2I;     // get all parameters from C2I_msg_queue passed by C_EXEC thread

                pthread_mutex_lock(&mutex);
                msgC2I = queue_pop_head(&C2I_msg_queue);
                pthread_mutex_unlock(&mutex);
                fd = *(int *)(msgC2I->data);        //get file descriptor parameter
                free(msgC2I);

                pthread_mutex_lock(&mutex);
                msgC2I = queue_pop_head(&C2I_msg_queue);
                pthread_mutex_unlock(&mutex);
                buf = (char *)(msgC2I->data);       //get buffer that has info to write
                free(msgC2I);

                pthread_mutex_lock(&mutex);
                msgC2I = queue_pop_head(&C2I_msg_queue);
                pthread_mutex_unlock(&mutex);
                count = *(int *)(msgC2I->data);         //size of data to write
                free(msgC2I);

                if ( write(fd, buf, count) == -1  ){            //call write to write to the FD (file)
                    printf("Error writing file\n Exiting\n");
                    exit(-1);
                }

                IO_operation = 0;       //clear IO_operation
            }
            else if (IO_operation == 3)     //read command
            {
                int fd;
                char *buf;
                int count;
                
                if (DEBUG) printf("read called \n");
                
                struct queue_entry *msgC2I;

                pthread_mutex_lock(&mutex);
                msgC2I = queue_pop_head(&C2I_msg_queue);    // get all parameters from C2I_msg_queue passed by C_EXEC thread
                pthread_mutex_unlock(&mutex);
                fd = *(int *)(msgC2I->data);        //get file descriptor parameter
                free(msgC2I);

                pthread_mutex_lock(&mutex);
                msgC2I = queue_pop_head(&C2I_msg_queue);
                pthread_mutex_unlock(&mutex);
                buf = (char *)(msgC2I->data);       //get buffer to read to
                free(msgC2I);

                pthread_mutex_lock(&mutex);
                msgC2I = queue_pop_head(&C2I_msg_queue);
                pthread_mutex_unlock(&mutex);
                count = *(int *)(msgC2I->data);     ///size of data to read
                free(msgC2I);

                if ( read(fd, buf, count) == -1 ) {             //call read 
                    printf("Error reading file\n Exiting\n");
                    exit(-1);
                }

                struct queue_entry *msgI2C = queue_new_node(buf);       // return the ptr to read buffer to  C_EXEC thread
                pthread_mutex_lock(&mutex);
                queue_insert_tail(&I2C_msg_queue, msgI2C);
                pthread_mutex_unlock(&mutex);

                IO_operation = 0;   //clear IO_operation
            }
            else if (IO_operation == 4) //close command
            {
                if (DEBUG) printf("close called \n");

                pthread_mutex_lock(&mutex);
                struct queue_entry *msgC2I = queue_pop_head(&C2I_msg_queue);        //get fd to close
                pthread_mutex_unlock(&mutex);

                if ( close(*(int *)(msgC2I->data)) != 0  ) {        //call close in fd
                    printf("Error closing file\n Exiting\n");
                    exit(-1);
                }

                free(msgC2I);
                IO_operation = 0;   //clear IO_operation
            }

            if (DEBUG) printf("adding task from IO to Exec \n");

            pthread_mutex_lock(&mutex);
            queue_insert_tail(&task_ready_queue, queue_head);     //once IO task is completed, add task back from wait queue to ready queue
            pthread_mutex_unlock(&mutex);

            if (DEBUG) printf("task added from IO to Exec \n");
        }
        else
        {
            if ((numthreads_exited == numthreads_created) && shutdown_called)
            {
                printf("Exiting I-EXEC \n");
                return NULL; // all tasks finished, ready list and wait list are empty, exit
            }

            usleep(100 * 1000);     // no task in wait queue, go to sleep for 100 us
        }
    }
}

void sut_init()
{
    numthreads_created = 0;
    numthreads_exited = 0;
    IO_operation = 0;
    shutdown_called = false;

    task_ready_queue = queue_create(); //create processor ready queue
    wait_queue = queue_create();       //create IO wait queue
    C2I_msg_queue = queue_create();    //create Cexec to  msg queue
    I2C_msg_queue = queue_create();    //create msg queue

    queue_init(&task_ready_queue); //initialize processor ready queue
    queue_init(&wait_queue);       //initialize IO wait queue
    queue_init(&C2I_msg_queue);    //initialize msg queue
    queue_init(&I2C_msg_queue);    //initialize msg queue

    pthread_create(&C_EXEC_1, NULL, c_executor, NULL);  // create kernel level thread to run C_EXEC 1

    if (NUM_C_EXEC == 2)        //if two C_EXEC are to be used
    {
        pthread_create(&C_EXEC_2, NULL, c_executor, NULL);   // create kernel level thread to run C_EXEC 2
    }
    pthread_create(&I_EXEC, NULL, i_executor, NULL);        //create kernel level thread to run I_EXEC

   
}

bool sut_create(sut_task_f fn)
{
    if (numthreads_created > MAX_THREADS)       //check number of threads created
    {
        printf("Maximum thread limit reached at 30\n");
        return false;
    }
    else
    {
        sut_task *new_task = (sut_task *)malloc(sizeof(sut_task));      //make struct for new task
        new_task->taskid = numthreads_created;                          //unique id for task
        new_task->taskstack = (char *)malloc(THREAD_STACK_SIZE);        //set stack for new task
        getcontext(&(new_task->taskcontext));                           // save caller context to task context
        new_task->taskcontext.uc_stack.ss_sp = new_task->taskstack;     // point task's stack to new stack 
        new_task->taskcontext.uc_stack.ss_size = THREAD_STACK_SIZE;     // define stack size in context
        new_task->taskcontext.uc_link = 0;
        new_task->taskcontext.uc_stack.ss_flags = 0;
        new_task->taskfunc = fn;                                       //function executed by new task
        makecontext(&(new_task->taskcontext), fn, 0);                   // make the new context

        struct queue_entry *node = queue_new_node(new_task);             //add task to ready queue
        pthread_mutex_lock(&mutex);
        queue_insert_tail(&task_ready_queue, node);
        pthread_mutex_unlock(&mutex);
        numthreads_created++;
        return true;
    }
}

void sut_yield()
{   
    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)
    {   
        pthread_mutex_lock(&mutex);
        queue_insert_tail(&task_ready_queue, cur_task_exec1);     //add the current task running in C_EXEC 1 to end of ready queue
        pthread_mutex_unlock(&mutex);

        sut_task *task = (sut_task *)cur_task_exec1->data;
        swapcontext(&(task->taskcontext), &c_exec_1_context);       // change context back to C_EXEC 1 and save context of current task 1
        return;
    }

    if (NUM_C_EXEC == 2)
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
        {
            pthread_mutex_lock(&mutex);                             //add the current task running in C_EXEC 2 to end of ready queue
            queue_insert_tail(&task_ready_queue, cur_task_exec2);
            pthread_mutex_unlock(&mutex);

            sut_task *task = (sut_task *)cur_task_exec2->data;
            swapcontext(&(task->taskcontext), &c_exec_2_context);   // change context back to C_EXEC 2 and save context of current task 2
            return;
        }
    }
}

void sut_exit()
{
    
    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)       
    {
        sut_task *remove_task = (sut_task *)cur_task_exec1->data;   //get current context
        free(remove_task->taskstack);           // free stack of task
        free(remove_task);                      // free task struct
        free(cur_task_exec1);                   // free queue entry struct

        numthreads_exited++;
        setcontext(&c_exec_1_context);      // not used swaocontext because no need to save context of exited task
    }

    if (NUM_C_EXEC == 2)
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
        {
            sut_task *remove_task = (sut_task *)cur_task_exec2->data;
            free(remove_task->taskstack);   // free stack of task
            free(remove_task);              // free task struct
            free(cur_task_exec2);           // free queue entry struct

            numthreads_exited++;
            setcontext(&c_exec_2_context);
        }
    }
}

void sut_shutdown()
{   
    shutdown_called = true;             //this flag will now allow kernel level threads to exit
    pthread_join(C_EXEC_1, NULL);       // wait for C_EXEC kernel level thread to return
    pthread_join(I_EXEC, NULL);         // wait for I_EXEC kernel level thread to return
    if (NUM_C_EXEC == 2)    
    {
        pthread_join(C_EXEC_2, NULL);        // wait for C_EXEC kernel level thread to return
    }
    printf("Exiting Program\n");
    if (DEBUG) printf("Total number of threads created = %d\n", numthreads_created);
}

int sut_open(char *dest)
{
    int IO = 1; //indicate open operation

    struct queue_entry *msgC2I;                 //add parameters to C2I_msg_queue
    pthread_mutex_lock(&mutex);
    msgC2I = queue_new_node(&IO);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(dest);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    

    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)
    {
        
        queue_insert_tail(&wait_queue, cur_task_exec1);     //add current task to IO wait queue
        pthread_mutex_unlock(&mutex);

        sut_task *task = (sut_task *)cur_task_exec1->data;
        swapcontext(&(task->taskcontext), &c_exec_1_context);      //give control back to C_EXEC_1

        pthread_mutex_lock(&mutex);
        struct queue_entry *msgI2C = queue_pop_head(&I2C_msg_queue);    //at this point, file will be open
        pthread_mutex_unlock(&mutex);                                     // pop I2C_msg_queue to get fd

        int rfd = *(int *)(msgI2C->data);       //return fd
        free(msgI2C);
        return rfd;
    }

    if (NUM_C_EXEC == 2)
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
        {

            queue_insert_tail(&wait_queue, cur_task_exec2);      //add current task to IO wait queue
            pthread_mutex_unlock(&mutex);

            sut_task *task = (sut_task *)cur_task_exec2->data;
            swapcontext(&(task->taskcontext), &c_exec_2_context);       //give control back to C_EXEC_2

            
            pthread_mutex_lock(&mutex);                                      //at this point, file will be open
            struct queue_entry *msgI2C = queue_pop_head(&I2C_msg_queue);    // pop I2C_msg_queue to get fd
            pthread_mutex_unlock(&mutex);

            int rfd = *(int *)(msgI2C->data);       //return fd
            free(msgI2C);
            return rfd;
        }

    
    }

}

void sut_write(int fd, char *buf, int size)
{
    int IO = 2; //indicate write operation

    struct queue_entry *msgC2I;                  //add parameters to C2I_msg_queue
    pthread_mutex_lock(&mutex);
    msgC2I = queue_new_node(&IO);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(&fd);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(buf);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(&size);
    queue_insert_tail(&C2I_msg_queue, msgC2I);

    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)
    {

        queue_insert_tail(&wait_queue, cur_task_exec1);     //add current task to IO wait queue
        pthread_mutex_unlock(&mutex);

        sut_task *task = (sut_task *)cur_task_exec1->data;
        swapcontext(&(task->taskcontext), &c_exec_1_context);  //give control back to C_EXEC_1
        return;
    }

    if (NUM_C_EXEC == 2)
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
        {
            queue_insert_tail(&wait_queue, cur_task_exec2);             //add current task to IO wait queue
            pthread_mutex_unlock(&mutex);

            sut_task *task = (sut_task *)cur_task_exec2->data;
            swapcontext(&(task->taskcontext), &c_exec_2_context);       //give control back to C_EXEC_2
            return;
        }
    }
}

char *sut_read(int fd, char *buf, int size)
{
    int IO = 3; //indicate read operation

    struct queue_entry *msgC2I;         //add parameters to C2I_msg_queue
    pthread_mutex_lock(&mutex);
    msgC2I = queue_new_node(&IO);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(&fd);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(buf);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(&size);
    queue_insert_tail(&C2I_msg_queue, msgC2I);

    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)
    {
        queue_insert_tail(&wait_queue, cur_task_exec1);         //add current task to IO wait queue
        pthread_mutex_unlock(&mutex);

        sut_task *task = (sut_task *)cur_task_exec1->data;
        swapcontext(&(task->taskcontext), &c_exec_1_context);       //give control back to C_EXEC_1

        pthread_mutex_lock(&mutex);                                     //at this point, buffer will have read data
        struct queue_entry *msgI2C = queue_pop_head(&I2C_msg_queue);    // pop I2C_msg_queue to get buffer
        pthread_mutex_unlock(&mutex);
        //printf("returning buffer : %s \n", (char *)(msgI2C->data));

        char *rbuf = (char *)(msgI2C->data);        //return buffer
        free(msgI2C);
        return rbuf;
    }

    if (NUM_C_EXEC == 2)
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
        {

            queue_insert_tail(&wait_queue, cur_task_exec2);         //add current task to IO wait queue
            pthread_mutex_unlock(&mutex);

            sut_task *task = (sut_task *)cur_task_exec2->data;
            swapcontext(&(task->taskcontext), &c_exec_2_context);  //give control back to C_EXEC_2

            pthread_mutex_lock(&mutex);                                      //at this point, buffer will have read data
            struct queue_entry *msgI2C = queue_pop_head(&I2C_msg_queue);        // pop I2C_msg_queue to get buffer
            pthread_mutex_unlock(&mutex);

            char *rbuf = (char *)(msgI2C->data);        //return buffer
            free(msgI2C);
            return rbuf;
        }
    }


}

void sut_close(int fd)
{
    int IO = 4; //indicate close operation

    struct queue_entry *msgC2I;     //add parameters to C2I_msg_queue
    pthread_mutex_lock(&mutex);
    msgC2I = queue_new_node(&IO);
    queue_insert_tail(&C2I_msg_queue, msgC2I);
    msgC2I = queue_new_node(&fd);
    queue_insert_tail(&C2I_msg_queue, msgC2I);

    if (pthread_equal(C_EXEC_1, pthread_self()) != 0)
    {
        
        queue_insert_tail(&wait_queue, cur_task_exec1); //add current task to IO wait queue
        pthread_mutex_unlock(&mutex);

        sut_task *task = (sut_task *)cur_task_exec1->data;
        swapcontext(&(task->taskcontext), &c_exec_1_context);  //give control back to C_EXEC_1
        return;                                                 //return on completion
    }

    if (NUM_C_EXEC == 2)
    {
        if (pthread_equal(C_EXEC_2, pthread_self()) != 0)
        {
            queue_insert_tail(&wait_queue, cur_task_exec2);     //add current task to IO wait queue
            pthread_mutex_unlock(&mutex);

            sut_task *task = (sut_task *)cur_task_exec2->data;  
            swapcontext(&(task->taskcontext), &c_exec_2_context);       //give control back to C_EXEC_2
            return;                                                       //return on completion
        }
    }
}
