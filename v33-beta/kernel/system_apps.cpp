#include "include/system_apps.hpp"
#include "include/user_manager.hpp"
#include "include/process.hpp"
#include "include/scheduler.hpp"
#include "include/driver_manager.hpp"
#include "include/audio.hpp"
#include "include/network.hpp"
#include "include/interrupts.hpp"
#include "include/safe_fs.hpp"
#include "include/quality.hpp"
#include "include/security.hpp"
#include "include/ahci.hpp"
#include "include/nvme.hpp"
#include "include/usb.hpp"
#include "include/beta.hpp"
#include "include/event_log.hpp"
#include "include/acpi.hpp"
#include "include/classic_ui.hpp"
#include "include/executable.hpp"
#include "include/gui.hpp"
#include "include/usermode.hpp"
#include "include/memory.hpp"
#include "include/paging.hpp"
#include "include/heap.hpp"
#include "include/platform_hardware.hpp"
#include "include/display.hpp"
#include <stddef.h>

namespace {
constexpr uint32_t MAX_ITEMS=64;
constexpr uint32_t NOTE_CAP=8192;
constexpr uint32_t CLIP_CAP=8192;
constexpr uint32_t CMD_CAP=96;
constexpr uint32_t CMD_LINES=12;
constexpr uint32_t HIST_MAX=8;
struct Item { char name[256]; uint8_t attr; uint32_t size; };
struct WindowState { int x=0,y=0,w=720,h=520; int restoreX=0,restoreY=0,restoreW=720,restoreH=520; bool minimized=false,maximized=false,dragging=false,resizing=false; int dragOffX=0,dragOffY=0; };
enum class DialogType:uint8_t {None,Info,Error,Unsaved,Overwrite,ConfirmDelete,InputNewFolder,InputRename,InputSaveAs,Properties};
enum class PendingAction:uint8_t {None,CloseApp,OpenFile,DeleteExplorer,SaveAs};
static ZeroNTBootInfo* gBoot=nullptr;
static Fat32Volume* gFat=nullptr;
static bool gInit=false,gDirty=true,gStart=false,gLogoutRequested=false;
static uint8_t gStartSubmenu=0; // 0 none, 1 Programs, 2 Settings
static SystemAppId gActive=SystemAppId::Desktop;
static WindowState gWin{};
static int gMouseX=0,gMouseY=0; static uint8_t gMouseButtons=0,gPrevButtons=0;
static bool gShift=false,gCtrl=false,gAlt=false;
static char gStatus[96]="READY";
static Item gExplorer[MAX_ITEMS]{}; static uint32_t gExplorerCount=0,gExplorerSel=0,gExplorerScroll=0;
static char gExplorerRoot[1024]{},gExplorerPath[1024]{},gExplorerLogical[1024]{};
static char gHistPath[HIST_MAX][1024]{}; static char gHistLogical[HIST_MAX][1024]{}; static uint32_t gHistCount=0,gHistIndex=0;
static Item gRecycle[MAX_ITEMS]{}; static uint32_t gRecycleCount=0,gRecycleSel=0,gRecycleScroll=0;
static char gNote[NOTE_CAP]{}; static uint32_t gNoteLen=0,gNoteCursor=0,gSelAnchor=0,gSelEnd=0,gNoteScroll=0; static bool gNoteDirty=false,gNoteSelecting=false; static uint64_t gNoteSaves=0;
static char gNotePath[1024]{},gNoteName[256]="NOTE.TXT";
static char gUndo[NOTE_CAP]{},gRedo[NOTE_CAP]{}; static uint32_t gUndoLen=0,gRedoLen=0; static bool gUndoValid=false,gRedoValid=false;
static char gTextClipboard[CLIP_CAP]{}; static uint32_t gTextClipLen=0;
static char gFileClipName[256]{},gFileClipSource[1024]{}; static bool gFileClipCut=false;
static char gCmd[CMD_CAP]{}; static uint32_t gCmdLen=0; static char gCmdLines[CMD_LINES][82]{}; static uint32_t gCmdLineCount=0; static uint64_t gCommands=0;
static DialogType gDialog=DialogType::None; static PendingAction gPending=PendingAction::None; static char gDialogTitle[40]{},gDialogText[256]{},gDialogInput[256]{}; static uint32_t gDialogInputLen=0;
static char gPendingOpenPath[1024]{},gPendingOpenName[256]{},gPendingSavePath[1024]{},gPendingSaveName[256]{};
static bool gContext=false; static int gContextX=0,gContextY=0;
enum class ClickScope:uint8_t {None,Desktop,Explorer,MyComputer,Documents};
static uint64_t gLastClickTick=0;
static uint32_t gLastClickItem=0xFFFFFFFFu;
static ClickScope gLastClickScope=ClickScope::None;
static int gLastClickX=0,gLastClickY=0;
static int gDesktopSel=-1,gMyComputerSel=-1;
static bool gFileDragCandidate=false,gDraggingFile=false; static uint32_t gDragItem=0; static int gDragStartX=0,gDragStartY=0;
static uint32_t rgb(uint8_t r,uint8_t g,uint8_t b){ return classic_ui::pack({r,g,b}); }
static void px(int x,int y,uint32_t c){
    if(!gBoot||x<0||y<0||x>=int(gBoot->width)||y>=int(gBoot->height))return;
    auto*fb=(volatile uint32_t*)(uintptr_t)gBoot->framebuffer;
    fb[uint64_t(y)*gBoot->pixelsPerScanLine+uint32_t(x)]=c;
}
static void rect(int x,int y,int w,int h,uint32_t c){
    if(!gBoot||w<=0||h<=0)return;
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h;
    if(x1>(int)gBoot->width)x1=(int)gBoot->width;if(y1>(int)gBoot->height)y1=(int)gBoot->height;
    for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx)px(xx,yy,c);
}
static classic_ui::Color unpack_ui(uint32_t c){
    // rgb() already packs to framebuffer order. Convert by checking pixel format.
    if(gBoot && gBoot->pixelFormat==0) return {(uint8_t)(c&255),(uint8_t)((c>>8)&255),(uint8_t)((c>>16)&255)};
    return {(uint8_t)((c>>16)&255),(uint8_t)((c>>8)&255),(uint8_t)(c&255)};
}
static void chr(int x,int y,char c,uint32_t fg,int scale=1){ classic_ui::draw_char(x,y,c,unpack_ui(fg),scale); }
static void text(int x,int y,const char*s,uint32_t fg,int scale=1){ classic_ui::text(x,y,s,unpack_ui(fg),scale); }
static void u64text(int x,int y,uint64_t v,uint32_t fg){char b[24];int n=0;if(!v)b[n++]='0';else{char r[24];int z=0;while(v&&z<23){r[z++]=char('0'+v%10);v/=10;}while(z)b[n++]=r[--z];}b[n]=0;text(x,y,b,fg);}
static uint32_t slen(const char*s){uint32_t n=0;if(s)while(s[n])++n;return n;}
static void scopy(char*d,uint32_t cap,const char*s){if(!d||!cap)return;uint32_t i=0;if(s)while(i+1<cap&&s[i]){d[i]=s[i];++i;}d[i]=0;}
static void sadd(char*d,uint32_t cap,const char*s){uint32_t n=slen(d);if(!s)return;for(uint32_t i=0;s[i]&&n+1<cap;++i)d[n++]=s[i];d[n]=0;}
static void sadd_u32(char*d,uint32_t cap,uint32_t v){char r[16];int n=0;if(!v)r[n++]='0';while(v&&n<15){r[n++]=char('0'+v%10);v/=10;}char b[16];int z=0;while(n)b[z++]=r[--n];b[z]=0;sadd(d,cap,b);}
static void ip_string(char*out,uint32_t cap,const uint8_t ip[4]){if(!out||!cap)return;out[0]=0;for(int i=0;i<4;++i){sadd_u32(out,cap,ip[i]);if(i!=3)sadd(out,cap,".");}}
static bool same_ci(const char*a,const char*b){if(!a||!b)return false;for(;;++a,++b){char x=*a,y=*b;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return false;if(!x)return true;}}
static bool starts_ci(const char*a,const char*b){if(!a||!b)return false;while(*b){char x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return false;}return true;}
static bool hit(int x,int y,int l,int t,int r,int b){return x>=l&&x<r&&y>=t&&y<b;}
static void reset_double_click(){
    gLastClickTick=0;gLastClickItem=0xFFFFFFFFu;gLastClickScope=ClickScope::None;gLastClickX=gLastClickY=0;
}
static bool register_click(ClickScope scope,uint32_t item,int x,int y){
    const uint64_t now=timer_ticks();
    const int dx=x-gLastClickX,dy=y-gLastClickY;
    const bool closeEnough=(dx*dx+dy*dy)<=256; // 16 px pointer tolerance
    const bool dbl=gLastClickScope==scope&&gLastClickItem==item&&
                   now>=gLastClickTick&&(now-gLastClickTick)<=50&&closeEnough; // 500 ms @ 100 Hz
    if(dbl){reset_double_click();return true;}
    gLastClickScope=scope;gLastClickItem=item;gLastClickTick=now;gLastClickX=x;gLastClickY=y;
    return false;
}

// Explorer toolbar layout.  The classic shell opens the first window at about
// 490 px wide, while the original toolbar was laid out for ~720 px.  On narrow
// windows we wrap the commands into two rows so every button stays inside.
static constexpr int EXPLORER_BUTTON_COUNT=11;
static const int kExplorerButtonWidths[EXPLORER_BUTTON_COUNT]={48,52,42,58,56,48,58,62,62,68,72};
static const char* kExplorerButtonLabels[EXPLORER_BUTTON_COUNT]={"Back","Fwd","Up","New","Copy","Cut","Paste","Rename","Delete","Prop","Refresh"};
static bool explorer_toolbar_wrapped(int w){return w<690;}
static classic_ui::Rect explorer_button_rect(int x,int y,int w,int index){
    if(index<0||index>=EXPLORER_BUTTON_COUNT)return {0,0,0,0};
    const bool wrap=explorer_toolbar_wrapped(w);
    int row=0,start=0;
    if(wrap&&index>=6){row=1;start=6;}
    int bx=x+10;
    for(int i=start;i<index;++i)bx+=kExplorerButtonWidths[i]+4;
    return {bx,y+30+row*26,kExplorerButtonWidths[index],22};
}
static int explorer_address_y(int y,int w){return y+(explorer_toolbar_wrapped(w)?83:57);}
static int explorer_list_top(int y,int w){return explorer_address_y(y,w)+25;}
static bool launch_system_app(SystemAppId a);

static const char* app_title(SystemAppId a){switch(a){
case SystemAppId::Explorer:return "zeroNT Explorer";
case SystemAppId::Notepad:return "Untitled - Notepad";
case SystemAppId::CommandPrompt:return "Command Prompt";
case SystemAppId::ControlPanel:return "Control Panel";
case SystemAppId::TaskManager:return "Task Manager";
case SystemAppId::DeviceManager:return "Device Manager";
case SystemAppId::RecycleBin:return "Recycle Bin";
case SystemAppId::Services:return "Services";
case SystemAppId::EventViewer:return "Event Viewer";
case SystemAppId::MyComputer:return "My Computer";
case SystemAppId::NetworkNeighborhood:return "Network Neighborhood";
case SystemAppId::Documents:return "My Documents";
case SystemAppId::Printers:return "Printers";
default:return "Desktop";}}

static void status(const char* s){scopy(gStatus,sizeof(gStatus),s?s:"");gDirty=true;}
static void clear_dialog(){gDialog=DialogType::None;gDialogInput[0]=0;gDialogInputLen=0;gPending=PendingAction::None;gDirty=true;}
static void show_message(DialogType t,const char* title,const char* msg,PendingAction p=PendingAction::None){gDialog=t;scopy(gDialogTitle,sizeof(gDialogTitle),title);scopy(gDialogText,sizeof(gDialogText),msg);gPending=p;gDirty=true;}
static void show_error(const char* msg){show_message(DialogType::Error,"ZERONT ERROR",msg);}
static void show_info(const char* title,const char* msg){show_message(DialogType::Info,title,msg);}
static void show_input(DialogType t,const char* title,const char* initial=""){gDialog=t;scopy(gDialogTitle,sizeof(gDialogTitle),title);scopy(gDialogText,sizeof(gDialogText),"TYPE A NAME, THEN CLICK OK");scopy(gDialogInput,sizeof(gDialogInput),initial);gDialogInputLen=slen(gDialogInput);gDirty=true;}

static void push_cmd_line(const char*s){if(gCmdLineCount<CMD_LINES){scopy(gCmdLines[gCmdLineCount++],82,s);return;}for(uint32_t i=1;i<CMD_LINES;++i)scopy(gCmdLines[i-1],82,gCmdLines[i]);scopy(gCmdLines[CMD_LINES-1],82,s);}
struct ListCtx { Item* items; uint32_t* count; };
static void list_cb2(const Fat32DirEntry&e,void*ctx){auto*c=(ListCtx*)ctx;if(!c||!c->items||!c->count||*c->count>=MAX_ITEMS)return;auto&i=c->items[(*c->count)++];scopy(i.name,sizeof(i.name),e.name);i.attr=e.attributes;i.size=e.size;}
static bool docs_path(char*out,uint32_t cap,bool write){UserSystemStats u=user_system_stats();if(!u.loggedIn)return false;char logical[1024]{};scopy(logical,sizeof(logical),u.logicalHome);sadd(logical,sizeof(logical),"\\Documents");return user_resolve_own_path(logical,out,cap,write);}
static bool recycle_path(char*out,uint32_t cap,bool write){UserSystemStats u=user_system_stats();if(!u.loggedIn)return false;if(write&&!user_has_permission(UserPermWriteOwnFiles))return false;scopy(out,cap,u.backingHome);sadd(out,cap,"/RECYCLE");return true;}
static void join_path(char*out,uint32_t cap,const char*base,const char*name){scopy(out,cap,base);uint32_t n=slen(out);if(n&&out[n-1]!='/'&&out[n-1]!='\\')sadd(out,cap,"/");sadd(out,cap,name);}
static void strip_last(char* path){uint32_t n=slen(path);while(n&&path[n-1]=='/')path[--n]=0;while(n&&path[n-1]!='/')path[--n]=0;if(n>1)path[n-1]=0;}
static bool is_txt(const char* n){uint32_t l=slen(n);return l>=4&&n[l-4]=='.'&&((n[l-3]|32)=='t')&&((n[l-2]|32)=='x')&&((n[l-1]|32)=='t');}
static bool valid_lfn_name(const char* n){if(!n||!*n)return false;uint32_t l=slen(n);if(l>255)return false;if(n[l-1]==' '||n[l-1]=='.')return false;for(uint32_t i=0;i<l;++i){unsigned char c=(unsigned char)n[i];if(c<0x20||c==0x7F||c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return false;}return true;}
static void ensure_recycle(){char p[1024]{};if(gFat&&recycle_path(p,sizeof(p),true))fat32_mkdir(*gFat,p);}

static void history_reset(){gHistCount=1;gHistIndex=0;scopy(gHistPath[0],sizeof(gHistPath[0]),gExplorerPath);scopy(gHistLogical[0],sizeof(gHistLogical[0]),gExplorerLogical);}
static void history_push(){if(gHistIndex+1<HIST_MAX){++gHistIndex;gHistCount=gHistIndex+1;}else{for(uint32_t i=1;i<HIST_MAX;++i){scopy(gHistPath[i-1],sizeof(gHistPath[i-1]),gHistPath[i]);scopy(gHistLogical[i-1],sizeof(gHistLogical[i-1]),gHistLogical[i]);}gHistIndex=HIST_MAX-1;gHistCount=HIST_MAX;}scopy(gHistPath[gHistIndex],sizeof(gHistPath[gHistIndex]),gExplorerPath);scopy(gHistLogical[gHistIndex],sizeof(gHistLogical[gHistIndex]),gExplorerLogical);}
static void refresh_explorer(){gExplorerCount=0;if(gFat&&*gExplorerPath){ListCtx c{gExplorer,&gExplorerCount};if(!fat32_list_directory(*gFat,gExplorerPath,list_cb2,&c))status("EXPLORER: DIRECTORY READ FAILED");}if(gExplorerSel>=gExplorerCount)gExplorerSel=gExplorerCount?gExplorerCount-1:0;int relTop=explorer_list_top(0,gWin.w);int avail=gWin.h-34-relTop-10;uint32_t visible=avail>17?uint32_t(avail/17):1;if(gExplorerScroll>gExplorerSel)gExplorerScroll=gExplorerSel;if(gExplorerCount&&gExplorerSel>=gExplorerScroll+visible)gExplorerScroll=gExplorerSel-visible+1;if(gExplorerScroll>gExplorerCount)gExplorerScroll=0;}
static void refresh_recycle(){gRecycleCount=0;char p[1024]{};if(gFat&&recycle_path(p,sizeof(p),false)){fat32_mkdir(*gFat,p);ListCtx c{gRecycle,&gRecycleCount};fat32_list_directory(*gFat,p,list_cb2,&c);}if(gRecycleSel>=gRecycleCount)gRecycleSel=gRecycleCount?gRecycleCount-1:0;if(gRecycleScroll>gRecycleSel)gRecycleScroll=gRecycleSel;}
static bool selected_path(char*out,uint32_t cap){if(!gExplorerCount||gExplorerSel>=gExplorerCount)return false;join_path(out,cap,gExplorerPath,gExplorer[gExplorerSel].name);return true;}
static bool selected_recycle_path(char*out,uint32_t cap){if(!gRecycleCount||gRecycleSel>=gRecycleCount)return false;char bin[1024]{};if(!recycle_path(bin,sizeof(bin),false))return false;join_path(out,cap,bin,gRecycle[gRecycleSel].name);return true;}

static void note_set_path(const char* path,const char* name){scopy(gNotePath,sizeof(gNotePath),path);scopy(gNoteName,sizeof(gNoteName),name?name:"NOTE.TXT");}
static void note_snapshot_undo(){scopy(gUndo,sizeof(gUndo),gNote);gUndoLen=gNoteLen;gUndoValid=true;gRedoValid=false;}
static void note_restore_from(char*src,uint32_t len){if(len>=NOTE_CAP)len=NOTE_CAP-1;for(uint32_t i=0;i<len;++i)gNote[i]=src[i];gNote[len]=0;gNoteLen=len;if(gNoteCursor>gNoteLen)gNoteCursor=gNoteLen;gSelAnchor=gSelEnd=gNoteCursor;gNoteDirty=true;gDirty=true;}
static void note_undo(){if(!gUndoValid)return;scopy(gRedo,sizeof(gRedo),gNote);gRedoLen=gNoteLen;gRedoValid=true;char temp[NOTE_CAP];scopy(temp,sizeof(temp),gUndo);uint32_t n=gUndoLen;gUndoValid=false;note_restore_from(temp,n);status("NOTEPAD: UNDO");}
static void note_redo(){if(!gRedoValid)return;scopy(gUndo,sizeof(gUndo),gNote);gUndoLen=gNoteLen;gUndoValid=true;char temp[NOTE_CAP];scopy(temp,sizeof(temp),gRedo);uint32_t n=gRedoLen;gRedoValid=false;note_restore_from(temp,n);status("NOTEPAD: REDO");}
static uint32_t sel_lo(){return gSelAnchor<gSelEnd?gSelAnchor:gSelEnd;}
static uint32_t sel_hi(){return gSelAnchor>gSelEnd?gSelAnchor:gSelEnd;}
static bool has_sel(){return gSelAnchor!=gSelEnd;}
static void note_delete_range(uint32_t a,uint32_t b){if(a>b){uint32_t t=a;a=b;b=t;}if(b>gNoteLen)b=gNoteLen;if(a>=b)return;for(uint32_t i=b;i<=gNoteLen;++i)gNote[a+i-b]=gNote[i];gNoteLen-=b-a;gNoteCursor=a;gSelAnchor=gSelEnd=a;gNoteDirty=true;}
static void note_copy(){if(!has_sel())return;uint32_t a=sel_lo(),b=sel_hi();uint32_t n=b-a;if(n>=CLIP_CAP)n=CLIP_CAP-1;for(uint32_t i=0;i<n;++i)gTextClipboard[i]=gNote[a+i];gTextClipboard[n]=0;gTextClipLen=n;status("NOTEPAD: COPIED");}
static void note_cut(){if(!has_sel())return;note_snapshot_undo();note_copy();note_delete_range(sel_lo(),sel_hi());status("NOTEPAD: CUT");}
static void note_insert_bytes(const char*s,uint32_t n){if(!s||!n)return;if(has_sel())note_delete_range(sel_lo(),sel_hi());if(gNoteLen+n>=NOTE_CAP)n=NOTE_CAP-1-gNoteLen;if(!n)return;for(uint32_t i=gNoteLen+1;i>gNoteCursor;--i)gNote[i+n-1]=gNote[i-1];for(uint32_t i=0;i<n;++i)gNote[gNoteCursor+i]=s[i];gNoteLen+=n;gNoteCursor+=n;gSelAnchor=gSelEnd=gNoteCursor;gNoteDirty=true;}
static void note_paste(){if(!gTextClipLen)return;note_snapshot_undo();note_insert_bytes(gTextClipboard,gTextClipLen);status("NOTEPAD: PASTED");}
static bool load_note_path(const char*path,const char*name){if(!gFat||!path)return false;size_t got=0;gNote[0]=0;if(!fat32_read_file(*gFat,path,gNote,NOTE_CAP-1,got))return false;if(got>=NOTE_CAP)got=NOTE_CAP-1;gNoteLen=uint32_t(got);gNote[gNoteLen]=0;gNoteCursor=gNoteLen;gSelAnchor=gSelEnd=gNoteCursor;gNoteDirty=false;gUndoValid=gRedoValid=false;gNoteScroll=0;note_set_path(path,name);return true;}
static void load_default_note(){char docs[1024]{},p[1024]{};if(!docs_path(docs,sizeof(docs),false)){gNoteLen=0;gNote[0]=0;return;}join_path(p,sizeof(p),docs,"NOTE.TXT");if(!load_note_path(p,"NOTE.TXT")){gNoteLen=0;gNote[0]=0;gNoteCursor=0;gSelAnchor=gSelEnd=0;gNoteDirty=false;note_set_path(p,"NOTE.TXT");}}
static bool save_note_to(const char*path,const char*name){if(!gFat||!path||!user_has_permission(UserPermWriteOwnFiles)){show_error("ACCESS DENIED WHILE SAVING");quality_record_denied();return false;}if(!safe_fs_atomic_write(path,gNote,gNoteLen)){show_error("SAFE WRITE FAILED - ORIGINAL FILE PRESERVED");return false;}gNoteDirty=false;++gNoteSaves;note_set_path(path,name);refresh_explorer();status("NOTEPAD: VERIFIED SAFE SAVE");return true;}
static bool save_note(){return save_note_to(gNotePath,gNoteName);}

static bool file_exists_here(const char*name){for(uint32_t i=0;i<gExplorerCount;++i)if(same_ci(gExplorer[i].name,name))return true;return false;}
static bool copy_file_between(const char*src,const char*dst,bool removeSrc){if(!gFat)return false;if(!safe_fs_copy_tree(src,dst,removeSrc)){show_error("SAFE COPY/MOVE FAILED");return false;}return true;}
static bool move_to_recycle(uint32_t idx){if(!gFat||idx>=gExplorerCount)return false;char src[1024]{},bin[1024]{},dst[1024]{};join_path(src,sizeof(src),gExplorerPath,gExplorer[idx].name);if(!recycle_path(bin,sizeof(bin),true))return false;fat32_mkdir(*gFat,bin);join_path(dst,sizeof(dst),bin,gExplorer[idx].name);if(!copy_file_between(src,dst,true))return false;refresh_explorer();refresh_recycle();status("MOVED TO RECYCLE BIN");return true;}
static void restore_selected(){if(!gFat||gRecycleSel>=gRecycleCount)return;char src[1024]{},docs[1024]{},dst[1024]{};if(!selected_recycle_path(src,sizeof(src))||!docs_path(docs,sizeof(docs),true))return;join_path(dst,sizeof(dst),docs,gRecycle[gRecycleSel].name);if(copy_file_between(src,dst,true)){refresh_explorer();refresh_recycle();status("RECYCLE BIN: RESTORED");}}
static void empty_recycle(){if(!gFat)return;for(;;){refresh_recycle();if(!gRecycleCount)break;char bin[1024]{},p[1024]{};recycle_path(bin,sizeof(bin),true);join_path(p,sizeof(p),bin,gRecycle[0].name);if(!safe_fs_delete_tree(p))break;}refresh_recycle();status("RECYCLE BIN: EMPTIED");}

static void explorer_init_root(){char docs[1024]{};if(!docs_path(docs,sizeof(docs),false))return;scopy(gExplorerRoot,sizeof(gExplorerRoot),docs);scopy(gExplorerPath,sizeof(gExplorerPath),docs);auto u=user_system_stats();scopy(gExplorerLogical,sizeof(gExplorerLogical),u.logicalHome);sadd(gExplorerLogical,sizeof(gExplorerLogical),"\\Documents");history_reset();gExplorerScroll=0;refresh_explorer();}
static void explorer_navigate(const char* path,const char* logical,bool push){scopy(gExplorerPath,sizeof(gExplorerPath),path);scopy(gExplorerLogical,sizeof(gExplorerLogical),logical);gExplorerSel=0;gExplorerScroll=0;if(push)history_push();refresh_explorer();}
static void explorer_open_selected(){if(gExplorerSel>=gExplorerCount)return;char p[1024]{};selected_path(p,sizeof(p));Item&i=gExplorer[gExplorerSel];if(i.attr&0x10){char log[1024]{};scopy(log,sizeof(log),gExplorerLogical);sadd(log,sizeof(log),"\\");sadd(log,sizeof(log),i.name);explorer_navigate(p,log,true);status("EXPLORER: OPENED FOLDER");return;}if(is_txt(i.name)){if(gNoteDirty){scopy(gPendingOpenPath,sizeof(gPendingOpenPath),p);scopy(gPendingOpenName,sizeof(gPendingOpenName),i.name);show_message(DialogType::Unsaved,"UNSAVED CHANGES","SAVE CHANGES BEFORE OPENING ANOTHER FILE?",PendingAction::OpenFile);return;}if(load_note_path(p,i.name)){launch_system_app(SystemAppId::Notepad);gWin.minimized=false;status("OPENED TEXT FILE");}else show_error("COULD NOT OPEN TEXT FILE");return;}show_info("FILE ASSOCIATION","NO APPLICATION IS ASSOCIATED WITH THIS FILE YET");}
static void explorer_back(){if(gHistIndex==0)return;--gHistIndex;scopy(gExplorerPath,sizeof(gExplorerPath),gHistPath[gHistIndex]);scopy(gExplorerLogical,sizeof(gExplorerLogical),gHistLogical[gHistIndex]);gExplorerSel=gExplorerScroll=0;refresh_explorer();}
static void explorer_forward(){if(gHistIndex+1>=gHistCount)return;++gHistIndex;scopy(gExplorerPath,sizeof(gExplorerPath),gHistPath[gHistIndex]);scopy(gExplorerLogical,sizeof(gExplorerLogical),gHistLogical[gHistIndex]);gExplorerSel=gExplorerScroll=0;refresh_explorer();}
static void explorer_up(){if(same_ci(gExplorerPath,gExplorerRoot))return;char p[1024]{},l[1024]{};scopy(p,sizeof(p),gExplorerPath);scopy(l,sizeof(l),gExplorerLogical);strip_last(p);uint32_t n=slen(l);while(n&&l[n-1]!='\\')l[--n]=0;if(n)l[n-1]=0;if(slen(p)<slen(gExplorerRoot))scopy(p,sizeof(p),gExplorerRoot);explorer_navigate(p,l,true);}
static void explorer_copy(bool cut){if(gExplorerSel>=gExplorerCount)return;char src[1024]{};selected_path(src,sizeof(src));scopy(gFileClipName,sizeof(gFileClipName),gExplorer[gExplorerSel].name);scopy(gFileClipSource,sizeof(gFileClipSource),src);gFileClipCut=cut;status(cut?"EXPLORER: CUT READY":"EXPLORER: COPIED");}
static void explorer_paste(){if(!gFileClipName[0])return;char dst[1024]{};join_path(dst,sizeof(dst),gExplorerPath,gFileClipName);if(same_ci(dst,gFileClipSource)){show_error("SOURCE AND DESTINATION ARE THE SAME");return;}if(!safe_fs_copy_tree(gFileClipSource,dst,gFileClipCut)){show_error("SAFE TREE COPY/MOVE FAILED");return;}if(gFileClipCut)gFileClipCut=false;refresh_explorer();status("EXPLORER: PASTE COMPLETE");}
static void explorer_delete_selected(){if(gExplorerSel>=gExplorerCount)return;show_message(DialogType::ConfirmDelete,"DELETE","MOVE SELECTED ITEM TO RECYCLE BIN?",PendingAction::DeleteExplorer);}
static void explorer_properties(){if(gExplorerSel>=gExplorerCount)return;char msg[96]{};scopy(msg,sizeof(msg),gExplorer[gExplorerSel].name);sadd(msg,sizeof(msg),(gExplorer[gExplorerSel].attr&0x10)?"  <DIR>":"  FILE");show_message(DialogType::Properties,"PROPERTIES",msg);}

static void open_app_direct(SystemAppId a){
    gActive=a;gStart=false;gStartSubmenu=0;gContext=false;gWin.minimized=false;
    if(a==SystemAppId::MyComputer||a==SystemAppId::NetworkNeighborhood||a==SystemAppId::Documents||a==SystemAppId::Printers){
        const int sw=int(gBoot->width),workH=int(gBoot->height)-28;
        gWin.w=(sw*58)/100;gWin.h=(workH*58)/100;
        if(gWin.w<490)gWin.w=490;if(gWin.h<335)gWin.h=335;
        if(gWin.w>900)gWin.w=900;if(gWin.h>620)gWin.h=620;
        if(gWin.w>sw-20)gWin.w=sw-20;if(gWin.h>workH-16)gWin.h=workH-16;
        gWin.x=(sw-gWin.w)/2;gWin.y=(workH-gWin.h)/2;if(gWin.y<8)gWin.y=8;
        gWin.restoreX=gWin.x;gWin.restoreY=gWin.y;gWin.restoreW=gWin.w;gWin.restoreH=gWin.h;gWin.maximized=false;
    }else if(gWin.w<360||gWin.h<240){
        const int sw=int(gBoot->width),workH=int(gBoot->height)-28;
        gWin.w=(sw*78)/100;gWin.h=(workH*76)/100;
        if(gWin.w<480)gWin.w=480;if(gWin.h<320)gWin.h=320;
        if(gWin.w>1280)gWin.w=1280;if(gWin.h>820)gWin.h=820;
        if(gWin.w>sw-20)gWin.w=sw-20;if(gWin.h>workH-16)gWin.h=workH-16;
    }
    if(a==SystemAppId::Explorer){explorer_init_root();}else if(a==SystemAppId::Notepad){if(!gNotePath[0])load_default_note();}else if(a==SystemAppId::RecycleBin)refresh_recycle();
    gDirty=true;
}
static const char* exe_path_for_app(SystemAppId a){
    switch(a){
        case SystemAppId::Explorer: return "/SYSTEM32/EXPLORER.EXE";
        case SystemAppId::Notepad: return "/SYSTEM32/NOTEPAD.EXE";
        case SystemAppId::CommandPrompt: return "/SYSTEM32/CMD.EXE";
        case SystemAppId::ControlPanel: return "/SYSTEM32/CONTROL.EXE";
        case SystemAppId::TaskManager: return "/SYSTEM32/TASKMGR.EXE";
        case SystemAppId::DeviceManager: return "/SYSTEM32/DEVMGMT.EXE";
        case SystemAppId::RecycleBin: return "/SYSTEM32/RECYCLE.EXE";
        case SystemAppId::Services: return "/SYSTEM32/SERVICES.EXE";
        case SystemAppId::EventViewer: return "/SYSTEM32/EVENTVWR.EXE";
        case SystemAppId::MyComputer: return "/SYSTEM32/MYCOMP.EXE";
        case SystemAppId::NetworkNeighborhood: return "/SYSTEM32/NETWORK.EXE";
        case SystemAppId::Documents: return "/SYSTEM32/MYDOCS.EXE";
        case SystemAppId::Printers: return "/SYSTEM32/PRINTERS.EXE";
        default: return nullptr;
    }
}

static bool launch_system_app(SystemAppId a){
    if(a==SystemAppId::Desktop){open_app_direct(a);return true;}
    const char* path=exe_path_for_app(a);
    if(!gFat||!path){open_app_direct(a);return false;}
    ExecutableStats ex{};
    if(executable_run(*gFat,path,ex) && ex.ran && ex.exitCode==0){
        gStart=false;gStartSubmenu=0;gContext=false;
        status(ex.running?"NATIVE RING-3 .EXE STARTED":"APPLICATION COMPLETED");
        gDirty=true;
        return true;
    }
    // Beta safety fallback: if the external image is missing/corrupt, keep the
    // desktop usable and make the failure visible instead of losing the app.
    open_app_direct(a);
    status("EXE LOAD FAILED - INTERNAL FALLBACK USED");
    return false;
}

static void close_active_force(){gActive=SystemAppId::Desktop;gWin.minimized=false;gWin.dragging=gWin.resizing=false;gContext=false;gDirty=true;}
static void request_close(){if(gActive==SystemAppId::Notepad&&gNoteDirty){show_message(DialogType::Unsaved,"UNSAVED CHANGES","SAVE CHANGES BEFORE CLOSING?",PendingAction::CloseApp);return;}close_active_force();}
static void win_defaults(){
    const int sw=int(gBoot->width),workH=int(gBoot->height)-28;
    gWin.w=(sw*78)/100;gWin.h=(workH*76)/100;
    if(gWin.w<480)gWin.w=480;if(gWin.h<320)gWin.h=320;
    if(gWin.w>1280)gWin.w=1280;if(gWin.h>820)gWin.h=820;
    if(gWin.w>sw-20)gWin.w=sw-20;if(gWin.h>workH-16)gWin.h=workH-16;
    gWin.x=(sw-gWin.w)/2;gWin.y=(workH-gWin.h)/2;if(gWin.y<4)gWin.y=4;
    gWin.restoreX=gWin.x;gWin.restoreY=gWin.y;gWin.restoreW=gWin.w;gWin.restoreH=gWin.h;
    gWin.minimized=false;gWin.maximized=false;gWin.dragging=false;gWin.resizing=false;
}
static void win_max_toggle(){if(!gWin.maximized){gWin.restoreX=gWin.x;gWin.restoreY=gWin.y;gWin.restoreW=gWin.w;gWin.restoreH=gWin.h;gWin.x=2;gWin.y=2;gWin.w=int(gBoot->width)-4;gWin.h=int(gBoot->height)-32;gWin.maximized=true;}else{gWin.x=gWin.restoreX;gWin.y=gWin.restoreY;gWin.w=gWin.restoreW;gWin.h=gWin.restoreH;gWin.maximized=false;}gDirty=true;}
static void clamp_window(){int sw=int(gBoot->width),sh=int(gBoot->height)-28;if(gWin.w<360)gWin.w=360;if(gWin.h<240)gWin.h=240;if(gWin.w>sw)gWin.w=sw;if(gWin.h>sh)gWin.h=sh;if(gWin.x<0)gWin.x=0;if(gWin.y<0)gWin.y=0;if(gWin.x+gWin.w>sw)gWin.x=sw-gWin.w;if(gWin.y+gWin.h>sh)gWin.y=sh-gWin.h;}

static uint32_t cmd_number(const char*s){uint32_t v=0;if(!s)return 0;while(*s==' ')++s;while(*s>='0'&&*s<='9'){uint32_t n=v*10u+uint32_t(*s-'0');if(n<v)return v;v=n;++s;}return v;}
static void cmd_make_path(char*out,uint32_t cap,const char*arg){if(!out||!cap)return;if(arg&&(arg[0]=='/'||arg[0]=='\\'))scopy(out,cap,arg);else join_path(out,cap,gExplorerPath,arg?arg:"");}
static bool cmd_two_args(const char*s,char*a,uint32_t ac,char*b,uint32_t bc){if(!s||!a||!b)return false;while(*s==' ')++s;uint32_t i=0;while(*s&&*s!=' '&&i+1<ac)a[i++]=*s++;a[i]=0;while(*s==' ')++s;i=0;while(*s&&*s!=' '&&i+1<bc)b[i++]=*s++;b[i]=0;return a[0]&&b[0];}

static void cmd_exec(){
    ++gCommands;
    if(!gCmdLen){push_cmd_line("");return;}
    char echo[82]{};scopy(echo,sizeof(echo),"> ");sadd(echo,sizeof(echo),gCmd);push_cmd_line(echo);
    if(same_ci(gCmd,"HELP")){push_cmd_line("HELP VER MODE WHOAMI PWD DIR APPS PS FREE MEM DRIVERS HEALTH BETA EVENTS");push_cmd_line("MKDIR RMDIR DEL COPY MOVE FSCK NVME USB AUDIO VOL TONE");push_cmd_line("IPCONFIG DHCP DNSFLUSH NETSTAT PING NSLOOKUP TCPTEST TCPSTAT HTTPGET");push_cmd_line("AUDIO VOL MUTE SOUND TONE RUN CLS ECHO");}
    else if(same_ci(gCmd,"WHOAMI")){auto u=user_system_stats();push_cmd_line(u.displayName);}
    else if(same_ci(gCmd,"VER"))push_cmd_line("zeroNT v33.0 Beta Dynamic Resolution");
    else if(same_ci(gCmd,"MODE")){char b[82]="DISPLAY: ";sadd_u32(b,sizeof(b),gBoot?gBoot->width:0);sadd(b,sizeof(b),"x");sadd_u32(b,sizeof(b),gBoot?gBoot->height:0);push_cmd_line(b);char p[82]="PITCH: ";sadd_u32(p,sizeof(p),gBoot?gBoot->pixelsPerScanLine:0);push_cmd_line(p);}
    else if(same_ci(gCmd,"PWD"))push_cmd_line(gExplorerLogical);
    else if(same_ci(gCmd,"DIR")){refresh_explorer();for(uint32_t i=0;i<gExplorerCount&&i<8;++i)push_cmd_line(gExplorer[i].name);if(!gExplorerCount)push_cmd_line("(EMPTY)");}
    else if(same_ci(gCmd,"APPS")){struct C{uint32_t n;};C c{};push_cmd_line("SYSTEM32 EXECUTABLES:");fat32_list_directory(*gFat,"/SYSTEM32",[](const Fat32DirEntry&e,void*x){auto*c=static_cast<C*>(x);if(c->n<8)push_cmd_line(e.name);++c->n;},&c);if(!c.n)push_cmd_line("(NO SYSTEM EXES)");}
    else if(same_ci(gCmd,"PS")){auto p=process_stats();char a[82]="PROCESSES: ";sadd_u32(a,sizeof(a),p.processCount);push_cmd_line(a);char b[82]="RUNNING: ";sadd_u32(b,sizeof(b),p.runningCount);push_cmd_line(b);char c[82]="EXITED: ";sadd_u32(c,sizeof(c),p.exitedCount);push_cmd_line(c);char d[82]="REAPED: ";sadd_u32(d,sizeof(d),static_cast<uint32_t>(p.reapedCount));push_cmd_line(d);}
    else if(same_ci(gCmd,"FREE")){uint64_t bytes=fat32_free_bytes(*gFat);char b[82]="FREE DISK KB: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(bytes/1024));push_cmd_line(b);}
    else if(same_ci(gCmd,"MEM")){auto m=pmm_stats();auto h=heap_stats();auto v=vmm_stats();auto sc=scheduler_stats();char a[82]="PMM FREE KB: ";sadd_u32(a,sizeof(a),static_cast<uint32_t>(m.freeBytes/1024));push_cmd_line(a);char b[82]="PMM OOM FAILS: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(m.allocationFailures));push_cmd_line(b);char c[82]="HEAP FREE KB: ";sadd_u32(c,sizeof(c),static_cast<uint32_t>(h.freeBytes/1024));push_cmd_line(c);char d[82]="HEAP FAILS: ";sadd_u32(d,sizeof(d),static_cast<uint32_t>(h.allocationFailures));push_cmd_line(d);char e[82]="USER PAGES RECLAIMED: ";sadd_u32(e,sizeof(e),static_cast<uint32_t>(v.reclaimedUserPages));push_cmd_line(e);char f[82]="GUARDED KSTACKS: ";sadd_u32(f,sizeof(f),sc.guardedStacks);push_cmd_line(f);push_cmd_line((pmm_validate()&&heap_validate())?"MEMORY VALIDATION: OK":"MEMORY VALIDATION: FAILED");}
    else if(starts_ci(gCmd,"MKDIR ")){char p[1024]{};cmd_make_path(p,sizeof(p),gCmd+6);if(fat32_mkdir(*gFat,p)){refresh_explorer();push_cmd_line("DIRECTORY CREATED");}else push_cmd_line("MKDIR FAILED");}
    else if(starts_ci(gCmd,"RMDIR ")){char p[1024]{};cmd_make_path(p,sizeof(p),gCmd+6);if(safe_fs_delete_tree(p)){refresh_explorer();push_cmd_line("DIRECTORY TREE DELETED");}else push_cmd_line("RMDIR FAILED");}
    else if(starts_ci(gCmd,"DEL ")){char p[1024]{};cmd_make_path(p,sizeof(p),gCmd+4);if(safe_fs_delete_tree(p)){refresh_explorer();push_cmd_line("DELETED");}else push_cmd_line("DELETE FAILED");}
    else if(starts_ci(gCmd,"COPY ")||starts_ci(gCmd,"MOVE ")){bool mv=starts_ci(gCmd,"MOVE ");char a[256]{},b[256]{},sa[1024]{},sb[1024]{};if(!cmd_two_args(gCmd+5,a,sizeof(a),b,sizeof(b)))push_cmd_line("USAGE: COPY SRC DST / MOVE SRC DST");else{cmd_make_path(sa,sizeof(sa),a);cmd_make_path(sb,sizeof(sb),b);if(safe_fs_copy_tree(sa,sb,mv)){refresh_explorer();push_cmd_line(mv?"MOVE COMPLETE":"COPY COMPLETE");}else push_cmd_line(mv?"MOVE FAILED":"COPY FAILED");}}
    else if(same_ci(gCmd,"DRIVERS")){auto d=driver_manager_stats();char b[82]="PCI DEVICES: ";sadd_u32(b,sizeof(b),d.pciDevices);push_cmd_line(b);char m[82]="MSI/MSIX DEVICES: ";sadd_u32(m,sizeof(m),d.msiDevices);sadd(m,sizeof(m),"/");sadd_u32(m,sizeof(m),d.msixDevices);push_cmd_line(m);push_cmd_line(d.localApic?"LOCAL APIC: READY":"LOCAL APIC: FALLBACK");push_cmd_line(d.hpet?"HPET: RUNNING":"HPET: PIT FALLBACK");auto a=ahci_stats();char q[82]="AHCI RETRIES: ";sadd_u32(q,sizeof(q),static_cast<uint32_t>(a.ioRetries));push_cmd_line(q);auto nv=nvme_stats();push_cmd_line(nv.ioQueueReady?"NVME: I/O READY":(nv.found?"NVME: FOUND / NOT ACTIVE":"NVME: NOT FOUND"));auto x=xhci_stats();push_cmd_line(x.running?"XHCI: RUNNING":(x.found?"XHCI: INIT FAILED":"XHCI: NOT FOUND"));}
    else if(same_ci(gCmd,"NVME")){auto n=nvme_stats();push_cmd_line(n.ioQueueReady?"NVME: NAMESPACE READY":(n.found?"NVME: FOUND":"NVME: NOT FOUND"));char a[82]="SECTOR SIZE: ";sadd_u32(a,sizeof(a),n.sectorSize);push_cmd_line(a);char b[82]="READ CMDS: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(n.readCommands));push_cmd_line(b);char c[82]="WRITE CMDS: ";sadd_u32(c,sizeof(c),static_cast<uint32_t>(n.writeCommands));push_cmd_line(c);}
    else if(same_ci(gCmd,"USB")){auto x=xhci_stats();push_cmd_line(x.running?"XHCI: RUNNING":(x.found?"XHCI: NOT RUNNING":"XHCI: NOT FOUND"));char a[82]="ROOT PORTS: ";sadd_u32(a,sizeof(a),x.maxPorts);push_cmd_line(a);char b[82]="CONNECTED: ";sadd_u32(b,sizeof(b),x.connectedPorts);push_cmd_line(b);char c[82]="HOTPLUG EVENTS: ";sadd_u32(c,sizeof(c),static_cast<uint32_t>(x.hotplugEvents));push_cmd_line(c);}
    else if(same_ci(gCmd,"HEALTH")){auto q=quality_stats();auto f=safe_fs_stats();auto sec=security_stats();char b[82]="USER FAULTS RECOVERED: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(q.userFaultsRecovered));push_cmd_line(b);char c[82]="SAFE WRITES: ";sadd_u32(c,sizeof(c),static_cast<uint32_t>(f.atomicWrites));push_cmd_line(c);char d[82]="DENIED SYSCALLS: ";sadd_u32(d,sizeof(d),static_cast<uint32_t>(sec.deniedSyscalls));push_cmd_line(d);char e[82]="TREE OPS: ";sadd_u32(e,sizeof(e),static_cast<uint32_t>(f.treeCopies+f.treeMoves+f.treeDeletes));push_cmd_line(e);}
    else if(same_ci(gCmd,"BETA")){auto b=beta_stats();char a[82]="UPTIME TICKS: ";sadd_u32(a,sizeof(a),static_cast<uint32_t>(b.uptimeTicks));push_cmd_line(a);char m[82]="FREE MEM LOW KB: ";sadd_u32(m,sizeof(m),static_cast<uint32_t>(b.freeMemoryLowWater/1024));push_cmd_line(m);push_cmd_line(b.readyForSoak?"BETA HEALTH: READY FOR SOAK TEST":"BETA HEALTH: WARNINGS PRESENT");}
    else if(same_ci(gCmd,"EVENTS")){auto e=event_log_stats();char a[82]="EVENTS: ";sadd_u32(a,sizeof(a),static_cast<uint32_t>(e.events));push_cmd_line(a);char b[82]="ERRORS/CRITICAL: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(e.errors+e.critical));push_cmd_line(b);push_cmd_line(event_log_last_message());}
    else if(same_ci(gCmd,"FSCK")){bool ok=safe_fs_check_volume()&&safe_fs_recover_pending();push_cmd_line(ok?"FILESYSTEM CHECK: OK":"FILESYSTEM CHECK: FAILED");}
    else if(same_ci(gCmd,"NET")||same_ci(gCmd,"IPCONFIG")){auto n=network_stats();push_cmd_line(n.linkUp?"NETWORK: LINK UP":"NETWORK: NO LINK");push_cmd_line(n.staticConfigured?"CONFIG: STATIC":(n.dhcpBound?"CONFIG: DHCP BOUND":(n.dhcpFailed?"CONFIG: DHCP FAILED":"CONFIG: DHCP WAITING")));char b[82]="IP: ";char ip[24]{};ip_string(ip,sizeof(ip),n.ipv4);sadd(b,sizeof(b),ip);push_cmd_line(b);char gtw[82]="GATEWAY: ";ip_string(ip,sizeof(ip),n.gateway);sadd(gtw,sizeof(gtw),ip);push_cmd_line(gtw);char d[82]="DNS: ";ip_string(ip,sizeof(ip),n.dns);sadd(d,sizeof(d),ip);push_cmd_line(d);}
    else if(same_ci(gCmd,"DHCP")){push_cmd_line(network_restart_dhcp()?"DHCP DISCOVERY RESTARTED":"DHCP RESTART FAILED");}
    else if(same_ci(gCmd,"DNSFLUSH")){network_dns_cache_flush();push_cmd_line("DNS CACHE FLUSHED");}
    else if(same_ci(gCmd,"NETSTAT")){auto n=network_stats();char a[82]="TCP/UDP SOCKETS: ";sadd_u32(a,sizeof(a),n.openTcpSockets);sadd(a,sizeof(a),"/");sadd_u32(a,sizeof(a),n.openUdpSockets);push_cmd_line(a);char b[82]="TCP RETRANSMITS: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(n.tcpRetransmits));push_cmd_line(b);char c[82]="DNS CACHE HITS: ";sadd_u32(c,sizeof(c),static_cast<uint32_t>(n.dnsCacheHits));push_cmd_line(c);char d[82]="LINK CHANGES: ";sadd_u32(d,sizeof(d),static_cast<uint32_t>(n.linkTransitions));push_cmd_line(d);}
    else if(same_ci(gCmd,"PING")){bool ok=network_ping_gateway_wait();push_cmd_line(ok?"PING REPLY FROM GATEWAY":"PING TIMEOUT/FAILED");}
    else if(starts_ci(gCmd,"NSLOOKUP ")){const char*host=gCmd+9;uint8_t ip4[4]{};if(network_dns_query(host,ip4)){char b[82]="DNS A: ";char ip[24]{};ip_string(ip,sizeof(ip),ip4);sadd(b,sizeof(b),ip);push_cmd_line(b);}else push_cmd_line("DNS LOOKUP FAILED/TIMED OUT");}
    else if(same_ci(gCmd,"TCPTEST")){auto n=network_stats();bool ok=network_tcp_probe_wait(n.gateway,80);push_cmd_line(ok?"TCP RESPONSE FROM GATEWAY:80":"TCP PROBE TIMEOUT/FAILED");}
    else if(same_ci(gCmd,"TCPSTAT")){auto t=network_tcp_client_stats();push_cmd_line(t.state==TcpClientState::Established?"TCP CLIENT: CONNECTED":(t.state==TcpClientState::Closed?"TCP CLIENT: IDLE":"TCP CLIENT: ACTIVE"));char b[82]="TX/RX BYTES: ";auto n=network_stats();sadd_u32(b,sizeof(b),static_cast<uint32_t>(n.tcpBytesSent));sadd(b,sizeof(b),"/");sadd_u32(b,sizeof(b),static_cast<uint32_t>(n.tcpBytesReceived));push_cmd_line(b);}
    else if(starts_ci(gCmd,"HTTPGET ")){static uint8_t response[1024];uint16_t got=0;if(network_http_get(gCmd+8,"/",response,sizeof(response),got)){char b[82]="HTTP BYTES: ";sadd_u32(b,sizeof(b),got);push_cmd_line(b);char first[82]{};uint32_t j=0;while(j+1<sizeof(first)&&j<got&&response[j]!='\r'&&response[j]!='\n'){first[j]=static_cast<char>(response[j]);++j;}first[j]=0;if(j)push_cmd_line(first);}else push_cmd_line("HTTP GET FAILED/TIMED OUT");}
    else if(same_ci(gCmd,"AUDIO")){auto a=audio_stats();push_cmd_line(a.initialized?"AUDIO: READY":(a.backend==AudioBackend::HdaDetectedUnsupported?"AUDIO: HDA DETECTED / DRIVER PENDING":"AUDIO: NOT FOUND"));char b[82]="VOLUME: ";sadd_u32(b,sizeof(b),a.volumePercent);sadd(b,sizeof(b),a.muted?"% MUTED":"%");push_cmd_line(b);char q[82]="QUEUED FRAMES: ";sadd_u32(q,sizeof(q),a.queuedFrames);push_cmd_line(q);}
    else if(starts_ci(gCmd,"VOL ")){uint32_t v=cmd_number(gCmd+4);if(v>100)v=100;push_cmd_line(audio_set_volume(static_cast<uint8_t>(v))?"VOLUME UPDATED":"AUDIO NOT READY");}
    else if(same_ci(gCmd,"MUTE")||same_ci(gCmd,"MUTE ON")){push_cmd_line(audio_set_mute(true)?"AUDIO MUTED":"AUDIO NOT READY");}
    else if(same_ci(gCmd,"MUTE OFF")){push_cmd_line(audio_set_mute(false)?"AUDIO UNMUTED":"AUDIO NOT READY");}
    else if(starts_ci(gCmd,"SOUND ")){const char*which=gCmd+6;SystemSound snd=SystemSound::Notify;if(same_ci(which,"STARTUP"))snd=SystemSound::Startup;else if(same_ci(which,"ERROR"))snd=SystemSound::Error;else if(same_ci(which,"CLICK"))snd=SystemSound::Click;push_cmd_line(audio_play_system_sound(snd)?"SYSTEM SOUND QUEUED":"SYSTEM SOUND FAILED");}
    else if(starts_ci(gCmd,"TONE ")){uint32_t hz=cmd_number(gCmd+5);push_cmd_line(audio_play_tone(hz,100)?"TONE QUEUED":"TONE FAILED");}
    else if(starts_ci(gCmd,"RUN ")){
        const char* arg=gCmd+4; char path[96]="/SYSTEM32/"; sadd(path,sizeof(path),arg);
        uint32_t l=slen(path); bool hasDot=false; for(uint32_t i=0;i<l;++i)if(path[i]=='.')hasDot=true; if(!hasDot)sadd(path,sizeof(path),".EXE");
        ExecutableStats ex{};
        bool ok=executable_run(*gFat,path,ex);
        if(!ok){path[0]=0;scopy(path,sizeof(path),"/APPS/");sadd(path,sizeof(path),arg);l=slen(path);hasDot=false;for(uint32_t i=0;i<l;++i)if(path[i]=='.')hasDot=true;if(!hasDot)sadd(path,sizeof(path),".ZEX");ok=executable_run(*gFat,path,ex);}
        if(ok){char b[82]="APP EXIT CODE: ";sadd_u32(b,sizeof(b),static_cast<uint32_t>(ex.exitCode));push_cmd_line(b);}else push_cmd_line("APP LOAD/RUN FAILED");
    }
    else if(same_ci(gCmd,"CLS")){gCmdLineCount=0;}
    else if(starts_ci(gCmd,"ECHO "))push_cmd_line(gCmd+5);
    else push_cmd_line("BAD COMMAND. TYPE HELP.");
    gCmdLen=0;gCmd[0]=0;
}

static void frame(int x,int y,int w,int h,const char*title){
    classic_ui::window_frame({x,y,w,h},title,true,true,true,true);
    // Classic resize grip.
    for(int i=0;i<3;++i){
        classic_ui::hline(x+w-4-i*3,x+w-2,y+h-3-i*3,classic_ui::SHADOW);
        classic_ui::hline(x+w-3-i*3,x+w-2,y+h-2-i*3,classic_ui::HILIGHT);
    }
}
static void button(int x,int y,int w,int h,const char*label){
    bool pressed=hit(gMouseX,gMouseY,x,y,x+w,y+h)&&((gMouseButtons&1)!=0);
    classic_ui::button({x,y,w,h},label,pressed,false);
}
static int taskbar_y(){ return int(gBoot->height)-28; }
static classic_ui::Rect classic_start_menu_rect(){ return {2,taskbar_y()-236,205,236}; }
static classic_ui::Rect classic_programs_rect(){ classic_ui::Rect m=classic_start_menu_rect(); return {m.x+m.w-2,m.y+2,188,150}; }
static classic_ui::Rect classic_settings_rect(){ classic_ui::Rect m=classic_start_menu_rect(); return {m.x+m.w-2,m.y+58,180,94}; }

static void draw_taskbar(){
    int ty=taskbar_y();
    classic_ui::fill({0,ty,(int)gBoot->width,28},classic_ui::FACE);
    classic_ui::hline(0,(int)gBoot->width-1,ty,classic_ui::HILIGHT);
    classic_ui::hline(0,(int)gBoot->width-1,ty+1,classic_ui::LIGHT);

    bool startPressed=gStart||(hit(gMouseX,gMouseY,2,ty+2,60,ty+26)&&((gMouseButtons&1)!=0));
    classic_ui::button({2,ty+2,58,24},"",startPressed,false);
    classic_ui::draw_start_mark(7,ty+7);
    classic_ui::text(27+(startPressed?1:0),ty+10+(startPressed?1:0),"Start",classic_ui::BLACK);

    if(gActive!=SystemAppId::Desktop){
        bool appPressed=!gWin.minimized;
        int maxW=int(gBoot->width)-60-86-8;
        int appW=maxW>150?150:maxW;
        if(appW<70)appW=70;
        classic_ui::button({64,ty+2,appW,24},"",appPressed,false);
        classic_ui::draw_window_icon(69,ty+6);
        const char* title=app_title(gActive);
        char clipped[22]{}; int cap=(appW-31)/6; if(cap>20)cap=20; if(cap<1)cap=1;
        int n=0; while(title&&title[n]&&n<cap){clipped[n]=title[n];++n;} clipped[n]=0;
        classic_ui::text(90+(appPressed?1:0),ty+10+(appPressed?1:0),clipped,classic_ui::BLACK);
    }

    int tx=int(gBoot->width)-83;
    classic_ui::fill({tx,ty+3,80,22},classic_ui::FACE);
    classic_ui::frame3d({tx,ty+3,80,22},true);
    uint64_t secs=timer_ticks()/100ULL;
    uint32_t hh=(uint32_t)((secs/3600ULL)%24ULL), mm=(uint32_t)((secs/60ULL)%60ULL);
    char clock[6]={(char)('0'+hh/10),(char)('0'+hh%10),':',(char)('0'+mm/10),(char)('0'+mm%10),0};
    classic_ui::text(tx+25,ty+11,clock,classic_ui::BLACK);
}

static void draw_original_desktop_icon(int x,int y,const char*label,int kind,int index){
    bool hover=hit(gMouseX,gMouseY,x,y,x+92,y+58);bool selected=(gDesktopSel==index);
    if(kind==0) classic_ui::draw_computer_icon(x+9,y);
    else if(kind==1) classic_ui::draw_network_icon(x+8,y+2);
    else if(kind==2) classic_ui::draw_trash_icon(x+8,y+2);
    else classic_ui::draw_folder_icon(x+8,y+3);
    int tw=classic_ui::text_width(label);
    if(hover||selected) classic_ui::fill({x+2,y+31,tw+4,12},classic_ui::ACTIVEBLUE);
    classic_ui::text(x+4,y+34,label,classic_ui::WHITE);
}

static void draw_start_item(classic_ui::Rect r,const char*label,bool arrow,int kind){
    bool hover=hit(gMouseX,gMouseY,r.x,r.y,r.x+r.w,r.y+r.h);
    if(hover)classic_ui::fill(r,classic_ui::ACTIVEBLUE);
    if(kind==0)classic_ui::draw_folder_icon(r.x+5,r.y+2);
    else if(kind==1)classic_ui::draw_computer_icon(r.x+5,r.y+3);
    else if(kind==2)classic_ui::draw_network_icon(r.x+5,r.y+3);
    else if(kind==3)classic_ui::draw_trash_icon(r.x+5,r.y+3);
    else { classic_ui::fill({r.x+11,r.y+7,14,14},classic_ui::WHITE); classic_ui::frame3d({r.x+11,r.y+7,14,14},false); }
    classic_ui::text(r.x+38,r.y+10,label,hover?classic_ui::WHITE:classic_ui::BLACK);
    if(arrow){
        classic_ui::Color c=hover?classic_ui::WHITE:classic_ui::BLACK;
        for(int i=0;i<5;++i){int half=i<=2?i:4-i;for(int k=0;k<=half;++k)classic_ui::pixel(r.x+r.w-12+i,r.y+12-half+2*k,c);}
    }
}

static void draw_original_start_menu(){
    if(!gStart)return;
    classic_ui::Rect m=classic_start_menu_rect();
    classic_ui::fill(m,classic_ui::FACE); classic_ui::frame3d(m,false);
    classic_ui::fill({m.x+3,m.y+3,21,m.h-6},classic_ui::SHADOW);
    classic_ui::text(m.x+7,m.y+10,"0",classic_ui::WHITE,2);
    classic_ui::text(m.x+4,m.y+m.h-17,"zeroNT",classic_ui::WHITE);
    const int firstY=m.y+5;
    const int ys[]={firstY,firstY+28,firstY+56,firstY+84,firstY+112,firstY+140,firstY+174,firstY+202};
    draw_start_item({m.x+24,ys[0],m.w-26,28},"Programs",true,0);
    draw_start_item({m.x+24,ys[1],m.w-26,28},"Documents",false,0);
    draw_start_item({m.x+24,ys[2],m.w-26,28},"Settings",true,1);
    draw_start_item({m.x+24,ys[3],m.w-26,28},"Find",false,4);
    draw_start_item({m.x+24,ys[4],m.w-26,28},"Help",false,4);
    draw_start_item({m.x+24,ys[5],m.w-26,28},"Run...",false,4);
    classic_ui::hline(m.x+29,m.x+m.w-7,ys[6]-3,classic_ui::SHADOW);
    classic_ui::hline(m.x+29,m.x+m.w-7,ys[6]-2,classic_ui::HILIGHT);
    draw_start_item({m.x+24,ys[6],m.w-26,28},"Log Off...",false,4);
    draw_start_item({m.x+24,ys[7],m.w-26,28},"Shut Down...",false,4);

    if(gStartSubmenu==1){
        classic_ui::Rect p=classic_programs_rect(); classic_ui::fill(p,classic_ui::FACE); classic_ui::frame3d(p,false);
        const char* a[]={"zeroNT Explorer","Command Prompt","Notepad","Task Manager","Event Viewer"};
        for(int i=0;i<5;++i)draw_start_item({p.x+3,p.y+3+i*28,p.w-6,28},a[i],false,i==0?0:(i==2?4:1));
    } else if(gStartSubmenu==2){
        classic_ui::Rect q=classic_settings_rect(); classic_ui::fill(q,classic_ui::FACE); classic_ui::frame3d(q,false);
        const char* a[]={"Control Panel","Device Manager","Services"};
        for(int i=0;i<3;++i)draw_start_item({q.x+3,q.y+3+i*28,q.w-6,28},a[i],false,1);
    }
}

static void draw_desktop(){
    classic_ui::fill({0,0,(int)gBoot->width,(int)gBoot->height},classic_ui::DESKTOP);
    draw_original_desktop_icon(8,18,"MY COMPUTER",0,0);
    draw_original_desktop_icon(8,80,"NETWORK",1,1);
    draw_original_desktop_icon(8,142,"RECYCLE BIN",2,2);
    draw_original_desktop_icon(8,204,"MY DOCUMENTS",3,3);
    draw_taskbar();
    draw_original_start_menu();
}

static void draw_my_computer(int x,int y,int w,int h){
    int menuY=y+23;
    classic_ui::fill({x+3,menuY,w-6,19},classic_ui::FACE);
    classic_ui::text(x+9,menuY+6,"FILE",classic_ui::BLACK);
    classic_ui::text(x+52,menuY+6,"EDIT",classic_ui::BLACK);
    classic_ui::text(x+96,menuY+6,"VIEW",classic_ui::BLACK);
    classic_ui::text(x+142,menuY+6,"HELP",classic_ui::BLACK);
    int top=menuY+19, statusY=y+h-22;
    classic_ui::fill({x+4,top,w-8,statusY-top},classic_ui::WHITE); classic_ui::frame3d({x+4,top,w-8,statusY-top},true);
    classic_ui::draw_computer_icon(x+31,top+25); classic_ui::text(x+16,top+57,"3.5 FLOPPY (A:)",classic_ui::BLACK);
    classic_ui::draw_computer_icon(x+166,top+25); classic_ui::text(x+158,top+57,"LOCAL DISK (C:)",classic_ui::BLACK);
    classic_ui::draw_computer_icon(x+303,top+25); classic_ui::text(x+292,top+57,"CD-ROM (D:)",classic_ui::BLACK);
    classic_ui::draw_folder_icon(x+31,top+103); classic_ui::text(x+16,top+135,"CONTROL PANEL",classic_ui::BLACK);
    classic_ui::draw_folder_icon(x+166,top+103); classic_ui::text(x+158,top+135,"PRINTERS",classic_ui::BLACK);
    classic_ui::fill({x+5,statusY,w-10,17},classic_ui::FACE); classic_ui::frame3d({x+5,statusY,w-10,17},true); classic_ui::text(x+10,statusY+5,"5 OBJECT(S)",classic_ui::BLACK);
}

static void draw_network_neighborhood(int x,int y,int w,int h){
    int top=y+42,statusY=y+h-22; classic_ui::fill({x+4,top,w-8,statusY-top},classic_ui::WHITE);classic_ui::frame3d({x+4,top,w-8,statusY-top},true);
    classic_ui::draw_network_icon(x+26,top+20); classic_ui::text(x+68,top+28,"ENTIRE NETWORK",classic_ui::BLACK);
    auto n=network_stats(); classic_ui::text(x+24,top+76,n.linkUp?"NETWORK ADAPTER IS CONNECTED.":"NO NETWORK PROVIDER IS AVAILABLE.",classic_ui::SHADOW);
    classic_ui::fill({x+5,statusY,w-10,17},classic_ui::FACE);classic_ui::frame3d({x+5,statusY,w-10,17},true);classic_ui::text(x+10,statusY+5,"1 OBJECT(S)",classic_ui::BLACK);
}

static void draw_documents_original(int x,int y,int w,int h){
    int top=y+42,statusY=y+h-22;classic_ui::fill({x+4,top,w-8,statusY-top},classic_ui::WHITE);classic_ui::frame3d({x+4,top,w-8,statusY-top},true);
    char docs[1024]{}; if(docs_path(docs,sizeof(docs),false)){Item tmp[MAX_ITEMS]{};uint32_t count=0;ListCtx c{tmp,&count};fat32_list_directory(*gFat,docs,list_cb2,&c);for(uint32_t i=0;i<count&&i<10;++i){int yy=top+16+int(i)*24;if(tmp[i].attr&0x10)classic_ui::draw_folder_icon(x+18,yy-5);else classic_ui::draw_document_icon(x+18,yy-6);classic_ui::text(x+58,yy+3,tmp[i].name,classic_ui::BLACK);}if(!count)classic_ui::text(x+18,top+28,"THIS FOLDER IS EMPTY.",classic_ui::SHADOW);char st[32]="OBJECTS: ";sadd_u32(st,sizeof(st),count);classic_ui::fill({x+5,statusY,w-10,17},classic_ui::FACE);classic_ui::frame3d({x+5,statusY,w-10,17},true);classic_ui::text(x+10,statusY+5,st,classic_ui::BLACK);}else classic_ui::text(x+18,top+28,"DOCUMENTS ARE NOT AVAILABLE.",classic_ui::SHADOW);
}

static void draw_printers_original(int x,int y,int w,int h){
    int top=y+42,statusY=y+h-22;classic_ui::fill({x+4,top,w-8,statusY-top},classic_ui::WHITE);classic_ui::frame3d({x+4,top,w-8,statusY-top},true);
    classic_ui::draw_folder_icon(x+28,top+22);classic_ui::text(x+68,top+30,"ADD PRINTER",classic_ui::BLACK);classic_ui::text(x+28,top+78,"NO PRINTER DRIVER IS INSTALLED.",classic_ui::SHADOW);
    classic_ui::fill({x+5,statusY,w-10,17},classic_ui::FACE);classic_ui::frame3d({x+5,statusY,w-10,17},true);classic_ui::text(x+10,statusY+5,"1 OBJECT(S)",classic_ui::BLACK);
}


static void draw_explorer(int x,int y,int w,int h){
    uint32_t black=rgb(0,0,0),white=rgb(255,255,255),sel=rgb(0,0,128);
    // Backup-style classic toolbar.  Wrap only when needed; wide windows keep
    // the original one-line layout.
    int toolbarH=explorer_toolbar_wrapped(w)?58:32;
    rect(x+5,y+25,w-10,toolbarH,rgb(192,192,192));
    for(int i=0;i<EXPLORER_BUTTON_COUNT;++i){
        auto b=explorer_button_rect(x,y,w,i);
        button(b.x,b.y,b.w,b.h,kExplorerButtonLabels[i]);
    }
    int addressY=explorer_address_y(y,w);
    text(x+12,addressY+5,"Address:",black);
    classic_ui::fill({x+66,addressY,w-80,20},classic_ui::WHITE);
    classic_ui::frame3d({x+66,addressY,w-80,20},true);
    text(x+71,addressY+6,gExplorerLogical,black);
    int listTop=explorer_list_top(y,w),listBottom=y+h-34;
    classic_ui::fill({x+10,listTop,w-36,listBottom-listTop},classic_ui::WHITE);
    classic_ui::frame3d({x+10,listTop,w-36,listBottom-listTop},true);
    uint32_t visible=listBottom>listTop+10?uint32_t((listBottom-listTop-10)/17):1;
    for(uint32_t row=0;row<visible;++row){
        uint32_t i=gExplorerScroll+row;if(i>=gExplorerCount)break;int yy=listTop+8+int(row)*17;
        if(i==gExplorerSel){rect(x+14,yy-2,w-48,16,sel);text(x+38,yy,gExplorer[i].name,white);}else text(x+38,yy,gExplorer[i].name,black);
        if(gExplorer[i].attr&0x10){classic_ui::draw_folder_icon(x+14,yy-5);text(x+w-110,yy,"Folder",i==gExplorerSel?white:black);}else classic_ui::draw_document_icon(x+14,yy-6);
    }
    button(x+w-24,listTop+2,16,22,"^");button(x+w-24,listBottom-24,16,22,"v");
    if(!gExplorerCount)text(x+18,listTop+12,"This folder is empty.",black);
    classic_ui::fill({x+8,y+h-27,w-16,20},classic_ui::FACE);classic_ui::frame3d({x+8,y+h-27,w-16,20},true);text(x+14,y+h-20,gStatus,black);
}
static void note_layout_index(uint32_t index,int cols,uint32_t& row,uint32_t& col){row=0;col=0;for(uint32_t i=0;i<index&&i<gNoteLen;++i){char c=gNote[i];if(c=='\n'){++row;col=0;continue;}++col;if(col>=uint32_t(cols)){++row;col=0;}}}
static uint32_t note_index_from_xy(int mx,int my,int x,int y,int w){int left=x+14,top=y+84;int cols=(w-28)/6;if(cols<1)cols=1;int row=(my-top)/13+int(gNoteScroll);int col=(mx-left)/6;if(row<0)row=0;if(col<0)col=0;uint32_t r=0,c=0;for(uint32_t i=0;i<=gNoteLen;++i){if(int(r)==row&&int(c)>=col)return i;if(i==gNoteLen)return i;char ch=gNote[i];if(ch=='\n'){++r;c=0;}else{++c;if(c>=uint32_t(cols)){++r;c=0;}}}return gNoteLen;}
static void note_ensure_cursor_visible(int h,int w){int cols=(w-28)/6;if(cols<1)cols=1;uint32_t row=0,col=0;note_layout_index(gNoteCursor,cols,row,col);uint32_t rows=h>120?uint32_t((h-112)/13):1;if(row<gNoteScroll)gNoteScroll=row;else if(row>=gNoteScroll+rows)gNoteScroll=row-rows+1;}
static void draw_notepad(int x,int y,int w,int h){
    uint32_t black=rgb(0,0,0),blue=rgb(0,0,128),selFg=rgb(255,255,255);
    rect(x+5,y+25,w-10,32,rgb(192,192,192));
    button(x+10,y+30,52,22,"Save");button(x+66,y+30,70,22,"Save As");button(x+140,y+30,54,22,"Undo");button(x+198,y+30,54,22,"Redo");button(x+256,y+30,48,22,"Cut");button(x+308,y+30,52,22,"Copy");button(x+364,y+30,58,22,"Paste");
    text(x+432,y+37,gNoteName,black);if(gNoteDirty)text(x+w-78,y+37,"Modified",black);
    int top=y+60;classic_ui::fill({x+8,top,w-16,h-84},classic_ui::WHITE);classic_ui::frame3d({x+8,top,w-16,h-84},true);
    int cols=(w-28)/6;if(cols<1)cols=1;uint32_t row=0,col=0;uint32_t lo=sel_lo(),hi=sel_hi();
    for(uint32_t i=0;i<=gNoteLen;++i){if(row>=gNoteScroll&&int(row-gNoteScroll)*13+top+8<y+h-32){int cx=x+14+int(col)*6,cy=top+8+int(row-gNoteScroll)*13;if(i==gNoteCursor)rect(cx,cy,1,9,black);if(i<gNoteLen){char c=gNote[i];if(c!='\n'&&c!='\r'){bool selected=i>=lo&&i<hi;if(selected)rect(cx,cy,6,9,blue);chr(cx,cy,c,selected?selFg:black);}}}if(i==gNoteLen)break;char c=gNote[i];if(c=='\n'){++row;col=0;}else if(c!='\r'){++col;if(col>=uint32_t(cols)){++row;col=0;}}}
    button(x+w-24,top+2,16,22,"^");button(x+w-24,y+h-48,16,22,"v");
    classic_ui::fill({x+8,y+h-27,w-16,20},classic_ui::FACE);classic_ui::frame3d({x+8,y+h-27,w-16,20},true);text(x+14,y+h-20,gStatus,black);
}
static void draw_cmd(int x,int y,int w,int h){uint32_t white=rgb(255,255,255),black=rgb(0,0,0);classic_ui::fill({x+8,y+30,w-16,h-54},classic_ui::BLACK);classic_ui::frame3d({x+8,y+30,w-16,h-54},true);int yy=y+38;for(uint32_t i=0;i<gCmdLineCount;++i){text(x+14,yy,gCmdLines[i],white);yy+=14;}text(x+14,y+h-40,">",white);text(x+26,y+h-40,gCmd,white);classic_ui::fill({x+8,y+h-24,w-16,18},classic_ui::FACE);classic_ui::frame3d({x+8,y+h-24,w-16,18},true);text(x+14,y+h-18,gStatus,black);}
static void draw_control(int x,int y){
    uint32_t black=rgb(0,0,0);
    auto u=user_system_stats();auto a=audio_stats();auto n=network_stats();auto t=network_tcp_client_stats();
    auto sf=safe_fs_stats();auto sec=security_stats();auto b=beta_stats();auto ev=event_log_stats();
    text(x+14,y+38,"USER",black);text(x+145,y+38,u.displayName,black);
    text(x+14,y+58,"ROLE",black);text(x+145,y+58,u.currentRole==UserRole::Administrator?"ADMINISTRATOR":(u.currentRole==UserRole::Standard?"STANDARD":"GUEST"),black);
    text(x+14,y+78,"HOME",black);text(x+145,y+78,u.logicalHome,black);
    text(x+14,y+108,"AUDIO",black);text(x+145,y+108,a.initialized?"READY":(a.backend==AudioBackend::HdaDetectedUnsupported?"HDA / DRIVER PENDING":"NOT FOUND"),black);
    text(x+14,y+128,"VOLUME",black);u64text(x+145,y+128,a.volumePercent,black);text(x+175,y+128,"%",black);
    text(x+14,y+148,"NETWORK",black);text(x+145,y+148,n.linkUp?"LINK UP":"NO LINK",black);
    text(x+14,y+168,"ADDRESS",black);text(x+145,y+168,n.staticConfigured?"STATIC":(n.dhcpBound?"DHCP":(n.dhcpFailed?"DHCP FAILED":"WAITING")),black);
    text(x+14,y+188,"TCP CLIENT",black);text(x+145,y+188,t.state==TcpClientState::Established?"CONNECTED":(t.state==TcpClientState::Closed?"IDLE":"ACTIVE"),black);
    text(x+14,y+218,"PERMISSIONS",black);
    text(x+145,y+218,user_has_permission(UserPermChangeSettings)?"SETTINGS: YES":"SETTINGS: NO",black);
    text(x+145,y+238,user_has_permission(UserPermInstallApps)?"INSTALL APPS: YES":"INSTALL APPS: NO",black);
    text(x+145,y+258,user_has_permission(UserPermManageUsers)?"MANAGE USERS: YES":"MANAGE USERS: NO",black);
    text(x+14,y+288,"SAFE FILESYSTEM",black);text(x+160,y+288,sf.volumeHealthy?"HEALTHY":"CHECK FAILED",black);
    text(x+14,y+308,"LOGIN FAILURES",black);u64text(x+160,y+308,sec.loginFailures,black);
    text(x+14,y+328,"BETA HEALTH",black);text(x+160,y+328,b.readyForSoak?"READY":"WARNINGS",black);
    text(x+14,y+348,"EVENTS",black);u64text(x+160,y+348,ev.events,black);
    auto ds=display_stats();
    text(x+14,y+374,"DISPLAY DRIVER",black);text(x+145,y+374,display_backend_name(),black);
    text(x+14,y+394,"RESOLUTION",black);u64text(x+145,y+394,ds.width,black);text(x+178,y+394,"x",black);u64text(x+190,y+394,ds.height,black);
    button(x+300,y+368,90,22,"800x600");button(x+396,y+368,96,22,"1024x768");
    button(x+300,y+396,90,22,"1280x720");button(x+396,y+396,96,22,"1366x768");
    button(x+300,y+424,90,22,"1600x900");button(x+396,y+424,96,22,"1920x1080");
    button(x+300,y+452,90,22,"1280x800");button(x+396,y+452,96,22,"1920x1200");
    button(x+300,y+116,58,22,"Vol -");
    button(x+364,y+116,58,22,"Vol +");
    button(x+300,y+144,122,22,"Ping gateway");
    button(x+300,y+172,122,22,"Renew DHCP");
    button(x+300,y+200,122,22,a.muted?"Unmute":"Mute");
    button(x+300,y+284,122,22,"Check disk");
}
static void draw_task(int x,int y){
    uint32_t black=rgb(0,0,0);auto p=process_stats();auto s=scheduler_stats();auto b=beta_stats();auto um=usermode_stats();
    text(x+14,y+38,"PROCESS COUNT",black);u64text(x+170,y+38,p.processCount,black);
    text(x+14,y+58,"RUNNING",black);u64text(x+170,y+58,p.runningCount,black);
    text(x+14,y+78,"EXITED",black);u64text(x+170,y+78,p.exitedCount,black);
    text(x+14,y+98,"REAPED",black);u64text(x+170,y+98,p.reapedCount,black);
    text(x+14,y+128,"THREADS",black);u64text(x+170,y+128,s.threadCount,black);
    text(x+14,y+148,"CURRENT PID",black);u64text(x+170,y+148,s.currentPid,black);
    text(x+14,y+168,"CURRENT TID",black);u64text(x+170,y+168,s.currentTid,black);
    text(x+14,y+188,"CONTEXT SWITCHES",black);u64text(x+170,y+188,s.contextSwitches,black);
    text(x+14,y+218,"RING3 SYSCALLS",black);u64text(x+170,y+218,um.syscalls,black);
    text(x+14,y+238,"UPTIME TICKS",black);u64text(x+170,y+238,b.uptimeTicks,black);
    text(x+14,y+258,"BETA HEALTH",black);text(x+170,y+258,b.readyForSoak?"READY":"WARNINGS",black);
    text(x+14,y+292,"PID",black);text(x+70,y+292,"STATE",black);text(x+145,y+292,"PROCESS",black);
    struct ProcDrawCtx{int x;int y;uint32_t color;uint32_t row;};
    ProcDrawCtx ctx{x,y,black,0};
    process_enumerate([](const ProcessInfo& info,void* raw){
        auto*c=static_cast<ProcDrawCtx*>(raw);if(c->row>=7)return;
        const int yy=c->y+312+static_cast<int>(c->row)*18;
        u64text(c->x+14,yy,info.pid,c->color);
        text(c->x+70,yy,info.state==ProcessState::Running?"RUN":"EXIT",c->color);
        text(c->x+145,yy,info.name,c->color);
        ++c->row;
    },&ctx);
}
static void draw_device(int x,int y){
    uint32_t black=rgb(0,0,0);auto d=driver_manager_stats();auto a=ahci_stats();auto n=nvme_stats();auto u=xhci_stats();
    text(x+14,y+38,"PCI DEVICES",black);u64text(x+180,y+38,d.pciDevices,black);
    text(x+14,y+58,"MSI / MSI-X",black);u64text(x+180,y+58,d.msiDevices,black);text(x+210,y+58,"/",black);u64text(x+222,y+58,d.msixDevices,black);
    text(x+14,y+78,"STORAGE CONTROLLERS",black);u64text(x+180,y+78,d.storageControllers,black);
    text(x+14,y+98,"USB CONTROLLERS",black);u64text(x+180,y+98,d.usbControllers,black);
    text(x+14,y+118,"NETWORK / AUDIO",black);u64text(x+180,y+118,d.networkControllers,black);text(x+210,y+118,"/",black);u64text(x+222,y+118,d.audioControllers,black);
    text(x+14,y+143,"LOCAL APIC",black);text(x+180,y+143,d.localApic?"READY":"FALLBACK",black);
    text(x+14,y+163,"HPET",black);text(x+180,y+163,d.hpet?"RUNNING":"PIT FALLBACK",black);
    text(x+14,y+188,"AHCI RETRIES/ERRORS",black);u64text(x+180,y+188,a.ioRetries,black);text(x+220,y+188,"/",black);u64text(x+232,y+188,a.ioErrors,black);
    text(x+14,y+213,"NVME",black);text(x+180,y+213,n.ioQueueReady?"I/O READY":(n.found?"FOUND / INIT FAIL":"NO"),black);
    text(x+14,y+233,"NVME SECTOR SIZE",black);u64text(x+180,y+233,n.sectorSize,black);
    text(x+14,y+258,"XHCI",black);text(x+180,y+258,u.running?"RUNNING":(u.found?"INIT FAILED":"NO"),black);
    text(x+14,y+278,"XHCI PORTS",black);u64text(x+180,y+278,u.connectedPorts,black);text(x+205,y+278,"/",black);u64text(x+217,y+278,u.maxPorts,black);
    text(x+14,y+298,"HOTPLUG EVENTS",black);u64text(x+180,y+298,d.hotplugEvents,black);
    text(x+14,y+323,"ACPI",black);text(x+180,y+323,d.acpi?"READY":"NO",black);
    text(x+14,y+348,"DISPLAY",black);text(x+180,y+348,display_backend_name(),black);
}
static void draw_services(int x,int y){
    uint32_t black=rgb(0,0,0);auto n=network_stats();auto t=network_tcp_client_stats();auto a=audio_stats();auto f=safe_fs_stats();auto b=beta_stats();auto xh=xhci_stats();auto nv=nvme_stats();
    text(x+14,y+38,"SERVICE",black);text(x+210,y+38,"STATUS",black);
    text(x+14,y+68,"FILESYSTEM",black);text(x+210,y+68,f.volumeHealthy?"RUNNING":"DEGRADED",black);
    text(x+14,y+88,"NVME STORAGE",black);text(x+210,y+88,nv.ioQueueReady?"RUNNING":(nv.found?"DEGRADED":"N/A"),black);
    text(x+14,y+108,"XHCI HOST",black);text(x+210,y+108,xh.running?"RUNNING":(xh.found?"DEGRADED":"N/A"),black);
    text(x+14,y+128,"NETWORK",black);text(x+210,y+128,n.initialized?"RUNNING":"STOPPED",black);
    text(x+14,y+148,"TCP CLIENT",black);text(x+210,y+148,t.state==TcpClientState::Established?"CONNECTED":"READY",black);
    text(x+14,y+168,"AUDIO",black);text(x+210,y+168,a.initialized?"RUNNING":"STOPPED",black);
    text(x+14,y+188,"SCHEDULER",black);text(x+210,y+188,"RUNNING",black);
    text(x+14,y+208,"EVENT LOG",black);text(x+210,y+208,"RUNNING",black);
    text(x+14,y+238,"BETA MONITOR",black);text(x+210,y+238,b.readyForSoak?"HEALTHY":"WARNINGS",black);
}
static void draw_event_viewer(int x,int y){uint32_t black=rgb(0,0,0);auto e=event_log_stats();text(x+14,y+38,"EVENT LOG SUMMARY",black);text(x+14,y+68,"TOTAL",black);u64text(x+180,y+68,e.events,black);text(x+14,y+88,"WARNINGS",black);u64text(x+180,y+88,e.warnings,black);text(x+14,y+108,"ERRORS",black);u64text(x+180,y+108,e.errors,black);text(x+14,y+128,"CRITICAL",black);u64text(x+180,y+128,e.critical,black);text(x+14,y+148,"PERSISTED",black);u64text(x+180,y+148,e.persisted,black);text(x+14,y+188,"LAST EVENT",black);text(x+14,y+208,event_log_last_message(),black);text(x+14,y+248,"FILE: /SYSTEM/EVENT.LOG",black);}
static void draw_recycle(int x,int y,int w,int h){uint32_t black=rgb(0,0,0),white=rgb(255,255,255),sel=rgb(0,0,128);button(x+12,y+32,80,22,"Restore");button(x+100,y+32,80,22,"Delete");button(x+188,y+32,72,22,"Empty");classic_ui::fill({x+10,y+62,w-36,h-90},classic_ui::WHITE);classic_ui::frame3d({x+10,y+62,w-36,h-90},true);uint32_t visible=(h-106)/17;for(uint32_t row=0;row<visible;++row){uint32_t i=gRecycleScroll+row;if(i>=gRecycleCount)break;int yy=y+70+int(row)*17;if(i==gRecycleSel){rect(x+14,yy-2,w-48,16,sel);text(x+38,yy,gRecycle[i].name,white);}else text(x+38,yy,gRecycle[i].name,black);classic_ui::draw_trash_icon(x+12,yy-6);}button(x+w-24,y+64,16,22,"^");button(x+w-24,y+h-52,16,22,"v");if(!gRecycleCount)text(x+18,y+76,"Recycle Bin is empty.",black);classic_ui::fill({x+8,y+h-27,w-16,20},classic_ui::FACE);classic_ui::frame3d({x+8,y+h-27,w-16,20},true);text(x+14,y+h-20,gStatus,black);}
static void draw_context(){
    if(!gContext)return;
    const char*items[]={"Open","Copy","Cut","Rename","Delete","Properties"};
    int w=126,h=6*20+4; classic_ui::fill({gContextX,gContextY,w,h},classic_ui::FACE); classic_ui::frame3d({gContextX,gContextY,w,h},false);
    for(int i=0;i<6;++i){int yy=gContextY+2+i*20;bool hov=hit(gMouseX,gMouseY,gContextX+2,yy,gContextX+w-2,yy+20);if(hov)classic_ui::fill({gContextX+2,yy,w-4,20},classic_ui::ACTIVEBLUE);classic_ui::text(gContextX+9,yy+7,items[i],hov?classic_ui::WHITE:classic_ui::BLACK);}
}
static void draw_dialog(){
    if(gDialog==DialogType::None)return;
    bool input=(gDialog==DialogType::InputNewFolder||gDialog==DialogType::InputRename||gDialog==DialogType::InputSaveAs);
    int w=420,h=input?170:150;int x=(int(gBoot->width)-w)/2,y=(int(gBoot->height)-h)/2;
    classic_ui::window_frame({x,y,w,h},gDialogTitle,true,false,false,true);
    classic_ui::text(x+18,y+48,gDialogText,classic_ui::BLACK);
    if(input){
        classic_ui::fill({x+18,y+74,w-36,24},classic_ui::WHITE);classic_ui::frame3d({x+18,y+74,w-36,24},true);classic_ui::text(x+24,y+82,gDialogInput,classic_ui::BLACK);
        classic_ui::vline(x+24+classic_ui::text_width(gDialogInput),y+80,y+90,classic_ui::BLACK);
        classic_ui::button({x+w-170,y+h-38,70,24},"OK",false,false);classic_ui::button({x+w-90,y+h-38,70,24},"Cancel",false,false);
    }else if(gDialog==DialogType::Unsaved){
        classic_ui::button({x+92,y+h-38,70,24},"Save",false,false);classic_ui::button({x+170,y+h-38,90,24},"Don't Save",false,false);classic_ui::button({x+268,y+h-38,70,24},"Cancel",false,false);
    }else if(gDialog==DialogType::ConfirmDelete||gDialog==DialogType::Overwrite){
        classic_ui::button({x+w-170,y+h-38,70,24},"Yes",false,false);classic_ui::button({x+w-90,y+h-38,70,24},"No",false,false);
    }else classic_ui::button({x+w-90,y+h-38,70,24},"OK",false,false);
}
static void do_pending(PendingAction a){if(a==PendingAction::CloseApp){close_active_force();return;}if(a==PendingAction::OpenFile){if(load_note_path(gPendingOpenPath,gPendingOpenName)){launch_system_app(SystemAppId::Notepad);status("OPENED TEXT FILE");}else show_error("COULD NOT OPEN FILE");}}
static void accept_input_dialog(){if(!valid_lfn_name(gDialogInput)){show_error("INVALID FILE OR FOLDER NAME");return;}if(gDialog==DialogType::InputNewFolder){char p[1024]{};join_path(p,sizeof(p),gExplorerPath,gDialogInput);if(!fat32_mkdir(*gFat,p)){show_error("COULD NOT CREATE FOLDER");return;}refresh_explorer();status("EXPLORER: FOLDER CREATED");clear_dialog();return;}if(gDialog==DialogType::InputRename){if(gExplorerSel>=gExplorerCount){clear_dialog();return;}char oldp[1024]{},newp[1024]{};selected_path(oldp,sizeof(oldp));join_path(newp,sizeof(newp),gExplorerPath,gDialogInput);if(!fat32_rename(*gFat,oldp,newp)){show_error("RENAME FAILED OR NAME EXISTS");return;}refresh_explorer();status("EXPLORER: RENAMED");clear_dialog();return;}if(gDialog==DialogType::InputSaveAs){char docs[1024]{},dst[1024]{};if(!docs_path(docs,sizeof(docs),true)){show_error("ACCESS DENIED");return;}join_path(dst,sizeof(dst),docs,gDialogInput);scopy(gPendingSavePath,sizeof(gPendingSavePath),dst);scopy(gPendingSaveName,sizeof(gPendingSaveName),gDialogInput);refresh_explorer();if(file_exists_here(gDialogInput)){show_message(DialogType::Overwrite,"OVERWRITE FILE","FILE EXISTS. REPLACE IT?",PendingAction::SaveAs);return;}if(save_note_to(dst,gDialogInput))clear_dialog();}}
static bool dialog_mouse_click(int x,int y){if(gDialog==DialogType::None)return false;int w=420,h=(gDialog==DialogType::InputNewFolder||gDialog==DialogType::InputRename||gDialog==DialogType::InputSaveAs)?170:150;int dx=(int(gBoot->width)-w)/2,dy=(int(gBoot->height)-h)/2;bool input=(gDialog==DialogType::InputNewFolder||gDialog==DialogType::InputRename||gDialog==DialogType::InputSaveAs);if(input){if(hit(x,y,dx+w-170,dy+h-38,dx+w-100,dy+h-14)){accept_input_dialog();return true;}if(hit(x,y,dx+w-90,dy+h-38,dx+w-20,dy+h-14)){clear_dialog();return true;}return true;}if(gDialog==DialogType::Unsaved){if(hit(x,y,dx+92,dy+h-38,dx+162,dy+h-14)){PendingAction a=gPending;if(save_note()){clear_dialog();do_pending(a);}return true;}if(hit(x,y,dx+170,dy+h-38,dx+260,dy+h-14)){PendingAction a=gPending;gNoteDirty=false;clear_dialog();do_pending(a);return true;}if(hit(x,y,dx+268,dy+h-38,dx+338,dy+h-14)){clear_dialog();return true;}return true;}if(gDialog==DialogType::ConfirmDelete||gDialog==DialogType::Overwrite){if(hit(x,y,dx+w-170,dy+h-38,dx+w-100,dy+h-14)){PendingAction a=gPending;if(gDialog==DialogType::ConfirmDelete&&a==PendingAction::DeleteExplorer){move_to_recycle(gExplorerSel);clear_dialog();}else if(gDialog==DialogType::Overwrite&&a==PendingAction::SaveAs){char p[1024]{},n[256]{};scopy(p,sizeof(p),gPendingSavePath);scopy(n,sizeof(n),gPendingSaveName);clear_dialog();save_note_to(p,n);}else clear_dialog();return true;}if(hit(x,y,dx+w-90,dy+h-38,dx+w-20,dy+h-14)){clear_dialog();return true;}return true;}if(hit(x,y,dx+w-90,dy+h-38,dx+w-20,dy+h-14)){clear_dialog();return true;}return true;}

static void context_action(int index){gContext=false;switch(index){case 0:explorer_open_selected();break;case 1:explorer_copy(false);break;case 2:explorer_copy(true);break;case 3:if(gExplorerSel<gExplorerCount)show_input(DialogType::InputRename,"RENAME",gExplorer[gExplorerSel].name);break;case 4:explorer_delete_selected();break;case 5:explorer_properties();break;default:break;}gDirty=true;}

}

const char* system_apps_executable_path(SystemAppId app){ return exe_path_for_app(app); }

bool system_apps_shell_open(uint32_t appId){
    if(!gInit || !user_is_logged_in()) return false;
    if(appId < static_cast<uint32_t>(SystemAppId::Explorer) || appId > static_cast<uint32_t>(SystemAppId::Printers)) return false;
    open_app_direct(static_cast<SystemAppId>(appId));
    return true;
}

bool system_apps_launch(SystemAppId app){ return launch_system_app(app); }

bool system_apps_init(ZeroNTBootInfo*boot,Fat32Volume*volume){gBoot=boot;classic_ui::init(boot);gFat=volume;gInit=boot&&volume;gActive=SystemAppId::Desktop;gDirty=true;if(gInit)win_defaults();return gInit;}
void system_apps_on_login(){if(!gInit)return;ensure_recycle();explorer_init_root();refresh_recycle();gActive=SystemAppId::Desktop;gStart=false;gStartSubmenu=0;gLogoutRequested=false;gCmdLineCount=0;push_cmd_line("zeroNT v33.0 Command Prompt");push_cmd_line("TYPE HELP FOR COMMANDS");load_default_note();win_defaults();launch_system_app(SystemAppId::MyComputer);status("READY");gDirty=true;}
void system_apps_on_logout(){executable_shutdown_all();gActive=SystemAppId::Desktop;gStart=false;gStartSubmenu=0;gLogoutRequested=false;gNoteLen=gCmdLen=0;gNote[0]=gCmd[0]=0;gNotePath[0]=0;gDialog=DialogType::None;gContext=false;status("LOGGED OUT");gDirty=true;}
void system_apps_set_modifiers(bool shift,bool ctrl,bool alt){gShift=shift;gCtrl=ctrl;gAlt=alt;}

void system_apps_key(uint8_t sc,char ascii){if(!gInit)return;if(gDialog!=DialogType::None){bool input=(gDialog==DialogType::InputNewFolder||gDialog==DialogType::InputRename||gDialog==DialogType::InputSaveAs);if(input){if(sc==0x0E){if(gDialogInputLen)gDialogInput[--gDialogInputLen]=0;gDirty=true;return;}if(sc==0x1C){accept_input_dialog();return;}if(ascii>=32&&ascii<=126&&gDialogInputLen+1<sizeof(gDialogInput)){gDialogInput[gDialogInputLen++]=ascii;gDialogInput[gDialogInputLen]=0;gDirty=true;}return;}if(sc==0x01){clear_dialog();return;}if(sc==0x1C&&(gDialog==DialogType::Info||gDialog==DialogType::Error||gDialog==DialogType::Properties)){clear_dialog();return;}return;}
if(gAlt&&sc==0x3E){request_close();return;}
if(gActive==SystemAppId::Notepad){if(gCtrl){if(ascii=='c'||ascii=='C'){note_copy();return;}if(ascii=='x'||ascii=='X'){note_cut();return;}if(ascii=='v'||ascii=='V'){note_paste();return;}if(ascii=='z'||ascii=='Z'){note_undo();return;}if(ascii=='y'||ascii=='Y'){note_redo();return;}if(ascii=='a'||ascii=='A'){gSelAnchor=0;gSelEnd=gNoteLen;gNoteCursor=gNoteLen;gDirty=true;return;}if(ascii=='s'||ascii=='S'){save_note();return;}}
uint32_t old=gNoteCursor;if(sc==0x4B&&gNoteCursor)gNoteCursor--;else if(sc==0x4D&&gNoteCursor<gNoteLen)gNoteCursor++;else if(sc==0x47)gNoteCursor=0;else if(sc==0x4F)gNoteCursor=gNoteLen;else if(sc==0x49){gNoteScroll=gNoteScroll>10?gNoteScroll-10:0;gDirty=true;return;}else if(sc==0x51){gNoteScroll+=10;gDirty=true;return;}else if(sc==0x53){if(has_sel()){note_snapshot_undo();note_delete_range(sel_lo(),sel_hi());}else if(gNoteCursor<gNoteLen){note_snapshot_undo();note_delete_range(gNoteCursor,gNoteCursor+1);}gDirty=true;return;}else if(sc==0x0E){if(has_sel()){note_snapshot_undo();note_delete_range(sel_lo(),sel_hi());}else if(gNoteCursor){note_snapshot_undo();note_delete_range(gNoteCursor-1,gNoteCursor);}gDirty=true;return;}else if(sc==0x1C){note_snapshot_undo();const char nl='\n';note_insert_bytes(&nl,1);gDirty=true;return;}else if(ascii>=32&&ascii<=126){note_snapshot_undo();note_insert_bytes(&ascii,1);gDirty=true;return;}if(old!=gNoteCursor){if(gShift)gSelEnd=gNoteCursor;else gSelAnchor=gSelEnd=gNoteCursor;note_ensure_cursor_visible(gWin.h,gWin.w);gDirty=true;}return;}
if(gActive==SystemAppId::CommandPrompt){if(sc==0x0E){if(gCmdLen)gCmd[--gCmdLen]=0;gDirty=true;return;}if(sc==0x1C){cmd_exec();gDirty=true;return;}if(ascii>=32&&ascii<=126&&gCmdLen+1<CMD_CAP){gCmd[gCmdLen++]=ascii;gCmd[gCmdLen]=0;gDirty=true;}return;}
if(gActive==SystemAppId::Explorer){if(sc==0x48&&gExplorerSel){--gExplorerSel;}else if(sc==0x50&&gExplorerSel+1<gExplorerCount){++gExplorerSel;}else if(sc==0x1C)explorer_open_selected();refresh_explorer();gDirty=true;return;}
if(gActive==SystemAppId::RecycleBin){if(sc==0x48&&gRecycleSel)--gRecycleSel;else if(sc==0x50&&gRecycleSel+1<gRecycleCount)++gRecycleSel;gDirty=true;return;}}

void system_apps_wheel(int32_t x,int32_t y,int8_t delta){if(!gInit||delta==0)return;gMouseX=x;gMouseY=y;if(gDialog!=DialogType::None)return;if(gActive==SystemAppId::Explorer&&!gWin.minimized){if(delta>0){gExplorerScroll=gExplorerScroll>3?gExplorerScroll-3:0;}else if(gExplorerScroll+1<gExplorerCount)gExplorerScroll+=3;refresh_explorer();gDirty=true;}else if(gActive==SystemAppId::Notepad&&!gWin.minimized){if(delta>0)gNoteScroll=gNoteScroll>3?gNoteScroll-3:0;else gNoteScroll+=3;gDirty=true;}else if(gActive==SystemAppId::RecycleBin&&!gWin.minimized){if(delta>0)gRecycleScroll=gRecycleScroll>3?gRecycleScroll-3:0;else if(gRecycleScroll+1<gRecycleCount)gRecycleScroll+=3;gDirty=true;}}

void system_apps_mouse(int32_t x,int32_t y,uint8_t buttons){if(!gInit)return;gMouseX=x;gMouseY=y;bool left=(buttons&1)!=0,right=(buttons&2)!=0,press=left&&!(gPrevButtons&1),release=!left&&(gPrevButtons&1),rpress=right&&!(gPrevButtons&2);gMouseButtons=buttons;
if(gDialog!=DialogType::None){if(press)dialog_mouse_click(x,y);gPrevButtons=buttons;return;}
if(gWin.dragging){if(left){gWin.x=x-gWin.dragOffX;gWin.y=y-gWin.dragOffY;clamp_window();gDirty=true;}else gWin.dragging=false;gPrevButtons=buttons;return;}
if(gWin.resizing){if(left){gWin.w=x-gWin.x;gWin.h=y-gWin.y;clamp_window();gDirty=true;}else gWin.resizing=false;gPrevButtons=buttons;return;}
if(gNoteSelecting&&gActive==SystemAppId::Notepad){if(left){gSelEnd=note_index_from_xy(x,y,gWin.x,gWin.y,gWin.w);gNoteCursor=gSelEnd;note_ensure_cursor_visible(gWin.h,gWin.w);gDirty=true;}else gNoteSelecting=false;}
if(gFileDragCandidate&&left&&!gDraggingFile){int dx=x-gDragStartX,dy=y-gDragStartY;if(dx*dx+dy*dy>25){gDraggingFile=true;gDirty=true;}}
if(gDraggingFile&&left)gDirty=true;
if(release&&gDraggingFile){if(gActive==SystemAppId::Explorer){int listTop=explorer_list_top(gWin.y,gWin.w),listBottom=gWin.y+gWin.h-34;if(hit(x,y,gWin.x+10,listTop,gWin.x+gWin.w-36,listBottom)){int row=(y-(listTop+6))/17;uint32_t target=gExplorerScroll+(row<0?0:uint32_t(row));if(target<gExplorerCount&&(gExplorer[target].attr&0x10)&&target!=gDragItem){char src[1024]{},dir[1024]{},dst[1024]{};uint32_t oldSel=gExplorerSel;gExplorerSel=gDragItem;selected_path(src,sizeof(src));gExplorerSel=target;selected_path(dir,sizeof(dir));join_path(dst,sizeof(dst),dir,gExplorer[gDragItem].name);if(copy_file_between(src,dst,true)){status("DRAG DROP: MOVED INTO FOLDER");refresh_explorer();}gExplorerSel=oldSel;}else {auto del=explorer_button_rect(gWin.x,gWin.y,gWin.w,8);if(hit(x,y,del.x,del.y,del.x+del.w,del.y+del.h))move_to_recycle(gDragItem);}}}gDraggingFile=false;gFileDragCandidate=false;gPrevButtons=buttons;return;}
if(release)gFileDragCandidate=false;
int taskY=taskbar_y();
if(press&&hit(x,y,2,taskY+2,60,taskY+26)){gStart=!gStart;if(!gStart)gStartSubmenu=0;gDirty=true;gPrevButtons=buttons;return;}
if(press&&gActive!=SystemAppId::Desktop&&hit(x,y,64,taskY+2,214,taskY+26)){gWin.minimized=!gWin.minimized;gDirty=true;gPrevButtons=buttons;return;}
if(gStart&&press){
    classic_ui::Rect m=classic_start_menu_rect();
    bool handled=false;
    if(hit(x,y,m.x+24,m.y+5,m.x+m.w-2,m.y+33)){gStartSubmenu=1;gDirty=true;handled=true;}
    else if(hit(x,y,m.x+24,m.y+33,m.x+m.w-2,m.y+61)){launch_system_app(SystemAppId::Documents);handled=true;}
    else if(hit(x,y,m.x+24,m.y+61,m.x+m.w-2,m.y+89)){gStartSubmenu=2;gDirty=true;handled=true;}
    else if(hit(x,y,m.x+24,m.y+89,m.x+m.w-2,m.y+117)){launch_system_app(SystemAppId::Explorer);status("FIND: USE EXPLORER SEARCH/BROWSE");handled=true;}
    else if(hit(x,y,m.x+24,m.y+117,m.x+m.w-2,m.y+145)){show_info("ZERONT HELP","USE START > PROGRAMS TO OPEN APPLICATIONS.");gStart=false;gStartSubmenu=0;handled=true;}
    else if(hit(x,y,m.x+24,m.y+145,m.x+m.w-2,m.y+173)){launch_system_app(SystemAppId::CommandPrompt);handled=true;}
    else if(hit(x,y,m.x+24,m.y+179,m.x+m.w-2,m.y+207)){gLogoutRequested=true;gStart=false;gStartSubmenu=0;gDirty=true;handled=true;}
    else if(hit(x,y,m.x+24,m.y+207,m.x+m.w-2,m.y+235)){acpi_poweroff();}
    if(!handled&&gStartSubmenu==1){
        classic_ui::Rect p=classic_programs_rect();
        if(hit(x,y,p.x+3,p.y+3,p.x+p.w-3,p.y+143)){
            int i=(y-(p.y+3))/28;
            if(i==0)launch_system_app(SystemAppId::Explorer);
            else if(i==1)launch_system_app(SystemAppId::CommandPrompt);
            else if(i==2)launch_system_app(SystemAppId::Notepad);
            else if(i==3)launch_system_app(SystemAppId::TaskManager);
            else if(i==4)launch_system_app(SystemAppId::EventViewer);
            handled=true;
        }
    }
    if(!handled&&gStartSubmenu==2){
        classic_ui::Rect q=classic_settings_rect();
        if(hit(x,y,q.x+3,q.y+3,q.x+q.w-3,q.y+87)){
            int i=(y-(q.y+3))/28;
            if(i==0)launch_system_app(SystemAppId::ControlPanel);
            else if(i==1)launch_system_app(SystemAppId::DeviceManager);
            else if(i==2)launch_system_app(SystemAppId::Services);
            handled=true;
        }
    }
    if(!handled){gStart=false;gStartSubmenu=0;gDirty=true;}
    gPrevButtons=buttons;return;
}
if(gActive==SystemAppId::Desktop){
    if(press){
        int item=-1;SystemAppId app=SystemAppId::Desktop;
        if(hit(x,y,8,18,100,76)){item=0;app=SystemAppId::MyComputer;}
        else if(hit(x,y,8,80,100,138)){item=1;app=SystemAppId::NetworkNeighborhood;}
        else if(hit(x,y,8,142,100,200)){item=2;app=SystemAppId::RecycleBin;}
        else if(hit(x,y,8,204,110,262)){item=3;app=SystemAppId::Documents;}
        if(item>=0){
            gDesktopSel=item;
            if(register_click(ClickScope::Desktop,static_cast<uint32_t>(item),x,y))launch_system_app(app);
            gDirty=true;
        }else{gDesktopSel=-1;reset_double_click();gDirty=true;}
    }
    gPrevButtons=buttons;return;
}
if(gWin.minimized){gPrevButtons=buttons;return;}
if(gContext&&press){if(hit(x,y,gContextX,gContextY,gContextX+126,gContextY+124)){int i=(y-(gContextY+2))/20;if(i>=0&&i<6)context_action(i);}else gContext=false;gPrevButtons=buttons;return;}
if(press&&hit(x,y,gWin.x+gWin.w-24,gWin.y+4,gWin.x+gWin.w-6,gWin.y+20)){request_close();gPrevButtons=buttons;return;}
if(press&&hit(x,y,gWin.x+gWin.w-40,gWin.y+5,gWin.x+gWin.w-24,gWin.y+19)){win_max_toggle();gPrevButtons=buttons;return;}
if(press&&hit(x,y,gWin.x+gWin.w-58,gWin.y+5,gWin.x+gWin.w-42,gWin.y+19)){gWin.minimized=true;gDirty=true;gPrevButtons=buttons;return;}
if(press&&!gWin.maximized&&hit(x,y,gWin.x+gWin.w-14,gWin.y+gWin.h-14,gWin.x+gWin.w,gWin.y+gWin.h)){gWin.resizing=true;gDirty=true;gPrevButtons=buttons;return;}
if(press&&!gWin.maximized&&hit(x,y,gWin.x,gWin.y,gWin.x+gWin.w-70,gWin.y+24)){gWin.dragging=true;gWin.dragOffX=x-gWin.x;gWin.dragOffY=y-gWin.y;gDirty=true;gPrevButtons=buttons;return;}
if(gActive==SystemAppId::MyComputer&&press){
    int top=gWin.y+42;int item=-1;
    if(hit(x,y,gWin.x+8,top+14,gWin.x+130,top+92))item=0;
    else if(hit(x,y,gWin.x+145,top+14,gWin.x+270,top+92))item=1;
    else if(hit(x,y,gWin.x+275,top+14,gWin.x+405,top+92))item=2;
    else if(hit(x,y,gWin.x+8,top+96,gWin.x+145,top+168))item=3;
    else if(hit(x,y,gWin.x+145,top+96,gWin.x+280,top+168))item=4;
    if(item>=0){
        gMyComputerSel=item;gDirty=true;
        if(register_click(ClickScope::MyComputer,static_cast<uint32_t>(item),x,y)){
            if(item==0)show_info("FLOPPY DRIVE","NO FLOPPY MEDIA IS INSERTED.");
            else if(item==1)launch_system_app(SystemAppId::Explorer);
            else if(item==2)show_info("CD-ROM DRIVE","NO CD-ROM MEDIA IS INSERTED.");
            else if(item==3)launch_system_app(SystemAppId::ControlPanel);
            else if(item==4)launch_system_app(SystemAppId::Printers);
        }
        gPrevButtons=buttons;return;
    }
}
if(gActive==SystemAppId::Documents&&press){
    if(hit(x,y,gWin.x+4,gWin.y+42,gWin.x+gWin.w-4,gWin.y+gWin.h-22)){
        if(register_click(ClickScope::Documents,0,x,y))launch_system_app(SystemAppId::Explorer);
        else status("DOUBLE-CLICK TO OPEN MY DOCUMENTS");
        gPrevButtons=buttons;return;
    }
}
if(gActive==SystemAppId::ControlPanel&&press){
    uint32_t dw=0,dh=0;
    if(hit(x,y,gWin.x+300,gWin.y+368,gWin.x+390,gWin.y+390)){dw=800;dh=600;}
    else if(hit(x,y,gWin.x+396,gWin.y+368,gWin.x+492,gWin.y+390)){dw=1024;dh=768;}
    else if(hit(x,y,gWin.x+300,gWin.y+396,gWin.x+390,gWin.y+418)){dw=1280;dh=720;}
    else if(hit(x,y,gWin.x+396,gWin.y+396,gWin.x+492,gWin.y+418)){dw=1366;dh=768;}
    else if(hit(x,y,gWin.x+300,gWin.y+424,gWin.x+390,gWin.y+446)){dw=1600;dh=900;}
    else if(hit(x,y,gWin.x+396,gWin.y+424,gWin.x+492,gWin.y+446)){dw=1920;dh=1080;}
    else if(hit(x,y,gWin.x+300,gWin.y+452,gWin.x+390,gWin.y+474)){dw=1280;dh=800;}
    else if(hit(x,y,gWin.x+396,gWin.y+452,gWin.x+492,gWin.y+474)){dw=1920;dh=1200;}
    if(dw&&dh){const bool ok=display_set_mode(dw,dh);if(ok)(void)display_save_mode(dw,dh);status(ok?"DISPLAY MODE APPLIED":"DISPLAY MODE NOT SUPPORTED");gDirty=true;gPrevButtons=buttons;return;}
    if(hit(x,y,gWin.x+300,gWin.y+116,gWin.x+358,gWin.y+138)){
        auto a=audio_stats();uint8_t v=a.volumePercent>=10?static_cast<uint8_t>(a.volumePercent-10):0;
        status(audio_set_volume(v)?"VOLUME LOWERED":"AUDIO NOT READY");gPrevButtons=buttons;return;
    }
    if(hit(x,y,gWin.x+364,gWin.y+116,gWin.x+422,gWin.y+138)){
        auto a=audio_stats();uint8_t v=a.volumePercent<=90?static_cast<uint8_t>(a.volumePercent+10):100;
        status(audio_set_volume(v)?"VOLUME RAISED":"AUDIO NOT READY");gPrevButtons=buttons;return;
    }
    if(hit(x,y,gWin.x+300,gWin.y+144,gWin.x+422,gWin.y+166)){
        status(network_ping_gateway_wait(400000)?"NETWORK PING OK":"NETWORK PING FAILED");gPrevButtons=buttons;return;
    }
    if(hit(x,y,gWin.x+300,gWin.y+172,gWin.x+422,gWin.y+194)){
        status(network_restart_dhcp()?"DHCP RENEW STARTED":"DHCP RENEW FAILED");gPrevButtons=buttons;return;
    }
    if(hit(x,y,gWin.x+300,gWin.y+200,gWin.x+422,gWin.y+222)){
        auto a=audio_stats();status(audio_set_mute(!a.muted)?(!a.muted?"AUDIO MUTED":"AUDIO UNMUTED"):"AUDIO NOT READY");gPrevButtons=buttons;return;
    }
    if(hit(x,y,gWin.x+300,gWin.y+284,gWin.x+422,gWin.y+306)){
        status((safe_fs_check_volume()&&safe_fs_recover_pending())?"FILESYSTEM CHECK OK":"FILESYSTEM CHECK FAILED");gPrevButtons=buttons;return;
    }
}
if(gActive==SystemAppId::Notepad){if(press){if(hit(x,y,gWin.x+10,gWin.y+30,gWin.x+62,gWin.y+52)){save_note();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+66,gWin.y+30,gWin.x+136,gWin.y+52)){show_input(DialogType::InputSaveAs,"SAVE AS",gNoteName);gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+140,gWin.y+30,gWin.x+194,gWin.y+52)){note_undo();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+198,gWin.y+30,gWin.x+252,gWin.y+52)){note_redo();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+256,gWin.y+30,gWin.x+304,gWin.y+52)){note_cut();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+308,gWin.y+30,gWin.x+360,gWin.y+52)){note_copy();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+364,gWin.y+30,gWin.x+422,gWin.y+52)){note_paste();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+gWin.w-24,gWin.y+60,gWin.x+gWin.w-8,gWin.y+82)){gNoteScroll=gNoteScroll>1?gNoteScroll-1:0;gDirty=true;gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+gWin.w-24,gWin.y+gWin.h-46,gWin.x+gWin.w-8,gWin.y+gWin.h-24)){++gNoteScroll;gDirty=true;gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+8,gWin.y+60,gWin.x+gWin.w-24,gWin.y+gWin.h-24)){gNoteCursor=note_index_from_xy(x,y,gWin.x,gWin.y,gWin.w);gSelAnchor=gSelEnd=gNoteCursor;gNoteSelecting=true;gDirty=true;}}gPrevButtons=buttons;return;}
if(gActive==SystemAppId::Explorer){
    int listTop=explorer_list_top(gWin.y,gWin.w),listBottom=gWin.y+gWin.h-34;
    uint32_t visible=listBottom>listTop+8?uint32_t((listBottom-listTop-8)/17):1;
    if(rpress&&hit(x,y,gWin.x+10,listTop,gWin.x+gWin.w-36,listBottom)){
        int row=(y-(listTop+6))/17;uint32_t i=gExplorerScroll+(row<0?0:uint32_t(row));
        if(i<gExplorerCount){gExplorerSel=i;gContext=true;gContextX=x;gContextY=y;gDirty=true;}
        gPrevButtons=buttons;return;
    }
    if(press){
        int cmd=-1;
        for(int i=0;i<EXPLORER_BUTTON_COUNT;++i){
            auto b=explorer_button_rect(gWin.x,gWin.y,gWin.w,i);
            if(hit(x,y,b.x,b.y,b.x+b.w,b.y+b.h)){cmd=i;break;}
        }
        if(cmd>=0){
            switch(cmd){
                case 0: explorer_back();gDirty=true;break;
                case 1: explorer_forward();gDirty=true;break;
                case 2: explorer_up();gDirty=true;break;
                case 3: show_input(DialogType::InputNewFolder,"NEW FOLDER","NEWFOLD");break;
                case 4: explorer_copy(false);break;
                case 5: explorer_copy(true);break;
                case 6: explorer_paste();break;
                case 7: if(gExplorerSel<gExplorerCount)show_input(DialogType::InputRename,"RENAME",gExplorer[gExplorerSel].name);break;
                case 8: explorer_delete_selected();break;
                case 9: explorer_properties();break;
                case 10: refresh_explorer();status("EXPLORER: REFRESHED");break;
                default: break;
            }
            gPrevButtons=buttons;return;
        }
        if(hit(x,y,gWin.x+gWin.w-24,listTop,gWin.x+gWin.w-8,listTop+22)){gExplorerScroll=gExplorerScroll?gExplorerScroll-1:0;gDirty=true;gPrevButtons=buttons;return;}
        if(hit(x,y,gWin.x+gWin.w-24,listBottom-22,gWin.x+gWin.w-8,listBottom)){if(gExplorerScroll+visible<gExplorerCount)++gExplorerScroll;gDirty=true;gPrevButtons=buttons;return;}
        if(hit(x,y,gWin.x+10,listTop,gWin.x+gWin.w-36,listBottom)){
            int row=(y-(listTop+6))/17;uint32_t i=gExplorerScroll+(row<0?0:uint32_t(row));
            if(i<gExplorerCount){bool dbl=register_click(ClickScope::Explorer,i,x,y);gExplorerSel=i;gFileDragCandidate=true;gDragItem=i;gDragStartX=x;gDragStartY=y;if(dbl)explorer_open_selected();gDirty=true;}
            gPrevButtons=buttons;return;
        }
    }
    gPrevButtons=buttons;return;
}
if(gActive==SystemAppId::RecycleBin){int listTop=gWin.y+62,listBottom=gWin.y+gWin.h-50;if(press){if(hit(x,y,gWin.x+12,gWin.y+32,gWin.x+92,gWin.y+54)){restore_selected();gDirty=true;gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+100,gWin.y+32,gWin.x+180,gWin.y+54)){if(gRecycleSel<gRecycleCount){char p[1024]{};if(selected_recycle_path(p,sizeof(p))){if(safe_fs_delete_tree(p)){refresh_recycle();status("RECYCLE BIN: DELETED");}else show_error("DELETE FAILED");}}gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+188,gWin.y+32,gWin.x+260,gWin.y+54)){empty_recycle();gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+gWin.w-24,listTop,gWin.x+gWin.w-8,listTop+22)){gRecycleScroll=gRecycleScroll?gRecycleScroll-1:0;gDirty=true;gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+gWin.w-24,listBottom-22,gWin.x+gWin.w-8,listBottom)){if(gRecycleScroll+1<gRecycleCount)++gRecycleScroll;gDirty=true;gPrevButtons=buttons;return;}if(hit(x,y,gWin.x+10,listTop,gWin.x+gWin.w-36,listBottom)){int row=(y-(listTop+6))/17;uint32_t i=gRecycleScroll+(row<0?0:uint32_t(row));if(i<gRecycleCount){gRecycleSel=i;gDirty=true;}}}gPrevButtons=buttons;return;}gPrevButtons=buttons;}

void system_apps_render(int32_t mouseX,int32_t mouseY){if(!gInit||!gBoot)return;gMouseX=mouseX;gMouseY=mouseY;draw_desktop();if(gActive!=SystemAppId::Desktop&&!gWin.minimized){frame(gWin.x,gWin.y,gWin.w,gWin.h,app_title(gActive));switch(gActive){case SystemAppId::Explorer:draw_explorer(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::Notepad:draw_notepad(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::CommandPrompt:draw_cmd(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::ControlPanel:draw_control(gWin.x,gWin.y);break;case SystemAppId::TaskManager:draw_task(gWin.x,gWin.y);break;case SystemAppId::DeviceManager:draw_device(gWin.x,gWin.y);break;case SystemAppId::RecycleBin:draw_recycle(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::Services:draw_services(gWin.x,gWin.y);break;case SystemAppId::EventViewer:draw_event_viewer(gWin.x,gWin.y);break;case SystemAppId::MyComputer:draw_my_computer(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::NetworkNeighborhood:draw_network_neighborhood(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::Documents:draw_documents_original(gWin.x,gWin.y,gWin.w,gWin.h);break;case SystemAppId::Printers:draw_printers_original(gWin.x,gWin.y,gWin.w,gWin.h);break;default:break;}draw_context();}draw_dialog();if(gDraggingFile)classic_ui::text(mouseX+14,mouseY+14,"Move",classic_ui::WHITE);gui_render();classic_ui::present();classic_ui::present_mouse(mouseX,mouseY);gDirty=false;}
bool system_apps_dirty(){return gDirty;}
void system_apps_mark_dirty(){gDirty=true;}
bool system_apps_take_logout_request(){bool v=gLogoutRequested;gLogoutRequested=false;return v;}
SystemAppsStats system_apps_stats(){return {gInit,gActive,gExplorerCount,gRecycleCount,gNoteSaves,gCommands};}

