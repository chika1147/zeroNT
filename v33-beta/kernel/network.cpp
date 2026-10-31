#include "include/network.hpp"
#include "include/pci.hpp"
#include "include/memory.hpp"
#include "include/paging.hpp"
#include "include/arch.hpp"
#include <stdint.h>
#include <stddef.h>

namespace {
constexpr uint16_t INTEL_VENDOR = 0x8086;
constexpr uint16_t E1000_82540EM = 0x100E; // QEMU e1000
constexpr uint16_t E1000_82545EM = 0x100F; // VMware e1000
constexpr uint16_t E1000E_82574L = 0x10D3; // common e1000e-compatible model
constexpr uint32_t RX_COUNT = 32;
constexpr uint32_t TX_COUNT = 16;
constexpr uint32_t RX_BUF_SIZE = 2048;

constexpr uint32_t REG_CTRL   = 0x0000;
constexpr uint32_t REG_STATUS = 0x0008;
constexpr uint32_t REG_ICR    = 0x00C0;
constexpr uint32_t REG_IMC    = 0x00D8;
constexpr uint32_t REG_RCTL   = 0x0100;
constexpr uint32_t REG_TCTL   = 0x0400;
constexpr uint32_t REG_TIPG   = 0x0410;
constexpr uint32_t REG_RDBAL  = 0x2800;
constexpr uint32_t REG_RDBAH  = 0x2804;
constexpr uint32_t REG_RDLEN  = 0x2808;
constexpr uint32_t REG_RDH    = 0x2810;
constexpr uint32_t REG_RDT    = 0x2818;
constexpr uint32_t REG_TDBAL  = 0x3800;
constexpr uint32_t REG_TDBAH  = 0x3804;
constexpr uint32_t REG_TDLEN  = 0x3808;
constexpr uint32_t REG_TDH    = 0x3810;
constexpr uint32_t REG_TDT    = 0x3818;
constexpr uint32_t REG_RAL    = 0x5400;
constexpr uint32_t REG_RAH    = 0x5404;

constexpr uint32_t RCTL_EN = 1u<<1;
constexpr uint32_t RCTL_BAM = 1u<<15;
constexpr uint32_t RCTL_SECRC = 1u<<26;
constexpr uint32_t TCTL_EN = 1u<<1;
constexpr uint32_t TCTL_PSP = 1u<<3;

constexpr uint16_t TCP_PROBE_PORT = 49152;
constexpr uint16_t TCP_CLIENT_PORT_FIRST = 49153;
constexpr uint16_t UDP_CLIENT_PORT_FIRST = 55000;
constexpr uint16_t TCP_RX_CAP = 4096;
constexpr uint16_t UDP_RX_CAP = 1400;
constexpr uint32_t TCP_CLIENTS = 4;
constexpr uint32_t UDP_SOCKETS = 4;
constexpr uint32_t ARP_CACHE_ENTRIES = 8;
constexpr uint32_t DNS_CACHE_ENTRIES = 8;

struct __attribute__((packed)) RxDesc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};
struct __attribute__((packed)) TxDesc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
};
struct TcpClient {
    TcpClientStats stats;
    bool active;
    uint8_t rx[TCP_RX_CAP];
    uint16_t rxLength;
};
struct UdpSocket {
    bool active;
    uint16_t localPort;
    uint8_t rx[UDP_RX_CAP];
    uint16_t rxLength;
    uint8_t sourceIp[4];
    uint16_t sourcePort;
};
struct ArpEntry {
    bool valid;
    uint8_t ip[4];
    uint8_t mac[6];
    uint64_t stamp;
};
struct DnsCacheEntry {
    bool valid;
    char host[64];
    uint8_t ip[4];
    uint64_t expiresPoll;
};

static NetworkStats g{};
static volatile uint32_t* gMmio = nullptr;
alignas(4096) static RxDesc gRx[RX_COUNT];
alignas(4096) static TxDesc gTx[TX_COUNT];
alignas(4096) static uint8_t gRxBuf[RX_COUNT][RX_BUF_SIZE];
alignas(4096) static uint8_t gTxBuf[TX_COUNT][2048];
static uint32_t gRxNext = 0;
static uint32_t gTxNext = 0;
static uint8_t gGatewayMac[6]{};
static uint32_t gIpIdent = 1;
static uint32_t gDhcpXid = 0x5A4E5432u;
static uint8_t gDhcpOffered[4]{};
static uint8_t gDhcpServer[4]{};
static bool gDhcpRequested = false;
static bool gPingSent = false;
static uint16_t gDnsId = 0x5A01;
static volatile bool gDnsWaiting = false;
static char gDnsWaitingHost[64]{};
static uint64_t gPollCounter = 0;
static uint32_t gDhcpRetryCount = 0;
static uint32_t gTcpSeq = 0x5A4E5400u;
static volatile bool gTcpWaiting=false;
static volatile bool gTcpAnswered=false;
static TcpClient gTcp[TCP_CLIENTS]{};
static UdpSocket gUdp[UDP_SOCKETS]{};
static uint16_t gNextTcpPort = TCP_CLIENT_PORT_FIRST;
static uint16_t gNextUdpPort = UDP_CLIENT_PORT_FIRST;
static ArpEntry gArp[ARP_CACHE_ENTRIES]{};
static DnsCacheEntry gDnsCache[DNS_CACHE_ENTRIES]{};
static uint32_t gArpReplace = 0;
static uint32_t gDnsReplace = 0;

static inline uint32_t mmio_read(uint32_t reg) { return gMmio[reg/4]; }
static inline void mmio_write(uint32_t reg,uint32_t value) { gMmio[reg/4]=value; (void)gMmio[reg/4]; }
static inline uint16_t be16(const uint8_t* p){return static_cast<uint16_t>((p[0]<<8)|p[1]);}
static inline uint32_t be32(const uint8_t* p){return (static_cast<uint32_t>(p[0])<<24)|(static_cast<uint32_t>(p[1])<<16)|(static_cast<uint32_t>(p[2])<<8)|p[3];}
static inline void put16(uint8_t*p,uint16_t v){p[0]=static_cast<uint8_t>(v>>8);p[1]=static_cast<uint8_t>(v);}
static inline void put32(uint8_t*p,uint32_t v){p[0]=static_cast<uint8_t>(v>>24);p[1]=static_cast<uint8_t>(v>>16);p[2]=static_cast<uint8_t>(v>>8);p[3]=static_cast<uint8_t>(v);}
static bool eq6(const uint8_t*a,const uint8_t*b){for(int i=0;i<6;++i)if(a[i]!=b[i])return false;return true;}
static bool eq4(const uint8_t*a,const uint8_t*b){for(int i=0;i<4;++i)if(a[i]!=b[i])return false;return true;}
static void cp4(uint8_t*d,const uint8_t*s){for(int i=0;i<4;++i)d[i]=s[i];}
static void cp6(uint8_t*d,const uint8_t*s){for(int i=0;i<6;++i)d[i]=s[i];}
static void zero4(uint8_t*d){for(int i=0;i<4;++i)d[i]=0;}
static bool any4(const uint8_t*a){return a[0]||a[1]||a[2]||a[3];}
static bool same_subnet(const uint8_t*a,const uint8_t*b){for(int i=0;i<4;++i)if((a[i]&g.subnet[i])!=(b[i]&g.subnet[i]))return false;return true;}
static char lower_ascii(char c){return(c>='A'&&c<='Z')?static_cast<char>(c-'A'+'a'):c;}
static bool host_eq(const char*a,const char*b){if(!a||!b)return false;for(uint32_t i=0;;++i){char ca=lower_ascii(a[i]),cb=lower_ascii(b[i]);if(ca!=cb)return false;if(!ca)return true;}}
static void host_copy(char*d,const char*s,size_t cap){if(!d||!cap)return;size_t i=0;if(s)for(;i+1<cap&&s[i];++i)d[i]=lower_ascii(s[i]);d[i]=0;}
static uint16_t checksum16(const uint8_t* data,size_t n){uint32_t s=0;for(size_t i=0;i<n;i+=2){uint16_t w=static_cast<uint16_t>(data[i])<<8;if(i+1<n)w|=data[i+1];s+=w;while(s>>16)s=(s&0xFFFF)+(s>>16);}return static_cast<uint16_t>(~s);}

static bool compatible_e1000(uint16_t vendor,uint16_t device){
    if(vendor!=INTEL_VENDOR)return false;
    return device==E1000_82540EM||device==E1000_82545EM||device==E1000E_82574L;
}
static bool find_nic(PciDeviceInfo& out) {
    struct C { PciDeviceInfo* out; bool found; } c{&out,false};
    pci_enumerate([](const PciDeviceInfo&i,void*ctx){
        auto*c=static_cast<C*>(ctx); if(c->found)return;
        if(i.classCode==0x02&&i.subclass==0x00&&compatible_e1000(i.vendorId,i.deviceId)){*c->out=i;c->found=true;}
    },&c);
    return c.found;
}

static bool tx_frame(const uint8_t* data,uint16_t len) {
    if(!g.initialized||!g.linkUp||!data||len<14||len>sizeof(gTxBuf[0])){++g.txFailures;return false;}
    const uint32_t idx=gTxNext;
    // Refuse to overwrite a descriptor that hardware still owns.
    uint32_t wait=0;while(!(gTx[idx].status&1u)&&wait++<200000)__asm__ __volatile__("pause");
    if(!(gTx[idx].status&1u)){++g.txFailures;return false;}
    for(uint16_t i=0;i<len;++i)gTxBuf[idx][i]=data[i];
    const uint64_t phys=vmm_translate(reinterpret_cast<uint64_t>(gTxBuf[idx])); if(!phys){++g.txFailures;return false;}
    gTx[idx].addr=phys;gTx[idx].length=len;gTx[idx].cso=0;gTx[idx].cmd=0x0B;gTx[idx].status=0;gTx[idx].css=0;gTx[idx].special=0;
    gTxNext=(idx+1)%TX_COUNT;mmio_write(REG_TDT,gTxNext);++g.txPackets;return true;
}

static uint16_t make_ipv4(uint8_t* out,uint8_t proto,const uint8_t dst[4],uint16_t payloadLen) {
    out[0]=0x45;out[1]=0;put16(out+2,static_cast<uint16_t>(20+payloadLen));put16(out+4,static_cast<uint16_t>(gIpIdent++));put16(out+6,0x4000);out[8]=64;out[9]=proto;put16(out+10,0);
    cp4(out+12,g.ipv4);cp4(out+16,dst);put16(out+10,checksum16(out,20));return 20;
}

static bool send_arp_request(const uint8_t targetIp[4]) {
    if(!targetIp||!g.linkUp)return false;
    uint8_t f[42]{};for(int i=0;i<6;++i)f[i]=0xFF;cp6(f+6,g.mac);put16(f+12,0x0806);put16(f+14,1);put16(f+16,0x0800);f[18]=6;f[19]=4;put16(f+20,1);cp6(f+22,g.mac);cp4(f+28,g.ipv4);for(int i=0;i<6;++i)f[32+i]=0;cp4(f+38,targetIp);return tx_frame(f,sizeof(f));
}
static bool send_arp_reply(const uint8_t dstMac[6],const uint8_t dstIp[4]) {
    uint8_t f[42]{};cp6(f,dstMac);cp6(f+6,g.mac);put16(f+12,0x0806);put16(f+14,1);put16(f+16,0x0800);f[18]=6;f[19]=4;put16(f+20,2);cp6(f+22,g.mac);cp4(f+28,g.ipv4);cp6(f+32,dstMac);cp4(f+38,dstIp);return tx_frame(f,sizeof(f));
}
static void arp_remember(const uint8_t ip[4],const uint8_t mac[6]){
    if(!ip||!mac)return;
    for(uint32_t i=0;i<ARP_CACHE_ENTRIES;++i)if(gArp[i].valid&&eq4(gArp[i].ip,ip)){cp6(gArp[i].mac,mac);gArp[i].stamp=gPollCounter;return;}
    ArpEntry&e=gArp[gArpReplace++%ARP_CACHE_ENTRIES];e.valid=true;cp4(e.ip,ip);cp6(e.mac,mac);e.stamp=gPollCounter;
}
static bool arp_lookup(const uint8_t ip[4],uint8_t mac[6]){
    for(uint32_t i=0;i<ARP_CACHE_ENTRIES;++i)if(gArp[i].valid&&eq4(gArp[i].ip,ip)){cp6(mac,gArp[i].mac);return true;}return false;
}
static const uint8_t* route_ip(const uint8_t dst[4]){
    if(any4(g.subnet)&&same_subnet(g.ipv4,dst))return dst;
    return g.gateway;
}
static bool ensure_neighbor(const uint8_t dst[4],uint8_t mac[6],uint32_t budget){
    if(!dst||!mac||!g.linkUp||!any4(g.ipv4))return false;
    const uint8_t*hop=route_ip(dst);if(!any4(hop))return false;
    if(arp_lookup(hop,mac))return true;
    for(uint32_t attempt=0;attempt<3;++attempt){
        send_arp_request(hop);
        uint32_t slice=budget/3u;if(slice<1000)slice=1000;
        for(uint32_t i=0;i<slice;++i){network_poll();if(arp_lookup(hop,mac))return true;__asm__ __volatile__("pause");}
    }
    return false;
}

static bool send_udp_raw(const uint8_t dstMac[6],const uint8_t srcIp[4],const uint8_t dstIp[4],uint16_t srcPort,uint16_t dstPort,const uint8_t*payload,uint16_t payloadLen,bool broadcast){
    uint8_t f[1514]{};const uint16_t total=static_cast<uint16_t>(14+20+8+payloadLen);if(total>sizeof(f))return false;
    if(broadcast)for(int i=0;i<6;++i)f[i]=0xFF;else cp6(f,dstMac);cp6(f+6,g.mac);put16(f+12,0x0800);
    uint8_t*ip=f+14;ip[0]=0x45;ip[1]=0;put16(ip+2,static_cast<uint16_t>(20+8+payloadLen));put16(ip+4,static_cast<uint16_t>(gIpIdent++));put16(ip+6,0);ip[8]=64;ip[9]=17;put16(ip+10,0);cp4(ip+12,srcIp);cp4(ip+16,dstIp);put16(ip+10,checksum16(ip,20));
    uint8_t*udp=ip+20;put16(udp,srcPort);put16(udp+2,dstPort);put16(udp+4,static_cast<uint16_t>(8+payloadLen));put16(udp+6,0);for(uint16_t i=0;i<payloadLen;++i)udp[8+i]=payload[i];return tx_frame(f,total);
}
static bool send_udp_routed(const uint8_t dstIp[4],uint16_t srcPort,uint16_t dstPort,const uint8_t*payload,uint16_t payloadLen,uint32_t budget){
    uint8_t mac[6]{};if(!ensure_neighbor(dstIp,mac,budget))return false;
    bool ok=send_udp_raw(mac,g.ipv4,dstIp,srcPort,dstPort,payload,payloadLen,false);if(ok)++g.udpDatagramsSent;return ok;
}

static uint16_t transport_checksum(const uint8_t src[4],const uint8_t dst[4],uint8_t proto,const uint8_t* data,uint16_t len){
    uint32_t sum=0;auto add=[&](uint16_t w){sum+=w;while(sum>>16)sum=(sum&0xFFFF)+(sum>>16);};
    add(static_cast<uint16_t>((src[0]<<8)|src[1]));add(static_cast<uint16_t>((src[2]<<8)|src[3]));
    add(static_cast<uint16_t>((dst[0]<<8)|dst[1]));add(static_cast<uint16_t>((dst[2]<<8)|dst[3]));
    add(proto);add(len);for(uint16_t i=0;i<len;i+=2){uint16_t w=static_cast<uint16_t>(data[i])<<8;if(i+1<len)w|=data[i+1];add(w);}return static_cast<uint16_t>(~sum);
}

static bool encode_dns_name(const char* host,uint8_t*out,uint16_t cap,uint16_t&used){
    used=0;if(!host||!*host)return false;const char*p=host;while(*p){const char*start=p;uint16_t n=0;while(*p&&*p!='.'){++p;++n;if(n>63)return false;}if(!n||used+1+n+1>cap)return false;out[used++]=static_cast<uint8_t>(n);for(uint16_t i=0;i<n;++i)out[used++]=static_cast<uint8_t>(lower_ascii(start[i]));if(*p=='.')++p;}out[used++]=0;return true;
}
static bool dns_cache_lookup(const char*host,uint8_t out[4]){
    for(uint32_t i=0;i<DNS_CACHE_ENTRIES;++i){DnsCacheEntry&e=gDnsCache[i];if(e.valid&&e.expiresPoll>gPollCounter&&host_eq(e.host,host)){cp4(out,e.ip);++g.dnsCacheHits;return true;}}return false;
}
static void dns_cache_store(const char*host,const uint8_t ip[4],uint32_t ttlSeconds){
    if(!host||!*host||!ip)return;DnsCacheEntry*e=nullptr;
    for(uint32_t i=0;i<DNS_CACHE_ENTRIES;++i)if(gDnsCache[i].valid&&host_eq(gDnsCache[i].host,host)){e=&gDnsCache[i];break;}
    if(!e)e=&gDnsCache[gDnsReplace++%DNS_CACHE_ENTRIES];e->valid=true;host_copy(e->host,host,sizeof(e->host));cp4(e->ip,ip);
    if(ttlSeconds<10)ttlSeconds=10;if(ttlSeconds>86400)ttlSeconds=86400;
    // poll counter is workload-dependent; this is deliberately conservative and bounded.
    e->expiresPoll=gPollCounter+static_cast<uint64_t>(ttlSeconds)*50000ULL;
}
static bool send_dns_query_packet(const char*host){
    if(!host||!*host||!any4(g.dns))return false;uint8_t payload[300]{};const uint16_t id=++gDnsId;put16(payload,id);put16(payload+2,0x0100);put16(payload+4,1);uint16_t qn=0;if(!encode_dns_name(host,payload+12,sizeof(payload)-16,qn))return false;uint16_t o=static_cast<uint16_t>(12+qn);put16(payload+o,1);put16(payload+o+2,1);o+=4;gDnsWaiting=true;++g.dnsQueries;g.dnsResolved=false;host_copy(gDnsWaitingHost,host,sizeof(gDnsWaitingHost));return send_udp_routed(g.dns,53000,53,payload,o,300000);
}
static void parse_dns(const uint8_t*p,uint16_t n){
    if(n<12||be16(p)!=gDnsId||!(be16(p+2)&0x8000))return;const uint16_t qd=be16(p+4),an=be16(p+6);uint16_t o=12;
    auto skip_name=[&](uint16_t&pos)->bool{uint16_t guard=0;while(pos<n&&guard++<128){uint8_t c=p[pos++];if(c==0)return true;if((c&0xC0)==0xC0){if(pos>=n)return false;++pos;return true;}if(c>63||pos+c>n)return false;pos=static_cast<uint16_t>(pos+c);}return false;};
    for(uint16_t i=0;i<qd;++i){if(!skip_name(o)||o+4>n)return;o=static_cast<uint16_t>(o+4);}for(uint16_t i=0;i<an;++i){if(!skip_name(o)||o+10>n)return;uint16_t type=be16(p+o),klass=be16(p+o+2);uint32_t ttl=be32(p+o+4);uint16_t rdlen=be16(p+o+8);o=static_cast<uint16_t>(o+10);if(o+rdlen>n)return;if(type==1&&klass==1&&rdlen==4){cp4(g.lastDnsIp,p+o);g.dnsResolved=true;gDnsWaiting=false;++g.dnsReplies;dns_cache_store(gDnsWaitingHost,g.lastDnsIp,ttl);return;}o=static_cast<uint16_t>(o+rdlen);}gDnsWaiting=false;
}

static bool send_tcp_segment(const uint8_t dstIp[4],uint16_t srcPort,uint16_t dstPort,
                             uint32_t seq,uint32_t ack,uint8_t flags,
                             const uint8_t*payload,uint16_t payloadLen){
    if(!dstIp||!srcPort||!dstPort||payloadLen>1200||!g.linkUp)return false;
    uint8_t mac[6]{};if(!ensure_neighbor(dstIp,mac,300000))return false;
    uint8_t f[14+20+20+1200]{};
    cp6(f,mac);cp6(f+6,g.mac);put16(f+12,0x0800);
    uint8_t*ip=f+14;make_ipv4(ip,6,dstIp,static_cast<uint16_t>(20+payloadLen));
    uint8_t*t=ip+20;put16(t,srcPort);put16(t+2,dstPort);put32(t+4,seq);put32(t+8,ack);t[12]=0x50;t[13]=flags;put16(t+14,4096);put16(t+16,0);put16(t+18,0);
    for(uint16_t i=0;i<payloadLen;++i)t[20+i]=payload[i];put16(t+16,transport_checksum(g.ipv4,dstIp,6,t,static_cast<uint16_t>(20+payloadLen)));
    return tx_frame(f,static_cast<uint16_t>(14+20+20+payloadLen));
}
static bool send_tcp_syn(const uint8_t dstIp[4],uint16_t port){
    if(!dstIp||!port)return false;const uint32_t seq=gTcpSeq++;++g.tcpSynSent;return send_tcp_segment(dstIp,TCP_PROBE_PORT,port,seq,0,0x02,nullptr,0);
}
static TcpClient* tcp_from_handle(int handle){if(handle<1||handle>static_cast<int>(TCP_CLIENTS))return nullptr;return &gTcp[static_cast<uint32_t>(handle-1)];}
static bool tcp_send_ack(TcpClient&c){if(!c.active)return false;return send_tcp_segment(c.stats.remoteIp,c.stats.localPort,c.stats.remotePort,c.stats.sendNext,c.stats.receiveNext,0x10,nullptr,0);}
static void tcp_reset_client(TcpClient&c){uint64_t connects=c.stats.connects,sends=c.stats.sends,receives=c.stats.receives,closes=c.stats.closes,retrans=c.stats.retransmits;c={};c.stats.state=TcpClientState::Closed;c.stats.connects=connects;c.stats.sends=sends;c.stats.receives=receives;c.stats.closes=closes;c.stats.retransmits=retrans;}
static void tcp_append_rx(TcpClient&c,const uint8_t*data,uint16_t size){if(!data||!size)return;uint16_t room=static_cast<uint16_t>(TCP_RX_CAP-c.rxLength);uint16_t take=size<room?size:room;for(uint16_t i=0;i<take;++i)c.rx[c.rxLength+i]=data[i];c.rxLength=static_cast<uint16_t>(c.rxLength+take);c.stats.receiveAvailable=c.rxLength;c.stats.receiveNext+=size;g.tcpBytesReceived+=size;if(take<size)++g.rxDrops;}

static void dhcp_send(uint8_t msgType,const uint8_t requested[4],const uint8_t server[4]) {
    uint8_t p[320]{};p[0]=1;p[1]=1;p[2]=6;p[3]=0;put32(p+4,gDhcpXid);put16(p+10,0x8000);cp6(p+28,g.mac);p[236]=99;p[237]=130;p[238]=83;p[239]=99;
    uint16_t o=240;p[o++]=53;p[o++]=1;p[o++]=msgType;p[o++]=61;p[o++]=7;p[o++]=1;for(int i=0;i<6;++i)p[o++]=g.mac[i];
    if(msgType==3&&requested&&server){p[o++]=50;p[o++]=4;for(int i=0;i<4;++i)p[o++]=requested[i];p[o++]=54;p[o++]=4;for(int i=0;i<4;++i)p[o++]=server[i];}
    p[o++]=55;p[o++]=4;p[o++]=1;p[o++]=3;p[o++]=6;p[o++]=15;p[o++]=255;
    const uint8_t zero[4]={0,0,0,0};const uint8_t bc[4]={255,255,255,255};const uint8_t ff[6]={255,255,255,255,255,255};send_udp_raw(ff,zero,bc,68,67,p,o,true);
}
static void parse_dhcp(const uint8_t* p,uint16_t n) {
    if(n<240||p[0]!=2||be32(p+4)!=gDhcpXid||!eq6(p+28,g.mac))return;
    uint8_t type=0;uint8_t server[4]{};uint8_t subnet[4]{};uint8_t router[4]{};uint8_t dns[4]{};
    uint16_t o=240;while(o<n){uint8_t code=p[o++];if(code==255)break;if(code==0)continue;if(o>=n)break;uint8_t len=p[o++];if(o+len>n)break;switch(code){case 53:if(len)type=p[o];break;case 54:if(len>=4)cp4(server,p+o);break;case 1:if(len>=4)cp4(subnet,p+o);break;case 3:if(len>=4)cp4(router,p+o);break;case 6:if(len>=4)cp4(dns,p+o);break;}o=static_cast<uint16_t>(o+len);}++g.dhcpPackets;
    if(type==2&&!gDhcpRequested){cp4(gDhcpOffered,p+16);cp4(gDhcpServer,server);gDhcpRequested=true;dhcp_send(3,gDhcpOffered,gDhcpServer);}
    else if(type==5){cp4(g.ipv4,p+16);if(any4(subnet))cp4(g.subnet,subnet);if(any4(router))cp4(g.gateway,router);if(any4(dns))cp4(g.dns,dns);g.dhcpBound=true;g.dhcpFailed=false;g.staticConfigured=false;gDhcpRetryCount=0;if(any4(g.gateway))send_arp_request(g.gateway);}
    else if(type==6){g.dhcpFailed=true;g.dhcpBound=false;++g.dhcpFailures;}
}

static void udp_deliver(const uint8_t srcIp[4],uint16_t srcPort,uint16_t dstPort,const uint8_t*data,uint16_t len){
    for(uint32_t i=0;i<UDP_SOCKETS;++i){UdpSocket&s=gUdp[i];if(!s.active||s.localPort!=dstPort)continue;if(s.rxLength){++g.rxDrops;return;}uint16_t take=len<UDP_RX_CAP?len:UDP_RX_CAP;for(uint16_t j=0;j<take;++j)s.rx[j]=data[j];s.rxLength=take;cp4(s.sourceIp,srcIp);s.sourcePort=srcPort;++g.udpDatagramsReceived;return;}
}

static void handle_frame(const uint8_t*f,uint16_t n) {
    if(n<14)return;const uint16_t eth=be16(f+12);
    if(eth==0x0806&&n>=42){const uint8_t*a=f+14;if(be16(a)!=1||be16(a+2)!=0x0800||a[4]!=6||a[5]!=4)return;const uint16_t op=be16(a+6);const uint8_t*sha=a+8;const uint8_t*spa=a+14;const uint8_t*tpa=a+24;if(op==2){arp_remember(spa,sha);++g.arpReplies;if(eq4(spa,g.gateway)){cp6(gGatewayMac,sha);g.gatewayResolved=true;}}else if(op==1&&any4(g.ipv4)&&eq4(tpa,g.ipv4)){arp_remember(spa,sha);send_arp_reply(sha,spa);}return;}
    if(eth!=0x0800||n<34)return;const uint8_t*ip=f+14;const uint8_t ihl=static_cast<uint8_t>((ip[0]&0x0F)*4);if((ip[0]>>4)!=4||ihl<20||14+ihl>n)return;const uint16_t ipLen=be16(ip+2);if(ipLen<ihl||14+ipLen>n)return;if(checksum16(ip,ihl)!=0)return;const uint8_t*srcIp=ip+12;const uint8_t*dstIp=ip+16;
    if(ip[9]==17&&ipLen>=ihl+8){const uint8_t*u=ip+ihl;const uint16_t sp=be16(u),dp=be16(u+2),ul=be16(u+4);if(ul>=8&&ihl+ul<=ipLen){++g.udpPackets;if(sp==67&&dp==68)parse_dhcp(u+8,static_cast<uint16_t>(ul-8));else if(sp==53&&dp==53000)parse_dns(u+8,static_cast<uint16_t>(ul-8));else udp_deliver(srcIp,sp,dp,u+8,static_cast<uint16_t>(ul-8));}return;}
    if(ip[9]==6&&ipLen>=ihl+20){
        const uint8_t*t=ip+ihl;const uint16_t srcPort=be16(t),dstPort=be16(t+2);const uint8_t dataOffset=static_cast<uint8_t>((t[12]>>4)*4);if(dataOffset<20||ihl+dataOffset>ipLen)return;const uint8_t flags=t[13];const uint32_t seq=be32(t+4),ack=be32(t+8);const uint16_t payloadLen=static_cast<uint16_t>(ipLen-ihl-dataOffset);
        if(dstPort==TCP_PROBE_PORT){++g.tcpReplies;if((flags&0x12)==0x12)++g.tcpSynAcks;if(flags&0x04)++g.tcpResets;gTcpAnswered=true;gTcpWaiting=false;return;}
        for(uint32_t ci=0;ci<TCP_CLIENTS;++ci){TcpClient&c=gTcp[ci];if(!c.active||dstPort!=c.stats.localPort||srcPort!=c.stats.remotePort||!eq4(srcIp,c.stats.remoteIp))continue;++g.tcpReplies;c.stats.peerAck=ack;if(flags&0x04){++g.tcpResets;c.stats.state=TcpClientState::Closed;c.active=false;++g.tcpClosed;return;}if(c.stats.state==TcpClientState::SynSent){if((flags&0x12)==0x12&&ack==c.stats.sendNext){++g.tcpSynAcks;c.stats.receiveNext=seq+1;c.stats.state=TcpClientState::Established;++g.tcpConnections;tcp_send_ack(c);}return;}uint32_t expected=c.stats.receiveNext;if(payloadLen&&seq==expected){tcp_append_rx(c,t+dataOffset,payloadLen);tcp_send_ack(c);}else if(payloadLen&&seq!=expected){tcp_send_ack(c);}if(flags&0x01){const uint32_t finSeq=seq+payloadLen;if(finSeq==c.stats.receiveNext)++c.stats.receiveNext;tcp_send_ack(c);c.stats.state=TcpClientState::CloseWait;++g.tcpClosed;}return;}
        return;
    }
    if(ip[9]==1&&any4(g.ipv4)&&eq4(dstIp,g.ipv4)&&ipLen>=ihl+8){const uint8_t*ic=ip+ihl;if(ic[0]==0){++g.icmpReplies;return;}if(ic[0]==8){uint8_t out[1514]{};const uint16_t icLen=static_cast<uint16_t>(ipLen-ihl);cp6(out,f+6);cp6(out+6,g.mac);put16(out+12,0x0800);uint8_t*oi=out+14;for(uint8_t i=0;i<20;++i)oi[i]=ip[i];cp4(oi+12,g.ipv4);cp4(oi+16,srcIp);put16(oi+10,0);put16(oi+10,checksum16(oi,20));uint8_t*oc=oi+20;for(uint16_t i=0;i<icLen;++i)oc[i]=ic[i];oc[0]=0;put16(oc+2,0);put16(oc+2,checksum16(oc,icLen));tx_frame(out,static_cast<uint16_t>(14+20+icLen));}return;}
}

static bool setup_rings() {
    for(uint32_t i=0;i<RX_COUNT;++i){gRx[i]={};gRx[i].addr=vmm_translate(reinterpret_cast<uint64_t>(gRxBuf[i]));if(!gRx[i].addr)return false;}
    for(uint32_t i=0;i<TX_COUNT;++i){gTx[i]={};gTx[i].status=1;}
    const uint64_t rxp=vmm_translate(reinterpret_cast<uint64_t>(gRx));const uint64_t txp=vmm_translate(reinterpret_cast<uint64_t>(gTx));if(!rxp||!txp)return false;
    mmio_write(REG_RDBAL,static_cast<uint32_t>(rxp));mmio_write(REG_RDBAH,static_cast<uint32_t>(rxp>>32));mmio_write(REG_RDLEN,sizeof(gRx));mmio_write(REG_RDH,0);mmio_write(REG_RDT,RX_COUNT-1);
    mmio_write(REG_TDBAL,static_cast<uint32_t>(txp));mmio_write(REG_TDBAH,static_cast<uint32_t>(txp>>32));mmio_write(REG_TDLEN,sizeof(gTx));mmio_write(REG_TDH,0);mmio_write(REG_TDT,0);
    mmio_write(REG_RCTL,RCTL_EN|RCTL_BAM|RCTL_SECRC);mmio_write(REG_TCTL,TCTL_EN|TCTL_PSP|(15u<<4)|(64u<<12));mmio_write(REG_TIPG,10u|(8u<<10)|(6u<<20));return true;
}
static void close_all_sockets(){for(uint32_t i=0;i<TCP_CLIENTS;++i){gTcp[i].active=false;gTcp[i].stats.state=TcpClientState::Closed;gTcp[i].rxLength=0;}for(uint32_t i=0;i<UDP_SOCKETS;++i)gUdp[i]={};g.openTcpSockets=0;g.openUdpSockets=0;}
}

bool network_init() {
    g={};gMmio=nullptr;gRxNext=0;gTxNext=0;gPollCounter=0;gDhcpRetryCount=0;gDhcpRequested=false;gPingSent=false;gDnsWaiting=false;gTcpWaiting=false;gTcpAnswered=false;gNextTcpPort=TCP_CLIENT_PORT_FIRST;gNextUdpPort=UDP_CLIENT_PORT_FIRST;for(auto&c:gTcp){c={};c.stats.state=TcpClientState::Closed;}for(auto&s:gUdp)s={};for(auto&a:gArp)a={};for(auto&d:gDnsCache)d={};gArpReplace=0;gDnsReplace=0;
    PciDeviceInfo dev{};if(!find_nic(dev))return false;g.nicFound=true;g.bus=dev.bus;g.device=dev.device;g.function=dev.function;g.vendorId=dev.vendorId;g.deviceId=dev.deviceId;pci_enable_mmio_busmaster(dev);
    uint64_t bar=dev.bar[0]&~0xFULL;if((dev.bar[0]&6u)==4u)bar|=static_cast<uint64_t>(dev.bar[1])<<32;if(!bar)return false;g.mmioBase=bar;
    for(uint64_t a=bar&~4095ULL;a<((bar+0x20000+4095)&~4095ULL);a+=4096){if(a>=0x100000000ULL&&!vmm_map_page(a,a,PagePresent|PageWritable|PageCacheDisable))return false;}
    gMmio=reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(bar));mmio_write(REG_CTRL,mmio_read(REG_CTRL)|(1u<<6));mmio_write(REG_IMC,0xFFFFFFFFu);(void)mmio_read(REG_ICR);
    const uint32_t ral=mmio_read(REG_RAL),rah=mmio_read(REG_RAH);g.mac[0]=ral;g.mac[1]=ral>>8;g.mac[2]=ral>>16;g.mac[3]=ral>>24;g.mac[4]=rah;g.mac[5]=rah>>8;if(!(rah&0x80000000u)&&!ral)return false;
    if(!setup_rings())return false;g.linkUp=(mmio_read(REG_STATUS)&2u)!=0;g.initialized=true;if(g.linkUp)network_restart_dhcp();
    for(uint32_t spin=0;spin<500000&&!g.dhcpBound&&g.linkUp;++spin){network_poll();__asm__ __volatile__("pause");}
    return true;
}

void network_poll() {
    if(!g.initialized)return;++gPollCounter;
    if((gPollCounter&0x0FFFu)==0){bool now=(mmio_read(REG_STATUS)&2u)!=0;if(now!=g.linkUp){g.linkUp=now;++g.linkTransitions;g.gatewayResolved=false;gPingSent=false;for(auto&a:gArp)a.valid=false;if(!now){close_all_sockets();g.dhcpBound=false;}else if(!g.staticConfigured)network_restart_dhcp();else if(any4(g.gateway))send_arp_request(g.gateway);}}
    for(uint32_t loops=0;loops<RX_COUNT;++loops){RxDesc&d=gRx[gRxNext];if(!(d.status&1u))break;if(d.errors==0&&d.length>=14&&d.length<=RX_BUF_SIZE){++g.rxPackets;handle_frame(gRxBuf[gRxNext],d.length);}else ++g.rxDrops;d.status=0;d.length=0;const uint32_t old=gRxNext;gRxNext=(gRxNext+1)%RX_COUNT;mmio_write(REG_RDT,old);}
    if(g.linkUp&&!g.staticConfigured&&!g.dhcpBound&&!g.dhcpFailed&&(gPollCounter%200000u)==0){if(gDhcpRetryCount<6){++gDhcpRetryCount;++g.dhcpRetries;dhcp_send(gDhcpRequested?3:1,gDhcpRequested?gDhcpOffered:nullptr,gDhcpRequested?gDhcpServer:nullptr);}else{g.dhcpFailed=true;++g.dhcpFailures;}}
    if(g.linkUp&&any4(g.gateway)&&!g.gatewayResolved&&(gPollCounter%100000u)==0)send_arp_request(g.gateway);
    if(g.gatewayResolved&&!gPingSent){gPingSent=network_ping_gateway();}
}

bool network_link_up(){return g.initialized&&g.linkUp;}

bool network_restart_dhcp(){
    if(!g.initialized||!g.linkUp)return false;g.staticConfigured=false;g.dhcpBound=false;g.dhcpFailed=false;gDhcpRequested=false;gDhcpRetryCount=0;++gDhcpXid;zero4(g.ipv4);zero4(g.subnet);zero4(g.gateway);zero4(g.dns);g.gatewayResolved=false;gPingSent=false;for(auto&a:gArp)a.valid=false;dhcp_send(1,nullptr,nullptr);return true;
}

bool network_configure_static(const uint8_t ip[4],const uint8_t subnet[4],const uint8_t gateway[4],const uint8_t dns[4]){
    if(!g.initialized||!ip||!subnet||!gateway||!dns||!any4(ip))return false;cp4(g.ipv4,ip);cp4(g.subnet,subnet);cp4(g.gateway,gateway);cp4(g.dns,dns);g.staticConfigured=true;g.dhcpBound=false;g.dhcpFailed=false;g.gatewayResolved=false;gPingSent=false;for(auto&a:gArp)a.valid=false;network_dns_cache_flush();if(g.linkUp&&any4(g.gateway))send_arp_request(g.gateway);return true;
}

void network_dns_cache_flush(){for(auto&d:gDnsCache)d={};gDnsReplace=0;}

bool network_ping_gateway() {
    if(!g.initialized||!g.linkUp||!any4(g.gateway))return false;uint8_t mac[6]{};if(!ensure_neighbor(g.gateway,mac,300000))return false;cp6(gGatewayMac,mac);g.gatewayResolved=true;uint8_t f[14+20+16]{};cp6(f,mac);cp6(f+6,g.mac);put16(f+12,0x0800);uint8_t*ip=f+14;make_ipv4(ip,1,g.gateway,16);uint8_t*ic=ip+20;ic[0]=8;ic[1]=0;put16(ic+2,0);put16(ic+4,0x5A4E);put16(ic+6,1);for(int i=8;i<16;++i)ic[i]=static_cast<uint8_t>(i);put16(ic+2,checksum16(ic,16));++g.pingRequests;return tx_frame(f,sizeof(f));
}
bool network_ping_gateway_wait(uint32_t pollBudget){const uint64_t before=g.icmpReplies;if(!network_ping_gateway())return false;for(uint32_t i=0;i<pollBudget&&g.icmpReplies==before;++i){network_poll();__asm__ __volatile__("pause");}if(g.icmpReplies==before){++g.pingTimeouts;return false;}return true;}

bool network_dns_query(const char* host,uint8_t outIp[4]){
    if(!g.initialized||!g.linkUp||!any4(g.ipv4)||!any4(g.dns)||!host||!*host||!outIp)return false;if(dns_cache_lookup(host,outIp))return true;
    for(uint32_t attempt=0;attempt<3;++attempt){if(attempt)++g.dnsRetries;if(!send_dns_query_packet(host))continue;for(uint32_t i=0;i<600000&&gDnsWaiting;++i){network_poll();__asm__ __volatile__("pause");}if(g.dnsResolved){cp4(outIp,g.lastDnsIp);return true;}gDnsWaiting=false;}++g.dnsTimeouts;return false;
}

bool network_tcp_probe(const uint8_t dstIp[4],uint16_t port){return send_tcp_syn(dstIp,port);}
bool network_tcp_probe_wait(const uint8_t dstIp[4],uint16_t port,uint32_t pollBudget){gTcpWaiting=true;gTcpAnswered=false;if(!send_tcp_syn(dstIp,port)){gTcpWaiting=false;return false;}for(uint32_t i=0;i<pollBudget&&gTcpWaiting;++i){network_poll();__asm__ __volatile__("pause");}if(!gTcpAnswered){++g.tcpTimeouts;gTcpWaiting=false;return false;}return true;}

int network_tcp_connect(const uint8_t dstIp[4],uint16_t port,uint32_t pollBudget){
    if(!g.initialized||!g.linkUp||!dstIp||!port||!any4(g.ipv4))return 0;uint32_t slot=TCP_CLIENTS;for(uint32_t i=0;i<TCP_CLIENTS;++i)if(!gTcp[i].active&&gTcp[i].stats.state==TcpClientState::Closed){slot=i;break;}if(slot==TCP_CLIENTS)return 0;TcpClient&c=gTcp[slot];tcp_reset_client(c);c.active=true;c.stats.state=TcpClientState::SynSent;c.stats.localPort=gNextTcpPort++;if(gNextTcpPort>=60000)gNextTcpPort=TCP_CLIENT_PORT_FIRST;c.stats.remotePort=port;cp4(c.stats.remoteIp,dstIp);++c.stats.connects;c.stats.sendNext=gTcpSeq;const uint32_t synSeq=c.stats.sendNext++;gTcpSeq+=0x101u;++g.tcpSynSent;++g.openTcpSockets;
    uint32_t slice=pollBudget/3u;if(slice<1000)slice=1000;for(uint32_t attempt=0;attempt<3&&c.stats.state==TcpClientState::SynSent;++attempt){if(attempt){++g.tcpRetransmits;++c.stats.retransmits;}if(!send_tcp_segment(dstIp,c.stats.localPort,port,synSeq,0,0x02,nullptr,0))continue;for(uint32_t i=0;i<slice&&c.stats.state==TcpClientState::SynSent;++i){network_poll();__asm__ __volatile__("pause");}}
    if(c.stats.state!=TcpClientState::Established){++g.tcpTimeouts;c.active=false;c.stats.state=TcpClientState::Closed;if(g.openTcpSockets)--g.openTcpSockets;return 0;}return static_cast<int>(slot+1);
}

bool network_tcp_send(int handle,const void*data,uint16_t size,uint32_t pollBudget){
    TcpClient*c=tcp_from_handle(handle);if(!c||!c->active||c->stats.state!=TcpClientState::Established||!data||!size)return false;const auto*src=static_cast<const uint8_t*>(data);uint16_t done=0;while(done<size){uint16_t chunk=static_cast<uint16_t>(size-done);if(chunk>1000)chunk=1000;const uint32_t seq=c->stats.sendNext;const uint32_t target=seq+chunk;c->stats.sendNext=target;bool acked=false;uint32_t slice=pollBudget/3u;if(slice<1000)slice=1000;for(uint32_t attempt=0;attempt<3&&!acked;++attempt){if(attempt){++g.tcpRetransmits;++c->stats.retransmits;}if(!send_tcp_segment(c->stats.remoteIp,c->stats.localPort,c->stats.remotePort,seq,c->stats.receiveNext,0x18,src+done,chunk))continue;for(uint32_t spin=0;spin<slice;++spin){network_poll();if(!c->active||c->stats.state==TcpClientState::Closed)return false;if(c->stats.peerAck>=target){acked=true;break;}__asm__ __volatile__("pause");}}if(!acked){++g.tcpTimeouts;return false;}g.tcpBytesSent+=chunk;++c->stats.sends;done=static_cast<uint16_t>(done+chunk);}return true;
}

int network_tcp_receive(int handle,void*out,uint16_t capacity){TcpClient*c=tcp_from_handle(handle);if(!c||!out||!capacity)return -1;if(!c->active&&c->rxLength==0&&c->stats.state==TcpClientState::Closed)return 0;uint16_t take=c->rxLength<capacity?c->rxLength:capacity;auto*dst=static_cast<uint8_t*>(out);for(uint16_t i=0;i<take;++i)dst[i]=c->rx[i];for(uint16_t i=take;i<c->rxLength;++i)c->rx[i-take]=c->rx[i];c->rxLength=static_cast<uint16_t>(c->rxLength-take);c->stats.receiveAvailable=c->rxLength;if(take)++c->stats.receives;return static_cast<int>(take);}

bool network_tcp_close(int handle,uint32_t pollBudget){TcpClient*c=tcp_from_handle(handle);if(!c)return false;if(!c->active){c->stats.state=TcpClientState::Closed;return true;}if(c->stats.state==TcpClientState::Established||c->stats.state==TcpClientState::CloseWait){const uint32_t finSeq=c->stats.sendNext++;uint8_t flags=c->stats.state==TcpClientState::Established?0x11:0x11;uint32_t slice=pollBudget/2u;if(slice<1000)slice=1000;for(uint32_t attempt=0;attempt<2;++attempt){if(attempt){++g.tcpRetransmits;++c->stats.retransmits;}send_tcp_segment(c->stats.remoteIp,c->stats.localPort,c->stats.remotePort,finSeq,c->stats.receiveNext,flags,nullptr,0);if(c->stats.state==TcpClientState::Established)c->stats.state=TcpClientState::FinWait;for(uint32_t i=0;i<slice&&c->stats.state==TcpClientState::FinWait;++i){network_poll();__asm__ __volatile__("pause");}if(c->stats.state!=TcpClientState::FinWait)break;}}c->active=false;c->stats.state=TcpClientState::Closed;++c->stats.closes;if(g.openTcpSockets)--g.openTcpSockets;return true;}

TcpClientStats network_tcp_client_stats(int handle){TcpClient*c=tcp_from_handle(handle);return c?c->stats:TcpClientStats{};}

int network_udp_open(uint16_t localPort){if(!g.initialized||!g.linkUp)return 0;if(!localPort){for(uint32_t tries=0;tries<1000;++tries){uint16_t p=gNextUdpPort++;if(gNextUdpPort>=64000)gNextUdpPort=UDP_CLIENT_PORT_FIRST;bool used=false;for(auto&s:gUdp)if(s.active&&s.localPort==p){used=true;break;}if(!used){localPort=p;break;}}}if(!localPort)return 0;for(auto&s:gUdp)if(s.active&&s.localPort==localPort)return 0;for(uint32_t i=0;i<UDP_SOCKETS;++i)if(!gUdp[i].active){gUdp[i]={};gUdp[i].active=true;gUdp[i].localPort=localPort;++g.openUdpSockets;return static_cast<int>(i+1);}return 0;}

bool network_udp_send(int handle,const uint8_t dstIp[4],uint16_t dstPort,const void*data,uint16_t size,uint32_t pollBudget){if(handle<1||handle>static_cast<int>(UDP_SOCKETS)||!dstIp||!dstPort||!data||!size||size>1200)return false;UdpSocket&s=gUdp[static_cast<uint32_t>(handle-1)];if(!s.active)return false;return send_udp_routed(dstIp,s.localPort,dstPort,static_cast<const uint8_t*>(data),size,pollBudget);}

int network_udp_receive(int handle,void*out,uint16_t capacity,UdpDatagramInfo*info){if(handle<1||handle>static_cast<int>(UDP_SOCKETS)||!out||!capacity)return -1;UdpSocket&s=gUdp[static_cast<uint32_t>(handle-1)];if(!s.active)return -1;if(!s.rxLength)return 0;uint16_t take=s.rxLength<capacity?s.rxLength:capacity;auto*dst=static_cast<uint8_t*>(out);for(uint16_t i=0;i<take;++i)dst[i]=s.rx[i];if(info){cp4(info->sourceIp,s.sourceIp);info->sourcePort=s.sourcePort;info->bytes=take;}s.rxLength=0;return static_cast<int>(take);}

bool network_udp_close(int handle){if(handle<1||handle>static_cast<int>(UDP_SOCKETS))return false;UdpSocket&s=gUdp[static_cast<uint32_t>(handle-1)];if(!s.active)return true;s={};if(g.openUdpSockets)--g.openUdpSockets;return true;}

bool network_http_get(const char*host,const char*path,void*out,uint16_t capacity,uint16_t&received,uint32_t pollBudget){received=0;if(!host||!*host||!out||capacity==0)return false;uint8_t ip[4]{};if(!network_dns_query(host,ip))return false;int h=network_tcp_connect(ip,80,pollBudget/2);if(!h)return false;char req[768]{};uint16_t n=0;auto add=[&](const char*text)->bool{if(!text)return true;for(uint16_t i=0;text[i];++i){if(n+1>=sizeof(req))return false;req[n++]=text[i];}return true;};const char*requestPath=(path&&*path)?path:"/";bool built=add("GET ")&&add(requestPath)&&add(" HTTP/1.0\r\nHost: ")&&add(host)&&add("\r\nConnection: close\r\nUser-Agent: zeroNT/33.0\r\n\r\n");if(!built||!network_tcp_send(h,req,n,pollBudget/2)){network_tcp_close(h);return false;}auto*dst=static_cast<uint8_t*>(out);uint32_t quiet=0;for(uint32_t i=0;i<pollBudget&&received<capacity;++i){network_poll();int got=network_tcp_receive(h,dst+received,static_cast<uint16_t>(capacity-received));if(got>0){received=static_cast<uint16_t>(received+got);quiet=0;}else ++quiet;TcpClientStats st=network_tcp_client_stats(h);if((st.state==TcpClientState::CloseWait||st.state==TcpClientState::Closed)&&st.receiveAvailable==0)break;if(quiet>300000&&received)break;__asm__ __volatile__("pause");}network_tcp_close(h);return received>0;}

NetworkStats network_stats(){return g;}
