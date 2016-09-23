#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

/**
 * This is the client
 **/


int main(int argc, char** argv){

  int sockfd = socket(AF_INET,SOCK_STREAM,0);
  if(sockfd < 0){
    printf("There was an error creating the socket");
    return 1;
  }

  struct sockaddr_in serveraddr;
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons(9876);
  serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int e = connect(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
  if(e<0){
    printf("There was an error with connecting\n");
    return 1;
  }

  while (1){
    printf("Enter a line: ");
    char cline[5000];
    fgets(cline,5000,stdin);

    if((send(sockfd,cline,strlen(cline),0))==-1){
      printf("Failure to send message from client.\n");
      break;
    }
    else {
      char sline[5000];
      int num = recv(sockfd,sline,5000,0);
      if (num<=0){
        printf("Error with the connection.\n");
        break;
      }
      printf("Received from server: %s\n",sline);
      memset(sline,0,strlen(sline));

      char quit[] = "Quit\n";
      if(strcmp(cline, quit) == 0){
        printf("Shutting down connection.\n");
        int s = shutdown(sockfd,SHUT_RD);
        char message[] = "Client shut down connection.\n";
        send(sockfd,message,strlen(message),0);
        if(s<0){
          printf("There was an error disconnecting the line.\n");
        }
      }
    }

    memset(cline,0,strlen(cline));
  }
  return 0;
}
