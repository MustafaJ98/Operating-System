/*
	ECSE427: Operating System
	Assignment 1: Simple Shell
	Author: Mustafa Javed - 260808710
 
 Credits: Parts of this code is given by Prof Maheswaran for ECSE 427 and due permission to reuse the code
*/

// INCLUDE BEGIN
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "ProcessList.c"
//INCLUDES END

// DEFINES BEGIN
#define MAX_PATH_LENGTH 4096
// DEFINES END

//Method declerations BEGIN
int getcmd(char *prompt, char ***args, int *background, int *redirect, int *piping, int *indexPiping);
void memomry_cleanup_arguments(char ***args, int cnt);
void updateProcessList(ProcessList *list);
void killChildProcesses(ProcessList *list);
static void sigHandler(int signum);
void assignSignalsToParent();
void assignSignalToChild();
//Method declerations END

// STATIC VARIABLES BEGIN
ProcessList *backgroundProcesses;		//List to keep record of background processes
pid_t ChildToKill;						// process running on forground to kill on Ctrl+Z
// STATIC VARIABLES END

// main method: entry point for program
int main(void)
{
	char **args;
	int bg, redirect, piping, indexPiping;
	ChildToKill = 0;

	backgroundProcesses = newList();	//Linked List to store background porcesses
	assignSignalsToParent(); 
	printf("Welcome to my shell:\n");
	while (1) {
		args = NULL;
		bg = 0;
		int cnt = getcmd("\n>> ", &args, &bg, &redirect, &piping, &indexPiping);
		updateProcessList(backgroundProcesses); //update list to remove killed processes

		args = realloc(args, sizeof(char *) * cnt + 1);		// append NULL at end of args 
		args[cnt] = NULL;										//as execvp args should end with NULL

		if (cnt == 0) {			// user input is empty line					  		
			printf("\n"); 		// do nothing
		}

		// BUILD IN COMMANDS BEGIN
		else if (!strcasecmp(args[0], "cd")) { 			//user input is "cd"
			if (cnt == 1)	{ 
				char cwd[MAX_PATH_LENGTH];
				if (getcwd(cwd, sizeof(cwd)) != NULL)
				{
					printf("Current working dir: %s\n", cwd);	// print working directory if just cd
				}
			}
			else if (cnt == 2)
			{
				if (chdir(args[1]) == -1)					// change directory to specified path
					printf("No such file or directory");	// print if path is invalid
			}
			else
			{
				printf("Invalid number of arguments \n");
			}
		}
		else if (!strcasecmp(args[0], "pwd")) { //user input == pwd
			char cwd[MAX_PATH_LENGTH];
			if (getcwd(cwd, sizeof(cwd)) != NULL) {
				printf("Current working dir: %s\n", cwd);
			}
			else {
				printf("Error finding current working directory \n");
			}
		}
		else if (!strcasecmp(args[0], "exit")) { 		//user input is "exit"
			printf("Shutting down... \nBye \n");
			memomry_cleanup_arguments(&args, cnt + 1); 		// Memory clean up
			killChildProcesses(backgroundProcesses);		// Kill all child Processes
			clear_list(backgroundProcesses);		   		// Memory clean up ProcessList
			exit(0);
		}
		else if (!strcasecmp(args[0], "jobs"))	{		//user input is "jobs"
			updateProcessList(backgroundProcesses); 	//update list to remove killed processes
			print_list(backgroundProcesses);
		}
		else if (!strcasecmp(args[0], "fg")) {	 //user input is "fg"
			if (cnt == 1) {
				pid_t pid_fg = find_from_list_by_index(backgroundProcesses, 0);
				if (pid_fg == -1) {
					printf("Process not found. See active processes in jobs\n");
				}
				else {
					kill(pid_fg, SIGCONT);
					ChildToKill = pid_fg;
					waitpid(pid_fg, NULL, 0);
				}
			}
			else if (cnt ==2 ) {
				pid_t pid_fg = find_from_list_by_index(backgroundProcesses, atoi(args[1]));
				if (pid_fg == -1) {
					printf("Process not found. See active processes in jobs\n");
				}
				else {
					kill(pid_fg, SIGCONT);
					ChildToKill = pid_fg;
					waitpid(pid_fg, NULL, 0);
				}
			}
			else {
				printf("Invalid number of arguments \n");
			}
			
		}
		// BUILD IN COMMANDS END
		else
		{
			pid_t pid = fork();			// create a child process to run command

			if (pid < 0)	{ 						//error fork failed
				printf("fork failed, unable to run command \n");
				memomry_cleanup_arguments(&args, cnt + 1); 		// Memory clean up
				// killChildProcesses(backgroundProcesses);		// Kill all child Processes
				// clear_list(backgroundProcesses);		   		// Memory clean up ProcessList
				//exit(-1);
			}
			else if (pid == 0) { 					//child process
				assignSignalToChild();			//turn off ctrl+Z but ctrl+C should exit child, see method below
				
				// REDIRECT BEGIN
				if (redirect)
				{
					close(STDOUT_FILENO); 		//close stdout so we can set redirect output 
					int fd = open(args[cnt - 1], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR); 	//args[cnt-1] will be the file to redirect to
					if (fd == -1) {							//error while opening file
						perror("could not open file \n");
						exit(-1);
					}
					args[cnt - 1] = NULL;  //remove output file from args to pass to execvp
				}
				//REDIRECT END

				//PIPIING BEGIN
				else if (piping)
				{
					int pf[2];
					pipe(pf);		//create a pipe

					int numberOfargs2 = cnt - indexPiping;  // we need to seperate arguments for the two pipe processes
					char **args_for_process2;				// array for args of process 2
					args_for_process2 = malloc(sizeof(char *) * (numberOfargs2 + 1));
					int i;
					for (i = 0; i < numberOfargs2; i++) {
						args_for_process2[i] = malloc(strlen(args[indexPiping + i]) + 1);  // make array for each args2
						strcpy(args_for_process2[i], args[indexPiping + i]);		//copy args2 from original args
						args[indexPiping + i] = NULL;								// replace the args[i] with NULL in original args 
					}															 // so we can pass it to execvp in process 1
					args_for_process2[i] = 0;							//End args_for_process2 with NULL

					pid_t pid_piping = fork();			//create a child for process after pipe

					if (pid_piping < 0)	{ 			//error while forking
						perror("fork failed");
						memomry_cleanup_arguments(&args, cnt+1); // Memory clean up
						clear_list(backgroundProcesses);	   // Memory clean up ProcessList
						exit(-1);
					}
					else if (pid_piping == 0){ //child process2 of running command after pipe

						close(pf[1]);				//close writing end of pipe as only reading end is used
						close(STDIN_FILENO);		// close(0) or close the stdin stream
						dup(pf[0]);					//redirect input stream to reading end of pipe
						close(pf[0]);				// pipe clean up
						if (execvp(args_for_process2[0], args_for_process2) == -1) {		// run command 2
							printf(" Piping Invalid command\n");
							exit(0);
						}
					}
					else {					  // set up pipe for the caller child process	
						close(pf[0]);			//close reading end of pipe as only writing end is used
						close(STDOUT_FILENO);	// close the stdout stream
						dup(pf[1]);				//redirect output stream to writing end of pipe
						close(pf[1]);			// pipe clean up
						memomry_cleanup_arguments(&args_for_process2, numberOfargs2);  // Memory clean up
					}
				}

				if (execvp(args[0], args) == -1) {   //run command in child process
					printf("Invalid command\n");
				}
			}
			else
			{ //parent process
				if (bg) {
					printf("Running in bacground:\tPID: %d \n", pid);
					add_to_list(backgroundProcesses, pid, args[0]);
				}
				else {
					ChildToKill = pid;	
					waitpid(pid, NULL, 0);
				}
			}
		}
		memomry_cleanup_arguments(&args, cnt+1); // Memory clean up
	}
}

/*
 * This function takes input from the user. It then allocates memory and stores the arguemtns on heap
 *
 *  Parameters:
 *  Prompt: string to display to user
 *  args: pointer to the array of string in which arguments are to be stored
 *  background: int set to 1 if & is present in user input
 *  Returns the number of arguments from user (including the command)
 */
int getcmd(char *prompt, char ***args, int *background, int *redirect,
		   int *piping, int *indexPiping) {
	int length, numberOfArgs = 0;
	char *token, *loc;
	char *line = NULL;
	char *linecopy = NULL;
	size_t linecap = 0;

	printf("%s", prompt); // print the promt and read the line
	length = getline(&line, &linecap, stdin);

	if (length <= 0)
	{ // exit if user enter <control>+<d> (indicated by len = -1)
		printf("Shutting down... \nBye \n");
		free(line);
		killChildProcesses(backgroundProcesses); // Kill all child Processes
		clear_list(backgroundProcesses); //--- Memory clean up ProcessList
		exit(0);
	}

	linecopy = strdup(line);		//copy line as we need to find pipingIndex
	char *tokenizer = line;			// pointer used for memory clean up
	char *findPipeIndex = linecopy;

	if ((loc = strchr(line, '&')) != NULL) {	 // Check if background is specified
		*background = 1; 						// and replace the & sign with space
		*loc = ' ';
	}
	else
		*background = 0;

	if ((loc = strchr(line, '>')) != NULL) { // Check if redirect is specified
		*redirect = 1;
		*loc = ' ';
	}
	else
		*redirect = 0;

	if ((loc = strchr(line, '|')) != NULL) { // Check if piping is specified

		int ii = 0;
		while ((token = strsep(&findPipeIndex, " \t\n")) != NULL) {   //find index of pipe char |
			if (!strcmp(token, "|"))
				*indexPiping = ii;
			ii++;
		}
		*piping = 1;
		*loc = ' ';
	}
	else
		*piping = 0;

	if (length == 1)
	{	//empty line
		//do nothing , this will return empty array
	}
	else
	{

		while ((token = strsep(&tokenizer, " \t\n")) != NULL)
		{
			for (int j = 0; j < strlen(token); j++)
				if (token[j] <= 32)
					token[j] = '\0';
			if (strlen(token) > 0) {
				numberOfArgs++;										   //size of args array on heap depends on number of argument
				*args = realloc(*args, sizeof(char *) * numberOfArgs); // dynamically allocate memory on heap for array of string
				(*args)[numberOfArgs - 1] = malloc(strlen(token) + 1); //allocate memory for each argument string
				strcpy(((*args)[numberOfArgs - 1]), token);			   // store string from getline to heap
			}
		}
	}
	free(line); // free memeory used by getline
	free(linecopy); // free memeory used by linecopy
	return numberOfArgs;
}


/*
 This method iterates the process list and removed any child that is no longer running
*/
void updateProcessList(ProcessList *list)
{
	int status;
	pid_t childPID;
	for (int i = 0; i < list->count; i++)
	{
		childPID = find_from_list_by_index(backgroundProcesses, i);
		if (childPID > 0)
		{
			if (waitpid(childPID, NULL, WNOHANG) != 0) //child has finished or not found
				remove_from_list_by_index(backgroundProcesses, i);
		}
	}
}
void memomry_cleanup_arguments(char ***args, int cnt)
{
	//--- Memory clean up args
	for (int i = 0; i < cnt; i++)
	{
		free((*args)[i]);
	}
	free(*args);
}

/*
 This method iterates the process list and sends kill signal to all children
*/
void killChildProcesses(ProcessList *list) {
	int status;
	pid_t childPID;
	for (int i = 0; i < list->count; i++)
	{
		childPID = find_from_list_by_index(backgroundProcesses, i);
		if (childPID > 0)
		{
			kill(childPID, SIGKILL);
		}
	}
}



void assignSignalsToParent() {
	if (signal(SIGINT, sigHandler) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGINT to parent signal handler \n");
		exit(-1);
	}

	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGTSTP to parent signal handler \n");
		exit(-1);
	}
}

void assignSignalToChild() {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGINT signal handler \n");
		exit(-1);
	}
	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGTSTP signal handler \n");
		exit(-1);
	}
}

void sigHandler(int signum) {
	if (signum == SIGINT){
			if (ChildToKill !=0 )
			kill(ChildToKill, SIGTERM);
	}
	
}