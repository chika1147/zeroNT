#include "include/nvme.hpp"
#include "include/memory.hpp"
#include "include/paging.hpp"
#include <stdint.h>
#include <stddef.h>

namespace {
constexpr uint64_t PAGE_SIZE=4096;
constexpr uint32_t ADMIN_Q_DEPTH=32;
constexpr uint32_t IO_Q_DEPTH=64;
constexpr uint32_t WAIT_SPINS=30000000;

constexpr uint32_t REG_CAP=0x00;
constexpr uint32_t REG_VS=0x08;
constexpr uint32_t REG_INTMS=0x0C;
constexpr uint32_t REG_CC=0x14;
constexpr uint32_t REG_CSTS=0x1C;
constexpr uint32_t REG_AQA=0x24;
constexpr uint32_t REG_ASQ=0x28;
constexpr uint32_t REG_ACQ=0x30;
constexpr uint32_t REG_DOORBELL=0x1000;

#pragma pack(push,1)
struct NvmeCommand {
    uint8_t opcode;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved2;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};
struct NvmeCompletion {
    uint32_t result;
    uint32_t reserved;
    uint16_t sqHead;
    uint16_t sqId;
    uint16_t cid;
    uint16_t status;
};
#pragma pack(pop)
static_assert(sizeof(NvmeCommand)==64,"NVMe SQE must be 64 bytes");
static_assert(sizeof(NvmeCompletion)==16,"NVMe CQE must be 16 bytes");

struct Queue {
    NvmeCommand* sq;
    NvmeCompletion* cq;
    uint64_t sqPhys;
    uint64_t cqPhys;
    uint16_t depth;
    uint16_t sqTail;
    uint16_t cqHead;
    uint16_t nextCid;
    uint8_t cqPhase;
    uint16_t qid;
};

struct Context {
    PciDeviceInfo pci;
    volatile uint8_t* mmio;
    uint64_t cap;
    uint32_t doorbellStride;
    Queue admin;
    Queue io;
    void* adminSqPage;
    void* adminCqPage;
    void* ioSqPage;
    void* ioCqPage;
    void* identifyPage;
    void* bouncePage;
    uint32_t nsid;
    BlockDevice block;
};

static NvmeProbeStats gProbe{};
static NvmeStats g{};
static Context gCtx{};

static void memzero(void* p,size_t n){auto*b=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)b[i]=0;}
static void memcpy8(void*d,const void*s,size_t n){auto*dd=static_cast<uint8_t*>(d);auto*ss=static_cast<const uint8_t*>(s);for(size_t i=0;i<n;++i)dd[i]=ss[i];}
static inline void fence(){__asm__ __volatile__("mfence" ::: "memory");}
static inline uint32_t rd32(uint32_t off){return *reinterpret_cast<volatile uint32_t*>(gCtx.mmio+off);}
static inline uint64_t rd64(uint32_t off){uint32_t lo=rd32(off),hi=rd32(off+4);return static_cast<uint64_t>(lo)|(static_cast<uint64_t>(hi)<<32);}
static inline void wr32(uint32_t off,uint32_t v){*reinterpret_cast<volatile uint32_t*>(gCtx.mmio+off)=v;(void)rd32(off);}
static inline void wr64(uint32_t off,uint64_t v){wr32(off,static_cast<uint32_t>(v));wr32(off+4,static_cast<uint32_t>(v>>32));}

static bool find_first(PciDeviceInfo& out){
    struct F{PciDeviceInfo*o;bool found;};F f{&out,false};
    pci_enumerate([](const PciDeviceInfo&i,void*ctx){auto*f=static_cast<F*>(ctx);if(f->found)return;if(i.classCode==0x01&&i.subclass==0x08&&i.progIf==0x02){*f->o=i;f->found=true;}},&f);
    return f.found;
}
static uint64_t pci_bar0(const PciDeviceInfo&i){uint64_t bar=i.bar[0]&~0xFULL;if((i.bar[0]&6u)==4u)bar|=static_cast<uint64_t>(i.bar[1])<<32;return bar;}
static bool map_mmio(uint64_t base,uint64_t bytes){
    if(!base)return false;
    if(base<0x100000000ULL)return true; // first 4 GiB already identity mapped
    const uint64_t start=base&~(PAGE_SIZE-1),end=(base+bytes+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    for(uint64_t a=start;a<end;a+=PAGE_SIZE)if(!vmm_map_page(a,a,PagePresent|PageWritable|PageCacheDisable))return false;
    return true;
}
static bool wait_ready(bool ready){for(uint32_t i=0;i<WAIT_SPINS;++i){if(((rd32(REG_CSTS)&1u)!=0)==ready)return true;__asm__ __volatile__("pause");}++g.timeouts;return false;}
static volatile uint32_t* doorbell(uint16_t qid,bool cq){uint32_t index=2u*qid+(cq?1u:0u);return reinterpret_cast<volatile uint32_t*>(gCtx.mmio+REG_DOORBELL+index*gCtx.doorbellStride);}

static bool alloc_queue(Queue&q,uint16_t qid,uint16_t depth,void*&sqPage,void*&cqPage){
    sqPage=pmm_alloc_page();cqPage=pmm_alloc_page();if(!sqPage||!cqPage)return false;
    memzero(sqPage,PAGE_SIZE);memzero(cqPage,PAGE_SIZE);
    q={};q.sq=static_cast<NvmeCommand*>(sqPage);q.cq=static_cast<NvmeCompletion*>(cqPage);q.sqPhys=vmm_translate(reinterpret_cast<uint64_t>(sqPage));q.cqPhys=vmm_translate(reinterpret_cast<uint64_t>(cqPage));
    if(!q.sqPhys||!q.cqPhys)return false;q.depth=depth;q.qid=qid;q.cqPhase=1;q.nextCid=1;return true;
}

static bool submit(Queue&q,NvmeCommand cmd,NvmeCompletion*out=nullptr){
    cmd.cid=q.nextCid++;
    q.sq[q.sqTail]=cmd;fence();
    q.sqTail=static_cast<uint16_t>((q.sqTail+1)%q.depth);*doorbell(q.qid,false)=q.sqTail;fence();
    for(uint32_t spin=0;spin<WAIT_SPINS;++spin){
        volatile NvmeCompletion* c=&q.cq[q.cqHead];
        const uint16_t status=c->status;
        if((status&1u)==q.cqPhase){
            NvmeCompletion done{};done.result=c->result;done.reserved=c->reserved;done.sqHead=c->sqHead;done.sqId=c->sqId;done.cid=c->cid;done.status=status;
            q.cqHead=static_cast<uint16_t>((q.cqHead+1)%q.depth);if(q.cqHead==0)q.cqPhase^=1u;*doorbell(q.qid,true)=q.cqHead;fence();
            if(out)*out=done;
            if(done.cid!=cmd.cid||((done.status>>1)&0x7FFFu)!=0){++g.commandErrors;return false;}
            return true;
        }
        __asm__ __volatile__("pause");
    }
    ++g.timeouts;return false;
}

static bool identify(uint32_t nsid,uint32_t cns,void*page){
    memzero(page,PAGE_SIZE);NvmeCommand c{};c.opcode=0x06;c.nsid=nsid;c.prp1=vmm_translate(reinterpret_cast<uint64_t>(page));c.cdw10=cns;return c.prp1&&submit(gCtx.admin,c);
}
static bool create_io_queues(){
    NvmeCommand cq{};cq.opcode=0x05;cq.prp1=gCtx.io.cqPhys;cq.cdw10=(static_cast<uint32_t>(gCtx.io.depth-1)<<16)|1u;cq.cdw11=1u; // physically contiguous
    if(!submit(gCtx.admin,cq))return false;
    NvmeCommand sq{};sq.opcode=0x01;sq.prp1=gCtx.io.sqPhys;sq.cdw10=(static_cast<uint32_t>(gCtx.io.depth-1)<<16)|1u;sq.cdw11=(1u<<16)|1u; // CQID=1, PC=1
    return submit(gCtx.admin,sq);
}

static bool rw_one(uint64_t lba,void*buffer,bool write){
    if(!g.namespaceReady||!gCtx.bouncePage||g.sectorSize==0||g.sectorSize>PAGE_SIZE||lba>=g.sectorCount)return false;
    if(write)memcpy8(gCtx.bouncePage,buffer,g.sectorSize);
    const uint64_t prp=vmm_translate(reinterpret_cast<uint64_t>(gCtx.bouncePage));if(!prp)return false;
    for(uint32_t attempt=0;attempt<3;++attempt){
        NvmeCommand c{};c.opcode=write?0x01:0x02;c.nsid=gCtx.nsid;c.prp1=prp;c.cdw10=static_cast<uint32_t>(lba);c.cdw11=static_cast<uint32_t>(lba>>32);c.cdw12=0;
        if(submit(gCtx.io,c)){if(!write)memcpy8(buffer,gCtx.bouncePage,g.sectorSize);if(write)++g.writeCommands;else ++g.readCommands;return true;}
        if(attempt+1<3)++g.retries;
    }
    return false;
}
static bool block_read_nvme(BlockDevice*,uint64_t lba,uint32_t count,void*out){if(!out||!count||lba+count<lba||lba+count>g.sectorCount)return false;auto*p=static_cast<uint8_t*>(out);for(uint32_t i=0;i<count;++i)if(!rw_one(lba+i,p+static_cast<uint64_t>(i)*g.sectorSize,false))return false;return true;}
static bool block_write_nvme(BlockDevice*,uint64_t lba,uint32_t count,const void*data){if(!data||!count||lba+count<lba||lba+count>g.sectorCount)return false;auto*p=static_cast<const uint8_t*>(data);for(uint32_t i=0;i<count;++i)if(!rw_one(lba+i,const_cast<uint8_t*>(p+static_cast<uint64_t>(i)*g.sectorSize),true))return false;return true;}
static bool block_flush_nvme(BlockDevice*){if(!g.ioQueueReady)return false;for(uint32_t attempt=0;attempt<3;++attempt){NvmeCommand c{};c.opcode=0x00;c.nsid=gCtx.nsid;if(submit(gCtx.io,c)){++g.flushCommands;return true;}if(attempt+1<3)++g.retries;}return false;}

static void cleanup_pages(){
    void** pages[]={&gCtx.adminSqPage,&gCtx.adminCqPage,&gCtx.ioSqPage,&gCtx.ioCqPage,&gCtx.identifyPage,&gCtx.bouncePage};
    for(auto p:pages)if(*p){pmm_free_page(*p);*p=nullptr;}
}
}

void nvme_probe(){gProbe={};PciDeviceInfo d{};if(!find_first(d))return;gProbe.found=true;gProbe.bus=d.bus;gProbe.device=d.device;gProbe.function=d.function;gProbe.bar0=pci_bar0(d);/* safe probe intentionally does not dereference BAR */gProbe.version=0;}
NvmeProbeStats nvme_probe_stats(){return gProbe;}

bool nvme_init_first(BlockDevice& out){
    out={};g={};gCtx={};PciDeviceInfo d{};if(!find_first(d))return false;
    g.found=true;g.bus=d.bus;g.device=d.device;g.function=d.function;g.vendorId=d.vendorId;g.deviceId=d.deviceId;g.bar0=pci_bar0(d);if(!g.bar0)return false;
    pci_enable_mmio_busmaster(d);if(!map_mmio(g.bar0,0x8000))return false;g.mmioMapped=true;gCtx.pci=d;gCtx.mmio=reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(g.bar0));
    gCtx.cap=rd64(REG_CAP);g.version=rd32(REG_VS);gProbe.version=g.version;const uint32_t mqes=static_cast<uint32_t>(gCtx.cap&0xFFFFu)+1u;gCtx.doorbellStride=4u<<static_cast<uint32_t>((gCtx.cap>>32)&0xFu);
    if(((gCtx.cap>>48)&0xFu)!=0)return false; // v33 PMM uses 4 KiB pages only
    uint32_t cc=rd32(REG_CC);if(cc&1u){wr32(REG_CC,cc&~1u);if(!wait_ready(false))return false;}
    const uint16_t adminDepth=static_cast<uint16_t>(mqes<ADMIN_Q_DEPTH?mqes:ADMIN_Q_DEPTH);const uint16_t ioDepth=static_cast<uint16_t>(mqes<IO_Q_DEPTH?mqes:IO_Q_DEPTH);if(adminDepth<2||ioDepth<2)return false;
    if(!alloc_queue(gCtx.admin,0,adminDepth,gCtx.adminSqPage,gCtx.adminCqPage))return false;
    if(!alloc_queue(gCtx.io,1,ioDepth,gCtx.ioSqPage,gCtx.ioCqPage)){cleanup_pages();return false;}
    gCtx.identifyPage=pmm_alloc_page();gCtx.bouncePage=pmm_alloc_page();if(!gCtx.identifyPage||!gCtx.bouncePage){cleanup_pages();return false;}
    wr32(REG_INTMS,0xFFFFFFFFu);wr32(REG_AQA,(static_cast<uint32_t>(adminDepth-1)<<16)|(adminDepth-1));wr64(REG_ASQ,gCtx.admin.sqPhys);wr64(REG_ACQ,gCtx.admin.cqPhys);
    cc=(6u<<16)|(4u<<20)|1u;wr32(REG_CC,cc);if(!wait_ready(true)){cleanup_pages();return false;}g.controllerReady=true;
    if(!identify(0,1,gCtx.identifyPage)){cleanup_pages();return false;}auto*ctrl=static_cast<uint8_t*>(gCtx.identifyPage);uint32_t nn=*reinterpret_cast<uint32_t*>(ctrl+516);if(nn==0){cleanup_pages();return false;}
    gCtx.nsid=1;if(!identify(gCtx.nsid,0,gCtx.identifyPage)){cleanup_pages();return false;}auto*ns=static_cast<uint8_t*>(gCtx.identifyPage);const uint64_t nsze=*reinterpret_cast<uint64_t*>(ns);const uint8_t flbas=ns[26]&0x0Fu;const uint8_t ds=ns[128+flbas*4+2];if(ds<9||ds>12||!nsze){cleanup_pages();return false;}g.sectorSize=1u<<ds;g.sectorCount=nsze;g.namespaceReady=true;
    if(!create_io_queues()){cleanup_pages();return false;}g.ioQueueReady=true;g.queueDepth=ioDepth;
    gCtx.block.name="NVMe namespace 1";gCtx.block.sectorSize=g.sectorSize;gCtx.block.sectorCount=g.sectorCount;gCtx.block.context=&gCtx;gCtx.block.read=block_read_nvme;gCtx.block.write=block_write_nvme;gCtx.block.flush=block_flush_nvme;out=gCtx.block;return true;
}
NvmeStats nvme_stats(){return g;}
