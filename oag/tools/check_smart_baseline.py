#!/usr/bin/env python3
"""نتأكد إن إضافة OAG ما عدلتش ملفات مسارات الإدخال والنقل المستقرة."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
baseline='2c7434a772b498ee7a8d53b3d45d87defb0c5231'
files=subprocess.check_output(['git','ls-tree','-r','--name-only',baseline,'oag'],cwd=root,text=True).splitlines()
protected=[]
for name in files:
    if name.startswith(('oag/src/protocol/','oag/include/oag/protocol/','oag/src/device/','oag/include/oag/device/','oag/src/output/','oag/include/oag/output/','oag/src/bluetooth/','oag/include/oag/bluetooth/','oag/src/feedback/','oag/include/oag/feedback/')):
        protected.append(name)
    elif name.startswith('oag/firmware/') and any(s in name for s in ('bluetooth','usb_pio','xinput','pc_native_km','usb_descriptors','diamond_config_store','diamond_game_library_store','lwipopts','btstack_config')):
        protected.append(name)
    elif name.startswith(('oag/src/mapping/','oag/include/oag/mapping/')) and any(s in name for s in ('logical_slot','pro_input','mouse_to_stick','keyboard_mouse','diamond_combo','native_km')):
        protected.append(name)
changed=subprocess.check_output(['git','diff',baseline,'--',*protected],cwd=root,text=True)
assert not changed,'فيه ملف من المسارات المستقرة اتغير'
print(f'OAG_STABLE_PATHS=PASS ({len(protected)} files match FIX2 {baseline})')
