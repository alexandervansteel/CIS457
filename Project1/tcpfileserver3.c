#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define SVR_PORT 6120
#define MAX_BUFF 256

int send_file(int, char*);
void sig_chld(int);

int main (int argc, char* argv[]){
  int sockfd, clientsocket;
  socklen_t cli_len;
  pid_t child_pid;
  char file_name[MAX_BUFF];

  /* Creates the socket structures and 0s the memory. */
  struct sockaddr_in serveraddr, clientaddr;
  memset(&serveraddr,0,sizeof(serveraddr));
  memset(&clientaddr,0,sizeof(clientaddr));

  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.sin_port = INADDR_ANY;
  /* Prompts the user to enter the port number. */
  char port[10];
  int p_num;
  printf("Enter the port number: ");
  if(fgets(port,10,stdin)==NULL){
    perror("Invalid port. Shutting down.\n");
    return 1;
  }
  /* Converts port string to int and checks range for validity. */
  sscanf(port,"%d",&p_num);
  if(p_num<1025 | p_num>65536){
    perror("Invalid port number. Shutting down.\n");
    return 1;
  }
  serveraddr.sin_port = htons((int)strtol(port,(char **)NULL,10));

  /* Creates socket */
  if((sockfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) < 0){
    perror("socket error");
    return -1;
  }

  /* Binds socket */
  if(bind(sockfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr)) < 0){
    perror("binding error");
    return -1;
  }

  /* Checks to make sure the server is listening and displays the port. */
  if(listen(sockfd,10) < 0) {
    printf("Failed to listen.\n");
    return -1;
  } else {
    printf("Listening on port number %d ...\n", ntohs(serveraddr.sin_port));
  }

  /* install a signal handler */
  signal (SIGCHLD, sig_chld);

  while(1){
    cli_len = sizeof(clientaddr);

    printf("Witing for client to connect...\n");
    if((clientsocket = accept(sockfd,(struct sockaddr*)&clientaddr,&cli_len)) < 0){
      perror("accept error");
      break;
    }

    if((child_pid = for()) == 0){
      close(sockfd);

      /* do the file work */
      char line[5000];
      /* Checks to make sure file name is received. */
      int rec_bytes;
      if((rec_bytes = recv(sockfd, line, 5000, 0)) < 0){
        perror("Receiving file name error.\n");
        break;
      }
      if(strcmp(line,"/exit\n")){
        printf("Client has chosen to close connection.\n");
        break;
      }
      /* Sends socket and file name to method to verify name and send file. */
      send_file(sockfd, line);
      /* do the file work */

      close(clientsocket);
      exit(0);
    }
    close(clientsocket);
  }
  close(sockfd);
  return 0;
}

/* Define signal handler. */
void sig_chld(int signo){
  pid_t pid;
  int stat;

  while((pid = waitpid(-1,&stat, WNOHANG)) > 0){
    printf("child %d terminated\n", pid);
  }
}

/* Function to verify file namd and send requested file. */
int send_file(int socket, char *file_name){
  int send_count;
  ssize_t r_bytes, s_bytes, sent_file_size;
  char send_buff[MAX_BUFF];
  int f;

  send_count = 0;
  sent_file_size = 0;

  /* Attempts to open requested file. */
  if((f=open(file_name,O_RDONLY)) < 0) {
    perror(file_name);
  } else {
    printf("Received request for file...\nSending file: %s\n", file_name);
    /* Sends file in increments of MAX_BUFF = 256 bits. */
    while( (r_bytes = read(f, send_buff, MAX_BUFF)) > 0) {
      if((s_bytes = send(socket, send_buff, MAX_BUFF, 0)) < r_bytes) {
        perror("Sending error.\n");
        return -1;
      }
      send_count++;
      sent_file_size += s_bytes;
    }
    close(f);
  }
  /* Displays sending information server side to verify. */
  printf("Sending complete.\n");
  printf("Sent file in %d packets.\n", send_count);
  printf("Sent file size of %d bits.\n", sent_file_size);
}
