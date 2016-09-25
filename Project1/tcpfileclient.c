#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define SRV_PORT 5105
#define MAX_RECV_BUF 256
#define MAX_SEND_BUF 256

int main(int argc, char** argv) {

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        printf("There was an error creating the socket");
        return 1;
    }

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;

    char port[10];
    printf("Enter port number: ");
    fgets(port, 10, stdin);
    serveraddr.sin_port = htons((int)port);

    char ip[10];
    char *s = ip;
    printf("Enter IP address: ");
    fgets(ip, 10, stdin);
    int x = inet_pton(AF_INET, s, &(serveraddr.sin_addr.s_addr));
    if( x < 1){
       printf("IP address error. Shutting down.");
       return 1;
    }

    int e = connect(sockfd, (struct sockaddr*) & serveraddr, sizeof(serveraddr));
    if(e < 0) {
        printf("There was an error with connecting\n");
        return 1;
    }

    int f;
    int sent_bytes;
    printf("Enter a file: ");
    char line[5000];
    fgets(line, 5000, stdin);
    send(sockfd, line, strlen(line), 0);
    int send_strlen = strlen(line); /* length of message to be transmitted */
    if( (sent_bytes = send(sockfd, line, send_strlen, 0)) < 0 ) {
      perror("send error");
      return -1;
    }
    /* attempt to create file to save received data. 0644 = rw-r--r-- */
    if ( (f = open(line, O_WRONLY|O_CREAT, 0644)) < 0 ){
      perror("error creating file");
      return -1;
    }

    int rcvd_bytes = 0;

    /* continue receiving until ? (data or close) */
    while ( (rcvd_bytes = recv(sockfd, line, MAX_RECV_BUF, 0)) > 0 ){

      if (write(f, line, rcvd_bytes) < 0 ) {
         perror("error writing to file");
         return -1;
      }

    }

    close(f); /* close file*/

    int i =  shutdown(sockfd, SHUT_RD);
    if(i < 0) {
        printf("There was an error disconnecting from the server");
        return 1;
    }

    return 0;
}
