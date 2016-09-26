#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define SRV_PORT 5105
#define MAX_BUFF 256

void get_file_name( int, char* );
int send_file( int, char* );

int main(int argc, char** argv){
  int sockfd;
  int clientsocket;
  char file_name[5000];

  /* Creates the socket structures and 0s out the memory. */
  struct sockaddr_in serveraddr, clientaddr;
  memset(&serveraddr, 0, sizeof(serveraddr));
  memset(&clientaddr, 0, sizeof(clientaddr));

  serveraddr.sin_family=AF_INET;
  /*
   * Accepts a specific port from client. If no port is specified, uses
   * random port.
   */
  //serveraddr.sin_port=(argc > 1) ? htons(atoi(argv[1])) : htons(0);
  serveraddr.sin_port=htons(SRV_PORT);
  serveraddr.sin_addr.s_addr=INADDR_ANY;

  /* Checks creation of socket. */
  if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("socket error");
    exit(1);
  }

  /* Checks binding of socket. */
  if(bind(sockfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
    perror("bind error");
    exit(1);
  }

  /* Checks to make sure the server is listening and displays the port. */
  if(listen(sockfd,10) < 0) {
    printf("Failed to listen.\n");
    exit(1);
  } else {
    printf("Listening on port number %d ...\n", ntohs(serveraddr.sin_port));
  }

  /* Loop to run open server. */
  while(1){
    int len = sizeof(clientaddr);

    printf("Waiting for client to connect...\n\n");

    /* Checks for client connections. */
    if((clientsocket = accept(sockfd, (struct sockaddr*) &clientaddr, &len))
          < 0) {
      perror("accept error");
      break;
    }
    else {
      printf("Client is connected.\n");
    }

    /* Checks to make sure file name is received. */
    int rec_bytes;
    if((rec_bytes = recv(clientsocket, file_name, 5000, 0)) < 0){
      perror("Receiving file name error.\n");
      return;
    }
    /* Sends socket and file name to method to verify name and send file. */
    send_file(clientsocket, file_name);

    printf("Closing connection.\n");
    close(clientsocket);
  }
  close(sockfd);
  return 0;
}

/* Function to verify file namd and send requested file. */
int send_file(int socket, char *file_name){
  int send_count;
  ssize_t r_bytes, s_bytes, sent_file_size;
  char send_buff[MAX_BUFF];
  char * error_message = "File not found.\n";
  int f;

  send_count = 0;
  sent_file_size = 0;

  /* Attempts to open requested file. */
  if(open(file_name,O_RDONLY) < 0) {
    perror(file_name);
    if((s_bytes=send(socket,error_message,strlen(error_message),0)) < 0) {
      perror("Error opening file.\n");
      return -1;
    }
  } else {
    printf("Received request for file...\nSending file: %s\n", file_name);
    /* Sends file in increments of MAX_BUFF = 256 bits. */
    while( (r_bytes = read(f, send_buff, MAX_BUFF)) > 0) {
      if((s_bytes = send(socket, send_buff, MAX_BUFF, 0)) < r_bytes) {
        perror("Sending error.\n");
        return -1;
      }
      send_count++;
      sent_file_size += s_bytes;
    }
    close(f);
  }
  /* Displays sending information server side to verify. */
  printf("Sending complete.\n");
  printf("Sent file in %d packets.\n", send_count);
  printf("Sent file size of %d bits.\n", sent_file_size);
}
