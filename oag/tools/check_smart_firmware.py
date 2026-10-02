#!/usr/bin/env python3
"""Fail a build if SMART Flash overlaps code or static RAM consumes the heap."""
import os
import subprocess
import sys
nm = os.environ.get('OAG_ARM_NM', 'arm-none-eabi-nm')
symbols = {}
for line in subprocess.check_output([nm, sys.argv[1]], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3 and fields[2] in ('__flash_binary_end', '__bss_end__', '__HeapLimit'):
        symbols[fields[2]] = int(fields[0], 16)
smart_start = 4 * 1024 * 1024 - 3 * 4096 - 2 * 4 * 4096 - 2 * 6 * 4096 - 20 * 2 * 4 * 4096 - 20 * 2 * 14 * 4096
flash_end = symbols['__flash_binary_end'] - 0x10000000
heap = symbols['__HeapLimit'] - symbols['__bss_end__']
if flash_end >= smart_start:
    raise SystemExit('OAG firmware overlaps SMART storage')
if heap < 32 * 1024:
    raise SystemExit('OAG firmware leaves less than 32 KiB for runtime heap')
print(f'OAG_SMART_MEMORY=PASS code_end={flash_end} smart_start={smart_start} flash_gap={smart_start-flash_end} heap_available={heap}')
