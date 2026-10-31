#include "user_api.hpp"
#ifndef APP_KIND
#define APP_KIND 1
#endif

namespace {
using namespace znt;
static uint32_t gClientW=534;
static uint32_t gClientH=312;
static uint32_t slen(const char*s){return znt::strlen(s);}
static void clear(char*b,uint32_t cap){if(cap)b[0]=0;}
static void copy(char*d,uint32_t cap,const char*s){if(!d||!cap)return;uint32_t i=0;if(s)while(i+1<cap&&s[i]){d[i]=s[i];++i;}d[i]=0;}
static void append(char*d,uint32_t cap,const char*s){uint32_t n=slen(d);if(!s)return;uint32_t i=0;while(n+1<cap&&s[i])d[n++]=s[i++];d[n]=0;}
static void append_char(char*d,uint32_t cap,char c){uint32_t n=slen(d);if(n+1<cap){d[n]=c;d[n+1]=0;}}
static void backspace(char*d){uint32_t n=slen(d);if(n)d[n-1]=0;}
static void u64toa(uint64_t v,char*out,uint32_t cap){if(!out||!cap)return;char r[24];uint32_t n=0;if(!v)r[n++]='0';while(v&&n<23){r[n++]=char('0'+v%10);v/=10;}uint32_t o=0;while(n&&o+1<cap)out[o++]=r[--n];out[o]=0;}
static char upper(char c){return(c>='a'&&c<='z')?char(c-32):c;}
static bool eq(const char*a,const char*b){while(*a&&*b){if(upper(*a++)!=upper(*b++))return false;}return *a==0&&*b==0;}
static bool starts(const char*a,const char*b){while(*b){if(upper(*a++)!=upper(*b++))return false;}return true;}
static const char* title(){
#if APP_KIND==1
return "zeroNT Explorer";
#elif APP_KIND==2
return "Notepad";
#elif APP_KIND==3
return "Command Prompt";
#elif APP_KIND==4
return "Control Panel";
#elif APP_KIND==5
return "Task Manager";
#elif APP_KIND==6
return "Device Manager";
#elif APP_KIND==7
return "Recycle Bin";
#elif APP_KIND==8
return "Services";
#elif APP_KIND==9
return "Event Viewer";
#elif APP_KIND==10
return "My Computer";
#elif APP_KIND==11
return "Network Neighborhood";
#elif APP_KIND==12
return "My Documents";
#else
return "Printers";
#endif
}
static void paint_static(uint32_t w){
    begin_paint(w);fill(w,0,0,gClientW,gClientH,0xFFFFFF);fill(w,0,0,gClientW,24,0xD6D6D6);
#if APP_KIND==1
    text(w,12,8,"File  Edit  View  Help",0x000000);text(w,12,42,"Address: C:\\",0x000000);text(w,12,72,"SYSTEM32",0x000080);text(w,12,94,"USERS",0x000080);text(w,12,116,"SYSTEM",0x000080);text(w,12,158,"Native Ring-3 Explorer.exe",0x606060);
#elif APP_KIND==4
    text(w,16,12,"CONTROL PANEL / DISPLAY",0x000000);
    const uint64_t ss=screen_size();const uint32_t sw=static_cast<uint32_t>(ss>>32),sh=static_cast<uint32_t>(ss);
    const uint64_t caps=display_caps();const uint32_t backend=static_cast<uint32_t>(caps&0xFFu);const bool runtime=(caps&(1ULL<<8))!=0;
    const uint32_t maxW=static_cast<uint32_t>((caps>>16)&0xFFFFu),maxH=static_cast<uint32_t>((caps>>32)&0xFFFFu);
    char cur[64]="Current: ";char n[24];u64toa(sw,n,sizeof(n));append(cur,sizeof(cur),n);append(cur,sizeof(cur)," x ");u64toa(sh,n,sizeof(n));append(cur,sizeof(cur),n);text(w,16,44,cur,0x000000);
    char drv[72]="Driver: ";append(drv,sizeof(drv),backend==1?"VMware SVGA II":(backend==2?"QEMU Bochs VBE":"UEFI GOP"));text(w,16,64,drv,0x000000);
    char mx[64]="Maximum: ";u64toa(maxW,n,sizeof(n));append(mx,sizeof(mx),n);append(mx,sizeof(mx)," x ");u64toa(maxH,n,sizeof(n));append(mx,sizeof(mx),n);text(w,16,84,mx,0x000000);
    text(w,16,104,runtime?"Runtime resolution change: READY":"Runtime resolution change: boot mode only",runtime?0x006000:0x800000);
    fill(w,16,132,145,28,0xD6D6D6);text(w,48,141,"800 x 600",0x000000);
    fill(w,174,132,145,28,0xD6D6D6);text(w,196,141,"1024 x 768",0x000000);
    fill(w,332,132,145,28,0xD6D6D6);text(w,354,141,"1280 x 720",0x000000);
    fill(w,16,172,145,28,0xD6D6D6);text(w,38,181,"1366 x 768",0x000000);
    fill(w,174,172,145,28,0xD6D6D6);text(w,196,181,"1600 x 900",0x000000);
    fill(w,332,172,145,28,0xD6D6D6);text(w,350,181,"1920 x 1080",0x000000);
    fill(w,16,212,145,28,0xD6D6D6);text(w,38,221,"1280 x 800",0x000000);
    fill(w,174,212,145,28,0xD6D6D6);text(w,192,221,"1920 x 1200",0x000000);
    text(w,16,254,"The selected mode is saved to SYSTEM/DISPLAY.CFG.",0x606060);
    text(w,16,276,"VMware/QEMU drivers apply it without reboot.",0x606060);
#elif APP_KIND==5
    text(w,16,12,"TASK MANAGER - USER MODE",0x000000);char p[48]="Current EXE PID: ";char n[24];u64toa(pid(),n,sizeof(n));append(p,sizeof(p),n);text(w,16,48,p,0x000000);text(w,16,76,"Each SYSTEM32 app owns a private CR3.",0x000000);text(w,16,98,"SysYield keeps processes alive cooperatively.",0x000000);
#elif APP_KIND==6
    text(w,16,12,"DEVICE MANAGER",0x000000);text(w,16,48,"Computer",0x000000);text(w,34,72,"Storage controllers",0x000000);text(w,34,94,"USB controllers",0x000000);text(w,34,116,"Network adapters",0x000000);text(w,34,138,"Sound devices",0x000000);text(w,16,182,"Hardware is provided by zeroNT kernel drivers.",0x606060);
#elif APP_KIND==7
    text(w,16,12,"RECYCLE BIN",0x000000);text(w,16,52,"Items deleted by the classic shell appear here.",0x000000);text(w,16,76,"Native EXE window/process isolation is active.",0x606060);
#elif APP_KIND==8
    text(w,16,12,"SERVICES",0x000000);text(w,16,48,"Filesystem       RUNNING",0x000000);text(w,16,70,"Scheduler        RUNNING",0x000000);text(w,16,92,"Event Log        RUNNING",0x000000);text(w,16,114,"Network          KERNEL DRIVER",0x000000);text(w,16,136,"Audio            KERNEL DRIVER",0x000000);
#elif APP_KIND==9
    text(w,16,12,"EVENT VIEWER",0x000000);text(w,16,48,"System events are persisted by zeroNT Event Log.",0x000000);text(w,16,72,"File: /SYSTEM/EVENT.LOG",0x606060);
#elif APP_KIND==10
    text(w,16,12,"MY COMPUTER",0x000000);text(w,24,54,"3.5 Floppy (A:)",0x000000);text(w,210,54,"Local Disk (C:)",0x000000);text(w,396,54,"CD-ROM (D:)",0x000000);text(w,24,110,"Control Panel",0x000000);text(w,210,110,"Printers",0x000000);
#elif APP_KIND==11
    text(w,16,12,"NETWORK NEIGHBORHOOD",0x000000);uint64_t ns=net_status();text(w,16,50,(ns&2)?"Network adapter: LINK UP":"Network adapter: NO LINK",0x000000);fill(w,16,92,132,28,0xD6D6D6);text(w,32,101,"Ping Gateway",0x000000);
#elif APP_KIND==12
    text(w,16,12,"MY DOCUMENTS",0x000000);text(w,16,50,"Personal files are stored in your zeroNT profile.",0x000000);text(w,16,74,"Explorer file APIs remain available in the classic shell.",0x606060);
#else
    text(w,16,12,"PRINTERS",0x000000);text(w,16,50,"Add Printer",0x000000);text(w,16,82,"No printer driver is installed.",0x606060);
#endif
}
static void paint_notepad(uint32_t w,const char*buf){begin_paint(w);fill(w,0,0,gClientW,gClientH,0xFFFFFF);fill(w,0,0,gClientW,24,0xD6D6D6);text(w,8,8,"File  Edit  Help",0x000000);text(w,8,38,buf[0]?buf:"Type here...",0x000000);const int sy=gClientH>42?static_cast<int>(gClientH)-26:16;text(w,8,sy,"Native Ring-3 NOTEPAD.EXE - close with [X]",0x606060);}
static void paint_cmd(uint32_t w,const char*out,const char*line){begin_paint(w);fill(w,0,0,gClientW,gClientH,0x000000);text(w,8,8,out,0xC0C0C0);char p[112]="C:\\>";append(p,sizeof(p),line);const int py=gClientH>48?static_cast<int>(gClientH)-34:14;text(w,8,py,p,0xFFFFFF);}
static void cmd_run(char*out,uint32_t cap,char*line,bool&quit){append(out,cap,"\nC:\\>");append(out,cap,line);append(out,cap,"\n");if(eq(line,"HELP"))append(out,cap,"HELP VER PID CLS ECHO EXIT");else if(eq(line,"VER"))append(out,cap,"zeroNT v33.0 Beta - Native EXE v2");else if(eq(line,"PID")){char n[24];u64toa(pid(),n,sizeof(n));append(out,cap,"PID ");append(out,cap,n);}else if(eq(line,"CLS"))copy(out,cap,"zeroNT Command Prompt");else if(eq(line,"EXIT"))quit=true;else if(starts(line,"ECHO "))append(out,cap,line+5);else if(line[0])append(out,cap,"Bad command or file name.");clear(line,80);}
}

extern "C" __attribute__((section(".text.entry"),noreturn)) void _start(){
    using namespace znt;
    const uint64_t ss=screen_size();
    uint32_t sw=static_cast<uint32_t>(ss>>32),sh=static_cast<uint32_t>(ss);
    if(sw<640)sw=640;if(sh<480)sh=480;
    const uint32_t maxW=sw>32?sw-32:sw;
    const uint32_t workH=sh>28?sh-28:sh;
    const uint32_t maxH=workH>32?workH-32:workH;
    uint32_t winW=0,winH=0;
#if APP_KIND==2
    winW=(sw*76u)/100u;winH=(workH*72u)/100u;
    if(winW<600)winW=600;if(winH<390)winH=390;
#elif APP_KIND==3
    winW=(sw*72u)/100u;winH=(workH*66u)/100u;
    if(winW<580)winW=580;if(winH<350)winH=350;
#else
    winW=(sw*58u)/100u;winH=(workH*58u)/100u;
    if(winW<500)winW=500;if(winH<320)winH=320;
#endif
    if(winW>1100)winW=1100;if(winH>760)winH=760;
    if(winW>maxW)winW=maxW;if(winH>maxH)winH=maxH;
    gClientW=winW>6?winW-6:winW;gClientH=winH>28?winH-28:winH;
    const uint32_t win=create_window(winW,winH);
    if(!win)exit(2);set_title(win,title());
#if APP_KIND==2
    char note[640];clear(note,sizeof(note));paint_notepad(win,note);
#elif APP_KIND==3
    char line[80];char out[640];clear(line,sizeof(line));copy(out,sizeof(out),"zeroNT v33.0 Command Prompt\nType HELP for commands.");paint_cmd(win,out,line);
#else
    paint_static(win);
#endif
    bool quit=false;
#if APP_KIND==4
    bool leftWasDown=false;
#endif
    for(;;){
        GuiEvent e{};
        while((e=get_event(win)).type!=None){
            if(e.type==Close){quit=true;break;}
#if APP_KIND==2
            if(e.type==Key){const uint32_t sc=(uint32_t)e.a;const char a=(char)e.b;if(sc==0x0E)backspace(note);else if(sc==0x1C)append_char(note,sizeof(note),'\n');else if(a>=32&&a<=126)append_char(note,sizeof(note),a);paint_notepad(win,note);}
#elif APP_KIND==3
            if(e.type==Key){const uint32_t sc=(uint32_t)e.a;const char a=(char)e.b;if(sc==0x0E)backspace(line);else if(sc==0x1C)cmd_run(out,sizeof(out),line,quit);else if(a>=32&&a<=126)append_char(line,sizeof(line),a);if(!quit)paint_cmd(win,out,line);}
#elif APP_KIND==4
            if(e.type==Mouse){
                const bool down=(e.c&1)!=0;const bool press=down&&!leftWasDown;leftWasDown=down;
                if(press){
                    uint32_t rw=0,rh=0;
                    if(e.a>=16&&e.a<161&&e.b>=132&&e.b<160){rw=800;rh=600;}
                    else if(e.a>=174&&e.a<319&&e.b>=132&&e.b<160){rw=1024;rh=768;}
                    else if(e.a>=332&&e.a<477&&e.b>=132&&e.b<160){rw=1280;rh=720;}
                    else if(e.a>=16&&e.a<161&&e.b>=172&&e.b<200){rw=1366;rh=768;}
                    else if(e.a>=174&&e.a<319&&e.b>=172&&e.b<200){rw=1600;rh=900;}
                    else if(e.a>=332&&e.a<477&&e.b>=172&&e.b<200){rw=1920;rh=1080;}
                    else if(e.a>=16&&e.a<161&&e.b>=212&&e.b<240){rw=1280;rh=800;}
                    else if(e.a>=174&&e.a<319&&e.b>=212&&e.b<240){rw=1920;rh=1200;}
                    if(rw&&rh){(void)set_resolution(rw,rh);paint_static(win);}
                }
            }
#elif APP_KIND==11
            if(e.type==Mouse && (e.c&1) && e.a>=16&&e.a<148&&e.b>=92&&e.b<126){ping_gateway();paint_static(win);}
#endif
        }
        if(quit){close_window(win);exit(0);}
        yield();
    }
}
