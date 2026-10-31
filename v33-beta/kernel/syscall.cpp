#include "include/syscall.hpp"
#include "include/gui.hpp"
#include "include/audio.hpp"
#include "include/network.hpp"
#include "include/user_manager.hpp"
#include "include/security.hpp"
#include "include/scheduler.hpp"
#include "include/usermode.hpp"
#include "include/system_apps.hpp"
#include "include/display.hpp"

namespace {
static volatile uint64_t gSyscalls = 0;
static volatile uint64_t gPings = 0;
static volatile int64_t gLastExit = -1;
}

void syscall_init() {
    gSyscalls = 0;
    gPings = 0;
    gLastExit = -1;
}

extern "C" uint64_t zeront_syscall_dispatch(SyscallFrame* frame) {
    if (!frame) return 0;
    const uint64_t appPid=usermode_current_pid();
    const uint64_t ownerPid=appPid?appPid:scheduler_current_pid();
    ++gSyscalls;
    switch (frame->rax) {
        case SysExit:
            gLastExit = static_cast<int64_t>(frame->rdi);
            frame->rax = 0;
            return 1; // assembly returns to the saved ring-0 caller
        case SysGetVersion:
            frame->rax = 33;
            return 0;
        case SysPing:
            ++gPings;
            frame->rax = gPings;
            return 0;
        case SysYield:
            frame->rax = 0;
            if(!usermode_capture_yield_frame(frame)) {
                security_record_denied_syscall();
                return 0;
            }
            return 2; // cooperative return to executable_poll(), process stays alive
        case SysGuiCreateWindow:
            if (!user_is_logged_in()) { frame->rax = 0; return 0; }
            frame->rax = gui_create_window(ownerPid, static_cast<uint32_t>(frame->rdi), static_cast<uint32_t>(frame->rsi));
            return 0;
        case SysGuiFillRect: {
            const uint32_t h=static_cast<uint32_t>(frame->rdi);if(!gui_window_owned_by(h,ownerPid)){security_record_denied_syscall();frame->rax=0;return 0;}
            frame->rax = gui_fill_rect(h, static_cast<int32_t>(frame->rsi), static_cast<int32_t>(frame->rdx),static_cast<uint32_t>(frame->r10), static_cast<uint32_t>(frame->r8), static_cast<uint32_t>(frame->r9)) ? 1 : 0;return 0;}
        case SysGuiGetEvent: {
            const uint32_t h=static_cast<uint32_t>(frame->rdi);if(!gui_window_owned_by(h,ownerPid)){security_record_denied_syscall();frame->rax=0;return 0;}
            GuiEvent e{}; bool ok=gui_get_event(h,e);
            frame->rax=ok?e.type:0; frame->rdi=static_cast<uint64_t>(static_cast<int64_t>(e.a)); frame->rsi=static_cast<uint64_t>(static_cast<int64_t>(e.b)); frame->rdx=static_cast<uint64_t>(static_cast<int64_t>(e.c));
            return 0; }
        case SysGuiCloseWindow: {
            const uint32_t h=static_cast<uint32_t>(frame->rdi);if(!gui_window_owned_by(h,ownerPid)){security_record_denied_syscall();frame->rax=0;return 0;}
            frame->rax = gui_close_window(h) ? 1 : 0; return 0;}
        case SysGuiSetTitle: {
            const uint32_t h=static_cast<uint32_t>(frame->rdi);
            const uint64_t ptr=frame->rsi; uint64_t len=frame->rdx;
            if(!gui_window_owned_by(h,ownerPid)||len==0||len>39||!security_validate_user_range(ptr,static_cast<size_t>(len),false)){
                security_record_denied_syscall();frame->rax=0;return 0;
            }
            char title[40]{};const char*src=reinterpret_cast<const char*>(ptr);
            uint64_t n=0;for(;n<len&&n+1<sizeof(title)&&src[n];++n)title[n]=src[n];title[n]=0;
            frame->rax=gui_set_title(h,title)?1:0;return 0;
        }
        case SysGuiDrawText: {
            const uint32_t h=static_cast<uint32_t>(frame->rdi);
            const int32_t x=static_cast<int32_t>(frame->rsi),y=static_cast<int32_t>(frame->rdx);
            const uint64_t ptr=frame->r10;uint64_t len=frame->r8;const uint32_t rgb=static_cast<uint32_t>(frame->r9);
            if(!gui_window_owned_by(h,ownerPid)||len==0||len>95||!security_validate_user_range(ptr,static_cast<size_t>(len),false)){
                security_record_denied_syscall();frame->rax=0;return 0;
            }
            char text[96]{};const char*src=reinterpret_cast<const char*>(ptr);
            uint64_t n=0;for(;n<len&&n+1<sizeof(text)&&src[n];++n)text[n]=src[n];text[n]=0;
            frame->rax=gui_draw_text(h,x,y,text,rgb)?1:0;return 0;
        }
        case SysGuiBeginPaint: {
            const uint32_t h=static_cast<uint32_t>(frame->rdi);
            if(!gui_window_owned_by(h,ownerPid)){security_record_denied_syscall();frame->rax=0;return 0;}
            frame->rax=gui_begin_paint(h)?1:0;return 0;
        }
        case SysGuiScreenSize:
            frame->rax=(static_cast<uint64_t>(gui_screen_width())<<32)|static_cast<uint64_t>(gui_screen_height());
            return 0;
        case SysDisplayCaps: {
            DisplayStats d=display_stats();
            uint64_t caps=static_cast<uint64_t>(d.backend)&0xFFULL;
            if(display_runtime_modes_supported())caps|=1ULL<<8;
            if(d.persistentConfig)caps|=1ULL<<9;
            caps|=(static_cast<uint64_t>(d.maxWidth&0xFFFFu)<<16);
            caps|=(static_cast<uint64_t>(d.maxHeight&0xFFFFu)<<32);
            frame->rax=caps;return 0;
        }
        case SysDisplaySetMode: {
            if(!user_has_permission(UserPermChangeSettings)){security_record_denied_syscall();frame->rax=0;return 0;}
            const uint32_t w=static_cast<uint32_t>(frame->rdi),h=static_cast<uint32_t>(frame->rsi);
            const bool changed=display_set_mode(w,h);
            if(changed){(void)display_save_mode(w,h);system_apps_mark_dirty();}
            frame->rax=changed?1:0;return 0;
        }
        case SysAudioTone:
            if (!user_has_permission(UserPermAudio)) { security_record_denied_syscall(); frame->rax = 0; return 0; }
            frame->rax = audio_play_tone(static_cast<uint32_t>(frame->rdi), static_cast<uint32_t>(frame->rsi)) ? 1 : 0;
            return 0;
        case SysAudioSetVolume:
            if (!user_has_permission(UserPermAudio)) { security_record_denied_syscall(); frame->rax = 0; return 0; }
            frame->rax = audio_set_volume(static_cast<uint8_t>(frame->rdi > 100 ? 100 : frame->rdi)) ? 1 : 0;
            return 0;
        case SysAudioSubmitPcm: {
            if (!user_has_permission(UserPermAudio)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            const uint64_t ptr=frame->rdi; uint64_t frames=frame->rsi; const uint32_t rate=static_cast<uint32_t>(frame->rdx);
            if(frames==0||frames>4096||rate<8000||rate>192000){frame->rax=0;return 0;}
            const size_t bytes=static_cast<size_t>(frames)*sizeof(int16_t)*2u;
            if(!security_validate_user_range(ptr,bytes,false)){security_record_denied_syscall();frame->rax=0;return 0;}
            frame->rax=audio_submit_pcm_s16_stereo(reinterpret_cast<const int16_t*>(ptr),static_cast<uint32_t>(frames),rate)?1:0;return 0;
        }
        case SysAudioStop:
            if (!user_has_permission(UserPermAudio)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            audio_stop();frame->rax=1;return 0;
        case SysAudioStatus: {
            if (!user_has_permission(UserPermAudio)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            AudioStats a=audio_stats();uint64_t flags=0;if(a.initialized)flags|=1ULL;if(a.playing)flags|=2ULL;if(a.muted)flags|=4ULL;flags|=(static_cast<uint64_t>(a.backend)<<8);
            frame->rax=flags;frame->rdi=a.queuedFrames;frame->rsi=a.volumePercent;return 0;
        }
        case SysAudioMute:
            if (!user_has_permission(UserPermAudio)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            frame->rax=audio_set_mute(frame->rdi!=0)?1:0;return 0;
        case SysNetworkPingGateway:
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax = 0; return 0; }
            frame->rax = network_ping_gateway() ? 1 : 0;
            return 0;
        case SysNetworkStatus: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax = 0; return 0; }
            NetworkStats n = network_stats();
            uint64_t flags = 0;
            if (n.initialized) flags |= 1ULL;
            if (n.linkUp) flags |= 2ULL;
            if (n.dhcpBound) flags |= 4ULL;
            if (n.gatewayResolved) flags |= 8ULL;
            frame->rax = flags;
            return 0;
        }
        case SysNetworkDnsLookup: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            const uint64_t hostPtr=frame->rdi, hostLen=frame->rsi, outPtr=frame->rdx;
            if(hostLen==0||hostLen>63||!security_validate_user_range(hostPtr,hostLen,false)||!security_validate_user_range(outPtr,4,true)){security_record_denied_syscall();frame->rax=0;return 0;}
            char host[64]{};const char*src=reinterpret_cast<const char*>(hostPtr);for(uint64_t i=0;i<hostLen;++i){char c=src[i];if(c==0)break;host[i]=c;}
            uint8_t ip[4]{};if(!network_dns_query(host,ip)){frame->rax=0;return 0;}auto*out=reinterpret_cast<uint8_t*>(outPtr);for(int i=0;i<4;++i)out[i]=ip[i];frame->rax=1;return 0;
        }
        case SysNetworkTcpProbe: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            uint8_t ip[4]={static_cast<uint8_t>(frame->rdi>>24),static_cast<uint8_t>(frame->rdi>>16),static_cast<uint8_t>(frame->rdi>>8),static_cast<uint8_t>(frame->rdi)};
            frame->rax=network_tcp_probe(ip,static_cast<uint16_t>(frame->rsi))?1:0;return 0;
        }
        case SysNetworkTcpConnect: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            uint8_t ip[4]={static_cast<uint8_t>(frame->rdi>>24),static_cast<uint8_t>(frame->rdi>>16),static_cast<uint8_t>(frame->rdi>>8),static_cast<uint8_t>(frame->rdi)};
            frame->rax=static_cast<uint64_t>(network_tcp_connect(ip,static_cast<uint16_t>(frame->rsi)));
            return 0;
        }
        case SysNetworkTcpSend: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            const int handle=static_cast<int>(frame->rdi);
            const uint64_t ptr=frame->rsi;
            const uint64_t len=frame->rdx;
            if(len==0||len>4096||!security_validate_user_range(ptr,static_cast<size_t>(len),false)){security_record_denied_syscall();frame->rax=0;return 0;}
            frame->rax=network_tcp_send(handle,reinterpret_cast<const void*>(ptr),static_cast<uint16_t>(len))?1:0;
            return 0;
        }
        case SysNetworkTcpReceive: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            const int handle=static_cast<int>(frame->rdi);
            const uint64_t ptr=frame->rsi;
            uint64_t cap=frame->rdx;if(cap>4096)cap=4096;
            if(cap==0||!security_validate_user_range(ptr,static_cast<size_t>(cap),true)){security_record_denied_syscall();frame->rax=0;return 0;}
            const int got=network_tcp_receive(handle,reinterpret_cast<void*>(ptr),static_cast<uint16_t>(cap));
            frame->rax=got<0?0:static_cast<uint64_t>(got);
            return 0;
        }
        case SysNetworkTcpClose:
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            frame->rax=network_tcp_close(static_cast<int>(frame->rdi))?1:0;
            return 0;
        case SysNetworkUdpOpen:
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            frame->rax=static_cast<uint64_t>(network_udp_open(static_cast<uint16_t>(frame->rdi)));return 0;
        case SysNetworkUdpSend: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            const int handle=static_cast<int>(frame->rdi);const uint64_t ipPacked=frame->rsi;const uint16_t port=static_cast<uint16_t>(frame->rdx);const uint64_t ptr=frame->r10;uint64_t len=frame->r8;
            if(!len||len>1200||!security_validate_user_range(ptr,static_cast<size_t>(len),false)){security_record_denied_syscall();frame->rax=0;return 0;}
            uint8_t ip[4]={static_cast<uint8_t>(ipPacked>>24),static_cast<uint8_t>(ipPacked>>16),static_cast<uint8_t>(ipPacked>>8),static_cast<uint8_t>(ipPacked)};
            frame->rax=network_udp_send(handle,ip,port,reinterpret_cast<const void*>(ptr),static_cast<uint16_t>(len))?1:0;return 0;
        }
        case SysNetworkUdpReceive: {
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            const int handle=static_cast<int>(frame->rdi);const uint64_t ptr=frame->rsi;uint64_t cap=frame->rdx;if(cap>1200)cap=1200;
            if(!cap||!security_validate_user_range(ptr,static_cast<size_t>(cap),true)){security_record_denied_syscall();frame->rax=0;return 0;}
            UdpDatagramInfo info{};int got=network_udp_receive(handle,reinterpret_cast<void*>(ptr),static_cast<uint16_t>(cap),&info);
            if(got<0){frame->rax=0;return 0;}frame->rax=static_cast<uint64_t>(got);frame->rdi=(static_cast<uint64_t>(info.sourceIp[0])<<24)|(static_cast<uint64_t>(info.sourceIp[1])<<16)|(static_cast<uint64_t>(info.sourceIp[2])<<8)|info.sourceIp[3];frame->rsi=info.sourcePort;return 0;
        }
        case SysNetworkUdpClose:
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            frame->rax=network_udp_close(static_cast<int>(frame->rdi))?1:0;return 0;
        case SysNetworkDhcpRestart:
            if (!user_has_permission(UserPermNetwork)) { security_record_denied_syscall(); frame->rax=0; return 0; }
            frame->rax=network_restart_dhcp()?1:0;return 0;
        case SysUserInfo: {
            UserSystemStats u = user_system_stats();
            frame->rax = u.loggedIn ? u.currentUid : 0;
            frame->rdi = static_cast<uint64_t>(u.currentRole);
            frame->rsi = u.permissions;
            return 0;
        }
        case SysUserPermissions:
            frame->rax = user_system_stats().permissions;
            return 0;
        case SysUserLogout:
            if(!user_is_logged_in()){security_record_denied_syscall();frame->rax=0;return 0;}
            user_logout();
            frame->rax = 1;
            return 0;
        case SysProcessId:
            frame->rax = ownerPid;
            return 0;
        case SysShellOpenApp:
            if(!user_is_logged_in()){security_record_denied_syscall();frame->rax=0;return 0;}
            frame->rax = system_apps_shell_open(static_cast<uint32_t>(frame->rdi)) ? 1 : 0;
            return 0;
        default:
            security_record_denied_syscall();
            frame->rax = static_cast<uint64_t>(-1LL);
            return 0;
    }
}

uint64_t syscall_count() { return gSyscalls; }
uint64_t syscall_ping_count() { return gPings; }
int64_t syscall_last_exit_code() { return gLastExit; }
