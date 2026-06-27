#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

void
netinit(void)
{
  initlock(&netlock, "netlock");
}

#define MAX_PAC_NUM 16
#define MAX_PORT_NUM 16

struct packet{
  uint32 ip_src;
  uint16 sport;
  char* buf;
  int payload_len;
};

struct port_entry{
  int valid;
  int port;
  struct packet pac_array[MAX_PAC_NUM];
  int head_pac;
  int tail_pac;
  int pac_num;
};

static struct{
  struct port_entry ports[MAX_PORT_NUM];
  // int bound_port_num;
}bound_ports;

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int port;
  argint(0,&port);
  for(int i=0; i<MAX_PORT_NUM; i++){
    if(bound_ports.ports[i].valid == 0){
      bound_ports.ports[i].port = port;
      bound_ports.ports[i].head_pac = 0;
      bound_ports.ports[i].tail_pac = 0;
      bound_ports.ports[i].valid = 1;
      bound_ports.ports[i].pac_num = 0;
      return 0;
    }
  }
  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0,&port);
  for(int i=0; i<MAX_PORT_NUM; i++){
    if(bound_ports.ports[i].port == port){
      bound_ports.ports[i].port = -1;
      bound_ports.ports[i].head_pac = 0;
      bound_ports.ports[i].tail_pac = 0;
      bound_ports.ports[i].valid = 0;
      bound_ports.ports[i].pac_num = 0;
    }
  }
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  int dport; argint(0, &dport);
  uint64 srcaddr; argaddr(1, &srcaddr);
  uint64 sportaddr; argaddr(2, &sportaddr);
  uint64 bufaddr; argaddr(3, &bufaddr);
  int maxlen; argint(4, &maxlen);

  acquire(&netlock);
  int port_idx = -1;
  for(int i=0; i<MAX_PORT_NUM; i++){
    if((bound_ports.ports[i].valid) && (bound_ports.ports[i].port == dport))
      port_idx = i;
  }
  if(port_idx == -1){
    release(&netlock);
    return -1;
  }

  struct port_entry* target_port = &bound_ports.ports[port_idx];
  
  while(target_port->pac_num==0){
    sleep(target_port,&netlock);
  }

  struct packet target_pac = target_port->pac_array[target_port->head_pac];
  target_port->head_pac = (target_port->head_pac + 1) % MAX_PAC_NUM;
  target_port->pac_num--;

  release(&netlock);


  struct proc *p = myproc();
  if(copyout(p->pagetable,srcaddr,(char*)&target_pac.ip_src,sizeof(target_pac.ip_src)) < 0){
    kfree(target_pac.buf);
    return -1;
  }
  if(copyout(p->pagetable,sportaddr,(char*)&target_pac.sport,sizeof(target_pac.sport)) < 0){
    kfree(target_pac.buf);
    return -1;
  }
  char* payload = target_pac.buf + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  int copylen = target_pac.payload_len < maxlen ? target_pac.payload_len : maxlen;
  if(copyout(p->pagetable,bufaddr,payload,copylen) < 0){
    kfree(target_pac.buf);
    return -1;
  }
  
  kfree(target_pac.buf);
  return target_pac.payload_len;  
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //
  struct ip* ip_ptr = (struct ip*)((uint64)buf+sizeof(struct eth));
  if(!(ip_ptr->ip_p == IPPROTO_UDP)){
    kfree(buf);
    return;
  }
  struct udp* udp_ptr = (struct udp*)(ip_ptr+1);
  int found = 0;
  for(int i=0; i<MAX_PORT_NUM; i++){
    if(bound_ports.ports[i].valid && bound_ports.ports[i].port == ntohs(udp_ptr->dport)){
      if(bound_ports.ports[i].pac_num == 16){
        kfree(buf);
        return;
      } 
      found = 1;
      int tail = bound_ports.ports[i].tail_pac;
      bound_ports.ports[i].pac_array[tail].buf = buf;
      bound_ports.ports[i].pac_array[tail].payload_len = ntohs(udp_ptr->ulen) - sizeof(struct udp);
      bound_ports.ports[i].pac_array[tail].ip_src = ntohl(ip_ptr->ip_src);
      bound_ports.ports[i].pac_array[tail].sport = ntohs(udp_ptr->sport);
      bound_ports.ports[i].tail_pac = (bound_ports.ports[i].tail_pac+1) % MAX_PAC_NUM;
      bound_ports.ports[i].pac_num ++;
      wakeup(&bound_ports.ports[i]);
    }
  }
  if(!found) kfree(buf);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
