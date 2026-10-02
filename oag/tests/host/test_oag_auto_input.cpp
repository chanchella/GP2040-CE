#include <cassert>
#include <climits>
#include <iostream>
#include "oag/mapping/oag_auto_input.h"
using namespace oag;
int main() {
    for (auto kind:{ProInputKind::Mouse,ProInputKind::Keyboard,ProInputKind::Gamepad}) {
        const auto& s=OagAutoInput::settings(kind);assert(validProInputSettings(s));assert(s.processingHz==1000&&s.targetDpi==2400&&s.sourceDpi==0);
    }
    OagAutoInput autoInput;const DeviceId d{0,1};
    auto a=autoInput.mouse(d,{8,-4},1000);assert(a.dx==6&&a.dy==-3);assert(autoInput.flush(d,1999).dx==0);
    auto b=autoInput.flush(d,2000);assert(a.dx+b.dx==8&&a.dy+b.dy==-4);assert(autoInput.flush(d,3000).dx==0);
    a=autoInput.mouse(d,{1,-1},3000);assert(a.dx==1&&a.dy==-1);
    a=autoInput.mouse(d,{500,-900},4000);assert(a.dx==500&&a.dy==-900);
    autoInput.mouse(d,{8,4},5000);a=autoInput.mouse(d,{-8,-4},5500);b=autoInput.flush(d,6500);assert(a.dx+b.dx==-6&&a.dy+b.dy==-3); // Remaining counts cancel the previous packet exactly.
    autoInput.mouse(d,{8,4},7000);assert(autoInput.flush({0,2},8000).dx==0); // Reconnect never inherits an old device's tail.
    autoInput.mouse({0,2},{8,4},9000);autoInput.reset();assert(autoInput.flush({0,2},10000).dx==0);
    LogicalGamepadState pad{};pad.connected=true;pad.buttons=123;pad.dpad=7;pad.rightTrigger=45678;pad.rx=100000000;
    auto shaped=autoInput.gamepad(d,pad);assert(shaped.rx==pad.rx);pad.rx+=1000000;shaped=autoInput.gamepad(d,pad);assert(shaped.rx<pad.rx&&shaped.rx>100000000);
    assert(shaped.buttons==pad.buttons&&shaped.dpad==pad.dpad&&shaped.rightTrigger==pad.rightTrigger);
    pad.rx=INT32_MAX;assert(autoInput.gamepad(d,pad).rx==INT32_MAX);pad.rx=0;assert(autoInput.gamepad(d,pad).rx==0);pad.rx=INT32_MIN;assert(autoInput.gamepad(d,pad).rx==INT32_MIN);
    assert(autoInput.gamepad({0,3},pad).rx==pad.rx);
    std::cout<<"OAG automatic input: reference defaults, motion conservation, bounded tail, reconnect and exact buttons PASS\n";
}
