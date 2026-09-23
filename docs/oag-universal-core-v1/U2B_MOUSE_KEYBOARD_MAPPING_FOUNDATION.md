# U2B — Mouse/keyboard input and mouse-to-stick foundation

This phase creates the software-side foundations needed before generic HID
host parsing is connected to the Universal Core.

## Input models

- Keyboard state uses a fixed 256-usage bitset with no heap allocation.
- Mouse state includes buttons, relative X/Y, wheel and horizontal pan.

## Mouse-to-stick mapper

The mapper is a clean OAG implementation informed by the behavior required
from the GIMX-style workflow:

- per-axis sensitivity;
- configurable nonlinear exponent / ballistic response;
- immediate deadzone compensation;
- circular or square stick boundary;
- optional Y inversion;
- full signed 32-bit canonical OAG stick output.

The 32-bit internal representation intentionally preserves much more precision
than the final XInput 16-bit report. Quantization and future sub-position
accumulation therefore remain output/profile concerns instead of polluting raw
mouse input.

## Runtime status

This phase does not yet claim generic HID hardware support. The next host phase
will parse real TinyUSB keyboard/mouse reports into these state objects, then
feed them into the mapping pipeline.
