/*
 * signal_handling.c
 *
 *  Created on: Sep 26, 2021
 *      Author: MUSTAFA
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void sigHandlerChildExit(int signum);
void assignSignalToParent();
void assignSignalToChild();

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
}
