/*
*	Created by: Andrew O'Shei
*
*	Multithreaded Server with thread pools
*	Uses linked queue to manage threads
*	1. Receives filepath from client
*	2. Reads the file
*	3. Returns the file data to client
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>

#define BUFFER_SIZE 4096
#define MAX_PACKET_SIZE 512
#define ERROR (-1)
#define SERVER_BACKLOG 100



// Sets the number of threads used in thread pool
#define THREAD_POOL_SIZE 20

// Declare thread_pool
pthread_t thread_pool[THREAD_POOL_SIZE];

// Create a Mutex Law to make the queue thread safe
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Create condition variable, allows threads to wait for a condition
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

// Declare node struct for making linked list/queue
struct node {
	struct node* next;
	int *client_socket;
};

// Set typedef so we can refer to struct node as simply node_t
typedef struct node node_t;
typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

// Declare function prototypes
void * handle_connection(void* p_client_socket);
int check(int exp, const char *msg);
void * thread_function(void *arg);
void enter_Queue(int *client_socket);
int* exit_Queue();
long int get_file_size(char *path);
char* long_to_char(unsigned long val);

// Declare head and tail for linking elements in our queue
node_t* q_head = NULL;
node_t* q_tail = NULL;

int main(int argc, char **argv)
{
	int server_socket, server_port, client_socket, addr_size;
	SA_IN server_addr, client_addr;

	// Check for server port
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		exit(0);
	}
	server_port = atoi(argv[1]);

	// Create the thread pool
	for (int i = 0; i < THREAD_POOL_SIZE; i++)
	{
		pthread_create(&thread_pool[i], NULL, thread_function, NULL);
	}

	check((server_socket = socket(AF_INET, SOCK_STREAM, 0)),
				"Failed to create socket");

	// Init Address Struct
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(server_port);

	check(bind(server_socket, (SA*)&server_addr,
				sizeof(server_addr)),
				"Failed to bind socket");

	check(listen(server_socket, SERVER_BACKLOG), "Listen failed");

	while(true)
	{
		printf("Waiting for connection\n");

		addr_size = sizeof(SA_IN);
		check(client_socket = accept(server_socket,
					(SA*)&client_addr, (socklen_t*)&addr_size),
					"Failed to accept connection");
		printf("Connected!\n");

		int *pclient = malloc(sizeof(int));
		*pclient = client_socket;

		// Lock the queue with the mutex
		pthread_mutex_lock(&mutex);

		// Add to queue
		enter_Queue(pclient);

		// Send condition signal here to end thread wait cycle
		pthread_cond_signal(&condition_var);

		// Unlock the queue
		pthread_mutex_unlock(&mutex);
	}
	return 0;
}

int check(int exp, const char *msg)
{
	if (exp == ERROR)
	{
		perror(msg);
		exit(1);
	}
	return exp;
}


// Put threads in an infinite loop
// End condition can be put here to terminate the server gracefully
void * thread_function(void *arg)
{
	while (true)
	{
		int *pclient;
		// Lock the mutex
		pthread_mutex_lock(&mutex);

		// Check if there is work to be done before waiting
		if ((pclient = exit_Queue()) == NULL)
		{
			// Set condition wait, thread will stall here unti it receives signal
			// Mutex variable is passed because conditions work closely with mutex
			// Basically, by passing mutex we can release the mutex lock when waiting
			pthread_cond_wait(&condition_var, &mutex);

			// Signal called try again
			pclient = exit_Queue();
		}
		// Unlock the mutex here
		pthread_mutex_unlock(&mutex);

		// If client has a connection
		if (pclient != NULL)
		{
			// We have a connection, the thread has work to do
			handle_connection(pclient);
		}
	}
}


void * handle_connection(void* p_client_socket)
{
	int client_socket = *((int*)p_client_socket);
	free(p_client_socket);
	char buffer[BUFFER_SIZE], *buf, *wr_size;
	size_t bytes_read;
	int msgsize = 0;
	char actualpath[PATH_MAX+1];

	// Read the client's message
	//while((bytes_read = read(client_socket, buffer + msgsize, sizeof(buffer) - msgsize - 1)) > 0)
	while ((bytes_read = recv(client_socket, buffer + msgsize, sizeof(buffer) - msgsize - 1, 0)) > 0)
	{
		msgsize += bytes_read;
		if (msgsize > BUFFER_SIZE - 1 || buffer[msgsize - 1] == '\n')
		{
			break;
		}
	}
	// If no bytes to read print error
	check(bytes_read, "recv error");
	// Null terminate the message (remove '\n'
	buffer[msgsize - 1] = 0;

	printf("REQUEST: %s\n", buffer);
	fflush(stdout);

	// Validity check
	if (realpath(buffer, actualpath) == NULL)
	{
		char message[] = "-> REQUESTED FILE NOT FOUND\n-> CLOSING CONNECTION\n";
		printf("BAD PATH: %s\n", buffer);
		write(client_socket, "\0\0\0\0", 4);
		write(client_socket, message, strlen(message));
		close(client_socket);
		return NULL;
	}

	// Read the file
	FILE *fp = fopen(actualpath, "r");
	if (fp == NULL)
	{
		char message[] = "-> UNABLE TO ACCESS REQUESTED FILE\n-> CLOSING CONNECTION\n";
		printf("FAILED TO OPEN: %s\n", buffer);
		write(client_socket, "\0\0\0\0", 4);
		write(client_socket, message, strlen(message));
		close(client_socket);
		return NULL;
	}
	
	// Get the file size
	unsigned long f_size = get_file_size(actualpath);
	printf("ST File Size: %ld\n", f_size);
	
	unsigned long f_index = 0, f_pack = f_size / MAX_PACKET_SIZE;
	
	printf("Number of packets to send: %ld\n", f_pack+1);
	
	if (f_size > 0)
	{
		// Create buffer memory and load file to memory
		buf = (char*)malloc(sizeof(char)*f_size + 4);
		if (buf == NULL)
		{
			printf("ERROR: Failed to allocate memory\n");
		}
		else
		{
			// Create buffer header (file size is encoded in the first 4-bytes of message))
			char *u_long;
			u_long = long_to_char(f_size);
			
			// Load the file into memory buffer
			fread(buf, sizeof(char), f_size, fp);
			fclose(fp);
			
			// TODO Need to handle sending a file greater than 65535 bytes
			// note, his will require breaking it into chunks
			printf("Sending the file...\n");
			
			// Write the packet header to TCP Socket
			write(client_socket, u_long, 4);
			
			int s = 0;
			// Send the file in chunks of 1024 bytes
			while(f_size > MAX_PACKET_SIZE)
			{			
				s = write(client_socket, buf + f_index, MAX_PACKET_SIZE);
				f_size -= s;
				f_index += s;
			}
			// If there is still a littld data left over send the rest
			if(f_size > 0)
			{
				s = write(client_socket, buf+f_index, f_size);
				f_index += s;
			}
			printf("Sent %ld bytes\n", f_index);
			free(buf);
			printf("Finished!\n");
			//Close the client connection
			close(client_socket);
			printf("Closing connection\n");
		}
	}
	else
	{
		printf("ERROR: File returned 0 size\n");
	}
	return NULL;	
}


// function for elements entering the queue
void enter_Queue(int *client_socket)
{
	node_t *newnode = malloc(sizeof(node_t));
	newnode->client_socket = client_socket;
	newnode->next = NULL;
	if (q_tail == NULL)
	{
		q_head = newnode;
	} else {
		q_tail->next = newnode;
	}
	q_tail = newnode;
}

// Function for elements leaving the queue
int* exit_Queue()
{
	if (q_head == NULL)
	{
		return NULL;
	} else {
		int *result = q_head->client_socket;
		node_t *temp = q_head;
		q_head = q_head->next;
		if (q_head == NULL)
		{
			q_tail = NULL;
		}
		free(temp);
		return result;
	}
}

// Returns the size of a file inbytes
long int get_file_size(char *path)
{
	struct stat st;
	stat(path, &st);
	return st.st_size;	
}

// Convert unsigned long int to char array
char* long_to_char(unsigned long val)
{
	char *buf, *ptr = (char*)&val;
	buf = malloc(sizeof(char)*4);
	for (int i = 0; i < 4; i++)
	{
		buf[i] = ptr[i];
	}
	return buf;
}
