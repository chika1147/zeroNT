#include "include/audio.hpp"
#include "include/pci.hpp"
#include "include/arch.hpp"
#include "include/paging.hpp"
#include <stdint.h>
#include <stddef.h>

namespace {
constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t MAX_PCM_BYTES = 32768;
constexpr uint32_t MAX_BDL = 8;
constexpr uint32_t HW_FRAMES = MAX_PCM_BYTES / 4u;
constexpr uint32_t QUEUE_FRAMES = 32768;

constexpr uint16_t NAM_RESET = 0x00;
constexpr uint16_t NAM_MASTER_VOL = 0x02;
constexpr uint16_t NAM_PCM_OUT_VOL = 0x18;
constexpr uint16_t NAM_EXT_AUDIO_STATUS = 0x2A;
constexpr uint16_t NAM_PCM_FRONT_DAC_RATE = 0x2C;

constexpr uint16_t NABM_PO_BDBAR = 0x10;
constexpr uint16_t NABM_PO_LVI   = 0x15;
constexpr uint16_t NABM_PO_SR    = 0x16;
constexpr uint16_t NABM_PO_CR    = 0x1B;
constexpr uint16_t NABM_GLOB_CNT = 0x2C;

constexpr uint8_t PO_CR_RPBM = 1u << 0;
constexpr uint8_t PO_CR_RR   = 1u << 1;
constexpr uint16_t PO_SR_DCH  = 1u << 0;
constexpr uint32_t BDL_IOC    = 1u << 31;

struct __attribute__((packed)) BufferDescriptor {
    uint32_t address;
    uint32_t lengthControl;
};

static AudioStats g{};
alignas(4096) static BufferDescriptor gBdl[MAX_BDL];
alignas(4096) static int16_t gPcm[MAX_PCM_BYTES / 2];
alignas(64) static int16_t gQueue[QUEUE_FRAMES * 2];
static uint32_t gQueueHead = 0;
static uint32_t gQueueTail = 0;
static uint32_t gQueueCount = 0;
static uint32_t gHwFrames = 0;
static uint8_t gEntries = 0;
static bool gHdaSeen = false;
static PciDeviceInfo gHda{};

static void pci_find_audio_cb(const PciDeviceInfo& i, void*) {
    if (i.classCode != 0x04) return;
    if (i.subclass == 0x03 && !gHdaSeen) { gHdaSeen = true; gHda = i; }
    if (g.controllerFound || i.subclass != 0x01) return;
    // Intel/QEMU AC97-compatible controllers expose mixer + bus-master I/O BARs.
    if ((i.bar[0] & 1u) == 0 || (i.bar[1] & 1u) == 0) return;
    g.controllerFound = true;
    g.backend = AudioBackend::AC97;
    g.bus = i.bus; g.device = i.device; g.function = i.function;
    g.vendorId = i.vendorId; g.deviceId = i.deviceId;
    g.mixerBase = static_cast<uint16_t>(i.bar[0] & ~3u);
    g.busMasterBase = static_cast<uint16_t>(i.bar[1] & ~3u);
    pci_enable_io_busmaster(i);
}

static void clear_hw() {
    for (uint32_t i=0;i<MAX_PCM_BYTES/2;++i) gPcm[i]=0;
    for (uint32_t i=0;i<MAX_BDL;++i) { gBdl[i].address=0; gBdl[i].lengthControl=0; }
    gEntries=0;gHwFrames=0;
}
static void clear_queue(){gQueueHead=gQueueTail=gQueueCount=0;g.queuedFrames=0;}
static uint32_t queue_free(){return QUEUE_FRAMES-gQueueCount;}
static bool queue_frame(int16_t left,int16_t right){if(gQueueCount>=QUEUE_FRAMES)return false;gQueue[gQueueTail*2]=left;gQueue[gQueueTail*2+1]=right;gQueueTail=(gQueueTail+1)%QUEUE_FRAMES;++gQueueCount;g.queuedFrames=gQueueCount;return true;}
static bool pop_frame(int16_t&left,int16_t&right){if(!gQueueCount)return false;left=gQueue[gQueueHead*2];right=gQueue[gQueueHead*2+1];gQueueHead=(gQueueHead+1)%QUEUE_FRAMES;--gQueueCount;g.queuedFrames=gQueueCount;return true;}

static void apply_volume(){
    if(!g.initialized)return;
    if(g.muted){outw(static_cast<uint16_t>(g.mixerBase+NAM_MASTER_VOL),0x8000u);outw(static_cast<uint16_t>(g.mixerBase+NAM_PCM_OUT_VOL),0x8000u);return;}
    const uint16_t attenuation=static_cast<uint16_t>(((100u-g.volumePercent)*31u)/100u);
    const uint16_t value=static_cast<uint16_t>((attenuation<<8)|attenuation);
    outw(static_cast<uint16_t>(g.mixerBase+NAM_MASTER_VOL),value);
    outw(static_cast<uint16_t>(g.mixerBase+NAM_PCM_OUT_VOL),value);
}

static bool start_hw_chunk(){
    if(!g.initialized||g.playing||!gQueueCount)return false;
    uint32_t frames=gQueueCount<HW_FRAMES?gQueueCount:HW_FRAMES;
    for(uint32_t i=0;i<frames;++i){int16_t l=0,r=0;pop_frame(l,r);gPcm[i*2]=l;gPcm[i*2+1]=r;}
    uint32_t bytes=frames*4u;for(uint32_t i=bytes/2;i<MAX_PCM_BYTES/2;++i)gPcm[i]=0;
    uint32_t remaining=bytes,offset=0;gEntries=0;
    while(remaining&&gEntries<MAX_BDL){const uint32_t pageOffset=offset&4095u;uint32_t chunk=4096u-pageOffset;if(chunk>remaining)chunk=remaining;const uint64_t virt=reinterpret_cast<uint64_t>(reinterpret_cast<uint8_t*>(gPcm)+offset);const uint64_t phys=vmm_translate(virt);if(!phys||phys>0xFFFFFFFFULL)return false;gBdl[gEntries].address=static_cast<uint32_t>(phys);gBdl[gEntries].lengthControl=chunk/2u;remaining-=chunk;offset+=chunk;++gEntries;}
    if(remaining||!gEntries)return false;gBdl[gEntries-1].lengthControl|=BDL_IOC;const uint64_t bdlPhys=vmm_translate(reinterpret_cast<uint64_t>(gBdl));if(!bdlPhys||bdlPhys>0xFFFFFFFFULL)return false;
    const uint16_t bm=g.busMasterBase;outb(static_cast<uint16_t>(bm+NABM_PO_CR),PO_CR_RR);for(volatile uint32_t i=0;i<1000;++i)__asm__ __volatile__("pause");outb(static_cast<uint16_t>(bm+NABM_PO_CR),0);outw(static_cast<uint16_t>(bm+NABM_PO_SR),0x001Cu);outl(static_cast<uint16_t>(bm+NABM_PO_BDBAR),static_cast<uint32_t>(bdlPhys));outb(static_cast<uint16_t>(bm+NABM_PO_LVI),static_cast<uint8_t>(gEntries-1));outb(static_cast<uint16_t>(bm+NABM_PO_CR),PO_CR_RPBM);gHwFrames=frames;g.playing=true;return true;
}

static bool queue_tone(uint32_t frequencyHz,uint32_t durationMs,uint16_t amplitude){
    if(frequencyHz<50||frequencyHz>12000)return false;if(durationMs<10)durationMs=10;if(durationMs>1000)durationMs=1000;uint32_t frames=(SAMPLE_RATE*durationMs)/1000u;if(frames>queue_free())return false;uint32_t halfPeriod=SAMPLE_RATE/(frequencyHz*2u);if(!halfPeriod)halfPeriod=1;for(uint32_t f=0;f<frames;++f){int16_t s=((f/halfPeriod)&1u)?static_cast<int16_t>(amplitude):-static_cast<int16_t>(amplitude);if(!queue_frame(s,s))return false;}return true;
}
static bool queue_silence(uint32_t durationMs){uint32_t frames=(SAMPLE_RATE*durationMs)/1000u;if(frames>queue_free())return false;for(uint32_t i=0;i<frames;++i)if(!queue_frame(0,0))return false;return true;}
}

bool audio_init() {
    g={};g.sampleRate=SAMPLE_RATE;g.volumePercent=80;g.backend=AudioBackend::None;gHdaSeen=false;gHda={};clear_hw();clear_queue();pci_enumerate(pci_find_audio_cb,nullptr);
    if(!g.controllerFound){if(gHdaSeen){g.controllerFound=true;g.backend=AudioBackend::HdaDetectedUnsupported;g.bus=gHda.bus;g.device=gHda.device;g.function=gHda.function;g.vendorId=gHda.vendorId;g.deviceId=gHda.deviceId;}return false;}
    if(!g.mixerBase||!g.busMasterBase)return false;
    outl(static_cast<uint16_t>(g.busMasterBase+NABM_GLOB_CNT),0x00000002u);for(volatile uint32_t i=0;i<200000;++i)__asm__ __volatile__("pause");outw(static_cast<uint16_t>(g.mixerBase+NAM_RESET),0x0000);
    uint16_t ext=inw(static_cast<uint16_t>(g.mixerBase+NAM_EXT_AUDIO_STATUS));outw(static_cast<uint16_t>(g.mixerBase+NAM_EXT_AUDIO_STATUS),static_cast<uint16_t>(ext|0x0001));outw(static_cast<uint16_t>(g.mixerBase+NAM_PCM_FRONT_DAC_RATE),SAMPLE_RATE);
    outb(static_cast<uint16_t>(g.busMasterBase+NABM_PO_CR),PO_CR_RR);for(volatile uint32_t i=0;i<10000;++i)__asm__ __volatile__("pause");outb(static_cast<uint16_t>(g.busMasterBase+NABM_PO_CR),0);outw(static_cast<uint16_t>(g.busMasterBase+NABM_PO_SR),0x001Cu);g.initialized=true;apply_volume();return true;
}

bool audio_submit_pcm_s16_stereo(const int16_t*samples,uint32_t frames,uint32_t sampleRate){
    if(!g.initialized||!samples||!frames||sampleRate<8000||sampleRate>192000)return false;uint64_t out64=(static_cast<uint64_t>(frames)*SAMPLE_RATE+sampleRate-1u)/sampleRate;if(!out64||out64>QUEUE_FRAMES)return false;uint32_t outFrames=static_cast<uint32_t>(out64);if(outFrames>queue_free())return false;
    for(uint32_t o=0;o<outFrames;++o){uint64_t src64=(static_cast<uint64_t>(o)*sampleRate)/SAMPLE_RATE;uint32_t s=static_cast<uint32_t>(src64);if(s>=frames)s=frames-1;if(!queue_frame(samples[s*2],samples[s*2+1]))return false;}
    ++g.pcmSubmissions;g.pcmFramesSubmitted+=outFrames;if(!g.playing)start_hw_chunk();return true;
}

bool audio_play_tone(uint32_t frequencyHz,uint32_t durationMs){if(!g.initialized)return false;if(!queue_tone(frequencyHz,durationMs,9000))return false;++g.tonesPlayed;if(!g.playing)start_hw_chunk();return true;}

bool audio_play_system_sound(SystemSound sound){
    if(!g.initialized)return false;bool ok=false;switch(sound){case SystemSound::Startup:ok=queue_tone(660,70,6500)&&queue_silence(20)&&queue_tone(880,90,7000);break;case SystemSound::Notify:ok=queue_tone(880,55,6500)&&queue_silence(15)&&queue_tone(1040,55,6500);break;case SystemSound::Error:ok=queue_tone(330,100,7500)&&queue_silence(20)&&queue_tone(220,140,7500);break;case SystemSound::Click:ok=queue_tone(1200,18,4500);break;}if(ok){++g.systemSounds;if(!g.playing)start_hw_chunk();}return ok;
}

void audio_poll(){if(!g.initialized)return;if(g.playing){const uint16_t status=inw(static_cast<uint16_t>(g.busMasterBase+NABM_PO_SR));if(status&PO_SR_DCH){outb(static_cast<uint16_t>(g.busMasterBase+NABM_PO_CR),0);outw(static_cast<uint16_t>(g.busMasterBase+NABM_PO_SR),0x001Cu);g.playing=false;g.pcmFramesPlayed+=gHwFrames;gHwFrames=0;if(gQueueCount)start_hw_chunk();else ++g.underruns;}}else if(gQueueCount)start_hw_chunk();}

bool audio_set_volume(uint8_t percent){if(percent>100)percent=100;g.volumePercent=percent;if(!g.initialized)return false;apply_volume();return true;}
bool audio_set_mute(bool mute){g.muted=mute;if(!g.initialized)return false;apply_volume();return true;}
void audio_stop(){if(g.initialized){outb(static_cast<uint16_t>(g.busMasterBase+NABM_PO_CR),0);outw(static_cast<uint16_t>(g.busMasterBase+NABM_PO_SR),0x001Cu);}g.playing=false;gHwFrames=0;clear_queue();}
AudioStats audio_stats(){g.queuedFrames=gQueueCount;return g;}
