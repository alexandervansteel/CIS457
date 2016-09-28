#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv){
  int sockfd = socket(AF_INET,SOCK_DGRAM,0);
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
  char ip[10];
  char *s = ip;
  printf("Enter IP address: ");
  if(fgets(ip,10,stdin) == NULL){
    perror("invalid input");
  } else {
    int x = inet_pton(AF_INET, s, &(serveraddr.sin_addr.s_addr));
    if(x<1){
      printf("IP address error. Shutting down.\n");
      return 1;
    }
  }
//  serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");

  printf("Enter a line: ");
  char line[1000];
  fgets(line,1000,stdin);

  sendto(sockfd,line,strlen(line),0,
    (struct sockaddr*)&serveraddr, sizeof(serveraddr));

  close(sockfd);

  return 0;
}
