// Browser integration uses the real queued C++ API and A/B store. Only Flash is mocked.
#include "oag/config/oag_smart_api.h"
#include <array>
#include <cstring>
#include <iostream>
#include <string>
using namespace oag;
class RamFlash final:public OagSmartStorage {
    alignas(256) std::array<std::array<std::uint8_t,32768>,40> banks_ {};
public:
    RamFlash() {for(auto& b:banks_)b.fill(0xff);}
    const OagSmartRecord* record(std::size_t g,std::uint8_t copy)const override {
        return g<20&&copy<2?reinterpret_cast<const OagSmartRecord*>(banks_[g*2+copy].data()):nullptr;
    }
    bool write(std::size_t g,std::uint8_t copy,const std::uint8_t* data,std::size_t size)override {
        if(g>=20||copy>1||size!=32768)return false;
        std::memcpy(banks_[g*2+copy].data(),data,size);return true;
    }
    bool ready()const override{return true;}
};
int main() {
    static RamFlash flash;OagSmartStore store(flash);store.activate(0);OagSmartApi api(store);api.runtime(true);
    std::string line;
    while(std::getline(std::cin,line)) {
        const auto a=line.find('\t'),b=line.find('\t',a+1);
        if(a==line.npos||b==line.npos)return 1;
        const auto method=line.substr(0,a),path=line.substr(a+1,b-a-1),body=line.substr(b+1);
        if(!api.handle(method.c_str(),path.c_str(),body.c_str()))std::cout<<"404\t{}"<<std::endl;
        else {
            std::cout<<api.status()<<'\t'<<api.response()<<std::endl;
            api.task();std::uint8_t game,weapon;
            if(api.selectContext(game,weapon))store.activate(game);
        }
    }
}
