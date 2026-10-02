#pragma once
#include "oag/config/oag_smart_store.h"
namespace oag {
class OagSmartApi {
public:
    explicit OagSmartApi(OagSmartStore& store) : store_(store) {}
    bool handle(const char* method,const char* path,const char* body);
    void task();
    const char* response() const { return response_.data(); }
    unsigned status() const { return status_; }
    void runtime(bool available) { runtime_=available; }
    void metrics(std::uint32_t runs,std::uint8_t slot) { executions_=runs; lastSlot_=slot; }
    bool selectContext(std::uint8_t& game,std::uint8_t& weapon);
private:
    enum class Op { None, Begin, Branch, Apply, SaveCombo, Weapon, SaveWeapon, Cancel, Test, Context, DiscardCombo, DiscardWeapon };
    void result(unsigned status,const char* message);
    void queue(Op,std::uint32_t game=0,std::uint32_t slot=0,std::uint32_t token=0,std::uint32_t index=0);
    OagSmartStore& store_;
    OagSmartCombo scratch_ {};
    OagBranch branch_ {};
    OagWeaponSettings weapon_ {};
    std::array<char,4096> response_ {};
    Op op_=Op::None;
    std::uint32_t game_=0,slot_=0,token_=0,index_=0,ticket_=0,done_=0;
    std::uint8_t contextGame_=255,contextWeapon_=255;
    unsigned status_=200;
    bool runtime_=false,success_=true;
    std::uint32_t executions_=0;
    std::uint8_t lastSlot_=255;
    const char* error_="";
};
} // namespace oag
