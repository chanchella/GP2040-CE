#include <cassert>
#include <vector>
#include <cstdint>
#include "oag/firmware/pc_native_km_output.h"
#include "oag/mapping/pro_profile_shortcut.h"
using namespace oag;
namespace { struct Sent {std::uint8_t instance;std::vector<std::uint8_t> data;};std::vector<Sent> sent;bool ready=true,accept=true,boot=false; }
bool tud_hid_n_ready(std::uint8_t){return ready;}
bool tud_hid_n_report(std::uint8_t n,std::uint8_t,const void* p,std::uint16_t size){if(!accept)return false;const auto* b=static_cast<const std::uint8_t*>(p);sent.push_back({n,{b,b+size}});return true;}
std::uint8_t tud_hid_n_get_protocol(std::uint8_t){return boot?0:1;}
namespace oag::firmware { std::uint8_t nativeKeyboardHidInstance(){return 4;}std::uint8_t nativeMouseHidInstance(){return 5;} }
std::int16_t axis(const Sent& s,unsigned at){return static_cast<std::int16_t>(s.data[at]|std::uint16_t(s.data[at+1])<<8);}
int main(){
 firmware::PcNativeKmOutput out;out.setEnabled(true);KeyboardState keys;MouseState mouse;keys.setPressed(4,true);out.updateState(keys,mouse);out.addMouseMotion(300,-400,2,-1);out.addMouseMotion(500,-600,0,0);
 out.task(1,false,false);assert(sent.empty());out.task(2,true,false);assert(sent.size()==1&&sent.back().instance==4);out.task(3,false,true);assert(sent.size()==2&&sent.back().data.size()==7&&axis(sent.back(),1)==800&&axis(sent.back(),3)==-1000);out.task(4);assert(sent.size()==2);
 ready=false;out.addMouseMotion(400,0,0,0);out.task(5);out.addMouseMotion(200,0,0,0);ready=true;accept=false;out.task(6);assert(sent.size()==2);accept=true;out.task(7);assert(axis(sent.back(),1)==600);
 out.addMouseMotion(1000,0,0,0);out.releaseAll();out.setEnabled(true);out.task(8,false,false);assert(sent[sent.size()-2].data[2]==0&&axis(sent.back(),1)==0);out.task(9);assert(sent.back().instance==4&&sent.back().data[2]==4);sent.clear();
 out.addMouseMotion(40000,-40000,0,0);out.task(10);assert(axis(sent.back(),1)==32767&&axis(sent.back(),3)==-32768);out.task(11);assert(axis(sent.back(),1)==7233&&axis(sent.back(),3)==-7232);
 sent.clear();boot=true;out.addMouseMotion(200,-200,1,1);out.task(12);assert(sent.back().data.size()==3&&static_cast<std::int8_t>(sent.back().data[1])==127);out.task(13);assert(static_cast<std::int8_t>(sent.back().data[1])==73);out.task(14);assert(sent.size()==2);
 ProProfileShortcut shortcut;keys={};keys.setPressed(0x3A,true);keys.setPressed(0x27,true);keys.setPressed(4,true);assert(shortcut.poll(keys,1).action==ProShortcutAction::None);assert(shortcut.poll(keys,1000001).action==ProShortcutAction::CancelGameProfile);assert(shortcut.poll(keys,2000001).action==ProShortcutAction::None);auto masked=keys;shortcut.mask(masked);assert(!masked.pressed(0x3A)&&!masked.pressed(0x27)&&masked.pressed(4));keys.setPressed(0x3A,false);shortcut.poll(keys,2000002);masked=keys;shortcut.mask(masked);assert(!masked.pressed(0x27));keys={};shortcut.poll(keys,2000003);keys.setPressed(0x27,true);masked=keys;shortcut.mask(masked);assert(masked.pressed(0x27));
 for(unsigned game:{10u,20u}){ProProfileShortcut sc;keys={};keys.setPressed(0x3A,true);keys.setPressed(0x1D+game/10,true);sc.poll(keys,1);keys.setPressed(0x1D+game/10,false);keys.setPressed(0x27,true);sc.poll(keys,200);auto result=sc.poll(keys,1000200);assert(result.action==ProShortcutAction::Game&&result.number==game);}
 ProProfileShortcut cancel;keys={};keys.setPressed(0x3A,true);keys.setPressed(0x27,true);cancel.poll(keys,1);keys.setPressed(0x27,false);assert(cancel.poll(keys,1000001).action==ProShortcutAction::None);
 ProProfileShortcut weapon;keys={};keys.setPressed(0x3E,true);keys.setPressed(0x21,true);weapon.poll(keys,1);assert(weapon.poll(keys,1000001).number==4);
}
