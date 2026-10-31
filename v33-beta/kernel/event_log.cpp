#include "include/event_log.hpp"
#include "include/safe_fs.hpp"
#include <stddef.h>

namespace {
constexpr uint32_t MAX_EVENTS=24;
constexpr uint32_t LINE=112;
struct Entry { uint64_t seq; EventLevel level; uint64_t code; char source[16]; char message[64]; };
static Entry gEntries[MAX_EVENTS]{};
static uint32_t gHead=0,gCount=0;
static uint64_t gSeq=0,gLastPersistTick=0;
static Fat32Volume* gVolume=nullptr;
static EventLogStats g{};
static char gLast[64]{};
static bool gInitialized=false;

static void scopy(char*d,size_t cap,const char*s){if(!d||!cap)return;size_t i=0;if(s)while(i+1<cap&&s[i]){d[i]=s[i];++i;}d[i]=0;}
static size_t slen(const char*s){size_t n=0;if(s)while(s[n])++n;return n;}
static void append(char*d,size_t cap,const char*s){size_t n=slen(d),i=0;if(!d||!cap||!s)return;while(n+1<cap&&s[i])d[n++]=s[i++];d[n]=0;}
static void append_u64(char*d,size_t cap,uint64_t v){char r[24]{};int n=0;if(!v)r[n++]='0';else{char t[24]{};int q=0;while(v&&q<23){t[q++]=char('0'+v%10);v/=10;}while(q)r[n++]=t[--q];}r[n]=0;append(d,cap,r);}
static const char* level_name(EventLevel l){switch(l){case EventLevel::Info:return "INFO";case EventLevel::Warning:return "WARN";case EventLevel::Error:return "ERROR";case EventLevel::Critical:return "CRIT";}return "?";}
static bool persist(){
    if(!gVolume||!gVolume->stats.mounted)return false;
    char out[4096]{};append(out,sizeof(out),"zeroNT v33.0 Event Log\r\n");
    uint32_t start=(gHead+MAX_EVENTS-gCount)%MAX_EVENTS;
    for(uint32_t n=0;n<gCount;++n){const Entry&e=gEntries[(start+n)%MAX_EVENTS];char line[LINE]{};append(line,sizeof(line),"#");append_u64(line,sizeof(line),e.seq);append(line,sizeof(line)," [");append(line,sizeof(line),level_name(e.level));append(line,sizeof(line),"] ");append(line,sizeof(line),e.source);append(line,sizeof(line),": ");append(line,sizeof(line),e.message);if(e.code){append(line,sizeof(line)," code=");append_u64(line,sizeof(line),e.code);}append(line,sizeof(line),"\r\n");append(out,sizeof(out),line);}
    if(!safe_fs_atomic_write("/SYSTEM/EVENT.LOG",out,slen(out))){++g.persistFailures;return false;}++g.persisted;return true;
}
}

void event_log_init(Fat32Volume* volume){gVolume=volume;if(!gInitialized){g={};gHead=gCount=0;gSeq=0;gLastPersistTick=0;gLast[0]=0;gInitialized=true;}}
void event_log_write(EventLevel level,const char*source,const char*message,uint64_t code){Entry&e=gEntries[gHead];e={};e.seq=++gSeq;e.level=level;e.code=code;scopy(e.source,sizeof(e.source),source?source:"SYSTEM");scopy(e.message,sizeof(e.message),message?message:"");scopy(gLast,sizeof(gLast),e.message);gHead=(gHead+1)%MAX_EVENTS;if(gCount<MAX_EVENTS)++gCount;++g.events;if(level==EventLevel::Warning)++g.warnings;else if(level==EventLevel::Error)++g.errors;else if(level==EventLevel::Critical)++g.critical;g.buffered=gCount;}
void event_log_poll(uint64_t ticks){if(!gVolume||gCount==0)return;if(ticks-gLastPersistTick<500)return;gLastPersistTick=ticks;persist();}
EventLogStats event_log_stats(){g.buffered=gCount;return g;}
const char* event_log_last_message(){return gLast;}
