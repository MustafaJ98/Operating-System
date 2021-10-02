// Parts of this code is given by Prof Maheswaran for ECSE 427 Lab1
// and due permission to reuse the code
//
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>


#include "ProcessList.c"
#include "signal_handling.c"


//Method declerations
int getcmd(char *prompt, char ***args, int *background);
void memomry_cleanup_arguments(char ***args, int cnt);
void updateProcessList(ProcessList *list);

int *child_pids;
ProcessList *backgroundProcesses;

int main(void) {
	char **args;
	int bg;

	backgroundProcesses = newList();
	assignSignalToParent();	//turn off ctrl+Z and ctrl+C for main so we can exit child

	printf("Welcome to my shell:\n");
	while (1) {
		args = NULL;
		bg = 0;
		int cnt = getcmd("\n>> ", &args, &bg);
		updateProcessList(backgroundProcesses);			//update list to remove killed processes

		// printf("number of arguments is %d \n", cnt);
		//  char *command = args[0];
		//  printf(" command is: %s \n", command);
		//  char *arguments[cnt - 1];
		//  for (int i = 0; i < cnt - 1; i++) {
		//  arguments[i] = args[i + 1];
		//  printf(" arugment: is  %s \n", arguments[i]);
		//  }

		if (cnt == 0) {			//empty line
			printf("\n");
		} else if (!strcasecmp(args[0], "cd")) {
			if (cnt == 1) {					//user input = cd
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

		} else if (!strcasecmp(args[0], "pwd")) {
			char cwd[4096];
			if (getcwd(cwd, sizeof(cwd)) != NULL) {
				printf("Current working dir: %s\n", cwd);
			} else {
				printf("Error finding current working directory \n");
			}

		} else if (!strcasecmp(args[0], "exit")) {
			printf("Shutting down... \nBye \n");
			memomry_cleanup_arguments(&args, cnt);	//--- Memory clean up
			clear_list(backgroundProcesses);//--- Memory clean up ProcessList
			exit(0);
		} else if (!strcasecmp(args[0], "jobs")) {
			updateProcessList(backgroundProcesses);			//update list to remove killed processes
			print_list(backgroundProcesses);
			//memomry_cleanup_arguments(&args,cnt);	//--- Memory clean up
		} else if (!strcasecmp(args[0], "fg")) {
			pid_t pid_fg = find_from_list_by_index(backgroundProcesses,atoi(args[1]));
			if(pid_fg == -1){
				printf("Process not found. See active processes in jobs\n");
			}
			else{
				kill(pid_fg,SIGCONT);
				waitpid(pid_fg, NULL,0);
			}
			//memomry_cleanup_arguments(&args,cnt);	//--- Memory clean up
		} else {
			pid_t pid = fork();

			if (pid < 0) { //error
				printf("fork failed");
				memomry_cleanup_arguments(&args, cnt);	//--- Memory clean up
				clear_list(backgroundProcesses);//--- Memory clean up ProcessList
				exit(-1);
			} else if (pid == 0) { //child process
				//printf(" child %d \n",getpid());
				assignSignalToChild();
				if (execvp(args[0], args) == -1) {
					printf("Invalid command\n");
				}
				
			} else { //parent process
				//printf("parent : %d \n",getpid());
				if (!bg) {
					//wait(NULL);
					waitpid(pid, NULL, 0);
				}
				else {
//					setpgid(getpid(), 0);
//					tcsetpgrp(STDIN_FILENO, getpid());		//	Tell child who the boss is:
//					tcsetpgrp(STDOUT_FILENO, getpid());		//  stop STDIN OUT stream from child
					kill(pid, SIGTTIN);						//	Tell child who the boss is:
					kill(pid, SIGTTOU);						//  stop stdin and stdout stream in child
					add_to_list(backgroundProcesses, pid, args[0]);
				}
			}
		}
		memomry_cleanup_arguments(&args, cnt);	//--- Memory clean up
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
int getcmd(char *prompt, char ***args, int *background) {
	int length, numberOfArgs = 0;
	char *token, *loc;
	char *line = NULL;
	size_t linecap = 0;

	printf("%s", prompt); // print the promt and read the line
	length = getline(&line, &linecap, stdin);
	if (length <= 0) { // exit if user enter <control>+<d> (indicated by len = -1)
		free(line);
		//--- Memory clean up ProcessList
		clear_list(backgroundProcesses);
		exit(-1);
	}

	char *tokenizer = line;
	if ((loc = strchr(tokenizer, '&')) != NULL) {// Check if background is specified
		*background = 1; // and replace the & sign with space
		*loc = ' ';
	} else
		*background = 0;

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
	return numberOfArgs;
}



void updateProcessList(ProcessList *list){
	int status;
	pid_t childPID;
	for(int i =0; i < list->count; i++){
		childPID = find_from_list_by_index(backgroundProcesses,i);
		if (childPID>0){
			if ( waitpid(childPID,NULL, WNOHANG) != 0 )		//child has finished or not found
				remove_from_list_by_index(backgroundProcesses,i);
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
