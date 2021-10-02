/*
 * ProcessList.c
 *
 *  Created on: Sep 27, 2021
 *      Author: MUSTAFA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct node Process;
typedef struct ProcessList ProcessList;

ProcessList *newList();
int add_to_list(ProcessList *list, pid_t processID, char* command);
int remove_from_list_by_pid(ProcessList *list, pid_t processID);
int remove_from_list_by_index(ProcessList *list, int index);
pid_t find_from_list_by_index(ProcessList *list, int index);
void print_list(ProcessList *list);
void clear_list(ProcessList *list);

struct node {
	pid_t processID;
	char* command;
	struct node *next;
};

Process *createProcess(pid_t PID, char *command) {
	Process *newProcess = malloc(sizeof(Process));
	newProcess->processID = PID;
	newProcess->command = malloc(strlen(command) + 1);
	strcpy((newProcess->command), command);
	newProcess->next = NULL;
	return newProcess;
}

struct ProcessList {
	int count;
	Process *head;
};

ProcessList * newList() {
	ProcessList *list = malloc(sizeof(ProcessList));
	list->count = 0;
	list->head = NULL;
	return list;
}

int add_to_list(ProcessList *list, pid_t processID, char* command) {
	if (list->head == NULL) { //list is empty
		list->head = createProcess(processID, command);
	} else {              //list has processes
		Process *current = list->head;
		while (current->next != NULL) {
			current = current->next;
		}
		current->next = createProcess(processID, command);
	}
	list->count = list->count + 1;
	return list->count;
}

int remove_from_list_by_pid(ProcessList *list, pid_t processID) {
	if (list == NULL)                             //Null check
		return 0;
	Process *current = list->head;
	Process *previous = current;
	while (current != NULL) {			//iterate through list to find Process
		if (current->processID == processID) {
			previous->next = current->next;
			if (current == list->head)			//if Process to remove is head
				list->head = current->next;	// make next item the current head
			free(current->command);
			free(current);
			list->count--;
			return list->count;
		}
		previous = current;
		current = current->next;
	}
	return -1;           //did not find Process to remove
}

int remove_from_list_by_index(ProcessList *list, int index) {
	if (list == NULL)                             //Null check
		return -1;
	if (index > list->count)
		return -1;
	Process *current = list->head;
	Process *previous = current;
	int i = 0;
	while (i < index) {			//iterate through list to find Process
		if (current == NULL)
			return -1;
		previous = current;
		current = current->next;
		i++;
	}
	previous->next = current->next;
	if (current == list->head)			//if Process to remove is head
		list->head = current->next;	// make next item the current head
	free(current->command);
	free(current);
	list->count--;
	return list->count;

	return -1;           //did not find Process to remove
}


pid_t find_from_list_by_index(ProcessList *list, int index) {
	if (list == NULL)                             //Null check
		return -1;
	if (index >= list->count)
		return -1;
	Process *current = list->head;
	int i = 0;
	while (i < index) {			//iterate through list to find Process
		if (current == NULL)
			return -1;
		current = current->next;
		i++;
	}
	return current->processID;
}

void print_list(ProcessList *list) {
	printf("\n");
	fflush(stdout);
	if (list->head == NULL) {
		printf("No running processes\n");
	} else {
		Process *current = list->head;
		int i = 0;
		while (current != NULL) {
			printf("%d \t Process ID: %d \t %s \n", i++, current->processID,
					current->command);
			current = current->next;
		}
	}
	fflush(stdout);
}

void clear_list(ProcessList *list) {
	if (list != NULL) {
		Process *current = list->head;
		Process *next = current;
		while (current != NULL) {
			next = current->next;
			free(current->command);
			free(current);
			current = next;
		}
		free(list);
	}
}
