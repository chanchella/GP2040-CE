#!/usr/bin/env python3
"""Host-test the actual firmware HTTP handler with only hardware/Flash stubbed."""
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
handler=s[s.index('void DiamondWifiPortal::handleHttpRequest('):s.rindex('\n} // namespace oag::firmware')]
helpers=s[s.index('bool safeName('):s.index('void releaseClient(')]
Path(sys.argv[2]).write_text('// Generated from the firmware portal.\n'+helpers+'\nnamespace oag::firmware {\n'+handler+'\n}\n')
