/*
*	Created by: Andrew O'Shei
*
*	tcpclient.c - A simple TCP client
*	usage: tcpclient <host> <port>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define BUFFER_SIZE 1024
#define MAX_PACKET_SIZE 512

typedef struct sockaddr SA;


unsigned long get_header(int sock);
void save_to_disk(unsigned char *data, unsigned long data_size, char *f_name);

//error - wrapper for perror
void error(char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char **argv)
{
	int client_socket, client_port, SIZE_CHECK;
	struct sockaddr_in serveraddr;
	struct hostent *server;
	char *hostname;
	unsigned char *RD_BUFFER;
	char PATH_BUFFER[BUFFER_SIZE];

	// check command line arguments
	if (argc != 3)
	{
		fprintf(stderr,"USAGE: %s <hostname> <port>\n", argv[0]);
		exit(0);
	}

	hostname = argv[1];
	client_port = atoi(argv[2]);

	// socket: create the socket
	client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (client_socket < 0)
	{
		error("ERROR: Failed to create socket");
		exit(0);
	}

	// gethostbyname: get the server's DNS entry
	server = gethostbyname(hostname);
	if (server == NULL)
	{
		fprintf(stderr,"ERROR: Unable to find host: %s\n", hostname);
		exit(0);
	}

	// build the server's Internet address
	memset((char *) &serveraddr, '\0', sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,
	(char *)&serveraddr.sin_addr.s_addr, server->h_length);
	serveraddr.sin_port = htons(client_port);

	// connect: create a connection with the server
	if (connect(client_socket, (SA*)&serveraddr, sizeof(serveraddr)) < 0)
	{
		error("ERROR: Unable to connect to socket");
		exit(0);
	}

	// get message line from the user
	printf("Enter the name of the file you wish to receive: ");
	memset(PATH_BUFFER, '\0', BUFFER_SIZE);
	fgets(PATH_BUFFER, BUFFER_SIZE, stdin);
	
	// send the message line to the server
	SIZE_CHECK = write(client_socket, PATH_BUFFER, strlen(PATH_BUFFER));
	if (SIZE_CHECK < 0)
	{
		error("ERROR: Socket write failed");
	}
	
	// Clear Line breaks
	PATH_BUFFER[strlen(PATH_BUFFER)-1] = '\0';
	
	// Retrieve Requested file size from the server	
	unsigned long RECV_SIZE = get_header(client_socket);
	printf("File size: %ld\n", RECV_SIZE);
	
	
	// TODO Need to handle receiving a file greater than 65535 bytes
	// note, his will require breaking it into chunks
	if (RECV_SIZE > 0)
	{
		RD_BUFFER = (char*)malloc(sizeof(char)*RECV_SIZE+1);
		if(RD_BUFFER == NULL)
		{
			error("ERROR: Out of memory!");
		}
		else
		{
			int index = 0;
			SIZE_CHECK = 0;
			while(RECV_SIZE > MAX_PACKET_SIZE)
			{
				int s = recv(client_socket, RD_BUFFER + index, MAX_PACKET_SIZE, 0);
				SIZE_CHECK += s;
				RECV_SIZE -= s;
				index = SIZE_CHECK;
			}
			if(RECV_SIZE > 0)
			{
				SIZE_CHECK += recv(client_socket, RD_BUFFER + index, MAX_PACKET_SIZE, 0);
			}
			
			printf("Received %d bytes from server\n", SIZE_CHECK);
			if(SIZE_CHECK > 0)
			{
				printf("FILE RECEIVED FROM SERVER!\n");
				save_to_disk(RD_BUFFER, SIZE_CHECK, PATH_BUFFER);
			}
			else
			{
				error("ERROR: No data received from server\n");
			}
		}
	}
	else
	{
		recv(client_socket, PATH_BUFFER, BUFFER_SIZE, 0);
		printf("REPLY FROM SERVER: \n%s", PATH_BUFFER);
	}
	free(RD_BUFFER);
	close(client_socket);
	return 0;
}

// Parses four bytes into an unsigned long int
unsigned long get_header(int sock)
{
	unsigned char rd[4];
	unsigned long val;
	// rd = (char*)malloc(sizeof(char)*4);
	recv(sock, rd, 4, 0);
	
	/*
	// big endian
	val = (unsigned long)rd[0] << 24 | (unsigned long)rd[1] << 16;
	val |= (unsigned long)rd[2] << 8 | rd[3];
	
	printf("Big Endian: %ld\n", val);
	*/
	
	// little endian
	val = (unsigned long)rd[3] << 24 | (unsigned long)rd[2] << 16;
	val |= (unsigned long)rd[1] << 8 | rd[0];
	
	// printf("Little Endian: %ld\n", val);	
	
	// free(rd);
	return val;
}

// Write file received from server to disk
void save_to_disk(unsigned char *data, unsigned long data_size, char *f_name)
{
	FILE *fp;
	// Check if file already exists
	if (access(f_name, F_OK) == 0)
	{	
		char buf[2];
		printf("ALERT:\n\tA file with the same name already exists at destination\n");
		while (1)
		{ 
			printf("\tWould you like to overwrite the existing file (y or n) ?\n");
			fgets(buf, 2, stdin);
			// if user entered too many characters flush PATH_BUFFERfer
			if (strrchr(buf, '\n') == NULL)
			{
				int c;
				while ((c = getchar()) != '\n' && c != EOF);
			}
			if (buf[0] == 'n' || buf[0] == 'N')
			{
				printf("\nCanceling write file operation\n");
				exit(0);
			}
			else if (buf[0] == 'y' || buf[0] == 'Y')
			{
				printf("Overwriting file with new data\n");
				remove(f_name);
				break;
			}
			printf("Invalid selection, you must either enter 'y' for yes or 'n' for no\n");
		}
	}
	
	// Attempt to open a new file
	if ((fp = fopen(f_name, "wb")) == NULL)
	{
		printf("ERROR: Unable to write file\n");
		exit(1);
	}
	
	// Write the file and check if successful
	if (fwrite(data, sizeof(char), data_size, fp) < data_size)
	{
		printf("ERROR: Failed to write file");
		exit(1);
	}
	fclose(fp);
}



