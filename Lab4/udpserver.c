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
    return -1;
  }

  if(bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr)) < 0){
    perror("bind error");
    return -1;
  }

  char line[1000];

  while(1){
    memset(line, 0, sizeof(line));    

    int len = sizeof(struct sockaddr_in);
    int n = recvfrom(sockfd,line,1000,0,
                      (struct sockaddr*)&serveraddr,&len);
    if(n<0){
      perror("receiving error");
      break;
    } else {
      printf("Received from client: %s\n", line);
    }
    
    int send = sendto(sockfd,line,strlen(line),0,
                       (struct sockaddr*)&serveraddr,sizeof(serveraddr));
    if(send<0){
      perror("sending error");
      break;
    } else {
      printf("Sent to client: %s\n", line);
    } 
  }
  close(sockfd);

  return 0;
}
