// Parts of this code is given by Prof Maheswaran for ECSE 427 Lab1
// and due permission to reuse the code
//
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "ProcessList.c"
//#include "signal_handling.c"

//Method declerations
int getcmd(char *prompt, char ***args, int *background, int*redirect,
		int*piping, int*indexPiping);
void memomry_cleanup_arguments(char ***args, int cnt);
void updateProcessList(ProcessList *list);
void sigHandlerChildfg (int signum);
void sigHandlerChildExit(int signum);
void assignSignalToParent();
void assignSignalToChild();


int save_in, save_out;
ProcessList *backgroundProcesses;

int main(void) {
	char **args;
	int bg, redirect, piping, indexPiping;


	backgroundProcesses = newList();
	assignSignalToParent();	//turn off ctrl+Z and ctrl+C for main so we can exit child

	printf("Welcome to my shell:\n");
	while (1) {
		args = NULL;
		bg = 0;
		int cnt = getcmd("\n>> ", &args, &bg, &redirect, &piping, &indexPiping);
		updateProcessList(backgroundProcesses);	//update list to remove killed processes

		args = realloc(args,sizeof(char *) *cnt+1);
		args[cnt] = 0;

		/*		 printf("number of arguments is %d \n", cnt);
		 char *command = args[0];
		 printf(" command is: %s \n", command);
		 char *arguments[cnt - 1];
		 for (int i = 0; i < cnt - 1; i++) {
		 arguments[i] = args[i + 1];
		 printf(" arugment: is  %s \n", arguments[i]);
		 }*/

		if (cnt == 0) {							//user input == empty line
			printf("\n");						// do nothing

		} else if (!strcasecmp(args[0], "cd")) {
			if (cnt == 1) {									//user input == cd
				char cwd[4096];
				if (getcwd(cwd, sizeof(cwd)) != NULL) {
					printf("Current working dir: %s\n", cwd);
				}
			} else if (cnt == 2) {
				if (chdir(args[1]) == -1)
					printf("No such file or directory");
			} else {
				printf("Invalid number of arguments \n");
			}

		} else if (!strcasecmp(args[0], "pwd")) {			//user input == pwd

			char cwd[4096];
			if (getcwd(cwd, sizeof(cwd)) != NULL) {
				printf("Current working dir: %s\n", cwd);
			} else {
				printf("Error finding current working directory \n");
			}

		} else if (!strcasecmp(args[0], "exit")) {		//user input == exit
			printf("Shutting down... \nBye \n");
			memomry_cleanup_arguments(&args, cnt+1);		// Memory clean up
			clear_list(backgroundProcesses);	// Memory clean up ProcessList
			exit(0);

		} else if (!strcasecmp(args[0], "jobs")) {
			updateProcessList(backgroundProcesses);	//update list to remove killed processes
			print_list(backgroundProcesses);

		} else if (!strcasecmp(args[0], "fg")) {
			pid_t pid_fg = find_from_list_by_index(backgroundProcesses,
					atoi(args[1]));
			if (pid_fg == -1) {
				printf("Process not found. See active processes in jobs\n");
			} else {
				kill(pid_fg, SIGCONT);
				//kill(pid_fg,  SIGUSR1);
				waitpid(pid_fg, NULL, 0);
			}

		} else {
			pid_t pid = fork();

			if (pid < 0) { //error
				printf("fork failed");
				memomry_cleanup_arguments(&args, cnt+1);		// Memory clean up
				clear_list(backgroundProcesses);// Memory clean up ProcessList
				exit(-1);

			} else if (pid == 0) { 						//child process
				assignSignalToChild();

				if (redirect) {
					close(STDOUT_FILENO);		//close stdout fd == close(1)
					int fd = open(args[cnt - 1], O_RDWR | O_CREAT | O_TRUNC,
							S_IRUSR | S_IWUSR | S_IXUSR);//args[cnt-1] will be the file to redirect to
					if (fd == -1) {
						perror("could not open file \n");
						exit(-1);
					}
					args[cnt - 1] = NULL;//remove output file from args to pass to execvp
				}

				else if (piping) {
					int pf[2];
					pipe(pf);

					int numberOfargs2 = cnt - indexPiping;

					char **args_for_process2;
					args_for_process2 = malloc(sizeof(char*) * (numberOfargs2+1) );
					int i;
					for (i = 0; i < numberOfargs2; i++) {
						args_for_process2[i] = malloc(strlen(args[indexPiping + i])+1);
						strcpy(args_for_process2[i], args[indexPiping + i]);
						args[indexPiping + i] = 0;
					}
					args_for_process2[i] = 0;

					pid_t pid_piping = fork();

					if (pid_piping < 0) { //error
						perror("fork failed");
						memomry_cleanup_arguments(&args, cnt);// Memory clean up
						clear_list(backgroundProcesses);// Memory clean up ProcessList
						exit(-1);
					} else if (pid_piping == 0) {	//child process of piping

						close(pf[1]);
						close(STDIN_FILENO);
						dup(pf[0]);
						close(pf[0]);
						if (execvp(args_for_process2[0], args_for_process2) == -1)
						{
						printf(" Piping Invalid command\n");
						exit(0);
						}
					} else 			// calling process of piping
						{
						close(pf[0]);
						close(STDOUT_FILENO);
						dup(pf[1]);
						close(pf[1]);
						memomry_cleanup_arguments(&args_for_process2, numberOfargs2);
					}
				}
				else if (bg){							//if process is in background
//					save_in = dup(STDIN_FILENO);
//					save_out = dup(STDOUT_FILENO);
//					close(0);
//					close(1);
				}

				if (execvp(args[0], args) == -1) {
					printf("Invalid command\n");
				}


			} else { 										//parent process
				if (!bg) {
					//wait(NULL);
					waitpid(pid, NULL, 0);
				} else {
//					setpgid(getpid(), 0);
//					tcsetpgrp(STDIN_FILENO, getpid());		//	Tell child who the boss is:
//					tcsetpgrp(STDOUT_FILENO, getpid());		//  stop STDIN OUT stream from child
//					kill(pid, SIGTTIN);			//	Tell child who the boss is:
//					kill(pid, SIGTTOU);	//  stop stdin and stdout stream in child
					add_to_list(backgroundProcesses, pid, args[0]);
				}
			}
		}
		memomry_cleanup_arguments(&args, cnt);	// Memory clean up
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
int getcmd(char *prompt, char ***args, int *background, int*redirect,
		int*piping, int*indexPiping) {
	int length, numberOfArgs = 0;
	char *token, *loc;
	char *line = NULL;
	char *linecopy = NULL;
	size_t linecap = 0;

	printf("%s", prompt); // print the promt and read the line
	length = getline(&line, &linecap, stdin);

	if (length <= 0) { // exit if user enter <control>+<d> (indicated by len = -1)
		printf("Shutting down... \nBye \n");
		free(line);
		clear_list(backgroundProcesses); //--- Memory clean up ProcessList
		exit(0);
	}

	linecopy = strdup(line);
	char *tokenizer = line;
	char *findPipeIndex = linecopy;

	if ((loc = strchr(line, '&')) != NULL) { // Check if background is specified
		*background = 1; // and replace the & sign with space
		*loc = ' ';
	} else
		*background = 0;

	if ((loc = strchr(line, '>')) != NULL) { // Check if redirect is specified
		*redirect = 1;
		*loc = ' ';
	} else
		*redirect = 0;

	if ((loc = strchr(line, '|')) != NULL) { // Check if piping is specified

		int ii = 0;
		while ((token = strsep(&findPipeIndex, " \t\n")) != NULL) {	//calculate index of pipe char |
			if (!strcmp(token, "|"))
				*indexPiping = ii;
			ii++;
		}
		*piping = 1;
		*loc = ' ';
	} else
		*piping = 0;

	if (length == 1) {			//empty line
								//do nothing , this will return empty array
	} else {

		while ((token = strsep(&tokenizer, " \t\n")) != NULL) {
			for (int j = 0; j < strlen(token); j++)
				if (token[j] <= 32)
					token[j] = '\0';
			if (strlen(token) > 0) {
				numberOfArgs++;	//size of args array on heap depends on number of argument
				*args = realloc(*args, sizeof(char *) * numberOfArgs); // dynamically allocate memory on heap for array of string
				(*args)[numberOfArgs - 1] = malloc(strlen(token) + 1); //allocate memory for each argument string
				strcpy(((*args)[numberOfArgs - 1]), token);	// store string from getline to heap
			}
		}

	}
	free(line); // free memeory used by getline
	free(linecopy);
	return numberOfArgs;
}

void updateProcessList(ProcessList *list) {
	int status;
	pid_t childPID;
	for (int i = 0; i < list->count; i++) {
		childPID = find_from_list_by_index(backgroundProcesses, i);
		if (childPID > 0) {
			if (waitpid(childPID, NULL, WNOHANG) != 0) //child has finished or not found
				remove_from_list_by_index(backgroundProcesses, i);
		}
	}
}
void memomry_cleanup_arguments(char ***args, int cnt) {
	//--- Memory clean up args
	for (int i = 0; i < cnt; i++) {
		free((*args)[i]);
	}
	free(*args);

}

void sigHandlerChildfg (int signum){
	printf("hello 1\n");
	dup2(save_in, 0);
	dup2(save_out, 1);
	close(save_in);
	close(save_out);
	printf("hello 2\n");
}

void sigHandlerChildExit(int signum) {
	kill(getpid(), SIGKILL);
}


void assignSignalToParent() {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGINT to parent signal handler \n");
		exit(-1);
	}

	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGTSTP to parent signal handler \n");
		exit(-1);
	}
}

void assignSignalToChild() {
	if (signal(SIGINT, sigHandlerChildExit) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGINT signal handler \n");
		exit(-1);
	}
	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
		printf("ERROR! Could not bind the SIGTSTP signal handler \n");
		exit(-1);
	}

//	if (signal(  50, sigHandlerChildfg) == SIG_ERR) {
//			printf("ERROR! Could not bind the SI_USER signal handler \n");
//			exit(-1);
//		}

//	struct sigaction sa;
//	        sigemptyset(&sa.sa_mask);
//	        sa.sa_handler = sigHandlerChildfg;
//	        sa.sa_flags = 0;
//	        if ( signal(SIGUSR1, sigHandlerChildfg) == SIG_DFL){
//	        	printf("ERROR! Could not bind the SIGTSTP signal handler \n");
//	        exit(-1);
//	        }
}
