#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv){
  int sockfd;
  if(sockfd<0){
    printf("Error making socket.\n");
  }
  struct sockaddr_in serveraddr;

  serveraddr.sin_family = AF_INET;

  char port[10];
  printf("Enter port number: ");
  if(fgets(port,10,stdin) == NULL){
    perror("invalid input");
  } else {
    serveraddr.sin_port = htons((int) strtol(port,(char **)NULL,10));
  }
//  serveraddr.sin_port = htons(9876);
  serveraddr.sin_addr.s_addr = INADDR_ANY;

  if((sockfd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
    perror("socket error");
    exit(1);
  }

  if(bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr)) < 0){
    perror("bind error");
    exit(1);
  } else {
    printf("Waiting to hear from client...\n");
  }

  while(1){
    int len = sizeof(struct sockaddr_in);
    char line[1000];
    int n = recvfrom(sockfd,line,1000,0,
      (struct sockaddr*)&serveraddr,&len);
    printf("Received from client: %s\n", line);
  }

  close(sockfd);

  return 0;
}
