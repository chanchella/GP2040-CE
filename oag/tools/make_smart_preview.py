#!/usr/bin/env python3
"""معاينة تفاعلية من نفس ملفات واجهة الـFirmware، في ملف HTML واحد."""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).parent / 'smart_ui'
page = (root / 'oag.html').read_text()
page = re.sub(r'<link rel="stylesheet" href="/(oag(?:-extra)?\.css)">',
              lambda m: '<style>' + (root / m[1]).read_text() + '</style>', page)
modules = ['controls', 'wire', 'net', 'condition', 'action', 'builder', 'history',
           'combo', 'weapon', 'advanced', 'weapon-events', 'boot']
scripts = '<script>globalThis.OAG_PREVIEW=true;</script>\n' + '\n'.join(
    '<script>' + (root / f'oag-{name}.js').read_text().replace('</script', '<\\/script') + '</script>'
    for name in modules)
page = page.replace('<script src="/oag-load.js"></script>', scripts)
page = page.replace('<a href="/" class="oag-logo">', '<a href="#" class="oag-logo">')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(page)
print(f'OAG_PREVIEW=PASS ({args.output.stat().st_size} bytes)')
