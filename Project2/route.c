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

void process_arp(char *buf, int rec, (struct sockaddr_ll) socket);
void process_icmp(char *buf, int rec, (struct sockaddr_ll) socket);
int calculateIPChecksum(char* buf, int length);

struct arp_hdr{
  uint16_t htype;
  uint16_t ptype;
  uint8_t hlen;
  uint8_t plen;
  uint16_t opcode;
  uint8_t sender_haddr[6];
  uint8_t sender_paddr[4];
  uint8_t target_haddr[6];
  uint8_t target_paddr[4];
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

int main(){
    int packet_socket;
    //get list of interfaces (actually addresses)
    struct ifaddrs *ifaddr, *tmp;
    if(getifaddrs(&ifaddr)==-1){
        perror("getifaddrs");
        return 1;
    }

    struct mac_addr mymacs[10];
    int nbrmacs = 0;
    int router_num;

    struct ip_addr myips[10];
    int nbrips = 0;

    fd_set sockets;
    FD_ZERO(&sockets);
    //have the list, loop over the list
    for(tmp = ifaddr; tmp!=NULL; tmp=tmp->ifa_next){
        //Check if this is a packet address, there will be one per
        //interface.  There are IPv4 and IPv6 as well, but we don't care
        //about those for the purpose of enumerating interfaces. We can
        //use the AF_INET addresses in this list for example to get a list
        //of our own IP addresses
        if(tmp->ifa_addr->sa_family==AF_PACKET){
            printf("Interface: %s\n",tmp->ifa_name);
            //create a packet socket on interface r?-eth1
            if(!strncmp(&(tmp->ifa_name[3]),"eth1",4)){
       			printf("Creating Socket on interface %s\n",tmp->ifa_name);
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

                struct mac_addr mac;
       	        mac.sock_id = packet_socket;
       	        mac.socket = (struct sockaddr_ll*)tmp->ifa_addr;
       	        strcpy(mac.inf_name,tmp->ifa_name);
       	        mymacs[nbrmacs] = mac;
       	        nbrmacs++;

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
    printf("Ready to recieve now\n");
    while(1){
        struct sockaddr_ll recvaddr;
        socklen_t recvaddrlen=sizeof(struct sockaddr_ll);

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

                //we can use recv, since the addresses are in the packet, but we
                //use recvfrom because it gives us an easy way to determine if
                //this packet is incoming or outgoing (when using ETH_P_ALL, we
                //see packets in both directions. Only outgoing can be seen when
                //using a packet socket with some specific protocol)
         	    int rec = recvfrom(packet_socket, buf, 1500,0,(struct sockaddr*)&recvaddr, &recvaddrlen);

                //Check for timeout
                if(rec == -1){
                    continue;
                }
                //ignore outgoing packets (we can't disable some from being sent
                //by the OS automatically, for example ICMP port unreachable
                //messages, so we will just ignore them here)
                if(recvaddr.sll_pkttype==PACKET_OUTGOING)
                    continue;

                //start processing all others
                printf("Got a %d byte packet\n", rec);

                //what else to do is up to you, you can send packets with send,
                //just like we used for TCP sockets (or you can use sendto, but it
                //is not necessary, since the headers, including all addresses,
                //need to be in the buffer you are sending)
                if(ntohs(recvaddr.sll_protocol) == ETH_P_ARP) {
                    printf("Received an ARP request.\n");
                    int j;
                    for(j = 0; j < nbrmacs; j++) {
                        if(packet_socket == mymacs[j].sock_id){
                            process_arp(buf,packet_socket, mymacs[j].socket);
                        }
                    }
                } else if(ntohs(recvaddr.sll_protocol) == ETH_P_IP) {
                    printf("Received an IP address.\n");

                    struct iphdr ipHeader;
                    memcpy(&ipHeader, buf + sizeof(struct ether_header), sizeof(struct iphdr));
                    memcpy(buf + sizeof(struct ether_header), &ipHeader, sizeof(struct iphdr));

                    if(ipHeader.protocol == IPPROTO_ICMP) {

                        struct icmphdr icmpHeader;
                        memcpy(&icmpHeader, buf + sizeof(struct ether_header) + sizeof(struct iphdr), sizeof(struct icmphdr));

                        if (icmpHeader.type == ICMP_ECHO) {
                            printf("Received an ICMP ECHO request.\n");
                            process_icmp(buf,rec);
                        }else if (icmpHeader.type == ICMP_ECHOREPLY) {
                            printf("Received an ICMP REPLY.\n");
                            process_icmp(buf,rec);
                        }
                    }

                } else {
                    printf("Dropping packet\n");
                }
            }
        }
    }
    //exit
    return 0;
}

void process_arp(char *buf, int rec, (struct sockaddr_ll) socket){
	struct ether_header ethHeader;
    memcpy(&ethHeader,buf,sizeof(struct ether_header));

    arp_hdr arpHeader;
    memcpy(&arpHeader,buf+sizeof(struct ether_header),sizeof(arp_hdr));


    //	struct ether_header ethHeader_tmp;
    //	arp_hdr arpHeader_tmp;

    if(arpHeader.opcode == ntohs(ARPOP_REPLY)){
  	    return;
    }

	// change op code to arp reply
	arpHeader.opcode = htons(2);

	// swap sender and target ip
	uint8_t tmpPaddr;
	int i;
	for(i=0;i<4;i++){
	    tmpPaddr = arpHeader.sender_paddr[i];
  	    arpHeader.sender_paddr[i] = arpHeader.target_paddr[i];
        arpHeader.target_paddr[i] = tmpPaddr;
	}

	// change sender mac to target mac
	for(i=0;i<6;i++){
		arpHeader.target_haddr[i] = arpHeader.sender_haddr[i];
	}

    // fill in our mac as sender
    memcpy(arpHeader.sender_haddr, socket.sll_addr, 6);
	// uint8_t tmpHaddr;
	// for(i=0;i<6;i++){
	// 	//arpHeader.sender_haddr[i] = tmpIface->haddr[i]; //IGNOREME
	// 	tmpHaddr = arpHeader.sender_haddr[i];
    //  	   arpHeader.sender_haddr[i] = arpHeader.target_haddr[i];
    //    arpHeader.target_haddr[i] = tmpHaddr;
	// }

	uint8_t tmpEhdr;
    for(i=0;i<6;i++) {
      //ethHeader.ether_dhost[i] = ethHeader.ether_shost[i];
      //ethHeader.ether_shost[i] = tmpIface->haddr[i];  //IGNOREME
      tmpEhdr = ethHeader.ether_dhost[i];
      ethHeader.ether_dhost[i] = ethHeader.ether_shost[i];
      ethHeader.ether_shost[i] = tmpEhdr;
    }

    memcpy(buf,&ethHeader,sizeof(struct ether_header));
    memcpy(buf+sizeof(struct ether_header),&arpHeader,sizeof(arp_hdr));

    send(rec,buf,sizeof(buf),0);

	return;
}

void process_icmp(char *buf, int rec, struct sockaddr_ll socket){

    struct ether_header ethHeader;
    memcpy(&ethHeader,buf,sizeof(struct ether_header));

	struct iphdr ip_hdr;
	memcpy(&ip_hdr,buf+sizeof(struct ether_header),sizeof(struct iphdr));

    struct icmphdr icmpHeader;
    memcpy(&icmpHeader,buf+sizeof(struct ether_header)+sizeof(struct iphdr),sizeof(struct icmphdr));

	if(ip_hdr.protocol == 1){
		printf("Received ICMP request.\n");

		// switch ethernet headers
		char tmpmac[6];
		memcpy(tmpmac,ethHeader.ether_shost,6);
		memcpy(ethHeader.ether_shost,ethHeader.ether_dhost,6);
		memcpy(ethHeader.ether_dhost,tmpmac,6);

		// switch ip headers
		uint32_t tmpip;
		tmpip = ip_hdr.saddr;
		ip_hdr.saddr = ip_hdr.daddr;
		ip_hdr.daddr = tmpip;

		icmpHeader.type = ICMP_ECHOREPLY;

		memcpy(buf,&ethHeader,sizeof(struct ether_header));
		memcpy(buf+sizeof(struct ether_header),&ip_hdr,sizeof(struct iphdr));
		memcpy(buf+sizeof(struct ether_header)+sizeof(struct iphdr),&icmpHeader,sizeof(struct icmphdr));

		send(rec,buf,sizeof(buf),0);

	}

	return;
}

int calculateIPChecksum(char* buf, int length) {


}
