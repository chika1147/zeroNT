#!/usr/bin/env python3
from pathlib import Path
import sys, zlib

if len(sys.argv)!=6:
    print('usage: patch_fat16_boot.py template.img out.img BOOTX64.EFI KERNEL.ELF ROOTFS.IMG')
    raise SystemExit(2)

tpl,outp,efip,kernelp,rootfsp=map(Path,sys.argv[1:])
b=bytearray(tpl.read_bytes())
efi=efip.read_bytes(); kernel=kernelp.read_bytes(); rootfs=rootfsp.read_bytes()
BPS=int.from_bytes(b[11:13],'little'); SPC=b[13]; RESERVED=int.from_bytes(b[14:16],'little'); NFATS=b[16]
ROOTENTS=int.from_bytes(b[17:19],'little'); TOTAL=int.from_bytes(b[19:21],'little') or int.from_bytes(b[32:36],'little'); FATSZ=int.from_bytes(b[22:24],'little')
if BPS!=512 or not FATSZ or not ROOTENTS: raise SystemExit('template is not FAT12/16')
ROOT_LBA=RESERVED+NFATS*FATSZ; ROOT_SECTORS=(ROOTENTS*32+BPS-1)//BPS; DATA_LBA=ROOT_LBA+ROOT_SECTORS
CLUSTER_BYTES=BPS*SPC

def fat_get(c):
    o=RESERVED*BPS+c*2
    return int.from_bytes(b[o:o+2],'little')
def fat_set(c,v):
    for f in range(NFATS):
        o=(RESERVED+f*FATSZ)*BPS+c*2
        b[o:o+2]=int(v&0xffff).to_bytes(2,'little')
def cluster_off(c): return (DATA_LBA+(c-2)*SPC)*BPS

def sfn(comp):
    comp=comp.upper(); parts=comp.split('.',1); base=parts[0]; ext=parts[1] if len(parts)>1 else ''
    return (base[:8].ljust(8)+ext[:3].ljust(3)).encode('ascii')

def find_entry(path):
    comps=[x for x in path.replace('\\','/').split('/') if x]
    dir_cluster=None
    for ix,comp in enumerate(comps):
        target=sfn(comp)
        entries=[]
        if dir_cluster is None:
            base=ROOT_LBA*BPS; count=ROOTENTS
            entries=[base+i*32 for i in range(count)]
        else:
            c=dir_cluster; guard=0
            while 2<=c<0xfff8 and guard<65536:
                base=cluster_off(c); entries.extend(base+i*32 for i in range(CLUSTER_BYTES//32)); c=fat_get(c); guard+=1
        found=None
        for o in entries:
            e=b[o:o+32]
            if e[0]==0: break
            if e[0]==0xe5 or e[11]==0x0f: continue
            if bytes(e[:11])==target: found=o; break
        if found is None: raise KeyError(path)
        if ix==len(comps)-1: return found
        if not (b[found+11]&0x10): raise KeyError(path)
        dir_cluster=int.from_bytes(b[found+26:found+28],'little')
    raise KeyError(path)

def chain(first):
    out=[]; c=first; guard=0
    while 2<=c<0xfff8 and guard<65536:
        out.append(c); c=fat_get(c); guard+=1
    return out

def free_chain(first):
    for c in chain(first): fat_set(c,0)

def alloc(n):
    got=[]
    maxc=(TOTAL-DATA_LBA)//SPC+1
    for c in range(2,maxc+1):
        if fat_get(c)==0:
            got.append(c)
            if len(got)==n: break
    if len(got)!=n: raise RuntimeError('FAT16 boot image full')
    for i,c in enumerate(got): fat_set(c, got[i+1] if i+1<len(got) else 0xffff)
    return got

def patch(paths,data):
    offs=[find_entry(p) for p in paths]
    oldfirst=int.from_bytes(b[offs[0]+26:offs[0]+28],'little')
    # Aliases share the same old chain in this template; free once.
    if oldfirst>=2: free_chain(oldfirst)
    n=max(1,(len(data)+CLUSTER_BYTES-1)//CLUSTER_BYTES)
    ch=alloc(n)
    pos=0
    for c in ch:
        o=cluster_off(c); take=min(CLUSTER_BYTES,len(data)-pos)
        b[o:o+CLUSTER_BYTES]=b'\0'*CLUSTER_BYTES
        if take>0: b[o:o+take]=data[pos:pos+take]; pos+=take
    for o in offs:
        b[o+26:o+28]=ch[0].to_bytes(2,'little'); b[o+28:o+32]=len(data).to_bytes(4,'little')
    print('patched',paths,'size',len(data),'cluster',ch[0],'n',len(ch))

kernel_crc=zlib.crc32(kernel)&0xffffffff; rootfs_crc=zlib.crc32(rootfs)&0xffffffff
cfg=(f'kernel=\\SYSTEM\\KERNEL.ELF\r\nrootfs=\\SYSTEM\\ROOTFS.IMG\r\nvideo=auto\r\nmode=normal\r\nregister_boot_entry=0\r\n'
     f'kernel_crc32=0x{kernel_crc:08X}\r\nrootfs_crc32=0x{rootfs_crc:08X}\r\n').encode('ascii')
nsh=b'@echo -off\r\n\\EFI\\BOOT\\BOOTX64.EFI\r\n'
patch(['/EFI/BOOT/BOOTX64.EFI'],efi)
patch(['/SYSTEM/KERNEL.ELF','/KERNEL.ELF'],kernel)
patch(['/SYSTEM/ROOTFS.IMG','/ROOTFS.IMG'],rootfs)
patch(['/CONFIG/BOOT.CFG'],cfg)
patch(['/STARTUP.NSH'],nsh)
outp.write_bytes(b)
print(outp, len(b), f'{zlib.crc32(b)&0xffffffff:08x}')
