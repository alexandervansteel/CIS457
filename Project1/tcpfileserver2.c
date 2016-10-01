#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

#define SRV_PORT 6120
#define MAX_BUFF 256

void* handleclient(void* arg){
  int clientsocket = (int)arg;
  while(1){
    char line[5000];
    /* Checks to make sure file name is received. */
    int rec_bytes;
    if((rec_bytes = recv(clientsocket, line, 5000, 0)) < 0){
      perror("Receiving file name error.\n");
      return;
    }
    if(strcmp(line,"/exit\n")){
      printf("Client has chosen to close connection.\n");
      break;
    }
    /* Sends socket and file name to method to verify name and send file. */
    send_file(clientsocket, line);
  }
  close(clientsocket);
}

int main(int argc, char** argv){

  int sockfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  fd_set sockets;
  FD_ZERO(&sockets);
  FD_SET(sockfd,&sockets);

  /* Creates the socket structuresand 0s the memory. */
  struct sockaddr_in serveraddr, clientaddr;
  memset(&serveraddr,0,sizeof(serveraddr));
  memset(&clientaddr,0,sizeof(clientaddr));

  serveraddr.sin_family=AF_INET;
  /* Prompts the user to enter the port number. */
  char port[10];
  int p_num;
  printf("Enter the port number: ");
  if(fgets(port,10,stdin)==NULL){
    perror("Invalid port. Shutting down.\n");
    return 1;
  }

  /* Converts port string to int and checks range for validity. */
  sscanf(port,"%d",&p_num);
  if(p_num<1025 | p_num>65536){
    perror("Invalid port number. Shutting down.\n");
    return 1;
  }

  serveraddr.sin_port = htons((int)strtol(port,(char **)NULL,10));
//  serveraddr.sin_port=htons(SRV_PORT);
  serveraddr.sin_addr.s_addr = INADDR_ANY;

  /* Checks creation of socket. */
  if(sockfd < 0){
    perror("socket error");
    return -1;
  }

  /* Checks binding of socket. */
  if(bind(sockfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
    perror("bind error");
    return -1;
  }

  /* Checks to make sure the server is listening and displays the port. */
  if(listen(sockfd,10) < 0) {
    printf("Failed to listen.\n");
    return -1;
  } else {
    printf("Listening on port number %d ...\n", ntohs(serveraddr.sin_port));
  }

  while(1){
    fd_set tmpset = sockets;
    select(FD_SETSIZE,&tmpset,NULL,NULL,NULL);
    int i;
    for(i=0;i<FD_SETSIZE;i++){
      if(FD_ISSET(i,&tmpset)){
        if(i==sockfd){
          int len = sizeof(clientaddr);
          int clientsocket = accept(sockfd,(struct sockaddr*)&clientaddr,&len);
          FD_SET(clientsocket,&sockets);
        } else {
          while(1){
            char line[5000];
            /* Checks to make sure file name is received. */
            int rec_bytes;
            if((rec_bytes = recv(sockfd, line, 5000, 0)) < 0){
              perror("Receiving file name error.\n");
              return;
            }
            if(strcmp(line,"/exit\n")){
              printf("Client has chosen to close connection.\n");
              break;
            }
            /* Sends socket and file name to method to verify name and send file. */
            send_file(sockfd, line);
          }
          close(sockfd);
        }
      }
    }
  }
  return 0;
}

/* Function to verify file namd and send requested file. */
int send_file(int socket, char *file_name){
  int send_count;
  ssize_t r_bytes, s_bytes, sent_file_size;
  char send_buff[MAX_BUFF];
  int f;

  send_count = 0;
  sent_file_size = 0;

  /* Attempts to open requested file. */
  if((f=open(file_name,O_RDONLY)) < 0) {
    perror(file_name);
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
