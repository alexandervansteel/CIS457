#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
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

typedef struct {
  uint16_t hrd;               /* Hardware address format */
  uint16_t pro;               /* Protocol address format */
  uint8_t  hln;               /* Length of hardware address */
  uint8_t  pln;               /* Length of protocol address */
  uint16_t op;                /* ARP opcode */
  uint8_t  sha[6];            /* Sender hardware address */
  uint8_t  spa[4];            /* Sender IP address */
  uint8_t  tha[6];            /* Target hardware address */
  uint8_t  tpa[4];            /* Target IP address */
} arphdr;

struct interface {              /* Structure to store Interfaces */
  char *name;                 /* Interface name */
  uint8_t haddr[6];           /* Interface hardware address */
  uint32_t paddr;             /* Interface IP address */
  int packet_socket;          /* Socket to send and recieve for this interface */
  struct interface *next;     /* Pointer to next interface in a linked list */
  struct tbl_element *first;  /* Pointer to routing table element */
};

struct tbl_element {            /* Structure to store routing table entries */
  uint32_t paddr;             /* Routing table entry IP address */
  int prefix_len;             /* Length of the*/
  uint32_t dst_addr;          /* Destination address, Next hop IP*/
  struct tbl_element * next;  /* Pointer to next element sharing the same interface */
};

struct interface * interfaces;

#define MAC_ADDR_LEN 6


void process_ARP(struct interface *iface, char *buff, int length);
void process_ICMP(struct interface *iface, char *buff, int length);
int calc_checksum(char* data, int length);
void load_routing_table(FILE * fp);
uint32_t get_interface(uint32_t daddr, struct interface ** iface);
void send_arp_request(uint8_t ** haddr, uint32_t daddr, struct interface * tbl_entry);
int  decrement_TTL(char ** buff, int length);
void send_icmp_error(struct interface * iface, char *buff, char * haddr, int error);
void forward_packet(struct interface * iface, char * buff, int length);

int main(){

  //get list of interfaces (actually addresses)
  struct ifaddrs *ifaddr, *tmp;
  if(getifaddrs(&ifaddr)==-1){
    perror("getifaddrs");
    return 1;
  }

  int router_num;
  //have the list, loop over the list
  for(tmp = ifaddr; tmp!=NULL; tmp=tmp->ifa_next){
    struct interface* cur_iface, *prev_iface;
    // lo, eth0(mac), eth0(ip), eth0(?), eth1(mac), eth1(ip), eth1()

    if(strcmp(tmp->ifa_name,"lo") != 0){
      int found = 0;
      for(cur_iface = interfaces; cur_iface != NULL; cur_iface = cur_iface->next) {
        if(strcmp(tmp->ifa_name,cur_iface->name) == 0){
          found = 1;
          break;
        }
        prev_iface = cur_iface;
      }

      if(found == 0) {
        cur_iface = malloc(sizeof(struct interface));
        cur_iface->name = tmp->ifa_name;
        cur_iface->next = NULL;
        cur_iface->first = NULL;
        if(interfaces == NULL){
          interfaces = cur_iface;
        } else {
          prev_iface->next = cur_iface;
        }
      }


      if(tmp->ifa_addr->sa_family == AF_INET){
        // set up ip address
        cur_iface->paddr = ((struct sockaddr_in *)tmp->ifa_addr)->sin_addr.s_addr;
        break;
      }

      else if(tmp->ifa_addr->sa_family == AF_PACKET){
        int packet_socket;

        if(!strncmp(&(tmp->ifa_name[3]),"eth",3)){
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
    }else if(router_num == 2){
      strcpy(fileName, "r2-table.txt");
    }

    fp = fopen(fileName, "r");
    if(fp == NULL){
      printf("Invalid File Name\n");
      return -1;
    }
  }

  load_routing_table(fp);

  //loop and recieve packets. We are only looking at one interface,
  //for the project you will probably want to look at more (to do so,
  //a good way is to have one socket per interfaces and use select to
  //see which ones have data)
  printf("Ready to recieve\n");
  while(1){

    struct interface * curr_iface;
    for(curr_iface = interfaces; curr_iface!=NULL; curr_iface=curr_iface->next) {

      char buff[1500];
      struct sockaddr_ll recvaddr;
      int recvaddrlen=sizeof(struct sockaddr_ll);
      //we can use recv, since the addresses are in the packet, but we
      //use recvfrom because it gives us an easy way to determine if
      //this packet is incoming or outgoing (when using ETH_P_ALL, we
      //see packets in both directions. Only outgoing can be seen when
      //using a packet socket with some specific protocol)
      int n = recvfrom(curr_iface->packet_socket, buff, 1500,0,(struct sockaddr*)&recvaddr, &recvaddrlen);
      //ignore outgoing packets (we can't disable some from being sent
      //by the OS automatically, for example ICMP port unreachable
      //messages, so we will just ignore them here)
      if(recvaddr.sll_pkttype==PACKET_OUTGOING){
        continue;
      }
      //start processing all others

      //Check for timeout
      if(n == -1){
        continue;
      }

      if(ntohs(recvaddr.sll_protocol) == ETH_P_ARP){
        printf("Received ARP\n");
        process_ARP(curr_iface, buff, n);

      } else if(ntohs(recvaddr.sll_protocol) == ETH_P_IP){
        printf("Received IP\n");
        struct iphdr ip_header;
        memcpy(&ip_header, buff + sizeof(struct ether_header), sizeof(struct iphdr));
        int checksum = ip_header.check;
        ip_header.check = 0;
        memcpy(buff + sizeof(struct ether_header), &ip_header, sizeof(struct iphdr));

        char ipbuff[sizeof(struct iphdr)];
        memcpy(ipbuff, &ip_header, sizeof(struct iphdr));

        ip_header.check = htons(calc_checksum(ipbuff, sizeof(struct iphdr)));
        memcpy(buff + sizeof(struct ether_header), &ip_header, sizeof(struct iphdr));

        printf("Check : %d, Our Check: %d \n\n", checksum, ip_header.check);
        if(checksum == ip_header.check){

          if(ip_header.protocol == IPPROTO_ICMP){
            printf("Received ICMP\n");
            struct icmphdr icmp_header;
            memcpy(&icmp_header, buff + sizeof(struct ether_header) + sizeof(struct iphdr), sizeof(struct icmphdr));

            if (icmp_header.type == ICMP_ECHO){
              printf("Received ICMP ECHO\n");
              process_ICMP(curr_iface, buff, n);
            } else if (icmp_header.type == ICMP_ECHOREPLY){
              printf("Received ICMP REPLY\n");
              process_ICMP(curr_iface, buff, n);
            } else {
              forward_packet(curr_iface, buff, n);
            }

          } else {
            forward_packet(curr_iface, buff, n);
          }
        } else {
          printf("Droppin' this packet\n\n");
        }
      }
    }

  }
  return 0;
}

void process_ARP(struct interface *iface, char *buff, int length){
  printf("PROCESS ARP \n");

  struct ether_header eth_header;
  memcpy(&eth_header,buff,sizeof(struct ether_header));

  arphdr arpHeader;
  memcpy(&arpHeader,buff+sizeof(struct ether_header),sizeof(arphdr));

  if(arpHeader.op == ntohs(ARPOP_REPLY)){
    return;
  }

  //uint8_t ipaddress = arpHeader.tpa;
  //memcpy(ipaddress, arpHeader.tpa, 4);
  uint32_t ipaddress = arpHeader.tpa[3] << 24 |
  arpHeader.tpa[2] << 16 | arpHeader.tpa[1] << 8 |
  arpHeader.tpa[0];

  if(iface->paddr == ipaddress){
    // change opcode
    arpHeader.op = htons(2);

    // swap sender ip and target ip
    uint8_t tmpPaddr;
    int i;
    for(i = 0; i < 4; i++){
      tmpPaddr = arpHeader.spa[i];
      arpHeader.spa[i] = arpHeader.tpa[i];
      arpHeader.tpa[i] = tmpPaddr;
    }

    // move sender mac to target mac
    //memcpy(arpHeader.tha, arpHeader.sha, 6);
    int j;
    for(j = 0; j < 6; j++){
      arpHeader.tha[j] = arpHeader.sha[j];
    }

    // fill in sender mac with self
    //memcpy(arpHeader.sha, iface->haddr, 6);
    for(j = 0; j < 6; j++){
      arpHeader.sha[j] = iface->haddr[j];
    }



    int k;
    for(k = 0; k < 6; k++ ){
      eth_header.ether_dhost[k] = eth_header.ether_shost[k];
      eth_header.ether_shost[k] = iface->haddr[k];
    }

    memcpy(buff,&eth_header,sizeof(struct ether_header)); // Copy eth header back
    memcpy(buff+sizeof(struct ether_header),&arpHeader,sizeof(arphdr)); // Copy arp header back

    send(iface->packet_socket,buff,length,0);
  } else {
    return;
  }
}

void process_ICMP(struct interface *iface, char *buff, int length){
  printf("PROCESS ICMP \n");
  struct ether_header eth_header;
  memcpy(&eth_header,buff,sizeof(struct ether_header));

  struct iphdr ip_header;
  memcpy(&ip_header,buff+sizeof(struct ether_header),sizeof(struct iphdr));

  struct icmphdr icmp_header;
  memcpy(&icmp_header,buff+sizeof(struct ether_header)+sizeof(struct iphdr),sizeof(struct icmphdr));

  // find interfaces that the message is for
  char *ifaceName = NULL;
  struct interface *tmp;
  for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
    if(tmp->paddr == ip_header.daddr){
      ifaceName = tmp->name;
    }
  }

  // the interfaces name was found
  if(ifaceName != NULL){
    uint8_t *self_haddr = malloc(sizeof(uint8_t) * MAC_ADDR_LEN);
    for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      if(strcmp(tmp->name, ifaceName)){
        self_haddr = tmp->haddr;
      }
    }

    // send an ICMP echo reply with the same ID, sequence number, and data as the response
    // construct the Ethernet, IP, and ICMP headers.
    icmp_header.type = ICMP_ECHOREPLY;

    icmp_header.checksum = 0; // Zero out checksum and copy back to buff
    memcpy(buff+sizeof(struct ether_header)+sizeof(struct iphdr),&icmp_header,sizeof(struct icmphdr));
    int ttl = decrement_TTL(&buff, length);
    if(ttl < 0){
      send_icmp_error(iface, buff, eth_header.ether_shost, ICMP_TIME_EXCEEDED);
    }

    char icmp_buff[sizeof(struct icmphdr)+8];
    memcpy(icmp_buff, &buff+sizeof(struct ether_header)+sizeof(iphdr), sizeof(struct icmphdr)+8);

    icmp_header.checksum = htons(calc_checksum(icmp_buff, sizeof(struct icmphdr)+8));
    memcpy(&ip_header, buff + sizeof(struct ether_header), sizeof(struct iphdr));
    uint32_t tmpAddr;
    tmpAddr = ip_header.daddr;
    ip_header.daddr = ip_header.saddr;
    ip_header.saddr = tmpAddr;

    int k;
    for(k = 0; k < 6; k++ ){
      eth_header.ether_dhost[k] = eth_header.ether_shost[k];
      eth_header.ether_shost[k] = self_haddr[k];
    }

    // Copy eth header back
    memcpy(buff,&eth_header,sizeof(struct ether_header));
    // Copy ip header back
    memcpy(buff+sizeof(struct ether_header),&ip_header,sizeof(struct iphdr));
    // Copy icmp header back
    memcpy(buff+sizeof(struct ether_header)+sizeof(struct iphdr),&icmp_header,sizeof(struct icmphdr));

    send(iface->packet_socket,buff,length,0);
  } else {
    //ICMP echo is not for us, figure out the correct interfaces to send to
    //Check that address in on our routing table
    struct interface * tbl_entry;
    uint32_t ipVal = get_interface(ip_header.daddr, &tbl_entry);
    if(tbl_entry != NULL){
      uint8_t * haddr;
      if(ipVal == 0){
        send_arp_request(&haddr, ip_header.daddr, tbl_entry);
      }
      else{
        send_arp_request(&haddr, ipVal, tbl_entry);
      }

      if(haddr == NULL){
        //send ICMP error
        send_icmp_error(iface, buff, eth_header.ether_shost, ICMP_DEST_UNREACH);
      } else {
        struct ether_header ether_header1;
        ether_header1.ether_type = htons(ETH_P_IP);
        for(int i = 0; i < 6; i++){
          ether_header1.ether_dhost[i] = haddr[i];
          ether_header1.ether_shost[i] = tbl_entry->haddr[i];
        }
        icmp_header.checksum = 0; // Zero out checksum and copy back to buff
        memcpy(buff+sizeof(struct ether_header)+sizeof(struct iphdr),&icmp_header,sizeof(struct icmphdr));
        int ttl = decrement_TTL(&buff, length);
        if(ttl < 0){
          //ICMP error
          send_icmp_error(iface, buff, eth_header.ether_shost, ICMP_TIME_EXCEEDED);
        }
        char icmp_buff[sizeof(struct icmphdr)+8];
        memcpy(icmp_buff, &buff+sizeof(struct ether_header)+sizeof(struct iphdr), sizeof(struct icmphdr)+8);

        icmp_header.checksum = htons(calc_checksum(icmp_buff, sizeof(struct icmphdr)+8));
        memcpy(buff, &ether_header1, sizeof(struct ether_header));
        memcpy(buff + sizeof(struct ether_header) + sizeof(struct iphdr), &icmp_header, sizeof(struct icmphdr));
        send(tbl_entry->packet_socket, buff, length, 0);
      }

    } else {
      send_icmp_error(iface, buff, eth_header.ether_shost, ICMP_DEST_UNREACH);
    }
  }
}

void forward_packet(struct interface * iface, char * buff, int length){
  printf("FORWARD PACKET \n");
  struct ether_header eth_header;
  memcpy(&eth_header,buff,sizeof(struct ether_header));

  struct iphdr ip_header;
  memcpy(&ip_header,buff+sizeof(struct ether_header),sizeof(struct iphdr));

  // find interfaces that the message is for
  char *ifaceName = NULL;
  struct interface *tmp;
  for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
    if(tmp->paddr == ip_header.daddr){
      ifaceName = tmp->name;
    }
  }

  // the interfaces name was found
  if(ifaceName != NULL){
    uint8_t *self_haddr = malloc(sizeof(uint8_t) * MAC_ADDR_LEN);
    for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      if(strcmp(tmp->name, ifaceName)){
        self_haddr = tmp->haddr;
      }
    }

    // send an ICMP echo reply with the same ID, sequence number, and data as the response
    // construct the Ethernet, IP, and ICMP headers.
    int k;
    for(k = 0; k < 6; k++ ){
      eth_header.ether_dhost[k] = eth_header.ether_shost[k];
      eth_header.ether_shost[k] = self_haddr[k];
    }

    // Copy eth header back
    memcpy(buff,&eth_header,sizeof(struct ether_header));
    // // Copy ip header back
    memcpy(buff+sizeof(struct ether_header),&ip_header,sizeof(struct iphdr));

    send(iface->packet_socket,buff,length,0);
  } else {
    //ICMP echo is not for us, figure out the correct interfaces to send to
    //Check that address in on our routing table
    struct interface * tbl_entry;
    uint32_t ipVal = get_interface(ip_header.daddr, &tbl_entry);
    if(tbl_entry != NULL){
      uint8_t * haddr;
      if(ipVal == 0){
        send_arp_request(&haddr, ip_header.daddr, tbl_entry);
      } else {
        send_arp_request(&haddr, ipVal, tbl_entry);
      }

      if(haddr == NULL){
        //send ICMP error
      } else {
        struct ether_header ether_header1;
        ether_header1.ether_type = htons(ETH_P_IP);
        for(int i = 0; i < 6; i++){
          ether_header1.ether_dhost[i] = haddr[i];
          ether_header1.ether_shost[i] = tbl_entry->haddr[i];
        }
        memcpy(buff, &ether_header1, sizeof(struct ether_header));
        send(tbl_entry->packet_socket, buff, length, 0);
      }

    } else {
      return;
    }
  }
}

void send_icmp_error(struct interface * iface, char * buff, char * haddr, int error){
  printf("SEND ICMP ERROR \n");
  struct ether_header eth_header;

  struct iphdr ip_header;
  memcpy(&ip_header,buff+sizeof(struct ether_header),sizeof(struct iphdr));

  struct icmphdr icmp_header;
  memcpy(&icmp_header,buff+sizeof(struct ether_header)+sizeof(struct iphdr),sizeof(struct icmphdr));

  memset(eth_header.ether_shost, iface->haddr, 6);
  memset(eth_header.ether_dhost, haddr, 6);

  eth_header.ether_type = htons(ETHERTYPE_IP);

  ip_header.daddr = ip_header.saddr;
  ip_header.saddr = iface->paddr;
  ip_header.check = 0;
  ip_header.frag_off = 0;
  ip_header.ihl = 5;
  ip_header.protocol = IPPROTO_ICMP;
  ip_header.tos = 0;
  ip_header.tot_len = htons(28);
  ip_header.ttl = 64;
  ip_header.version = 4;

  icmp_header.type = error;
  icmp_header.checksum = 0;
  icmp_header.code = ICMP_HOST_UNREACH;

  int length =  sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct icmphdr);

  memcpy(buff, &eth_header, sizeof(struct ether_header));
  memcpy(buff + sizeof(struct ether_header), &ip_header, sizeof(struct iphdr));
  memcpy(buff + sizeof(struct ether_header) + sizeof(struct iphdr), &icmp_header, sizeof(struct icmphdr));

  char ipbuff[sizeof(ip_header)];
  memcpy(ipbuff, &ip_header, sizeof(ip_header));

  ip_header.check = htons(calc_checksum(ipbuff, sizeof(ip_header)));
  memcpy(buff + sizeof(struct ether_header), &ip_header, sizeof(struct iphdr));
  char icmp_buff[sizeof(icmp_header)+8];
  memcpy(icmp_buff, &buff+sizeof(struct ether_header)+sizeof(struct iphdr), sizeof(structicmp_header)+8);

  icmp_header.checksum = htons(calc_checksum(icmp_buff, sizeof(icmp_header)+8));
  memcpy(buff + sizeof(struct ether_header) + sizeof(struct iphdr), &icmp_header, sizeof(struct icmphdr));

  send(iface->packet_socket, buff,  length, 0);

}

uint32_t get_interface(uint32_t daddr, struct interface ** iface){
  struct interface * tmp;
  for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
    struct tbl_element * tbl_tmp;
    for(tbl_tmp = tmp->first; tbl_tmp!=NULL; tbl_tmp = tbl_tmp->next){
      if (tbl_tmp->paddr << (32 - tbl_tmp->prefix_len) == daddr << (32 - tbl_tmp->prefix_len)){
        *iface = tmp;
        return tbl_tmp->dst_addr;
      }
    }
  }
  return 0;
}

void load_routing_table(FILE *fp){
  printf("LOAD TABLE \n");
  char line[35];
  while(fgets(line, sizeof(line), fp) != NULL){
    char ip[INET_ADDRSTRLEN];
    int count = 0;
    while(line[count] != '/'){
      ip[count] = line[count];
      count++;
    }
    ip[count] = '\0';

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
      dst_addr = (uint32_t)inet_addr(temp_ip);
    }
    while(line[count] != ' '){
      count++;
    }

    count++;

    char ifName[15];
    sscanf(&line[count], "%s", ifName);

    struct interface *iface = NULL;
    struct interface *tmp;
    for(tmp = interfaces; tmp!=NULL; tmp=tmp->next){
      if(strcmp(tmp->name, ifName) == 0){
        iface = tmp;
      }
    }
    if(iface->first == NULL){
      iface->first = malloc(sizeof(struct tbl_element));
      iface->first->paddr =(uint32_t)inet_addr(ip);
      iface->first->dst_addr = dst_addr;
      iface->first->prefix_len = length;
      iface->first->next = NULL;
    }else{
      struct tbl_element * prev = iface->first;
      struct tbl_element * tmp;
      for(tmp = prev->next; tmp != NULL; tmp = tmp->next){
        prev = tmp;
      }
      prev->next = malloc(sizeof(struct tbl_element));
      prev->next->prefix_len = length;
      prev->next->dst_addr = dst_addr;
      prev->next->paddr = (uint32_t)inet_addr(ip);
      prev->next->next = NULL;
    }
  }
}

int decrement_TTL(char ** buff, int length){
  printf("DECREMENT TTL \n");
  struct iphdr ip_header;
  memcpy(&ip_header, *buff + sizeof(struct ether_header), sizeof(struct iphdr));
  if(ip_header.ttl == 1){
    return -1;
  } else {
    ip_header.ttl = ip_header.ttl - 1;
    ip_header.check = 0;
    memcpy(*buff + sizeof(struct ether_header), &ip_header, sizeof(ip_header));

    char ipbuff[sizeof(struct iphdr)];
    memcpy(ipbuff, &ip_header, sizeof(struct iphdr));

    ip_header.check = htons(calc_checksum(ipbuff, sizeof(struct iphdr)));
    memcpy(*buff + sizeof(struct ether_header), &ip_header, sizeof(struct iphdr));
    return 1;
  }
}

int calc_checksum(char* data, int length){
  printf("CALCULATE CHECKSUM \n");
  unsigned int checksum = 0;

  int i;
  for(i = 0; i < length; i+=2){
    checksum += (uint32_t) ((uint8_t) data[i] << 8 | (uint8_t) data[i + 1]);
  }

  checksum = (checksum >> 16) + (checksum & 0xffff);
  return (uint16_t) ~checksum;
}

void send_arp_request(uint8_t ** haddr, uint32_t daddr, struct interface * tbl_entry){
  printf("SEND ARP REQUEST \n");
  struct ether_header eth;
  eth.ether_type = htons(ETH_P_ARP);
  memcpy(eth.ether_shost, tbl_entry->haddr, 6);
  for (int i = 0; i < 6; i++) {
    eth.ether_dhost[i] = 255;
  }

  arphdr arp_header;
  arp_header.hrd = htons(ARPHRD_ETHER);
  arp_header.pro = htons(ETH_P_IP);
  arp_header.hln = 6;
  arp_header.pln = 4;
  arp_header.op = htons(ARPOP_REQUEST);
  memcpy(arp_header.sha, tbl_entry->haddr, 6);

  for (int i = 0; i < 6; i++) {
    arp_header.tha[i] = 0;
  }
  arp_header.tpa[3] = (uint8_t) (daddr >> 24);
  arp_header.tpa[2] = (uint8_t) (daddr >> 16);
  arp_header.tpa[1] = (uint8_t) (daddr >> 8);
  arp_header.tpa[0] = (uint8_t) (daddr);

  arp_header.spa[3] = (uint8_t) (tbl_entry->paddr>> 24);
  arp_header.spa[2] = (uint8_t) (tbl_entry->paddr>> 16);
  arp_header.spa[1] = (uint8_t) (tbl_entry->paddr>> 8);
  arp_header.spa[0] = ((uint8_t) tbl_entry->paddr);

  char buff[1500];
  memcpy(buff, &eth, sizeof(eth));
  memcpy(buff + sizeof(eth), &arp_header, sizeof(arp_header));

  send(tbl_entry->packet_socket, buff, sizeof(struct ether_header) + sizeof(arphdr), 0);

  struct sockaddr_ll recvaddr;

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 1000 * 20;
  if (setsockopt(tbl_entry->packet_socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0){
    perror("Error");
  }

  int recvaddrlen = sizeof(struct sockaddr_ll);
  int n = recvfrom(tbl_entry->packet_socket, buff, 1500, 0, (struct sockaddr*)&recvaddr, &recvaddrlen);
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
  memcpy(&new_arphdr, buff + sizeof(struct ether_header), sizeof(arphdr));
  *haddr = malloc(sizeof(uint8_t) * 6);
  //memcpy(*haddr, new_arphdr.sha, 6);
  for(int i = 0; i < 6; i++){
    (*haddr)[i] = new_arphdr.sha[i];
  }
}
