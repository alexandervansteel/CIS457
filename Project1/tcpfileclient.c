#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#define SRV_PORT 6120
#define MAX_RECV_BUF 256
#define MAX_SEND_BUF 256

int main(int argc, char** argv) {
    /* Initializes the socket. */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        printf("There was an error creating the socket.\n");
        return 1;
    }

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    /* Prompts user for port and sets port for client and server. */
    char port[10];
    int p_num;
    printf("Enter port number: ");
    if(fgets(port, 10, stdin) == NULL){
      perror("Invalid port. Shutting down. \n");
      return 1;
    }

    /*Convert port to int and check range validity */
    sscanf(port, "%d", &p_num);
    if(p_num < 1025 | p_num > 65536){
      perror("Invalid port. Shutting down. \n");
      return 1;
    }
    serveraddr.sin_port = htons((int) strtol(port,(char **)NULL,10));
    // serveraddr.sin_port=htons(SRV_PORT);

    /* Prompts user for IP address to connect to. */
    char ip[10];
    char *s = ip;
    printf("Enter IP address: ");
    fgets(ip, 10, stdin);
    int x = inet_pton(AF_INET, s, &(serveraddr.sin_addr.s_addr));
    if( x < 1){
       perror("IP address error. Shutting down.\n");
       return 1;
    }

    int e = connect(sockfd, (struct sockaddr*) & serveraddr, sizeof(serveraddr));
    if(e < 0) {
        printf("There was an error with connecting.\n");
        return 1;
    }

    while(1){
      /* Requests file from server by sending file name. */
      int file;
      int sent_bytes;
      printf("Enter a file or type /exit to close the connection: ");
      char line[5000];
      memset(line,0, sizeof(line));
      scanf("%s", line);
//      fgets(line,256,stdin);
      if(strcmp(line,"/exit") == 0){
        printf("Client has chosen to close connection.\n");
        send(sockfd,line,strlen(line),0);
        int i =  shutdown(sockfd, SHUT_RD);
        if(i < 0) {
            printf("There was an error disconnecting from the server.\n");
            return 1;
        }
        return 0;
      }

      printf("File requested: %s\n",line);
      if(line != NULL){
        int send_file = send(sockfd, line, strlen(line), 0);
        if(send_file < 0) {
          perror("send error");
          return -1;
        }
      }

      /* Attempt to create file to save received data. 0644 = rw-r--r-- */
      char new_file [5000] = "copy";
      strcat(new_file, line);
      printf("%s\n", new_file);
      if ( (file = open(new_file, O_WRONLY|O_CREAT, 0644)) < 0 ){
        perror("error creating file");
        return -1;
      }

      ssize_t rcvd_bytes = 0;

      /* Continue receiving until ? (data or close) */
      while ( (rcvd_bytes = recv(sockfd, line, MAX_RECV_BUF, 0)) > 0 ){

        if (write(file, line, rcvd_bytes) < 0 ) {
          perror("Error writing to file. Closing program...\n");
          close(file);
          return -1;
        }

      }
      close(file); /* close file*/
      printf("Transfer successful.\n");
    }
    return 0;
}
