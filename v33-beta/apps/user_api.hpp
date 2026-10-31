#pragma once
#include <stdint.h>
#include <stddef.h>

namespace znt {
enum Sys : uint64_t {
    Exit=1, GetVersion=2, Ping=3, Yield=4,
    GuiCreateWindow=10, GuiFillRect=11, GuiGetEvent=12, GuiCloseWindow=13,
    GuiSetTitle=14, GuiDrawText=15, GuiBeginPaint=16, GuiScreenSize=17, DisplayCaps=18, DisplaySetMode=19,
    AudioTone=20, AudioSetVolume=21, AudioSubmitPcm=22, AudioStop=23, AudioStatus=24, AudioMute=25,
    NetworkPingGateway=30, NetworkStatus=31, NetworkDnsLookup=32, NetworkTcpProbe=33, NetworkTcpConnect=34, NetworkTcpSend=35, NetworkTcpReceive=36, NetworkTcpClose=37,
    NetworkUdpOpen=70, NetworkUdpSend=71, NetworkUdpReceive=72, NetworkUdpClose=73, NetworkDhcpRestart=74,
    UserInfo=40, UserPermissions=41, UserLogout=42,
    ProcessId=50
};
enum Event : uint32_t { None=0, Key=1, Mouse=2, Close=3, Paint=4 };
struct GuiEvent { uint32_t type; int32_t a,b,c; };

static inline uint64_t call0(uint64_t n){
    register uint64_t a asm("rax")=n;
    asm volatile("int $0x80" : "+a"(a) : : "rcx","r11","memory");
    return a;
}
static inline uint64_t call1(uint64_t n,uint64_t p1){
    register uint64_t a asm("rax")=n;register uint64_t d asm("rdi")=p1;
    asm volatile("int $0x80" : "+a"(a), "+D"(d) : : "rcx","r11","memory");return a;
}
static inline uint64_t call2(uint64_t n,uint64_t p1,uint64_t p2){
    register uint64_t a asm("rax")=n;register uint64_t d asm("rdi")=p1;register uint64_t s asm("rsi")=p2;
    asm volatile("int $0x80" : "+a"(a), "+D"(d), "+S"(s) : : "rcx","r11","memory");return a;
}
static inline uint64_t call3(uint64_t n,uint64_t p1,uint64_t p2,uint64_t p3){
    register uint64_t a asm("rax")=n;register uint64_t d asm("rdi")=p1;register uint64_t s asm("rsi")=p2;register uint64_t q asm("rdx")=p3;
    asm volatile("int $0x80" : "+a"(a), "+D"(d), "+S"(s), "+d"(q) : : "rcx","r11","memory");return a;
}
static inline uint64_t call6(uint64_t n,uint64_t p1,uint64_t p2,uint64_t p3,uint64_t p4,uint64_t p5,uint64_t p6){
    register uint64_t a asm("rax")=n;register uint64_t d asm("rdi")=p1;register uint64_t s asm("rsi")=p2;register uint64_t q asm("rdx")=p3;
    register uint64_t r10 asm("r10")=p4;register uint64_t r8 asm("r8")=p5;register uint64_t r9 asm("r9")=p6;
    asm volatile("int $0x80" : "+a"(a), "+D"(d), "+S"(s), "+d"(q), "+r"(r10), "+r"(r8), "+r"(r9) : : "rcx","r11","memory");return a;
}
static inline uint32_t strlen(const char*s){uint32_t n=0;if(s)while(s[n])++n;return n;}
static inline uint32_t create_window(uint32_t w,uint32_t h){return (uint32_t)call2(GuiCreateWindow,w,h);}
static inline uint64_t screen_size(){return call0(GuiScreenSize);}
static inline uint64_t display_caps(){return call0(DisplayCaps);}
static inline bool set_resolution(uint32_t w,uint32_t h){return call2(DisplaySetMode,w,h)!=0;}
static inline bool set_title(uint32_t h,const char*s){return call3(GuiSetTitle,h,(uint64_t)s,strlen(s))!=0;}
static inline bool begin_paint(uint32_t h){return call1(GuiBeginPaint,h)!=0;}
static inline bool fill(uint32_t h,int x,int y,uint32_t w,uint32_t hh,uint32_t rgb){return call6(GuiFillRect,h,(uint64_t)x,(uint64_t)y,w,hh,rgb)!=0;}
static inline bool text(uint32_t h,int x,int y,const char*s,uint32_t rgb=0){return call6(GuiDrawText,h,(uint64_t)x,(uint64_t)y,(uint64_t)s,strlen(s),rgb)!=0;}
static inline GuiEvent get_event(uint32_t h){
    register uint64_t a asm("rax")=GuiGetEvent;register uint64_t d asm("rdi")=h;register uint64_t s asm("rsi")=0;register uint64_t q asm("rdx")=0;
    asm volatile("int $0x80" : "+a"(a), "+D"(d), "+S"(s), "+d"(q) : : "rcx","r11","memory");
    return {(uint32_t)a,(int32_t)d,(int32_t)s,(int32_t)q};
}
static inline void close_window(uint32_t h){call1(GuiCloseWindow,h);}
static inline void yield(){call0(Yield);}
[[noreturn]] static inline void exit(int code){call1(Exit,(uint64_t)(int64_t)code);for(;;)asm volatile("ud2");}
static inline uint64_t pid(){return call0(ProcessId);}
static inline uint64_t net_status(){return call0(NetworkStatus);}
static inline bool ping_gateway(){return call0(NetworkPingGateway)!=0;}
static inline bool tone(uint32_t hz,uint32_t ms){return call2(AudioTone,hz,ms)!=0;}
static inline bool set_volume(uint32_t p){return call1(AudioSetVolume,p)!=0;}

static inline bool audio_submit_pcm(const int16_t*stereo,uint32_t frames,uint32_t rate){return call3(AudioSubmitPcm,(uint64_t)stereo,frames,rate)!=0;}
static inline void audio_stop(){call0(AudioStop);}
static inline bool audio_mute(bool m){return call1(AudioMute,m?1:0)!=0;}
static inline bool dns_lookup(const char*host,uint8_t out[4]){return call3(NetworkDnsLookup,(uint64_t)host,strlen(host),(uint64_t)out)!=0;}
static inline int tcp_connect(uint32_t ipBe,uint16_t port){return (int)call2(NetworkTcpConnect,ipBe,port);}
static inline bool tcp_send(int h,const void*p,uint32_t n){return call3(NetworkTcpSend,(uint64_t)h,(uint64_t)p,n)!=0;}
static inline uint32_t tcp_recv(int h,void*p,uint32_t cap){return (uint32_t)call3(NetworkTcpReceive,(uint64_t)h,(uint64_t)p,cap);}
static inline bool tcp_close(int h){return call1(NetworkTcpClose,(uint64_t)h)!=0;}
static inline int udp_open(uint16_t localPort=0){return (int)call1(NetworkUdpOpen,localPort);}
static inline bool udp_send(int h,uint32_t ipBe,uint16_t port,const void*p,uint32_t n){return call6(NetworkUdpSend,(uint64_t)h,ipBe,port,(uint64_t)p,n,0)!=0;}
static inline uint32_t udp_recv(int h,void*p,uint32_t cap){return (uint32_t)call3(NetworkUdpReceive,(uint64_t)h,(uint64_t)p,cap);}
static inline bool udp_close(int h){return call1(NetworkUdpClose,(uint64_t)h)!=0;}
static inline bool dhcp_restart(){return call0(NetworkDhcpRestart)!=0;}
}
