#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define MAX_BUFF 256;

void get_file_name(int, char*):
int send_file(int, char*);

int main(int argc, char* argv){
  int sockfd;
  int clientsocket;
  char file_name[MAX_BUFF];


  struct sockaddr_in serveraddr, clientaddr;
  memset(&serveraddr, 0, sizeof(serveraddr));
  memset(&clientaddr, 0, sizeof(clientaddr));

  serveraddr.sin_family=AF_INET;
  serveraddr.sin_port=(argc > 1 ? htons(atoi(argv[1])) : htons(0));
  serveraddr.sin_addr.s_addr=INADDR_ANY;

  if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) < 0) {
    perror("socket error");
    exit(1);
  }

  if(bind(sockfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
    perror("bind error");
    exit(1);
  }

  if(listen(sockfd,10) < 0) {
    printf("Failed to listen.\n");
    exit(1);
  } else {
    printf("Listening on port number %d ...\n", ntohs(serveraddr.sin_port));
  }

  while(1){

    int len = sizeof(clientaddr);

    printf("Waiting for client to connect...\n\n", );

    if((clientsocket = accept(sockfd,
          (struct sockaddr*) &clientaddr,&len)) < 0) {
      perror("accept error");
      break;
    } else {
      printf("Client is connected.\n");
    }

    get_file_name(,file_name);
    send_file(,file_name);

    printf("Closing connection.\n");
    close(clientsocket);
  }
  close(sockfd);
  return 0;
}

void get_file_name(int socket, char* file_name) {
  char rec_str[MAX_BUFF];
  ssize_t rec_bytes;

  if((rec_bytes = recv(socket, rec_str, MAX_BUFF, 0)) < 0) {
    perror("Receiving file name error.\n");
    return;
  }

  sscanf(rec_str, "%s\n", *file_name);
}

int send_file(int socket, char *file_name){
  int send_count;
  ssize_t r_bytes, s_bytes, sent_file_size;
  char send_buff[MAX_BUFF];
  char * error_message = "File not found.\n";
  int f;

  sent_count = 0;
  sent_file_size = 0;

  if((f=open(file_name,O_RDONLY)) < 0) {
    perror(file_name);
    if((s_bytes=send(clientsocket,error_message,strlen(error_message),0)) < 0) {
      perror("Sending error.\n");
      return -1;
    }
  } else {
    printf("Received request for file...\nSending file: %d\n", file_name);
    while((r_bytes = read(f, send_buff, MAX_BUFF)) > 0) {
      if((s_bytes = send(clientsocket, send_buff, MAX_BUFF, 0)) < r_bytes) {
        perror("Sending error.\n");
        return -1;
      }
      send_count++;
      sent_file_size += s_bytes;
    }
    close(f);
  }
  printf("Sending complete.\n");
}