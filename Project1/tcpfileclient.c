#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, char** argv){

  int sockfd;
  if((sockfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) ==-1){
    perror("socket");
    exit(1);
  }

  struct sockaddr_in serveraddr;
  serveraddr.sin_family=AF_INET;
  serveraddr.sin_port=htons(0);
  serveraddr.sin_addr.s_addr=inet_addr("127.0.0.1");

  int e = connect(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
  if(e<0){
    printf("There was an error with connecting\n");
    return 1;
  }

  printf("Enter a line: ");
  char line[5000];
  fgets(line,5000,stdin);

  send(sockfd,line,strlen(line),0);

  recv(sockfd,line,5000,0);
  printf("Received from server: %s\n",line);

  int s = shutdown(sockfd,SHUT_RD);
  if(s<0){
    printf("There was an error disconnecting the line client side.");
  }

  return 0;
}
