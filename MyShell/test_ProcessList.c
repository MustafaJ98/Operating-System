/*
 * test.c
 *
 *  Created on: Sep 27, 2021
 *      Author: MUSTAFA
 */


#include "ProcessList.c"
#include <stdio.h>
#include <stdlib.h>


int main(void) {
	ProcessList *list = newList();
	print_list(list);
	for( int i = 0; i< 15; i++){
		add_to_list(list, i);
	}

	print_list(list);
	remove_from_list(list, 13);
	remove_from_list(list, 7);
	print_list(list);
	remove_from_list(list, 20);
	print_list(list);
	clear_list(list);

}
