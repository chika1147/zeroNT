#pragma once
#include <stdint.h>
#include <stddef.h>

struct NetworkStats {
    bool nicFound;
    bool initialized;
    bool linkUp;
    bool dhcpBound;
    bool dhcpFailed;
    bool staticConfigured;
    bool gatewayResolved;
    uint8_t bus, device, function;
    uint16_t vendorId, deviceId;
    uint64_t mmioBase;
    uint8_t mac[6];
    uint8_t ipv4[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint64_t linkTransitions;
    uint64_t txPackets;
    uint64_t rxPackets;
    uint64_t arpReplies;
    uint64_t icmpReplies;
    uint64_t udpPackets;
    uint64_t udpDatagramsSent;
    uint64_t udpDatagramsReceived;
    uint64_t dhcpPackets;
    uint64_t dhcpRetries;
    uint64_t dhcpFailures;
    uint64_t dnsQueries;
    uint64_t dnsReplies;
    uint64_t dnsRetries;
    uint64_t dnsTimeouts;
    uint64_t dnsCacheHits;
    uint64_t pingRequests;
    uint64_t pingTimeouts;
    uint64_t tcpSynSent;
    uint64_t tcpReplies;
    uint64_t tcpSynAcks;
    uint64_t tcpResets;
    uint64_t tcpTimeouts;
    uint64_t tcpRetransmits;
    uint64_t txFailures;
    uint64_t rxDrops;
    bool dnsResolved;
    uint8_t lastDnsIp[4];
    uint64_t tcpConnections;
    uint64_t tcpBytesSent;
    uint64_t tcpBytesReceived;
    uint64_t tcpClosed;
    uint32_t openTcpSockets;
    uint32_t openUdpSockets;
};

enum class TcpClientState : uint8_t {
    Closed = 0,
    SynSent,
    Established,
    FinWait,
    CloseWait,
};

struct TcpClientStats {
    TcpClientState state;
    uint16_t localPort;
    uint16_t remotePort;
    uint8_t remoteIp[4];
    uint32_t sendNext;
    uint32_t receiveNext;
    uint32_t peerAck;
    uint32_t receiveAvailable;
    uint64_t connects;
    uint64_t sends;
    uint64_t receives;
    uint64_t closes;
    uint64_t retransmits;
};

struct UdpDatagramInfo {
    uint8_t sourceIp[4];
    uint16_t sourcePort;
    uint16_t bytes;
};

bool network_init();
void network_poll();
bool network_link_up();

bool network_restart_dhcp();
bool network_configure_static(const uint8_t ip[4], const uint8_t subnet[4],
                              const uint8_t gateway[4], const uint8_t dns[4]);
void network_dns_cache_flush();

bool network_ping_gateway();
bool network_ping_gateway_wait(uint32_t pollBudget=1200000);
bool network_dns_query(const char* host, uint8_t outIp[4]);
bool network_tcp_probe(const uint8_t dstIp[4], uint16_t port);
bool network_tcp_probe_wait(const uint8_t dstIp[4], uint16_t port, uint32_t pollBudget=1200000);

// Small polling socket layer. Handles are positive integers and are validated.
// Up to four TCP sockets and four UDP sockets may be open concurrently.
int network_tcp_connect(const uint8_t dstIp[4], uint16_t port, uint32_t pollBudget=1200000);
bool network_tcp_send(int handle, const void* data, uint16_t size, uint32_t pollBudget=1200000);
int network_tcp_receive(int handle, void* out, uint16_t capacity);
bool network_tcp_close(int handle, uint32_t pollBudget=600000);
TcpClientStats network_tcp_client_stats(int handle=1);

int network_udp_open(uint16_t localPort=0);
bool network_udp_send(int handle, const uint8_t dstIp[4], uint16_t dstPort,
                      const void* data, uint16_t size, uint32_t pollBudget=300000);
int network_udp_receive(int handle, void* out, uint16_t capacity, UdpDatagramInfo* info=nullptr);
bool network_udp_close(int handle);

// Convenience HTTP/1.0 GET built on the TCP client. Returns received bytes.
bool network_http_get(const char* host, const char* path, void* out, uint16_t capacity,
                      uint16_t& received, uint32_t pollBudget=2400000);

NetworkStats network_stats();
