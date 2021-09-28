/*
 * LinkedList.c
 *
 *  Created on: Sep 27, 2021
 *      Author: MUSTAFA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "LinkedList.h"

typedef struct node Process;
typedef struct LinkedList ProcessList;

ProcessList *newList();
int add_to_list(ProcessList *list, int processID);
int remove_from_list(ProcessList *list, int processID);
void print_list(ProcessList *list);
void clear_list(ProcessList *list);

struct node {
	int processID;
	//char* args;
	struct node *next;
};


Process *createProcess(int PID) {
	Process *newProcess = malloc(sizeof(Process));
	newProcess->processID = PID;
	newProcess->next = NULL;
	//newProcess->args;
	return newProcess;
}

struct LinkedList {
	int count;
	Process *head;
};


ProcessList * newList() {
	ProcessList *list = malloc(sizeof(ProcessList));
	list->count = 0;
	list->head = NULL;
	return list;
}

int add_to_list(ProcessList *list, int processID) {
	if (list->head == NULL) { //list is empty
		list->head = createProcess(processID);
	} else {              //list has processes
		Process *current = list->head;
		while (current->next != NULL) {
			current = current->next;
		}
		current->next = createProcess(processID);
	}
	list->count = list->count + 1;
	return list->count;
}

int remove_from_list(ProcessList *list, int processID) {
	if (list == NULL)                             //Null check
		return 0;
	Process *current = list->head;
	Process *previous = current;
	while (current != NULL) {			//iterate through list to find Process
		if (current->processID == processID) {
			previous->next = current->next;
			if (current == list->head)			//if Process to remove is head
				list->head = current->next;	// make next item the current head
			free(current);
			list->count--;
			return list->count;
		}
		previous = current;
		current = current->next;
	}
	return -1;           //did not find Process to remove
}

void print_list(ProcessList *list) {
	printf("\n");
	fflush(stdout);
	if (list->head == NULL) {
		printf("List is empty \n");
	} else {
		Process *current = list->head;
		int i = 0;
		while (current != NULL) {
			printf("%d \t Process ID: %d \n", i++, current->processID);
			current = current->next;
		}
	}

}

void clear_list(ProcessList *list) {
	Process *current = list->head;
	Process *next = current;
	while (current != NULL) {
		next = current->next;
		free(current);
		current = next;
	}
	free(list);
}
