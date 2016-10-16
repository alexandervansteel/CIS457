#include <sys/socket.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <string.h>
#include <time.h>

//Authors: Chad Teitsma and Zack Patterson

struct arpheader{
   unsigned short int ar_hrd;		/* Format of hardware address.  */
   unsigned short int ar_pro;		/* Format of protocol address.  */
   unsigned char ar_hln;			/* Length of hardware address.  */
   unsigned char ar_pln;			/* Length of protocol address.  */
   unsigned short int ar_op;		/* ARP opcode (command).  */

   unsigned char ar_sha[6];		/* Sender hardware address.  */
   unsigned char ar_sip[4];		/* Sender IP address.  */
   unsigned char ar_tha[6];		/* Target hardware address.  */
   unsigned char ar_tip[4];		/* Target IP address.  */
};

struct ipheader{
   unsigned char stuff[8];
   unsigned char ttl;
   unsigned char protocol;
   unsigned short checksum;
   unsigned char srcip[4];
   unsigned char dstip[4];
};

struct mac_addr{
   char inf_name[8];
   int sock_id;
   struct sockaddr_ll* socket;
};

struct ip_addr{
   char inf_name[8];
   int ip;
};

//function prototypes
unsigned short calcCheckSum(char *data, int len);


int main(){
   int packet_socket;
   //get (linked) list of interfaces (actually addresses) (IPv4, IPv6, MAC Address)
   struct ifaddrs *ifaddr, *tmp;
   if(getifaddrs(&ifaddr)==-1){
      perror("getifaddrs");
      return 1;
   }

   struct ip_addr myips[10];
   int nbrips = 0;

   struct mac_addr mymacs[10];
   int nbrmacs = 0;
   int router_num;

   fd_set sockets;
   FD_ZERO(&sockets);


   //have the list, loop over the list
   for(tmp = ifaddr; tmp!=NULL; tmp=tmp->ifa_next){

      //check if this is an IPv4
      if(tmp->ifa_addr->sa_family==AF_INET){
         struct sockaddr_in *sa;
         struct ip_addr address;
         sa = (struct sockaddr_in *)tmp->ifa_addr;
         //char *addr=inet_ntoa(sa->sin_addr);
         int test = sa->sin_addr.s_addr;
         printf("Current IP added to list: %d\n",test);
         strcpy(address.inf_name, tmp->ifa_name);
         address.ip = test;
         myips[nbrips] = address;
         nbrips++;
      }

      //Check if this is a packet address, there will be one per
      //interface.  There are IPv4 and IPv6 as well, but we don't care
      //about those for the purpose of enumerating interfaces. We can
      //use the AF_INET addresses in this list for example to get a list
      //of our own IP addresses
      if(tmp->ifa_addr->sa_family==AF_PACKET){
         printf("Interface: %s\n",tmp->ifa_name);
         //create a packet socket on interface r?-eth?
         if(!strncmp(&(tmp->ifa_name[3]),"eth",3)){
            printf("Creating Socket on interface %s\n",tmp->ifa_name);
            if(!strncmp(&(tmp->ifa_name[0]),"r1",2)){
               router_num = 1;
            }
            if(!strncmp(&(tmp->ifa_name[0]),"r2",2)){
               router_num = 2;
            }
            //create a packet socket
            //AF_PACKET makes it a packet socket
            //SOCK_RAW makes it so we get the entire packet
            //could also use SOCK_DGRAM char ar_tip[4]to cut off link layer header
            //ETH_P_ALL indicates we want all (upper layer) protocols
            //we could specify just a specific one
            packet_socket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
            if(packet_socket<0){
               perror("socket");
               return 2;
            }
            //Bind the socket to the address, so we only get packets
            //recieved on this specific interface. For packet sockets, the
            //address structure is a struct sockaddr_ll (see the man page
            //for "packet"), but of course bind takes a struct sockaddr.
            //Here, we can use the sockaddr we got from getifaddrs (which
            //we could convert to sockadrouterdr_ll if we needed to)
            if(bind(packet_socket,tmp->ifa_addr,sizeof(struct sockaddr_ll))==-1){
               perror("bind");
            }

            //add to our struct
            struct mac_addr mac;
            mac.sock_id = packet_socket;
            mac.socket = (struct sockaddr_ll*)tmp->ifa_addr;
            strcpy(mac.inf_name,tmp->ifa_name);
            mymacs[nbrmacs] = mac;
            nbrmacs++;

            //add this socket to our list
            FD_SET(packet_socket,&sockets);

         }
      }
   }
   //free the interface list when we don't need it anymore
   freeifaddrs(ifaddr);
   //loop and recieve packets. We are only looking at one interface,
   //for the project you will probably want to look at more (to do so,
   //a good way is to have one socket per interface and use select to
   //see which ones have data)
   printf("Ready to recieve now: I am router %d\n",router_num);
   while(1){

      struct sockaddr_ll recvaddr;
      int recvaddrlen=sizeof(struct sockaddr_ll);

      //create copy and select from which has data

      fd_set tmp_set = sockets;
      if(select(FD_SETSIZE,&tmp_set, NULL,NULL,NULL)==-1){
         printf("select error: %s\n",strerror(errno));
      }


      int i;
      for(i = 0; i < FD_SETSIZE;i++){
         char buf[1500]={0};
         struct ether_header ethheader;
         struct ipheader ipheader;
         struct arpheader arpheader;
         int response_sent = 0;

         //printf("Checking on socket: %d\n", i);
         if(FD_ISSET(i,&tmp_set)){
            printf("SELECT ON SOCKET %d\n", i);
            //maybe need to do this to be able to handle new connections??
            //if(i==packet_socket)

            //we can use recv, since the addresses are in the packet, but we
            //use recvfrom because it gives us an easy way to determine if
            //this packet is incoming or outgoing (when using ETH_P_ALL, we
            //see packets in both directions. Only outgoing can be seen when
            //using a packet socket with some specific protocol)
            int n = recvfrom(i, buf, 1500,0,(struct sockaddr*)&recvaddr, &recvaddrlen);
            //ignore outgoing packets (we can't disable some from being sent
            //by the OS automatically, for example ICMP port unreachable
            //messages, so we will just ignore them here)
            if(recvaddr.sll_pkttype==PACKET_OUTGOING)
            continue;
            //start processing all others
            printf("PACKET SIZE: %d\n", n);

            //copy ethernet header into the buffer
            memcpy(&ethheader,buf,14);
            ethheader.ether_type = ntohs(ethheader.ether_type);
            if (ethheader.ether_type == ETHERTYPE_ARP){
               printf("THIS IS ARP\n");

               memcpy(&arpheader,&buf[14],28);

               if(ntohs(arpheader.ar_op) == 1){
                  printf("THIS IS AN ARP REQUEST\n");

                  //convert from char arrays to integers
                  int source_ip,target_ip;
                  memcpy(&source_ip,arpheader.ar_sip,4);
                  memcpy(&target_ip,arpheader.ar_tip,4);

                  int j;
                  for(j=0;j<nbrips;j++){
                     if(myips[j].ip==target_ip){
                        //this is one of our ip addresses! respond.
                        arpheader.ar_op = htons(2);

                        //swap source hardware address to target hardware address
                        memcpy(arpheader.ar_tha,arpheader.ar_sha,6);

                        //swap target and source ips
                        unsigned char tempip[4];
                        memcpy(tempip,arpheader.ar_sip,4);
                        memcpy(arpheader.ar_sip,arpheader.ar_tip,4);
                        memcpy(arpheader.ar_tip,tempip,4);

                        //fill in our mac address
                        int k;
                        for(k=0;k<nbrmacs;k++){
                           if(i == mymacs[k].sock_id){
                              struct sockaddr_ll mysock = *mymacs[k].socket;
                              memcpy(arpheader.ar_sha,mysock.sll_addr,6);

                              //set ethernet source and destination
                              memcpy(ethheader.ether_shost,mysock.sll_addr,6);
                              memcpy(ethheader.ether_dhost,recvaddr.sll_addr,6);
                           }
                        }

                        //copy our ethernet and arp changes back to buffer
                        memcpy(buf,&ethheader,12);
                        memcpy(&buf[14],&arpheader,28);

                        //send back on the same socket we received
                        send(i,buf,42,0);
                        printf("THIS IS US. ARP REPLY SENT\n\n");
                        response_sent = 1;
                        if(n==-1){
                           printf("-----Send error: %s\n",strerror(errno));
                        }
                     }
                  }
                  if(response_sent == 0){
                     printf("NOT ONE OF OUR IPS. IGNORE THIS\n");
                  }
               }else if (ntohs(arpheader.ar_op) == 2){
                  printf("-----This is an arp reply... Do not handle this here\n");
               }

            }else if(ethheader.ether_type == ETHERTYPE_IP){
               printf("THIS IS IP\n");

               memcpy(&ipheader,&buf[14],20);

               //harvest target ip
               int target_ip;
               memcpy(&target_ip,ipheader.dstip,4);

               int j;
               for(j=0;j<nbrips;j++){
                  if(myips[j].ip==target_ip){
                     printf("THIS IS ONE OF OUR IPS\n");

                     if(ipheader.protocol==1){
                        printf("THIS IS ICMP\n");

                        //ETHERNET HEADER CHAchar prefix[9];NGES
                        //set ethernet header source and destination
                        unsigned char tempmac[6];
                        memcpy(tempmac,ethheader.ether_shost,6);
                        memcpy(ethheader.ether_shost,ethheader.ether_dhost,6);
                        memcpy(ethheader.ether_dhost,tempmac,6);
                        //IP HEADER CHANGES
                        //swap ip header destination and target ips
                        unsigned char tempip[4];
                        memcpy(tempip,ipheader.srcip,4);
                        memcpy(ipheader.srcip,ipheader.dstip,4);
                        memcpy(ipheader.dstip,tempip,4);

                        //ICMP HEADER CHANGES
                        //set type and code to 0. type starts at position 34
                        memset(&buf[34],0,2);

                        //copy our changes back to buffer
                        memcpy(buf,&ethheader,12);
                        memcpy(&buf[14],&ipheader,20);

                        //ether = 14
                        //ip = 20
                        //icmp header = 16
                        //icmp data = 48recvaddr
                        int sendCheck = send(i,buf,98,0);
                        printf("RESPONSED TO ICMP REQUEST\n\n");
                        response_sent = 1;
                        if(sendCheck==-1){
                           printf("-----Send Error: %s\n",strerror(errno));
                        }
                     }else{
                        printf("THIS IS NOT ICMP\n");
                     }
                  }
               }


               if(response_sent == 0){
                  printf("NOT ONE OF OUR IPS\n");
                  //printf("-----make FILE pointer\n");
                  FILE *fp;
                  char line[50];
                  char prefix[9]; //24 bits we need to match
                  char destination[9]; //destination ip (or '-' that says where to go next
                  char interface[8]; //current interface pulled from routing table

                  //IS THE DEST IN THE ROUTING TABLE?
                  if(router_num == 1){
                     fp = fopen("r1-table.txt", "r");
                     //printf("-----opening table 1\n");
                  }else if(router_num == 2){
                     fp = fopen("r2-table.txt", "r");
                     //printf("-----opening table 2\n");

                  } else{
                     printf("-----Invalid router number\n");
                     return -1;
                  }
                  
                  if(fp == NULL) {
                     perror("-----Error opening file");
                     return -1;
                  }

                  while(fgets(line, 50, fp) != NULL){
                     memcpy(prefix,line,8);
                     prefix[8] = '\0';
                     //check if prefix matches the target
                     //printf("-----prefix is %s\n", prefix);
                     int prefixInt, targetCheck, prefixCheck;
                     prefixInt = inet_addr(prefix);
                     targetCheck = 0;
                     prefixCheck = 0;

                     //printf("-----copying ints to proper target\n");
                     memcpy(&targetCheck, &target_ip, 3);
                     memcpy(&prefixCheck, &prefixInt, 3);
                     //printf("Target: %d, Prefix: %d\n",targetCheck,prefixCheck);

                     if(targetCheck == prefixCheck){
                        //printf("-----Found match in Routing Table. ");
                        memcpy(destination, &line[12],8);

                        if(!strncmp(destination,"-",1)){
                           //destination is the target_ip of the packet
                           memcpy(interface,&line[14],7);
                        }else{
                           memcpy(interface,&line[21],7);
                           //printf("-----destination is %s\n", destination);
                           destination[8] = '\0';
                           //printf("-----destination is %s\n", destination);
                           target_ip = inet_addr(destination);
                        }
                        interface[7] = '\0';
                        //printf("Line is %s\n",line);
                        //printf("Searching for interface %s\n",interface);
                        //loop to find which interface to send on
                        int k;
                        for(k=0;k<nbrmacs;k++){
                           //printf("Checking if %s = %s\n",interface,mymacs[k].inf_name);
                           if(!strncmp(interface,mymacs[k].inf_name,8)){
                              //found inteface for the routing table entry\n");
                              //printf("-----found inteface\n");
                              struct timeval timeout={0,1}; //set timeout for 2 seconds
                              /* set receive timeout */
                              setsockopt(mymacs[k].sock_id,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(struct timeval));

                              //construct ARP request header
                              struct arpheader arprequest;
                              arprequest.ar_hrd = htons(1);
                              arprequest.ar_pro = htons(2048);
                              arprequest.ar_hln = 6;
                              arprequest.ar_pln = 4;
                              arprequest.ar_op = htons(1);
                              //set sender hadware address
                              struct sockaddr_ll mysock = *mymacs[k].socket;
                              memcpy(arprequest.ar_sha,mysock.sll_addr,6);
                              //set sener ip address
                              int m, arpsender;
                              for(m = 0; m < nbrips; m++){
                                 if(!strncmp(mymacs[k].inf_name, myips[m].inf_name, 8))
                                 arpsender = myips[m].ip;
                              }
                              memcpy(arprequest.ar_sip, &arpsender, 4);
                              //set target hardware to all zeros
                              memset(arprequest.ar_tha, 0, 6);
                              //set target ip
                              //printf("-----destination is %d\n", target_ip);
                              int arptarget = target_ip;
                              memcpy(arprequest.ar_tip, &arptarget, 4);

                              //contruct Ethernet header
                              struct ether_header ethrequest;
                              memset(ethrequest.ether_dhost, 255, 6);
                              memcpy(ethrequest.ether_shost, mysock.sll_addr, 6);
                              ethrequest.ether_type = htons(ETHERTYPE_ARP);

                              char buffer[42];
                              memcpy(buffer, &ethrequest, 14);
                              memcpy(&buffer[14], &arprequest,28);

                              //printf("Send ARP Request for %s", inet_ntoa(target_ip));
                              //printf(" on interface %s\n",interface);
                              send(mymacs[k].sock_id, buffer, 42, 0);
                              int recvlen = recvfrom(mymacs[k].sock_id, buffer, 42, 0,(struct sockaddr*)&recvaddr, &recvaddrlen);
                              //printf("-----received arp reply with length %d\n", recvlen);
                              if(recvlen < 0){
                                 //TODO: variable length
                                 //ARP REPLY TIMEOUT
                                 //set ethernet header
                                 char errorBuf[100]={0};
                                 memcpy(errorBuf, ethheader.ether_shost, 6);
                                 memcpy(&errorBuf[6], ethheader.ether_dhost, 6);
                                 memcpy(&errorBuf[12], &buf[12], 2);

                                 //put current ip header into data section
                                 int padSet = 0;
                                 memcpy(&errorBuf[38], &padSet, 4);
                                 memcpy(&errorBuf[42], &ipheader, 20);
                                 memcpy(&errorBuf[62], &buf[34], 84);


                                 //set ip header
                                 unsigned short dataLen = htons(104);
                                 memcpy(&ipheader.stuff[2], &dataLen, 2);
                                 ipheader.ttl = 64;
                                 memcpy(ipheader.dstip, ipheader.srcip, 4);
                                 memcpy(ipheader.srcip, &myips[2].ip, 4);
                                 //Calculate ipheader checksum
                                 ipheader.checksum = 0;
                                 ipheader.protocol = 1;
                                 char buff[20];
                                 memcpy(buff,&ipheader,20);
                                 unsigned short ourSum = calcCheckSum(buff,20);
                                 ipheader.checksum = ourSum;

                                 memcpy(&errorBuf[14], &ipheader, 20);

                                 //set icmp
                                 errorBuf[34] = 3;
                                 errorBuf[35] = 1;
                                 char buff2[20];
                                 memcpy(buff2,&errorBuf[34], 32);
                                 unsigned short ourSum2 = calcCheckSum(buff2,32);
                                 memcpy(&errorBuf[36], &ourSum2, 2);

                                 printf("Send ICMP ARP timeout Error\n");
                                 response_sent = 1;
                                 send(i, errorBuf, 118, 0);
                              }else{

                                 //update ether header with correct mac_addr
                                 struct ether_header ethforward;
                                 memcpy(ethforward.ether_dhost, &buffer[6], 6);
                                 memcpy(ethforward.ether_shost, ethrequest.ether_shost, 6);

                                 //decrement ttl
                                 ipheader.ttl --;

                                 //TESTING TIME EXCEEDED
                                 //ipheader.ttl = 0;
                                 //END TESTING

                                 //Check if this goes past 0.
                                 if(ipheader.ttl < 1){
                                    //TODO. variable length?
                                    //Send ICMP error time exceeded
                                    //set ethernet header
                                    char errorBuf[100]={0};
                                    memcpy(errorBuf, ethheader.ether_shost, 6);
                                    memcpy(&errorBuf[6], ethheader.ether_dhost, 6);
                                    memcpy(&errorBuf[12], &buf[12], 2);

                                    //put current ip header into data section
                                    int padSet = 0;
                                    memcpy(&errorBuf[38], &padSet, 4);
                                    memcpy(&errorBuf[42], &ipheader, 20);
                                    memcpy(&errorBuf[62], &buf[34], 84);

                                    //set ip header
                                    unsigned short dataLen = htons(104);
                                    memcpy(&ipheader.stuff[2], &dataLen, 2);
                                    ipheader.ttl = 64;
                                    memcpy(ipheader.dstip, ipheader.srcip, 4);
                                    memcpy(ipheader.srcip, &myips[2].ip, 4);

                                    //Calculate ipheader checksum
                                    ipheader.checksum = 0;
                                    ipheader.protocol = 1;
                                    char buff[20];
                                    memcpy(buff,&ipheader,20);
                                    unsigned short ourSum = calcCheckSum(buff,20);
                                    ipheader.checksum = ourSum;

                                    memcpy(&errorBuf[14], &ipheader, 20);

                                    //set icmp
                                    errorBuf[34] = 11;
                                    errorBuf[35] = 0;
                                    char buff2[20];
                                    memcpy(buff2,&errorBuf[34], 32);
                                    unsigned short ourSum2 = calcCheckSum(buff2,32);
                                    memcpy(&errorBuf[36], &ourSum2, 2);

                                    printf("Send ICMP Time Exceeded Error\n");
                                    response_sent = 1;
                                    send(i, errorBuf, 118, 0);

                                 } else{
                                    //Packet is ready to be forwarded towards destination

                                    //Calculate ipheader checksum
                                    ipheader.checksum = 0;
                                    char buff[20];
                                    memcpy(buff,&ipheader,20);
                                    unsigned short ourSum = calcCheckSum(buff,20);
                                    ipheader.checksum = ourSum;

                                    //copy our changes back to buffer
                                    memcpy(buf,&ethforward,12);
                                    memcpy(&buf[14],&ipheader,20);

                                    int sendCheck = send(mymacs[k].sock_id,buf,n,0);
                                    printf("FORWARDED TOWARDS DESTINATION\n\n");
                                    response_sent = 1;
                                    if(sendCheck==-1){
                                       printf("-----Send Error: %s\n",strerror(errno));
                                    }
                                 }
                              }
                           }
                        }
                     }

                     if(response_sent == 0){
                        printf("End of routing table file\n");
                        //TODO. variable length?
                        //Send ICMP error network unreachable
                        char errorBuf[100]={0};
                        memcpy(errorBuf, ethheader.ether_shost, 6);
                        memcpy(&errorBuf[6], ethheader.ether_dhost, 6);
                        memcpy(&errorBuf[12], &buf[12], 2);

                        //put current ip header into data section
                        int padSet = 0;
                        memcpy(&errorBuf[38], &padSet, 4);
                        memcpy(&errorBuf[42], &ipheader, 20);
                        memcpy(&errorBuf[62], &buf[34], 84);


                        //set ip header
                        unsigned short dataLen = htons(104);
                        memcpy(&ipheader.stuff[2], &dataLen, 2);
                        ipheader.ttl = 64;
                        memcpy(ipheader.dstip, ipheader.srcip, 4);
                        memcpy(ipheader.srcip, &myips[2].ip, 4);
                        //Calculate ipheader checksum
                        ipheader.checksum = 0;
                        ipheader.protocol = 1;
                        char buff[20];
                        memcpy(buff,&ipheader,20);
                        unsigned short ourSum = calcCheckSum(buff,20);
                        ipheader.checksum = ourSum;


                        memcpy(&errorBuf[14], &ipheader, 20);

                        //set icmp
                        errorBuf[34] = 3;
                        errorBuf[35] = 0;
                        char buff2[20];
                        memcpy(buff2,&errorBuf[34], 32);
                        unsigned short ourSum2 = calcCheckSum(buff2,32);
                        memcpy(&errorBuf[36], &ourSum2, 2);

                        printf("Send ICMP Network Unreachable Error\n");
                        response_sent = 1;
                        send(i, errorBuf, 118, 0);
                     }
                  }
               } else{
                  printf("-----Not arp or ip. Type is #: %d\n",ethheader.ether_type);
               }
            }
         }
      }
   }
   //exit
   return 0;
}

unsigned short calcCheckSum(char *data, int len){
   unsigned int sum = 0;
   int i;

   /* Accumulate checksum */
   for (i = 0; i < len - 1; i += 2)
   {
      unsigned short temp = *(unsigned short *) &data[i];
      sum += temp;
   }

   //shift carry over
   while (sum>>16)
   sum = (sum & 0xFFFF) + (sum >> 16);

   return ~sum;
}
