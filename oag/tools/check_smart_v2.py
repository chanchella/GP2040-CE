#!/usr/bin/env python3
"""Guard the intentionally replaced systems and unchanged transport boundaries."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/'firmware/src/main.cpp').read_text()
portal=(root/'firmware/src/diamond_wifi_portal.cpp').read_text()
cmake=(root/'firmware/CMakeLists.txt').read_text()
for retired in ('diamondCombos_', 'nativeKmCombos_', 'smartLegacyPrograms_', 'activeRecoilProfile', 'proSettingsFor('):
    assert retired not in main, f'Retired runtime still connected: {retired}'
for retired in ('src/mapping/diamond_combo_engine.cpp', 'src/mapping/native_km_combo_engine.cpp'):
    assert retired not in cmake, f'Retired engine still in production build: {retired}'
assert 'handleProInputRequest(' not in portal and 'pro_input_portal.inc' not in portal
assert '410 Gone' in portal and '/api/pro-' in portal and '/api/recoil' in portal and '/api/combo' in portal
for retired in ('kDashboardHtml','kGwc','kComboCss','kNamesJs','kAppJs'):
    assert retired not in portal
for name in ('oag.html','oag-home.html'):
    page=(root/'tools/smart_ui'/name).read_text()
    assert 'lang="ar-EG"' in page and 'dir="rtl"' in page
assert 'api/recoil' not in (root/'tools/smart_ui/oag-history.js').read_text()
assert (root/'tools/smart_ui/oag-load.js').read_text().count("'cancel'")==1
assert 'serviceOagAutoMouse();' in main and 'OagAutoInput::settings(k)' in main
print('OAG_SMART_V2=PASS — retired runtimes/editors blocked, automatic input enabled, Arabic UI')
