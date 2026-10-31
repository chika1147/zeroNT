#include "include/user_manager.hpp"
#include "include/safe_fs.hpp"
#include "include/security.hpp"
#include "include/event_log.hpp"
#include "include/interrupts.hpp"

namespace {
constexpr const char* USER_DB = "/SYSTEM/USERS.DAT";
constexpr uint32_t MAX_USERS = 8;
constexpr uint32_t HASH_ROUNDS = 16384;
constexpr uint32_t MAX_LOGIN_FAILURES = 5;
constexpr uint64_t LOCKOUT_TICKS = 3000; // 30 seconds at the 100 Hz PIT used by zeroNT.

struct UserRecord {
    uint64_t uid;
    UserRole role;
    char displayName[32];
    char alias[9];
    uint8_t salt[16];
    uint8_t hash[32];
};

static UserRecord gUsers[MAX_USERS]{};
static uint32_t gUserCount = 0;
static int32_t gCurrent = -1;
static bool gInitialized = false;
static uint8_t gLoginFailures[MAX_USERS]{};
static uint64_t gUnlockTick[MAX_USERS]{};

static size_t str_len(const char* s) { size_t n=0; if(s) while(s[n]) ++n; return n; }
static bool str_eq_ci(const char* a,const char* b){ if(!a||!b)return false; for(size_t i=0;;++i){char x=a[i],y=b[i]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y)return false; if(!x)return true;} }
static void str_copy(char* d,size_t cap,const char* s){ if(!d||!cap)return; size_t i=0; if(s)while(i+1<cap&&s[i]){d[i]=s[i];++i;} d[i]=0; }
static void append(char* d,size_t cap,const char* s){ size_t n=str_len(d),i=0; if(!s)return; while(n+1<cap&&s[i])d[n++]=s[i++]; d[n]=0; }
static void append_u64(char* d,size_t cap,uint64_t v){ char r[24];int n=0;if(!v)r[n++]='0';while(v&&n<23){r[n++]=char('0'+v%10);v/=10;}size_t p=str_len(d);while(n&&p+1<cap)d[p++]=r[--n];d[p]=0; }
static bool is_hex(char c){return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');}
static uint8_t hex_val(char c){if(c>='0'&&c<='9')return uint8_t(c-'0');if(c>='A'&&c<='F')return uint8_t(c-'A'+10);return uint8_t(c-'a'+10);}
static char hex_digit(uint8_t v){static const char h[]="0123456789ABCDEF";return h[v&15];}
static void bytes_hex(const uint8_t* b,size_t n,char* out){for(size_t i=0;i<n;++i){out[i*2]=hex_digit(b[i]>>4);out[i*2+1]=hex_digit(b[i]);}out[n*2]=0;}
static bool hex_bytes(const char* s,uint8_t* out,size_t n){if(!s||str_len(s)!=n*2)return false;for(size_t i=0;i<n;++i){if(!is_hex(s[i*2])||!is_hex(s[i*2+1]))return false;out[i]=uint8_t((hex_val(s[i*2])<<4)|hex_val(s[i*2+1]));}return true;}

// Compact freestanding SHA-256 used for the on-disk password verifier.
struct Sha256 { uint32_t h[8]; uint64_t bits; uint8_t block[64]; uint32_t used; };
static uint32_t rr(uint32_t x,uint32_t n){return (x>>n)|(x<<(32-n));}
static uint32_t ch(uint32_t x,uint32_t y,uint32_t z){return (x&y)^(~x&z);} static uint32_t maj(uint32_t x,uint32_t y,uint32_t z){return (x&y)^(x&z)^(y&z);}
static uint32_t s0(uint32_t x){return rr(x,2)^rr(x,13)^rr(x,22);} static uint32_t s1(uint32_t x){return rr(x,6)^rr(x,11)^rr(x,25);} static uint32_t g0(uint32_t x){return rr(x,7)^rr(x,18)^(x>>3);} static uint32_t g1(uint32_t x){return rr(x,17)^rr(x,19)^(x>>10);}
static const uint32_t K[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void sha_init(Sha256& c){const uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};for(int i=0;i<8;++i)c.h[i]=h[i];c.bits=0;c.used=0;}
static void sha_block(Sha256& c,const uint8_t* p){uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(p[i*4])<<24)|(uint32_t(p[i*4+1])<<16)|(uint32_t(p[i*4+2])<<8)|p[i*4+3];for(int i=16;i<64;++i)w[i]=g1(w[i-2])+w[i-7]+g0(w[i-15])+w[i-16];uint32_t a=c.h[0],b=c.h[1],cc=c.h[2],d=c.h[3],e=c.h[4],f=c.h[5],g=c.h[6],h=c.h[7];for(int i=0;i<64;++i){uint32_t t1=h+s1(e)+ch(e,f,g)+K[i]+w[i],t2=s0(a)+maj(a,b,cc);h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}c.h[0]+=a;c.h[1]+=b;c.h[2]+=cc;c.h[3]+=d;c.h[4]+=e;c.h[5]+=f;c.h[6]+=g;c.h[7]+=h;}
static void sha_update(Sha256& c,const void* data,size_t n){const uint8_t* p=(const uint8_t*)data;c.bits+=uint64_t(n)*8;while(n){size_t take=64-c.used;if(take>n)take=n;for(size_t i=0;i<take;++i)c.block[c.used+i]=p[i];c.used+=uint32_t(take);p+=take;n-=take;if(c.used==64){sha_block(c,c.block);c.used=0;}}}
static void sha_final(Sha256& c,uint8_t out[32]){c.block[c.used++]=0x80;if(c.used>56){while(c.used<64)c.block[c.used++]=0;sha_block(c,c.block);c.used=0;}while(c.used<56)c.block[c.used++]=0;for(int i=7;i>=0;--i)c.block[c.used++]=uint8_t(c.bits>>(i*8));sha_block(c,c.block);for(int i=0;i<8;++i){out[i*4]=uint8_t(c.h[i]>>24);out[i*4+1]=uint8_t(c.h[i]>>16);out[i*4+2]=uint8_t(c.h[i]>>8);out[i*4+3]=uint8_t(c.h[i]);}}
static void password_hash(const uint8_t salt[16],const char* password,uint8_t out[32]){Sha256 c;sha_init(c);sha_update(c,salt,16);sha_update(c,password,str_len(password));sha_final(c,out);for(uint32_t r=1;r<HASH_ROUNDS;++r){sha_init(c);sha_update(c,out,32);sha_update(c,salt,16);sha_final(c,out);}}
static bool secure_eq(const uint8_t*a,const uint8_t*b,size_t n){uint8_t x=0;for(size_t i=0;i<n;++i)x|=a[i]^b[i];return x==0;}

static uint64_t role_permissions(UserRole r){
    if(r==UserRole::Administrator) return UserPermReadFiles|UserPermWriteOwnFiles|UserPermNetwork|UserPermAudio|UserPermShutdown|UserPermChangeSettings|UserPermInstallApps|UserPermManageUsers|UserPermWriteSystem;
    if(r==UserRole::Standard) return UserPermReadFiles|UserPermWriteOwnFiles|UserPermNetwork|UserPermAudio|UserPermShutdown|UserPermChangeSettings;
    return UserPermReadFiles|UserPermNetwork|UserPermAudio;
}
static char role_char(UserRole r){return r==UserRole::Administrator?'A':(r==UserRole::Standard?'S':'G');}
static UserRole char_role(char c){return c=='A'?UserRole::Administrator:(c=='S'?UserRole::Standard:UserRole::Guest);}
static uint64_t parse_u64(const char* s){uint64_t v=0;while(s&&*s>='0'&&*s<='9'){v=v*10+uint64_t(*s-'0');++s;}return v;}
static bool split_fields(char* line,char* f[],uint32_t count){uint32_t n=0;if(!line||!count)return false;f[n++]=line;for(char*p=line;*p;++p)if(*p=='|'){*p=0;if(n<count)f[n++]=p+1;}return n==count;}

static bool save_db(Fat32Volume& v){char buf[4096]{};for(uint32_t i=0;i<gUserCount;++i){char saltHex[33],hashHex[65];bytes_hex(gUsers[i].salt,16,saltHex);bytes_hex(gUsers[i].hash,32,hashHex);char line[256]{};append_u64(line,sizeof(line),gUsers[i].uid);append(line,sizeof(line),"|");char rc[2]={role_char(gUsers[i].role),0};append(line,sizeof(line),rc);append(line,sizeof(line),"|");append(line,sizeof(line),gUsers[i].displayName);append(line,sizeof(line),"|");append(line,sizeof(line),gUsers[i].alias);append(line,sizeof(line),"|");append(line,sizeof(line),saltHex);append(line,sizeof(line),"|");append(line,sizeof(line),hashHex);append(line,sizeof(line),"\n");append(buf,sizeof(buf),line);}return safe_fs_atomic_write(USER_DB,buf,str_len(buf));}
static bool load_db(Fat32Volume& v){gUserCount=0;char buf[4096]{};size_t got=0;if(!fat32_read_file(v,USER_DB,buf,sizeof(buf)-1,got))return false;buf[got]=0;char* p=buf;while(*p&&gUserCount<MAX_USERS){char* line=p;while(*p&&*p!='\n'&&*p!='\r')++p;if(*p){*p++=0;while(*p=='\n'||*p=='\r')++p;}if(!*line)continue;char* f[6]{};if(!split_fields(line,f,6))continue;UserRecord r{};r.uid=parse_u64(f[0]);r.role=char_role(f[1][0]);str_copy(r.displayName,sizeof(r.displayName),f[2]);str_copy(r.alias,sizeof(r.alias),f[3]);if(!hex_bytes(f[4],r.salt,16)||!hex_bytes(f[5],r.hash,32))continue;gUsers[gUserCount++]=r;}return gUserCount>0;}
static bool create_profile(Fat32Volume& v,const UserRecord&r){char base[40]="/USERS/";append(base,sizeof(base),r.alias);fat32_mkdir(v,"/USERS");fat32_mkdir(v,base);const char* dirs[]={"DESKTOP","DOCS","DOWNLOAD","PICTURES","MUSIC","SETTINGS"};for(const char* d:dirs){char p[64]{};str_copy(p,sizeof(p),base);append(p,sizeof(p),"/");append(p,sizeof(p),d);fat32_mkdir(v,p);}char profile[64]{};str_copy(profile,sizeof(profile),base);append(profile,sizeof(profile),"/PROFILE.TXT");char body[256]="uid=";append_u64(body,sizeof(body),r.uid);append(body,sizeof(body),"\r\ndisplay=");append(body,sizeof(body),r.displayName);append(body,sizeof(body),"\r\nlogical=C:\\Users\\");append(body,sizeof(body),r.displayName);append(body,sizeof(body),"\r\n");return safe_fs_atomic_write(profile,body,str_len(body));}
static void make_salt(uint64_t entropy,uint64_t uid,const char* name,uint8_t out[16]){uint64_t a=0xcbf29ce484222325ULL^entropy^uid,b=0x9e3779b97f4a7c15ULL+(entropy<<1);for(size_t i=0;i<str_len(name);++i){a^=uint8_t(name[i]);a*=0x100000001b3ULL;b^=(a+(b<<6)+(b>>2));}for(int i=0;i<8;++i){out[i]=uint8_t(a>>(i*8));out[8+i]=uint8_t(b>>(i*8));}}
static int find_user(const char* name){for(uint32_t i=0;i<gUserCount;++i)if(str_eq_ci(gUsers[i].displayName,name))return int(i);return -1;}
static bool prefix_ci(const char* s,const char* p){if(!s||!p)return false;for(size_t i=0;p[i];++i){char a=s[i],b=p[i];if(a>='a'&&a<='z')a-=32;if(b>='a'&&b<='z')b-=32;if(a!=b)return false;}return true;}
}

bool user_validate_display_name(const char* n){size_t len=str_len(n);if(len<1||len>31)return false;if(n[0]==' '||n[len-1]==' ')return false;for(size_t i=0;i<len;++i){unsigned char c=(unsigned char)n[i];if(c<32||c>126)return false;switch(c){case '\\':case '/':case ':':case '*':case '?':case '"':case '<':case '>':case '|':return false;default:break;}}return true;}
bool user_validate_password(const char* p){size_t n=str_len(p);if(n<8||n>47)return false;bool letter=false,digit=false;for(size_t i=0;i<n;++i){unsigned char c=(unsigned char)p[i];if(c<32||c>126)return false;if((c>='A'&&c<='Z')||(c>='a'&&c<='z'))letter=true;if(c>='0'&&c<='9')digit=true;}return letter&&digit;}

bool user_system_init(Fat32Volume& v){gCurrent=-1;gInitialized=v.stats.mounted;gUserCount=0;for(uint32_t i=0;i<MAX_USERS;++i){gLoginFailures[i]=0;gUnlockTick[i]=0;}if(!gInitialized)return false;load_db(v);return true;}
bool user_setup_create_admin(Fat32Volume& v,const char* name,const char* pass,uint64_t entropy){if(!gInitialized||gUserCount||!user_validate_display_name(name)||!user_validate_password(pass))return false;UserRecord r{};r.uid=1000;r.role=UserRole::Administrator;str_copy(r.displayName,sizeof(r.displayName),name);str_copy(r.alias,sizeof(r.alias),"U1000");make_salt(entropy,r.uid,name,r.salt);password_hash(r.salt,pass,r.hash);gUsers[0]=r;gUserCount=1;if(!create_profile(v,r)||!save_db(v)){gUserCount=0;return false;}gCurrent=0;return true;}
bool user_login(Fat32Volume&,const char* name,const char* pass){if(!gInitialized||!name||!pass){security_record_login(false);return false;}int i=find_user(name);if(i<0){security_record_login(false);return false;}const uint64_t now=timer_ticks();if(gUnlockTick[i]&&now<gUnlockTick[i]){security_record_login(false);security_record_lockout();event_log_write(EventLevel::Warning,"LOGIN","ACCOUNT TEMPORARILY LOCKED",gUsers[i].uid);return false;}if(gUnlockTick[i]&&now>=gUnlockTick[i]){gUnlockTick[i]=0;gLoginFailures[i]=0;}uint8_t h[32];password_hash(gUsers[i].salt,pass,h);if(!secure_eq(h,gUsers[i].hash,32)){security_record_login(false);if(++gLoginFailures[i]>=MAX_LOGIN_FAILURES){gUnlockTick[i]=now+LOCKOUT_TICKS;gLoginFailures[i]=0;security_record_lockout();event_log_write(EventLevel::Warning,"LOGIN","TOO MANY FAILED LOGINS",gUsers[i].uid);}return false;}gLoginFailures[i]=0;gUnlockTick[i]=0;gCurrent=i;security_record_login(true);event_log_write(EventLevel::Info,"LOGIN","LOGIN SUCCESS",gUsers[i].uid);return true;}
void user_logout(){gCurrent=-1;}
bool user_is_logged_in(){return gCurrent>=0&&uint32_t(gCurrent)<gUserCount;}
bool user_has_permission(uint64_t mask){if(!user_is_logged_in())return false;return (role_permissions(gUsers[gCurrent].role)&mask)==mask;}
UserSystemStats user_system_stats(){UserSystemStats s{};s.initialized=gInitialized;s.setupRequired=gInitialized&&gUserCount==0;s.userCount=gUserCount;s.loggedIn=user_is_logged_in();if(s.loggedIn){const auto&r=gUsers[gCurrent];s.currentUid=r.uid;s.currentRole=r.role;s.permissions=role_permissions(r.role);str_copy(s.displayName,sizeof(s.displayName),r.displayName);str_copy(s.logicalHome,sizeof(s.logicalHome),"C:\\Users\\");append(s.logicalHome,sizeof(s.logicalHome),r.displayName);str_copy(s.backingHome,sizeof(s.backingHome),"/USERS/");append(s.backingHome,sizeof(s.backingHome),r.alias);}return s;}
bool user_change_display_name(Fat32Volume& v,const char* n){if(!user_is_logged_in()||!user_validate_display_name(n)||find_user(n)>=0)return false;str_copy(gUsers[gCurrent].displayName,sizeof(gUsers[gCurrent].displayName),n);if(!create_profile(v,gUsers[gCurrent]))return false;return save_db(v);}
bool user_change_password(Fat32Volume& v,const char* oldp,const char* newp,uint64_t entropy){if(!user_is_logged_in()||!user_validate_password(newp))return false;uint8_t h[32];password_hash(gUsers[gCurrent].salt,oldp,h);if(!secure_eq(h,gUsers[gCurrent].hash,32))return false;make_salt(entropy,gUsers[gCurrent].uid,gUsers[gCurrent].displayName,gUsers[gCurrent].salt);password_hash(gUsers[gCurrent].salt,newp,gUsers[gCurrent].hash);return save_db(v);}
bool user_resolve_own_path(const char* logical,char* out,size_t cap,bool write){if(!user_is_logged_in()||!logical||!out||!cap)return false;if(write&&!user_has_permission(UserPermWriteOwnFiles))return false;UserSystemStats s=user_system_stats();if(!prefix_ci(logical,s.logicalHome))return false;const char* rest=logical+str_len(s.logicalHome);if(*rest && *rest!='\\' && *rest!='/')return false;while(*rest=='\\'||*rest=='/')++rest;str_copy(out,cap,s.backingHome);if(*rest){append(out,cap,"/"); // Map common long logical folder names to 8.3 backing names.
if(prefix_ci(rest,"Documents")){append(out,cap,"DOCS");rest+=9;}else if(prefix_ci(rest,"Downloads")){append(out,cap,"DOWNLOAD");rest+=9;}else{while(*rest&&*rest!='\\'&&*rest!='/'){char t[2]={*rest++,0};append(out,cap,t);}}while(*rest=='\\'||*rest=='/')++rest;if(*rest){append(out,cap,"/");append(out,cap,rest);for(size_t i=0;out[i];++i)if(out[i]=='\\')out[i]='/';}}return true;}
