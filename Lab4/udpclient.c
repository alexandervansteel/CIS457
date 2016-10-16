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

  char cline[1000];
  char sline[1000];
  char exit_cmd[10] = "/exit\n";

  while(1){
    memset(cline, 0, sizeof(cline));
    memset(sline, 0, sizeof(sline));

    printf("Enter a line: ");
    fgets(cline,1000,stdin);
    if(strcmp(cline,exit_cmd) == 0){
      printf("Closing connection.\n");
      break;
    }
    int send = sendto(sockfd,cline,strlen(cline),0,
                       (struct sockaddr*)&serveraddr,sizeof(serveraddr));
    if(send<0){
      perror("sending error");
      break;
    }

    int len = sizeof(struct sockaddr_in);
    int rec = recvfrom(sockfd,sline,1000,0,
                        (struct sockaddr*)&serveraddr,&len);
    if(rec<0){
      perror("receiving error");
      break;
    } else {
      printf("Received from server: %s\n",sline);
    }
  }
  close(sockfd);

  return 0;
}
