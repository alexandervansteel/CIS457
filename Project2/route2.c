#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netpacket/packet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <net/if_arp.h>

typedef struct {
   uint16_t htype;
   uint16_t ptype;
   uint8_t hlen;
   uint8_t plen;
   uint16_t opcode;
   uint8_t sender_haddr[6];
   uint8_t sender_paddr[4];
   uint8_t target_haddr[6];
   uint8_t target_paddr[4];
} arphdr;

struct interface {
   char *name;
   uint8_t haddr[6];
   uint32_t paddr;
   int packet_socket;
   struct interface *next;
   struct tbl_elem *first;
};

struct tbl_elem {
   uint32_t table_paddr;
   int prefix_len;
   uint32_t dst_addr;
   struct tbl_elem * next;
};

struct interface * interfaces;

#define MAC_ADDR_LEN 6

struct interface * getInterfaceFromIFName(char* ifName);
uint32_t getInterfaceFromAddress(uint32_t daddr, struct interface ** iface);
void findInterfaceWithIP(char **ifaceName, uint32_t ipaddress);
void findMacWithInterface(uint8_t *self_haddr, char *ifaceName);
void processArpRequest(struct interface *tmpIface, char *buf, int length);
void processIcmpEchoRequest(struct interface *tmpIface, char *buf, int length);
void loadRoutingTable(FILE * fp);
void interfaceDump();
void SendArpRequest(uint8_t ** haddr, uint32_t daddr, struct interface * tbl_entry);
int DecrementTTL(char ** buf, int length);
int calc_checksum(char* data, int length);
void SendICMPError(struct interface * tmpIface, char *buf, char * haddr, int error);
void forwardPacket(struct interface * tmpIface, char * buf, int length);

int main(){

   //get list of interfaces (actually addresses)
   struct ifaddrs *ifaddr, *tmp;
   if(getifaddrs(&ifaddr)==-1){
      perror("getifaddrs");
      return 1;
   }

   int router_num;
   //have the list, loop over the list
   printf("Looping through interfaces\n");
   for(tmp = ifaddr; tmp!=NULL; tmp=tmp->ifa_next){

      //printf("Looping through interfaces\n");fflush(stdout);

      // lo, eth0(mac), eth0(ip), eth0(?), eth1(mac), eth1(ip), eth1()

      if(strcmp(tmp->ifa_name,"lo") != 0){

				 struct interface* cur_iface, *prev_iface;
         cur_iface = malloc(sizeof(struct interface));
         cur_iface->name = tmp->ifa_name;
         cur_iface->next = NULL;
         cur_iface->first = NULL;

         if(interfaces == NULL){
            interfaces = cur_iface;
         } else {
            prev_iface->next = cur_iface;
         }


         if(tmp->ifa_addr->sa_family == AF_INET){
            // set up ip address
            cur_iface->paddr = ((struct sockaddr_in *)tmp->ifa_addr)->sin_addr.s_addr;
            break;
         }
         else if(tmp->ifa_addr->sa_family == AF_PACKET){
            int packet_socket;
            printf("Interface: %s\n",tmp->ifa_name);

            if(!strncmp(&(tmp->ifa_name[3]),"eth",3)){
               printf("Creating Socket on interface %s\n",tmp->ifa_name);
               if(!strncmp(&(tmp->ifa_name[0]),"r1",2)){
                  router_num = 1;
               }
               if(!strncmp(&(tmp->ifa_name[0]),"r2",2)){
                  router_num = 2;
               }
            }
            //create a packet socket
            //AF_PACKET makes it a packet socket
            //SOCK_RAW makes it so we get the entire packet
            //could also use SOCK_DGRAM to cut off link layer header
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
            //we could convert to sockaddr_ll if we needed to)
            if(bind(packet_socket,tmp->ifa_addr,sizeof(struct sockaddr_ll))==-1){
               perror("bind");
            }
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 1000;
            if (setsockopt(packet_socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0){
               perror("Error");
            }

            cur_iface->packet_socket = packet_socket;


            int i;
            for(i = 0; i < MAC_ADDR_LEN; i++){
               cur_iface->haddr[i] = ((struct sockaddr_ll*) tmp->ifa_addr)->sll_addr[i];
            }
         }
         prev_iface = cur_iface;
      }
   }
   //free the interfaces list when we don't need it anymore
   freeifaddrs(ifaddr);

   //Get routing table file name and fill the routing table
   FILE *fp = NULL;
   char fileName[50];
   while(fp == NULL){
      if(router_num == 1){
         strcpy(fileName, "r1-table.txt");
         //fileName = "r1-table.txt";
         //printf("-----opening table 1\n");
      }else if(router_num == 2){
      	 strcpy(fileName, "r2-table.txt");
         //fileName = "r2-table.txt";
         //printf("-----opening table 2\n");
      }

      fp = fopen(fileName, "r");
      if(fp == NULL){
         printf("Invalid File Name\n");
         return -1;
      }
   }

   loadRoutingTable(fp);

   interfaceDump();
   //loop and recieve packets. We are only looking at one interface,
   //for the project you will probably want to look at more (to do so,
   //a good way is to have one socket per interfaces and use select to
   //see which ones have data)
   printf("Ready to recieve\n");
   while(1){

      struct interface * interface;
      for(interface = interfaces; interface!=NULL; interface=interface->next) {

         char buf[1500];
         struct sockaddr_ll recvaddr;
         int recvaddrlen=sizeof(struct sockaddr_ll);
         //we can use recv, since the addresses are in the packet, but we
         //use recvfrom because it gives us an easy way to determine if
         //this packet is incoming or outgoing (when using ETH_P_ALL, we
         //see packets in both directions. Only outgoing can be seen when
         //using a packet socket with some specific protocol)
         int n = recvfrom(interface->packet_socket, buf, 1500,0,(struct sockaddr*)&recvaddr, &recvaddrlen);
         //ignore outgoing packets (we can't disable some from being sent
         //by the OS automatically, for example ICMP port unreachable
         //messages, so we will just ignore them here)
         if(recvaddr.sll_pkttype==PACKET_OUTGOING)
            continue;
         //start processing all others

         //Check for timeout
         if(n == -1)
            continue;

         printf("Got a %d byte packet.\n", n);

         if(ntohs(recvaddr.sll_protocol) == ETH_P_ARP){
            printf("Received ARP\n");

            processArpRequest(interface, buf, n);

         } else if(ntohs(recvaddr.sll_protocol) == ETH_P_IP){
            printf("Received IP\n");


            struct iphdr ipHeader;
            memcpy(&ipHeader, buf + sizeof(struct ether_header), sizeof(struct iphdr));
            int checksum = ipHeader.check;
            ipHeader.check = 0;
            memcpy(buf + sizeof(struct ether_header), &ipHeader, sizeof(struct iphdr));

            char ipbuff[sizeof(ipHeader)];
            memcpy(ipbuff, &ipHeader, sizeof(ipHeader));

            ipHeader.check = htons(calc_checksum(ipbuff, sizeof(ipbuff)));
            memcpy(buf + sizeof(struct ether_header), &ipHeader, sizeof(struct iphdr));

            if(checksum == ipHeader.check){

               if(ipHeader.protocol == IPPROTO_ICMP){
                  printf("Received ICMP\n");

                  struct icmphdr icmpHeader;
                  memcpy(&icmpHeader, buf + sizeof(struct ether_header) + sizeof(struct iphdr), sizeof(struct icmphdr));

                  if (icmpHeader.type == ICMP_ECHO){
                     printf("Received ICMP ECHO\n");
                     processIcmpEchoRequest(interface, buf, n);
                  } else if (icmpHeader.type == ICMP_ECHOREPLY){
                     printf("Received ICMP REPLY\n");
                     processIcmpEchoRequest(interface, buf, n);
                  } else {
                     forwardPacket(interface, buf, n);
                  }

               } else {
                  printf("Recieved something else (not ARP or ICMP)\n");
                  forwardPacket(interface, buf, n);
               }
            } else {
               printf("Droppin' this packet\n");
            }
            //what else to do is up to you, you can send packets with send,
            //just like we used for TCP sockets (or you can use sendto, but it
            //is not necessary, since the headers, including all addresses,
            //need to be in the buffer you are sending)
         }
      }

   }
   //exit
   return 0;
}

void interfaceDump(){
   struct interface * tmp;
   printf("==================\n");
   for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      printf("---------\n");
      printf("tmp->name: %s\n", tmp->name);
      printf("MAC: %x:%x:%x:%x:%x:%x\n",
      tmp->haddr[0],tmp->haddr[1],
      tmp->haddr[2],tmp->haddr[3],
      tmp->haddr[4],tmp->haddr[5]);
      printf("tmp->paddr: %d\n", htons(tmp->paddr));
      printf("tmp->packet_socket: %d\n", tmp->packet_socket);
      printf("tmp->next: %s\n",(tmp->next == NULL ? "NULL":"NOT NULL"));
      struct tbl_elem * tbl_tmp;
      for(tbl_tmp = tmp->first; tbl_tmp != NULL; tbl_tmp = tbl_tmp->next){
         printf("tbl_tmp->table_paddr: %d\n", tbl_tmp->table_paddr);
         printf("tbl_tmp->dst_addr: %d\n", tbl_tmp->dst_addr);
         printf("tbl_tmp->prefix_len: %d\n", tbl_tmp->prefix_len);
         printf("tbl_tmp->next: %s\n",(tbl_tmp->next == NULL ? "NULL":"NOT NULL"));
      }
   }
   printf("==================\n");
}

void findInterfaceWithIP(char **ifaceName, uint32_t ipaddress){

   struct interface * tmp;
   for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      if(tmp->paddr == ipaddress){
         printf("interface name: %s\n", tmp->name);
         *ifaceName = tmp->name;
         return;
      }
   }
   *ifaceName = NULL;
}

void findMacWithInterface(uint8_t *self_haddr, char *ifaceName){

   struct interface * tmp;
   for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      if(strcmp(tmp->name, ifaceName)){
         self_haddr = tmp->haddr;
         return;
      }
   }
   self_haddr = NULL;
}

void processArpRequest(struct interface *tmpIface, char *buf, int length){

   struct ether_header ethHeader;
   memcpy(&ethHeader,buf,sizeof(struct ether_header));

   arphdr arpHeader;
   memcpy(&arpHeader,buf+sizeof(struct ether_header),sizeof(arphdr));

   if(arpHeader.opcode == ntohs(ARPOP_REPLY)){
      return;
   }

   uint32_t ipaddress = arpHeader.target_paddr[3] << 24 |
   arpHeader.target_paddr[2] << 16 | arpHeader.target_paddr[1] << 8 |
   arpHeader.target_paddr[0];

   if(tmpIface->paddr == ipaddress){

      // change opcode
      arpHeader.opcode = htons(2);

      // swap sender ip and target ip
      uint8_t tmpPaddr;
      int i;
      for(i = 0; i < 4; i++){
         tmpPaddr = arpHeader.sender_paddr[i];
         arpHeader.sender_paddr[i] = arpHeader.target_paddr[i];
         arpHeader.target_paddr[i] = tmpPaddr;
      }

      // move sender mac to target mac
      int j;
      for(j = 0; j < 6; j++){
         arpHeader.target_haddr[j] = arpHeader.sender_haddr[j];
      }

      // fill in sender mac with self
      for(j = 0; j < 6; j++){
         arpHeader.sender_haddr[j] = tmpIface->haddr[j];
      }



      int k;
      for(k = 0; k < 6; k++ ){
         ethHeader.ether_dhost[k] = ethHeader.ether_shost[k];
         ethHeader.ether_shost[k] = tmpIface->haddr[k];
      }

      memcpy(buf,&ethHeader,sizeof(struct ether_header)); // Copy eth header back
      memcpy(buf+sizeof(struct ether_header),&arpHeader,sizeof(arphdr)); // Copy arp header back

      send(tmpIface->packet_socket,buf,length,0);
   } else {
      printf("Arp not for us\n");
   }
}

void processIcmpEchoRequest(struct interface *tmpIface, char *buf, int length){

   struct ether_header ethHeader;
   memcpy(&ethHeader,buf,sizeof(struct ether_header));

   struct iphdr ipHeader;
   memcpy(&ipHeader,buf+sizeof(struct ether_header),sizeof(struct iphdr));

   struct icmphdr icmpHeader;
   memcpy(&icmpHeader,buf+sizeof(struct ether_header)+sizeof(struct iphdr),sizeof(struct icmphdr));

   // find interfaces that the message is for
   printf("Trying to find match for target IP.\n");

   char *ifaceName;

   findInterfaceWithIP(&ifaceName, ipHeader.daddr);
   // the interfaces name was found
   if(ifaceName != NULL){
      printf("This is for us.\n");
      uint8_t *self_haddr = malloc(sizeof(uint8_t) * MAC_ADDR_LEN);
      findMacWithInterface(self_haddr, ifaceName);

      printf("MAC: %x:%x:%x:%x:%x:%x\n",
      self_haddr[0],self_haddr[1],
      self_haddr[2],self_haddr[3],
      self_haddr[4],self_haddr[5]);

      // send an ICMP echo reply with the same ID, sequence number, and data as the response
      // construct the Ethernet, IP, and ICMP headers.

      icmpHeader.type = ICMP_ECHOREPLY;

      icmpHeader.checksum = 0; // Zero out checksum and copy back to buf
      memcpy(buf+sizeof(struct ether_header)+sizeof(struct iphdr),&icmpHeader,sizeof(struct icmphdr));
      int ttl = DecrementTTL(&buf, length);
      if(ttl < 0){
         //ICMP error
         printf("Sending ICMP Error.\n");
         SendICMPError(tmpIface, buf, ethHeader.ether_shost, ICMP_TIME_EXCEEDED);
      }

      char icmp_buff[sizeof(icmpHeader)];
      memcpy(icmp_buff, &icmpHeader, sizeof(icmpHeader));

      icmpHeader.checksum = htons(calc_checksum(icmp_buff, sizeof(icmpHeader)));
      memcpy(&ipHeader, buf + sizeof(struct ether_header), sizeof(struct iphdr));
      uint32_t tmpAddr;
      tmpAddr = ipHeader.daddr;
      ipHeader.daddr = ipHeader.saddr;
      ipHeader.saddr = tmpAddr;

      int k;
      for(k = 0; k < 6; k++ ){
         ethHeader.ether_dhost[k] = ethHeader.ether_shost[k];
         ethHeader.ether_shost[k] = self_haddr[k];
      }

      // Copy eth header back
      memcpy(buf,&ethHeader,sizeof(struct ether_header));
      // Copy ip header back
      memcpy(buf+sizeof(struct ether_header),&ipHeader,sizeof(struct iphdr));
      // Copy icmp header back
      memcpy(buf+sizeof(struct ether_header)+sizeof(struct iphdr),&icmpHeader,sizeof(struct icmphdr));

      send(tmpIface->packet_socket,buf,length,0);
   } else {
      //ICMP echo is not for us, figure out the correct interfaces to send to
      printf("This is not for us.\n");
      //Check that address in on our routing table
      struct interface * tbl_entry;
      uint32_t ipVal = getInterfaceFromAddress(ipHeader.daddr, &tbl_entry);
      if(tbl_entry != NULL){
         uint8_t * haddr;
         if(ipVal == 0){
            SendArpRequest(&haddr, ipHeader.daddr, tbl_entry);
         }
         else{
            SendArpRequest(&haddr, ipVal, tbl_entry);
         }

         if(haddr == NULL){
            //send ICMP error
            printf("ARP returned no MAC\n");
            SendICMPError(tmpIface, buf, ethHeader.ether_shost, ICMP_DEST_UNREACH);
         } else {
            printf("ARP returned MAC:\n");
            printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
            haddr[0],haddr[1],
            haddr[2],haddr[3],
            haddr[4],haddr[5]);

            struct ether_header ether_header1;
            ether_header1.ether_type = htons(ETH_P_IP);
            for(int i = 0; i < 6; i++){
               ether_header1.ether_dhost[i] = haddr[i];
               ether_header1.ether_shost[i] = tbl_entry->haddr[i];
            }
            icmpHeader.checksum = 0; // Zero out checksum and copy back to buf
            memcpy(buf+sizeof(struct ether_header)+sizeof(struct iphdr),&icmpHeader,sizeof(struct icmphdr));
            int ttl = DecrementTTL(&buf, length);
            if(ttl < 0){
               //ICMP error
               printf("Sending ICMP Error.\n");
               SendICMPError(tmpIface, buf, ethHeader.ether_shost, ICMP_TIME_EXCEEDED);
            }
            char icmp_buff[sizeof(icmpHeader)];
            memcpy(icmp_buff, &icmpHeader, sizeof(icmpHeader));

            icmpHeader.checksum = htons(calc_checksum(icmp_buff, sizeof(icmpHeader)));
            memcpy(buf, &ether_header1, sizeof(struct ether_header));
            memcpy(buf + sizeof(struct ether_header) + sizeof(struct iphdr), &icmpHeader, sizeof(struct icmphdr));
            send(tbl_entry->packet_socket, buf, length, 0);
         }

      } else {
         printf("Address not found in table, sending error\n");
         SendICMPError(tmpIface, buf, ethHeader.ether_shost, ICMP_DEST_UNREACH);

      }
   }
}

void forwardPacket(struct interface * tmpIface, char * buf, int length){

   struct ether_header ethHeader;
   memcpy(&ethHeader,buf,sizeof(struct ether_header));

   struct iphdr ipHeader;
   memcpy(&ipHeader,buf+sizeof(struct ether_header),sizeof(struct iphdr));

   // find interfaces that the message is for
   printf("trying to find match for target IP\n");

   char *ifaceName;

   findInterfaceWithIP(&ifaceName, ipHeader.daddr);
   // the interfaces name was found
   if(ifaceName != NULL){
      printf("This is for us!\n");
      uint8_t *self_haddr = malloc(sizeof(uint8_t) * MAC_ADDR_LEN);
      findMacWithInterface(self_haddr, ifaceName);

      printf("MAC: %x:%x:%x:%x:%x:%x\n",
      self_haddr[0],self_haddr[1],
      self_haddr[2],self_haddr[3],
      self_haddr[4],self_haddr[5]);

      // send an ICMP echo reply with the same ID, sequence number, and data as the response
      // construct the Ethernet, IP, and ICMP headers.

      // uint32_t tmpAddr;
      // tmpAddr = ipHeader.daddr;
      // ipHeader.daddr = ipHeader.saddr;
      // ipHeader.saddr = tmpAddr;

      int k;
      for(k = 0; k < 6; k++ ){
         ethHeader.ether_dhost[k] = ethHeader.ether_shost[k];
         ethHeader.ether_shost[k] = self_haddr[k];
      }

      // Copy eth header back
      memcpy(buf,&ethHeader,sizeof(struct ether_header));
      // // Copy ip header back
      // memcpy(buf+sizeof(struct ether_header),&ipHeader,sizeof(struct iphdr));

      send(tmpIface->packet_socket,buf,length,0);
   } else {
      //ICMP echo is not for us, figure out the correct interfaces to send to
      printf("This isn't for us\n");
      //Check that address in on our routing table
      struct interface * tbl_entry;
      uint32_t ipVal = getInterfaceFromAddress(ipHeader.daddr, &tbl_entry);
      if(tbl_entry != NULL){
         uint8_t * haddr;
         if(ipVal == 0){
            SendArpRequest(&haddr, ipHeader.daddr, tbl_entry);
         } else {
            SendArpRequest(&haddr, ipVal, tbl_entry);
         }

         if(haddr == NULL){
            //send ICMP error
            printf("ARP returned no MAC\n");
         } else {
            printf("ARP returned MAC:\n");
            printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
            haddr[0],haddr[1],
            haddr[2],haddr[3],
            haddr[4],haddr[5]);

            struct ether_header ether_header1;
            ether_header1.ether_type = htons(ETH_P_IP);
            for(int i = 0; i < 6; i++){
               ether_header1.ether_dhost[i] = haddr[i];
               ether_header1.ether_shost[i] = tbl_entry->haddr[i];
            }
            memcpy(buf, &ether_header1, sizeof(struct ether_header));
            send(tbl_entry->packet_socket, buf, length, 0);
         }

      } else {
         printf("Address not found in table, sending error\n");
      }
   }
}

struct interface * getInterfaceFromIFName(char * ifName){
   struct interface * tmp;
   for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      if(strcmp(tmp->name, ifName) == 0){
         return tmp;
      }
   }
   return NULL;
}


void SendICMPError(struct interface * tmpIface, char * buf, char * haddr, int error){
   struct ether_header ethHeader;

   struct iphdr ipHeader;
   memcpy(&ipHeader,buf+sizeof(struct ether_header),sizeof(struct iphdr));

   struct icmphdr icmpHeader;
   memcpy(&icmpHeader,buf+sizeof(struct ether_header)+sizeof(struct iphdr),sizeof(struct icmphdr));

   for(int i = 0; i < 6; i++){
      ethHeader.ether_shost[i] = tmpIface->haddr[i];
      ethHeader.ether_dhost[i] = haddr[i];
   }
   ethHeader.ether_type = htons(ETHERTYPE_IP);

   ipHeader.daddr = ipHeader.saddr;
   ipHeader.saddr = tmpIface->paddr;
   ipHeader.check = 0;
   ipHeader.frag_off = 0;
   ipHeader.ihl = 5;
   ipHeader.protocol = IPPROTO_ICMP;
   ipHeader.tos = 0;
   ipHeader.tot_len = htons(28);
   ipHeader.ttl = 64;
   ipHeader.version = 4;

   icmpHeader.type = error;
   icmpHeader.checksum = 0;
   icmpHeader.code = ICMP_HOST_UNREACH;

   int length =  sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr);

   memcpy(buf, &ethHeader, sizeof(struct ether_header));
   memcpy(buf + sizeof(struct ether_header), &ipHeader, sizeof(struct iphdr));
   memcpy(buf + sizeof(struct ether_header) + sizeof(struct iphdr), &icmpHeader, sizeof(struct icmphdr));

   char ipbuff[sizeof(ipHeader)];
   memcpy(ipbuff, &ipHeader, sizeof(ipHeader));

   ipHeader.check = htons(calc_checksum(ipbuff, sizeof(ipHeader)));
   memcpy(buf + sizeof(struct ether_header), &ipHeader, sizeof(struct iphdr));
   char icmp_buff[sizeof(icmpHeader)];
   memcpy(icmp_buff, &icmpHeader, sizeof(icmpHeader));

   icmpHeader.checksum = htons(calc_checksum(icmp_buff, sizeof(icmpHeader)));
   memcpy(buf + sizeof(struct ether_header) + sizeof(struct iphdr), &icmpHeader, sizeof(struct icmphdr));

   send(tmpIface->packet_socket, buf,  length, 0);

}

uint32_t getInterfaceFromAddress(uint32_t daddr, struct interface ** iface){
   struct interface * tmp;
   for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      struct tbl_elem * tbl_tmp;
      for(tbl_tmp = tmp->first; tbl_tmp!=NULL; tbl_tmp = tbl_tmp->next){
         printf("Prefix len: %d\n", tbl_tmp->prefix_len);
         printf("%x, %x\n", tbl_tmp->table_paddr << (32 - tbl_tmp->prefix_len), daddr << (32 - tbl_tmp->prefix_len));

         if (tbl_tmp->table_paddr << (32 - tbl_tmp->prefix_len) == daddr << (32 - tbl_tmp->prefix_len)){
            *iface = tmp;
            return tbl_tmp->dst_addr;
         }
      }
   }

   return 0;
}

void loadRoutingTable(FILE *fp){
   char line[35];
   while(fgets(line, sizeof(line), fp) != NULL){
      char ip[INET_ADDRSTRLEN];
      int count = 0;
      while(line[count] != '/'){
         ip[count] = line[count];
         count++;
      }
      ip[count] = '\0';

      printf("%s\n", ip);

      count++;
      int length;
      sscanf(&line[count], "%d", &length);
      while(line[count] != ' '){
         count++;
      }
      count++;

      uint32_t dst_addr;
      char temp_ip[INET_ADDRSTRLEN];
      if(line[count] == '-'){
         dst_addr = 0;
      }
      else{
         sscanf(&line[count], "%s", temp_ip);
         printf("%s\n\n", temp_ip);
         dst_addr = (uint32_t)inet_addr(temp_ip);
      }
      while(line[count] != ' '){
         count++;
      }

      count++;

      char ifName[15];
      sscanf(&line[count], "%s", ifName);

      struct interface * iface = getInterfaceFromIFName(ifName);
      if(iface->first == NULL){
         iface->first = malloc(sizeof(struct tbl_elem));
         iface->first->table_paddr =(uint32_t)inet_addr(ip);
         iface->first->dst_addr = dst_addr;
         iface->first->prefix_len = length;
         iface->first->next = NULL;
      }
      else{
         struct tbl_elem * prev = iface->first;
         struct tbl_elem * tmp;
         for(tmp = prev->next; tmp != NULL; tmp = tmp->next){
            prev = tmp;
         }
         prev->next = malloc(sizeof(struct tbl_elem));
         prev->next->prefix_len = length;
         prev->next->dst_addr = dst_addr;
         prev->next->table_paddr = (uint32_t)inet_addr(ip);
         prev->next->next = NULL;
      }
   }
}

int DecrementTTL(char ** buf, int length){
   printf("Decrementing TTL\n");
   struct iphdr ipHeader;
   memcpy(&ipHeader, *buf + sizeof(struct ether_header), sizeof(struct iphdr));
   if(ipHeader.ttl == 1){
      return -1;
   } else {
      ipHeader.ttl = ipHeader.ttl - 1;
      ipHeader.check = 0;
      memcpy(*buf + sizeof(struct ether_header), &ipHeader, sizeof(ipHeader));

      char ipbuff[sizeof(ipHeader)];
      memcpy(ipbuff, &ipHeader, sizeof(ipHeader));

      ipHeader.check = htons(calc_checksum(ipbuff, sizeof(ipbuff)));
      memcpy(*buf + sizeof(struct ether_header), &ipHeader, sizeof(ipHeader));
      return ipHeader.ttl;
   }
}

int calc_checksum(char* data, int length){

   unsigned int checksum = 0;

   int i;
   for(i = 0; i < length; i+=2){
      checksum += *(uint16_t *) &data[i];
   }

   checksum = (checksum >> 16) + (checksum & 0xffff);
   return (uint16_t) ~checksum;
}

void SendArpRequest(uint8_t ** haddr, uint32_t daddr, struct interface * tbl_entry){
   struct ether_header eth;
   eth.ether_type = htons(ETH_P_ARP);
   memcpy(eth.ether_shost, tbl_entry->haddr, 6);
   for (int i = 0; i < 6; i++){
      eth.ether_dhost[i] = 255;
   }

   arphdr arp_header;
   arp_header.hlen = 6;
   arp_header.htype = htons(ARPHRD_ETHER);
   arp_header.opcode = htons(ARPOP_REQUEST);
   arp_header.ptype = htons(ETH_P_IP);
   arp_header.plen = 4;
   memcpy(arp_header.sender_haddr, tbl_entry->haddr, 6);

   arp_header.target_paddr[3] = (uint8_t) (daddr >> 24);
   arp_header.target_paddr[2] = (uint8_t) (daddr >> 16);
   arp_header.target_paddr[1] = (uint8_t) (daddr >> 8);
   arp_header.target_paddr[0] = (uint8_t) (daddr);

   for (int i = 0; i < 6; i++){
      arp_header.target_haddr[i] = 0;
   }
   arp_header.sender_paddr[3] = (uint8_t) (tbl_entry->paddr>> 24);
   arp_header.sender_paddr[2] = (uint8_t) (tbl_entry->paddr>> 16);
   arp_header.sender_paddr[1] = (uint8_t) (tbl_entry->paddr>> 8);
   arp_header.sender_paddr[0] = ((uint8_t) tbl_entry->paddr);

   printf("tbl_entry->paddr: %04x\n", tbl_entry->paddr);
   printf("daddr: %04x\n", daddr);

   char buffer[1500];
   memcpy(buffer, &eth, sizeof(eth));
   memcpy(buffer + sizeof(eth), &arp_header, sizeof(arp_header));

   send(tbl_entry->packet_socket, buffer, sizeof(struct ether_header) + sizeof(arphdr), 0);

   struct sockaddr_ll recvaddr;

   struct timeval tv;
   tv.tv_sec = 0;
   tv.tv_usec = 1000 * 20;
   if (setsockopt(tbl_entry->packet_socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0){
      perror("Error");
   }

   int recvaddrlen = sizeof(struct sockaddr_ll);
   int n = recvfrom(tbl_entry->packet_socket, buffer, 1500, 0, (struct sockaddr*)&recvaddr, &recvaddrlen);
   printf("%d\n", n);
   if(n < 1){
      *haddr = NULL;
      return;
   }

   tv.tv_sec = 0;
   tv.tv_usec = 1000;
   if (setsockopt(tbl_entry->packet_socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0){
      perror("Error");
   }

   arphdr new_arphdr;
   memcpy(&new_arphdr, buffer + sizeof(struct ether_header), sizeof(arphdr));
   *haddr = malloc(sizeof(uint8_t) * 6);
   for(int i = 0; i < 6; i++){
      (*haddr)[i] = new_arphdr.sender_haddr[i];
   }
}
