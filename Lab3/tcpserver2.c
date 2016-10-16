#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

void* handleclient(void* arg) {
  int clientsocket = *(int*)arg;
  char line[5000];
  recv(clientsocket,line,5000,0);

  printf("Got from client: %s\n",line);

  close(clientsocket);
}

int main(int argc, char** argv){
  int sockfd = socket(AF_INET,SOCK_STREAM,0);
  fd_set sockets;
  FD_ZERO(&sockets);
  FD_SET(sockfd,&sockets);

  struct sockaddr_in serveraddr, clientaddr;
  serveraddr.sin_family=AF_INET;
  serveraddr.sin_port=htons(9876);
  serveraddr.sin_addr.s_addr=INADDR_ANY;

  bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
  listen(sockfd,10);


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
          char line[5000];
          recv(i,line,5000,0);
          printf("Got from client: %s\n",line);
          close(i);
          FD_CLR(i,&sockets);
        }
      }
    }
  }
  return 0;
}

