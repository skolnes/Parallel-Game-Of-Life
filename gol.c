/**
 * File: gol.c
 *
 * Code for COMP280 Project 9 ("Parallel Game of Life")
 *
 * Authors: 
 * Robert de Brum 		rdebrum@sandiego.edu
 * Scott Kolnes   		skolnes@sandiego.edu
 *
 * Program that uses getoptarg to specify user input, read a configuration
 * file, and simulate Conway's Game of Life using threads.
 *
 * User also has the option of obtaining a file on the
 * comp.sandiego.edu server to run the simulation with.
 *
 */

#define _XOPEN_SOURCE 600
#define DELAY 100000
#define MAXFILE 10000

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>

// Use a struct to save the initial conditions to minimize function
// parameters.
typedef struct init {
	int num_rows;
	int num_cols;
	int iterations;
	int init_pairs;
} init_data;

// Creates and defines a thread struct that contains info on where the row 
// starts out and ends as well as its given neighbors
typedef struct threads {
	int row_start;
	int row_end;
	int neighbors;
	int print_thread;
	char *earth;
	int tid;
	int verbose;
	pthread_barrier_t *BARRIER;
	init_data *bounds;
} Threads;

void Pthread_barrier_wait(pthread_barrier_t *BARRIER);

char *initEarth(char *config_file, init_data *bounds, int verbose);

void printEarth(char *earth, init_data bounds, int iteration);

void simulateLife(Threads *thread_data);

int neighbors(char *earth, int index, init_data bounds);

void timeDiff (struct timeval *result, struct timeval *start, struct timeval *end);

void usage ();

int open_clientfd(char *hostname, char *port);

char *getFile(char *config_file);

void listRemoteFiles();

void *threadFunc(void *args);


/**
 *
 * Main function
 *
 * Parse the command line for user-input, simulate Conway's Game of Life using
 * a specified configuration file, and calculate the elapsed simulation time.
 *
 * @param argc; the number of command line arguments.
 * @param argv; a pointer to a list of the command line arguments.
 * @return 0.
 */
int main(int argc, char *argv[]) {

	// Parse the command line.
	int verbose = 0;
	char *config_file = NULL;
	int c = -1;
	int num_threads = 4;
	int p_flag = 0;
	// Variable to determine if -c or -n option is already specified.
	int corn = 0;
	while ((c = getopt(argc, argv, "vc:ln:t:p")) != -1) {
		switch (c) {
			case 'v':
				// Enable verbose mode.
				verbose = 1;
				printf("verbose mode enabled\n");
				break;
			case 'c':
				// If the <c> option was already chosen, print out an error
				// and exit.
				if (corn) { usage(); } 
				++corn;
				// Save the specified configuration file name.
				config_file = optarg;
				break;
			case 'l':
				// Call the function that will list the available
				// configuration files.
				printf("Available configuration files:\n");
				listRemoteFiles();
				// Exit the process; no need to continue.
				exit(0);
			case 'n':
				// If the <c> option was already chosen, print out an error
				// and exit.
				if (corn) { usage(); } 
				++corn;
				// Run the function that will obtain data from the server for
				// a new configuration file.
				printf("Running from remote server...\n");
				config_file = optarg;
				getFile(config_file);
				break;
			case 't':
				// Set the specified number of threads
				num_threads = strtol(optarg, NULL, 10);
				if (num_threads) 
					printf("%d threads\n", num_threads);
				break;
			case 'p':
				// Set the print per thread flag
				p_flag = 1;
				if (p_flag) 
					printf("PRINT THREAD PARTITION ENABLED\n");
				break;	
		}
	}

	// Locals
	int i = 0;
	init_data bounds;

	// Call the function to initialize our game board.
	char *earth = initEarth(config_file, &bounds, verbose);	
	if (earth == NULL) {
		printf("ERROR: initialization failed\n");
	}

	// Make sure the user isn't trying to run an unreasonable amount of
	// threads.
	if(num_threads < 1 || num_threads > bounds.num_rows){
		perror("Too little or too many threads\nGOOD BYE!\n");
		exit(1);
	}

	// Initialize the array of threads and array of structs for the
	// corresponding thread data
	Threads *thread_data;
	pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
	thread_data = malloc(num_threads * sizeof(Threads));	

	// Declare and initialize the barrier	
	pthread_barrier_t BARRIER;
	int check = pthread_barrier_init(&BARRIER, NULL, num_threads);
	if (check != 0) {
		perror("pthread error\n");
		exit(1);
	}
	// Initialize the thread data structs.
	for (i = 0; i < num_threads; ++i) {
		// divide the threads by their start and end rows.
		if (i == 0) { thread_data[i].row_start = 0; }
		else { thread_data[i].row_start = thread_data[i - 1].row_end + 1; }
		if (i < (bounds.num_rows % num_threads)) { thread_data[i].row_end = thread_data[i].row_start + (bounds.num_rows / num_threads); }
		else { thread_data[i].row_end = thread_data[i].row_start + (bounds.num_rows / num_threads) - 1; }
		
		// If print per thread is allowed then set that flag here.
		if (p_flag == 1) { thread_data[i].print_thread = 1; }
		
		// Specify the very first thread to print out the board if in verbose.
		if (verbose == 1 && i == 0) { thread_data[i].verbose = 1; }
		else { thread_data[i].verbose = 0; }
		
		// Give each thread the data required to run. 
		thread_data[i].bounds = &bounds;
		thread_data[i].earth = earth;
		thread_data[i].tid = i;
		thread_data[i].BARRIER = &BARRIER;
		thread_data[i].print_thread = p_flag;
	}

	// Declare the time structs and get the start time.
	struct timeval game_start, game_end, game_diff;
	gettimeofday(&game_start, NULL);
	
	//Creates the threads that will be used to divide up and run gol
	for (i = 0; i < num_threads; i++){
		pthread_create(&threads[i], NULL, threadFunc,&thread_data[i]);
	}
	//Joins the threads together to the main thread
	for(i = 0; i< num_threads; i++){
		pthread_join(threads[i], NULL);
	}
	
	// Stop the timer and calculate the elapsed time.
	gettimeofday(&game_end, NULL);
	timeDiff(&game_diff, &game_start, &game_end);
	printf("Time for %d iterations: %ld.%06ld seconds\n", bounds.iterations, game_diff.tv_sec, game_diff.tv_usec);
	
	// Destroy the barrier.
	check = pthread_barrier_destroy(&BARRIER);
	if (check != 0){
		printf("Barrier destroy error!!");
		exit(1);
	}

	//Frees all allocated memory
	free(earth);
	free(threads);
	free(thread_data);

	return 0;
}

/**
 * Used for testing if calls to pthread_barrier_wait worked or not
 * Not used in this code.
 **/
void Pthread_barrier_wait(pthread_barrier_t *BARRIER) {
	int check = pthread_barrier_wait(BARRIER);
	int success = 0;
	if (check == PTHREAD_BARRIER_SERIAL_THREAD) { 
		success = 1;
	}
	if (success) { printf("success\n"); }
}	

/**
 * threadFunc
 *
 * This function is a thread routine that will have each thread running it
 * simulate the game of life
 *
 * @param args: gets deconstructed into a Threads struct that holds all
 * 		necessary data for the Game of Life Simulation.
 * @return void
**/
void *threadFunc(void *args) {
	// Deconstruct the argument
	Threads *thread_data = (Threads*)args;
	// For each iteration:
	for(int i = 0; i < thread_data->bounds->iterations; ++i) {
		
		// Simulate life for each iteration
		pthread_barrier_wait(thread_data->BARRIER);
		simulateLife(thread_data);
		pthread_barrier_wait(thread_data->BARRIER);

		// If this is the designated board for printing then print the board
		// here and wait.
		if (thread_data->tid == 0 && thread_data->verbose == 1) { printEarth(thread_data->earth, *(thread_data->bounds), i); }
		pthread_barrier_wait(thread_data->BARRIER);
	}
	// If printing per thread is enabled, do so here. 
	pthread_barrier_wait(thread_data->BARRIER);
	if (thread_data->print_thread == 1) {
		printf("Thread %d:\t %d:%d\t(%d)\n", thread_data->tid, thread_data->row_start, 
				thread_data->row_end, thread_data->row_end - thread_data->row_start);
	} /* When each thread is done then return */
	pthread_barrier_wait(thread_data->BARRIER);
	return NULL;
}


/**
 *
 * Usage 
 *
 * Prints out the possible command line options.
 *
 * @param None 
 * @return void.
 **/
void usage () {
	printf("Command line should look like:\n");
	printf("./gol (-v) -c <configuration file>; OR\n");
	printf("./gol -l; OR\n");
	printf("./gol (-v) -n <server-configuration file>\n");
	printf("-v enables verbose mode\n");
	exit(1);
}

/**
 *
 * initEarth 
 *
 * Initializes the game board and the initial condition given by the user's
 * specified configuration file.
 *
 * @param config_file; the user specified configuration file name.
 * @param bounds; a pointer to an init_data struct holding the config file
 * 			specifications.
 * @param verbose; a signifier to whether or not user specifies verbose mode.
 * @return earth; a char pointer to the game board.
 **/
char *initEarth(char *config_file, init_data *bounds, int verbose){
	// Open the configuration file.	
	FILE *init_state = fopen(config_file, "r");
	if (init_state == NULL) {
		printf("ERROR: %s could not be opened\n", config_file);
		exit(1);
	}
	// Read the game specifications. Give an error message if values are not
	// appropriate.
	int ret = 0;	
	ret = fscanf(init_state, "%d", &bounds->num_rows);
	if (ret <= 0) { printf("ERROR\n"); }
	ret = fscanf(init_state, "%d", &bounds->num_cols);
	if (ret <= 0) { printf("ERROR\n"); }
	ret = fscanf(init_state, "%d", &bounds->iterations);
	if (ret < 0) { printf("ERROR\n"); }
	ret = fscanf(init_state, "%d", &bounds->init_pairs);
	if (ret < 0) { printf("ERROR\n"); }
	// Print if in verbose mode.
	if (verbose) {
		printf("number of rows %d\n", bounds->num_rows);
		printf("number of columns %d\n", bounds->num_cols);
		printf("number of iterations %d\n", bounds->iterations);
		printf("number of initial pairs %d\n", bounds->init_pairs);
	}
	// Initialize the values to be read.
	int col = 0;
	int row = 0;
	int index = 0;
	// Allocate memory for the game board.
	char *earth = malloc(bounds->num_rows * bounds->num_cols * sizeof(int));
	if (earth == NULL) {
		printf("ERROR: memory allocation failed\n");
		exit(1);
	}
	// Initialize each cell as dead.	
	for (int i = 0; i < bounds->num_rows * bounds->num_cols; ++i){
		earth[i] = '-';
	}
	// Read the cells that start alive.
	ret = fscanf(init_state, "%d %d", &col, &row);
	while (ret == 2 && ret != EOF) {
		// Convert to 1D array and set to alive.
		index = (bounds->num_cols * row) + col;
		earth[index] = '@';
		ret = fscanf(init_state, "%d %d", &col, &row);
	}
	// Close the file and return the pointer to the game board.
	fclose(init_state);
	return earth;
}

/**
 *
 * printEarth
 *
 * Print's the board out as a N x N torus
 *
 * @param earth; a pointer to the game board. 
 * @param bounds; a pointer to an init_data struct holding the config file
 * 			specifications.
 * @return void. 
 **/
void printEarth(char *earth, init_data bounds, int iteration) {
	// Local variables
	int i = 0;
	int j = 0;
	int index = 0;
	printf("DAY %d\n==================\n", iteration + 1);
	// Print variables out row by row.
	for (i = 0; i < bounds.num_rows; ++i) {
		// Print out each element in the curent column.
		for (j = 0; j < bounds.num_cols; ++j) {
			// Convert to 1D and print out the current element.
			index = (bounds.num_cols * i) + j;
			printf("%c ", earth[index]);
		}
		// Print next row underneath previous row.
		printf("\n");
	}
	usleep(DELAY);
}

/**
 *
 * simulateLife
 *
 * Simulates Conway's game of life for one iteration; determine's which cells
 * live or die based on # of neighbors, saving these cells to an array, and
 * then changing the cells in the array after checking the entire board. 
 *
 * @param thread_data; a pointer to a struct holding all the necessary data. 
 * @return void. 
 **/
void simulateLife(Threads *thread_data){

	// Start and end INDEXES (calculated by each threads row_start and row_end
	int start = thread_data->row_start * thread_data->bounds->num_cols;
	int end = (thread_data->row_end * thread_data->bounds->num_cols)
	   	+ thread_data->bounds->num_cols - 1;
	
	// Use an integer array (initialized to 0) to determine which cells
	// to kill or resurrect. change is same size as earth.
	int *change = calloc(thread_data->bounds->num_rows * thread_data->bounds->num_cols, sizeof(int));
	
	// Walk through the entire earth array.	
	pthread_barrier_wait(thread_data->BARRIER);
	for (int i = start; i < end + 1; i++) {	
		
		// If alive; check neigbors
		if (thread_data->earth[i] == '@') {
			
			// If alive and >= 1 neighbors; KILL
			if (neighbors(thread_data->earth, i, *(thread_data->bounds)) <= 1) {
				change[i] = -1;
			}
			// If alive and >= 4 neighbors; KILL
			else if (neighbors(thread_data->earth, i, *(thread_data->bounds)) >= 4) {
				change[i] = -1;
			}
		}
		
		// If dead with 3 neighbors; RESURRECT
		if (thread_data->earth[i] == '-') {
			if(neighbors(thread_data->earth, i, *(thread_data->bounds)) == 3) {
				change[i] = -2;
			}
		}
	}
	
	// change the indexes that are need to be changed
	pthread_barrier_wait(thread_data->BARRIER);
	for (int j = start; j < end + 1; ++j) {
		
		// -1 means that we kill the cell.
		if (change[j] == -1) {
			thread_data->earth[j] = '-';
		}
		
		// -2 means that we resurrect the cell.
		else if (change[j] == -2) {
			thread_data->earth[j] = '@';
		}
	}
	
	// Free the memory before next call to simulateLife.
	pthread_barrier_wait(thread_data->BARRIER);
	free(change);
}

/**
 *
 * neighbors
 *
 * Determine's the number of live neighbors a cell has by accessing the board
 * as torus. 
 *
 * @param earth; a pointer to the game board. 
 * @param index; the specific index that we are inspecting for neighbors.
 * @param bounds; a pointer to an init_data struct holding the config file
 * 			specifications.
 * @return neighbors; the number of surrounding live cells. 
 **/
int neighbors(char *earth, int index, init_data bounds) {
	// To access the cells, declare lots of variables for simplicity.
	int neighbors = 0;
	int col = (index % bounds.num_cols);
	int row = (index / bounds.num_cols);
	// Use the column and row to determine the cells to check for neighbors.
	// Determine the cells if the board is a torus.
	// Upper, lower, left, and right cells:
	int r_index = (row * bounds.num_cols) + ((col + 1) % bounds.num_cols);
	int l_index = (row * bounds.num_cols) + (((col - 1) + bounds.num_cols) % bounds.num_cols);
	int lower = (((row + 1) % bounds.num_rows) * bounds.num_cols) + col;
	int upper = ((((row - 1) + bounds.num_rows) % bounds.num_rows) * bounds.num_cols) + col; 
	// Split the next variables into two calculations due to length.
	// Use '%' operator to have the index warp around like a torus.
	// Right lower index:
	int r_lower = (lower / bounds.num_cols) * bounds.num_cols;
	r_lower += ((lower % bounds.num_cols) + 1) % bounds.num_cols;
	// Left lower index:
	int l_lower = (lower / bounds.num_cols) * bounds.num_cols;
	l_lower += ((lower % bounds.num_cols) - 1 + bounds.num_cols) % bounds.num_cols;
	// Right upper index:
	int r_upper = (upper / bounds.num_cols) * bounds.num_cols;
	r_upper += ((upper % bounds.num_cols) + 1) % bounds.num_cols;
	// Left upper index:
	int l_upper = (upper / bounds.num_cols) * bounds.num_cols;
	l_upper += ((upper % bounds.num_cols) - 1 + bounds.num_cols) % bounds.num_cols;
	// If there is a live cell at any ofthese indices; increment neighbors.
	// Check left and right of index for neighbors.
	if (earth[l_index] == '@') { neighbors += 1;	}
	if (earth[r_index] == '@') { neighbors += 1;	}
	// Check upper 3 cells for neighbors.
	if (earth[upper] == '@') { neighbors += 1;	}
	if (earth[l_upper] == '@') { neighbors += 1;	}
	if (earth[r_upper] == '@') { neighbors += 1;	}
	// Check lower 3 cells for neighbors.
	if (earth[lower] == '@') { neighbors += 1;	}
	if (earth[l_lower] == '@') { neighbors += 1;	}
	if (earth[r_lower] == '@') { neighbors += 1;	}
	// Return the total number of neighbors.
	return neighbors;
}

/**
 *
 * timeDiff
 *
 * Calculates the elapsed time of between the call to gettimeofday for the
 * beginning and end of the simulation 
 *
 * @param result; the timeval struct to save the resulting difference in. 
 * @param start; the timeval struct storing the starting time.
 * @param end; the timeval struct storing the ending time. 
 * @return void. 
 **/
void timeDiff (struct timeval *result, struct timeval *start, struct timeval *end){
	// Preform the carry operations.
	if (end->tv_usec < start->tv_usec) {
		int nsec = (start->tv_usec - end->tv_usec) / 1000000 + 1;
		start->tv_usec -= 1000000 * nsec;
		start->tv_usec += nsec;
	}
	if (end->tv_usec - start->tv_usec > 1000000) {
		int nsec = (end->tv_usec - start->tv_usec) / 1000000;
		start->tv_usec += 1000000 * nsec;
		start->tv_usec -= nsec;
	}
	// Compute the differences.
	result->tv_sec = end->tv_sec - start->tv_sec;
	result->tv_usec = end->tv_usec - start->tv_usec;
}

/**
 *
 * open_clientfd
 *
 * Determine's the client's file descriptor to use in order to connect to the
 * specified host name and port.
 *
 * @param hostname; the hostname to connect to.
 * @param port; the port to use when connecting.
 * @return clientfd; the client's file descriptor. 
 **/
int open_clientfd(char *hostname, char *port) {
	int clientfd;
	struct addrinfo hints, *listp, *p;

	// Set up the structs to be used;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_flags |= AI_ADDRCONFIG;
	// Get the desired address info.
	getaddrinfo(hostname, port, &hints, &listp);
	
	// Check each possible connection.
	for (p = listp; p; p = p->ai_next) {
		// Create a socket descriptor.
		if ((clientfd = socket(p->ai_family, p ->ai_socktype, p->ai_protocol)) < 0) {
			// If it failed, move on to the next connection.
			continue;
		}
		// Connect to the server.
		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1 ) {
			// Break the for loop if we are successful.
			break;
		}
		// Connection failed, try again.
		close(clientfd);
	}
	// Clean up the info.
	freeaddrinfo(listp);
	// If all connections failed, return -1.
	if (!p) {
		return -1;
	}
	// Otherwise, return the clientfd.
	else {
		return clientfd;
	}
}

/**
 *
 * listRemoteFiles
 * 
 * Uses open_clientfd to create a socket with which to send the message "list"
 * to the comp280.sandiego.edu server, and then prints out the result.
 *
 * @param None. 
 * @return void. 
 **/
void listRemoteFiles() {
	// Allocate memory for output and obtain clientfd.
	char *remote_files = malloc(MAXFILE);
	int clientfd = open_clientfd("comp280.sandiego.edu", "9181");
	// Send the 'list' command to the server.
	send(clientfd, "list", strlen("list"), 0);
	// Receive the feedback and print it out.
	recv(clientfd, remote_files, MAXFILE, 0);
	printf("%s\n", remote_files);
	// Free the allocated memory.
	free(remote_files);
}

/**
 *
 * getFile
 * 
 * Used open_clientfd to create a socket with which to send the message 
 * "get <config_filename>" to the comp280.sandiego.edu server, and writes the
 * data received to a new file to use as the configuration file.
 *
 * @param config_file; the configuration file that the user wants to get from
 * 			the server. 
 * @return config_file; the name of the newly written configuration file. 
 **/
char *getFile(char *config_file) {
	// Allocate memory for the command and server output.
	char *choice = malloc(MAXFILE);
	char *remote_data = malloc(MAXFILE);
	// Obtain clientfd.
	int clientfd = open_clientfd("comp280.sandiego.edu", "9181");
	// Create the command to send to the server.
	strcpy(choice, "get ");
	strcat(choice, config_file);
	// Send the message and receive the server output.
	send(clientfd, choice, strlen(choice), 0);
	recv(clientfd, remote_data, MAXFILE, 0);
	// Open a new file, and print the server data to it.
	FILE *remote = fopen(config_file, "w");
	fprintf(remote, "%s", remote_data);
	// Close the file and free the allocated memory space.
	fclose(remote);
	free(remote_data);
	free(choice);
	// Return the new configuration file to be read.
	return config_file;
}
