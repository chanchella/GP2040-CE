#!/usr/bin/env python3
"""فحص UF2 والذاكرة، مع حماية مساحة إعدادات OAG القديمة والجديدة."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

p=argparse.ArgumentParser()
p.add_argument('uf2',type=Path)
p.add_argument('--elf',type=Path)
p.add_argument('--size-tool',default='arm-none-eabi-size')
p.add_argument('--stack-dir',type=Path)
a=p.parse_args()
data=a.uf2.read_bytes()
assert data and len(data)%512==0,'حجم UF2 مش سليم'
pages=[];absolute=0
for at in range(0,len(data),512):
    block=data[at:at+512]
    m0,m1,flags,addr,size,index,count,family=struct.unpack_from('<8I',block)
    assert (m0,m1,struct.unpack_from('<I',block,508)[0])==(0x0a324655,0x9e5d5157,0x0ab16f30)
    assert size==256 and flags&0x2000,'بلوك UF2 مش معرّف للبيكو'
    if family==0xe48bff57:
        # Picotool RP2350-E10 ignored absolute marker; not application Flash.
        assert at==0 and addr==0x10ffff00 and flags==0xa000 and index==0 and count==2
        assert block[32:288]==b'\xef'*256 and struct.unpack_from('<I',block,288)[0]==0x9957e304
        absolute+=1
    else:
        assert family==0xe48bff59 and flags==0x2000,'الملف مش RP2350 ARM Secure'
        assert 0x10000000<=addr and addr+size<=0x10209000,'البرنامج دخل مساحة إعدادات OAG'
        pages.append((addr,size,index,count))
assert pages and absolute==1
assert all(v[2]==i and v[3]==len(pages) for i,v in enumerate(pages))
assert len({v[0] for v in pages})==len(pages)
assert pages[0][0]==0x10000000
result={'gate':'OAG_SMART_ARTIFACT_PASS','sha256':hashlib.sha256(data).hexdigest(),
        'uf2_bytes':len(data),'application_blocks':len(pages),'family':'RP2350_ARM_S',
        'application_end':hex(max(v[0]+v[1] for v in pages)),
        'smart_storage_start':'0x10209000','old_game_storage_start':'0x10349000'}
if a.elf:
    line=subprocess.check_output([a.size_tool,str(a.elf)],text=True).splitlines()[1].split()
    text,ramdata,bss=map(int,line[:3]);assert ramdata+bss<=480*1024,'مساحة RAM المتبقية قليلة'
    nmtool=a.size_tool.removesuffix('size')+'nm'
    symbols={}
    for row in subprocess.check_output([nmtool,str(a.elf)],text=True).splitlines():
        fields=row.split()
        if len(fields)==3:symbols[fields[2]]=int(fields[0],16)
    # GNU size counts executable .data as text. Linker symbols include RAM code,
    # alignment and vectors, and exclude the two reserved scratch-bank stacks.
    free=symbols['__HeapLimit']-symbols['__bss_end__']
    assert free>=24*1024,'مساحة Heap المتبقية قليلة'
    result.update(text_bytes=text,bss_bytes=bss,
                  initialized_ram_bytes=symbols['__data_end__']-symbols['__data_start__'],
                  heap_headroom_bytes=free,
                  reserved_stack_bytes=symbols['__StackTop']-symbols['__HeapLimit'])
if a.stack_dir:
    frames=[]
    for f in a.stack_dir.rglob('*.su'):
        for line in f.read_text(errors='replace').splitlines():
            fields=line.split('\t')
            if len(fields)>2 and any(s in fields[0] for s in ('/oag_smart_','/oag_weapon_tuning_','/oag_auto_input')):
                n=int(fields[1]);assert n<=1280,'إطار Stack كبير في الوحدة الجديدة: '+fields[0]
                frames.append(n)
    assert frames,'ملفات قياس Stack مش موجودة'
    result['largest_new_stack_frame']=max(frames)
print(json.dumps(result,indent=2,ensure_ascii=False))
