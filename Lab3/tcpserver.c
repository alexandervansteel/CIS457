#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/**
 * This is the server.
 **/

void* handleclient(void* arg) {
  int clientsocket = *(int*)arg;
  char cline[5000];

  while(1){
    printf("Enter a line: ");
    char sline[5000];
    fgets(sline,5000,stdin);

    if((send(clientsocket,sline,strlen(sline),0))==-1) {
      printf("Failure to send message from server.\n");
      close(clientsocket);
      break;
    }
    else {
      int num = recv(clientsocket,cline,5000,0);
      if(num<=0) {
        printf("Error with the connection.\n");
        close(clientsocket);
        break;
      }
      if(num==0) {
        printf("Connection closed.\n");
      }

    printf("Received from client: %s\n",cline);
    if((send(clientsocket,cline,strlen(cline),0))==-1) {
      printf("Failure to send message.\n");
      close(clientsocket);
      break;
    }

    char quit[] = "Quit\n";
    if(strcmp(sline,quit) == 0){
      printf("Shutting down connection.\n");
      int s = shutdown(clientsocket,SHUT_RD);
      char message[] = "Server shut down connection.\n";
      send(clientsocket,message,strlen(message),0);
      if(s<0){
        printf("There was an error disconnecting the line.\n");
      }
      close(clientsocket);
    }
    memset(&sline,0,strlen(sline));
    memset(&cline,0,strlen(cline));
    close(clientsocket);
    }
  }
}

int main(int argc, char** argv){
  int sockfd = socket(AF_INET,SOCK_STREAM,0);

  struct sockaddr_in serveraddr, clientaddr;
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons(9876);
  serveraddr.sin_addr.s_addr = INADDR_ANY;

  bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
  listen(sockfd,10);

  while(1){
    int len = sizeof(clientaddr);
    int clientsocket = accept(sockfd,(struct sockaddr*)&clientaddr,&len);
    pthread_t child;
    pthread_create(&child,NULL,handleclient,&clientsocket);
    pthread_detach(child);
  }
  return 0;
}
