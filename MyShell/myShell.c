// Parts of this code is given by Prof Maheswaran for ECSE 427 Lab1
// and due permission to reuse the code
//
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include<sys/wait.h>
#include<signal.h>

//Method declerations
int getcmd(char *prompt, char ***args, int *background);
static void sigHandlerChildExit(int signum);
static void sigHandlerIgnore(int signum);
void assignSignalToParent();
void assignSignalToChild();
void killAllChildren();
int *child_pids;
int number_of_children;

int main(void) {
	char **args;
	int bg;
	child_pids = NULL;
	number_of_children = 0;

	printf("Welcome to my shell: \n");
	while (1) {
		bg = 0;
		int cnt = getcmd("\n>> ", &args, &bg);

		/*		printf("number of arguments is %d \n", cnt);
		 char *command = args[0];
		 printf(" command is: %s \n", command);
		 char *arguments[cnt - 1];
		 for (int i = 0; i < cnt - 1; i++) {
		 arguments[i] = args[i + 1];
		 printf(" arugment: is  %s \n", arguments[i]);
		 }*/

		pid_t pid = fork();

		if (pid < 0) {				//error
			printf("fork failed");
			exit(-1);

		} else if (pid == 0) {		//child process
			// child_pids = realloc(number_of_children*sizeof(int));
			// child_pids[number_of_childs++] = getpid();
			assignSignalToChild();
			while (1) {
				printf("printing ... \n");
				sleep(1);
			}

		} else {				//parent process
			assignSignalToParent();

			if (!bg) {
				wait(NULL);
			}
			//waitpid(pid, NULL,0);
		}
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
	int length, i = 0;
	char *token, *loc;
	char *line = NULL;
	char *tokenizer = NULL;
	size_t linecap = 0;

	printf("%s", prompt);					// print the promt and read the line
	fflush(stdout);
	length = getline(&line, &linecap, stdin);
	if (length <= 0) {// exit if user enter <control>+<d> (indicated by len = -1)
		killAllChildren();
		exit(-1);
	}


	if ((loc = strchr(line, '&')) != NULL) { // Check if background is specified
		*background = 1;					// and replace the & sign with space
		*loc = ' ';
	} else
		*background = 0;

	*args = malloc(sizeof(char*) * length);	// dynamically allocate memory on heap for array of string
											// *args is pointer to pointers of strings
											// malloc return a pointer to block of memory
											// of size depending on number of user arguments
											// *args pointer is set to this pointer returned by malloc

	tokenizer = line;
	while ((token = strsep(&tokenizer, " \t\n")) != NULL) {
		for (int j = 0; j < strlen(token); j++)
			if (token[j] <= 32)
				token[j] = '\0';
		if (strlen(token) > 0) {					
			(*args)[i++] = token;					// pointer to string array [i] = token address in heap 
			//(*args)[i] = malloc(strlen(token) );	//allocate memory for each argument string
			//strcpy(((*args)[i++]), token);	// store string from getline to heap
		}

	}

	//free(line);					// free memeory used by getline
	//free(tokenizer);
	return i;
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
	if (signal(SIGINT, sigHandlerChildExit ) == SIG_ERR) {
			printf("ERROR! Could not bind the SIGINT signal handler \n");
			exit(-1);
		}
		if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
			printf("ERROR! Could not bind the SIGTSTP signal handler \n");
			exit(-1);
		}
	}


void killAllChildren(){

}
