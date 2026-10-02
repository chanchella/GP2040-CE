#include "oag/config/oag_smart_api.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace oag {
namespace {
bool field(const char* body,const char* key,char* out,std::size_t cap) {
    if (!body || !out || !cap) return false;
    const auto len=std::strlen(key); bool found=false;
    for (const char* p=body;*p;) {
        const char* end=std::strchr(p,'&'); if (!end) end=p+std::strlen(p);
        if (std::size_t(end-p)>len && !std::strncmp(p,key,len) && p[len]=='=') {
            if (found) return false; // reject ambiguous duplicate fields
            std::size_t n=0;
            for (auto* v=p+len+1;v<end;++v) {
                unsigned char c=static_cast<unsigned char>(*v);
                if (c=='+') c=' ';
                else if (c=='%') {
                    if (v+2>=end) return false;
                    const auto hex=[](char x)->int { return x>='0'&&x<='9'?x-'0':x>='a'&&x<='f'?x-'a'+10:x>='A'&&x<='F'?x-'A'+10:-1; };
                    const int a=hex(v[1]),b=hex(v[2]); if (a<0||b<0) return false;
                    c=static_cast<unsigned char>(a*16+b); v+=2;
                }
                if (!c || n+1>=cap) return false;
                out[n++]=char(c);
            }
            out[n]=0; found=true;
        }
        p=*end?end+1:end;
    }
    return found;
}
bool number(const char* body,const char* key,std::uint32_t lo,std::uint32_t hi,std::uint32_t& out) {
    char b[24]{}; if (!field(body,key,b,sizeof(b)) || !*b) return false;
    std::uint64_t n=0;
    for (const char* p=b;*p;++p) {
        if (*p<'0'||*p>'9') return false;
        n=n*10+unsigned(*p-'0'); if (n>hi) return false;
    }
    if (n<lo) return false;
    out=std::uint32_t(n); return true;
}
bool decode(const char* body,void* target,std::size_t size) {
    char text[2*sizeof(OagBranch)+1]{};
    if (size>sizeof(OagBranch) || !field(body,"wire",text,sizeof(text)) || std::strlen(text)!=size*2) return false;
    auto* bytes=static_cast<std::uint8_t*>(target);
    for (std::size_t i=0;i<size;++i) {
        const auto h=[](char c)->int { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1; };
        const int a=h(text[i*2]),b=h(text[i*2+1]); if (a<0||b<0) return false;
        bytes[i]=std::uint8_t(a*16+b);
    }
    return true;
}
struct Json {
    char* out; std::size_t capacity,used=0; bool ok=true;
    void add(const char* fmt,...) {
        if (!ok) return;
        va_list args; va_start(args,fmt);
        const int n=std::vsnprintf(out+used,capacity-used,fmt,args); va_end(args);
        if (n<0 || std::size_t(n)>=capacity-used) { ok=false; return; } used+=std::size_t(n);
    }
    void string(const char* s) {
        add("\"");
        for (;*s;++s) {
            const auto c=static_cast<unsigned char>(*s);
            if (c=='"'||c=='\\') add("\\%c",c);
            else if (c<32) add("\\u%04x",unsigned(c));
            else add("%c",c);
        }
        add("\"");
    }
    void wire(const void* p,std::size_t size) {
        const auto* bytes=static_cast<const std::uint8_t*>(p);
        add("\""); for (std::size_t i=0;i<size;++i) add("%02x",unsigned(bytes[i])); add("\"");
    }
};
}
void OagSmartApi::result(unsigned status,const char* text) {
    status_=status; Json j{response_.data(),response_.size()};
    j.add("{\"ok\":false,\"error\":"); j.string(text); j.add("}");
}
void OagSmartApi::queue(Op op,std::uint32_t game,std::uint32_t slot,std::uint32_t token,std::uint32_t index) {
    op_=op; game_=game; slot_=slot; token_=token; index_=index; ++ticket_; status_=202;
    std::snprintf(response_.data(),response_.size(),"{\"ok\":true,\"ticket\":%lu}",static_cast<unsigned long>(ticket_));
}
bool OagSmartApi::handle(const char* method,const char* path,const char* body) {
    if (!method||!path||std::strncmp(path,"/api/oag/",9)) return false;
    status_=200;
    if (!std::strcmp(method,"GET") && !std::strcmp(path,"/api/oag/status")) {
        Json j{response_.data(),response_.size()};
        j.add("{\"doneTicket\":%lu,\"ok\":%s,\"busy\":%s,\"runtime\":%s,\"ready\":%s,\"game\":%u,\"writes\":%lu,\"executions\":%lu,\"lastSlot\":%u,\"error\":",
            static_cast<unsigned long>(done_),success_?"true":"false",op_==Op::None?"false":"true",
            runtime_?"true":"false",store_.ready()?"true":"false",unsigned(store_.activeGame()+1),
            static_cast<unsigned long>(store_.flashWrites()),static_cast<unsigned long>(executions_),unsigned(lastSlot_));
        j.string(error_); j.add("}"); return true;
    }
    const char* query=std::strchr(path,'?'); query=query?query+1:"";
    if (op_!=Op::None && !std::strcmp(method,"GET")) {
        result(409,"استنى التعديل الحالي يخلص"); return true;
    }
    std::uint32_t game=0,slot=0,saved=0;
    if (!std::strcmp(method,"GET") &&
        (!std::strncmp(path,"/api/oag/combo?",15) || !std::strncmp(path,"/api/oag/weapon?",16))) {
        const bool weapon=path[9]=='w';
        if (!number(query,"game",1,20,game) || !number(query,"slot",1,weapon?24:16,slot)) {
            result(400,"اختار لعبة وخانة صح"); return true;
        }
        number(query,"saved",0,1,saved);
        Json j{response_.data(),response_.size()};
        j.add("{\"schema\":1,\"game\":%u,\"slot\":%u,",unsigned(game),unsigned(slot));
        if (weapon) {
            store_.weapon(game-1,slot-1,weapon_,saved!=0);
            j.add("\"wire\":"); j.wire(&weapon_,sizeof(weapon_)); j.add("}");
        } else {
            store_.combo(game-1,slot-1,scratch_,saved!=0);
            j.add("\"name\":"); j.string(scratch_.name.data());
            j.add(",\"enabled\":%u,\"mode\":%u,\"cancelable\":%u,\"branches\":[",
                unsigned(scratch_.enabled),unsigned(scratch_.mode),unsigned(scratch_.cancelable));
            for (std::size_t i=0;i<scratch_.branchCount;++i) { if (i) j.add(","); j.wire(&scratch_.branches[i],sizeof(OagBranch)); }
            j.add("]}");
        }
        if (!j.ok) result(500,"البيانات أكبر من مساحة الرد");
        return true;
    }
    if (std::strcmp(method,"POST")) { result(404,"الطلب ده مش موجود"); return true; }
    if (op_!=Op::None) { result(409,"استنى الطلب اللي قبله يخلص"); return true; }
    if (!std::strcmp(path,"/api/oag/cancel")) { queue(Op::Cancel); return true; }
    if (!std::strcmp(path,"/api/oag/discard-combo")) { queue(Op::DiscardCombo); return true; }
    if (!std::strcmp(path,"/api/oag/discard-weapon")) { queue(Op::DiscardWeapon); return true; }
    if (!number(body,"game",1,20,game)) { result(400,"رقم اللعبة مش صح"); return true; }
    if (!std::strcmp(path,"/api/oag/context")) {
        if (!number(body,"weapon",0,24,slot)) result(400,"رقم السلاح مش صح");
        else queue(Op::Context,game-1,slot?slot-1:255);
        return true;
    }
    const bool weapon=!std::strncmp(path,"/api/oag/weapon-",16);
    if (!number(body,"slot",1,weapon?24:16,slot)) { result(400,"رقم الخانة مش صح"); return true; }
    std::uint32_t token=0,index=0;
    if (!std::strcmp(path,"/api/oag/weapon-preview")) {
        if (!decode(body,&weapon_,sizeof(weapon_)) || !oagValidateWeapon(weapon_)) result(400,"راجع قيم السلاح والتوقيت");
        else queue(Op::Weapon,game-1,slot-1);
    } else if (!std::strcmp(path,"/api/oag/weapon-save")) queue(Op::SaveWeapon,game-1,slot-1);
    else if (!std::strcmp(path,"/api/oag/test")) {
        if (!number(body,"branch",0,3,index) || !runtime_ || game-1!=store_.activeGame()) result(400,"فعّل اللعبة وافتح الواي فاي أثناء اللعب علشان تختبر على البيكو");
        else queue(Op::Test,game-1,slot-1,0,index);
    } else {
        if (!number(body,"token",1,std::numeric_limits<std::uint32_t>::max(),token)) { result(400,"رقم جلسة التعديل مش صح"); return true; }
        if (!std::strcmp(path,"/api/oag/combo-begin")) {
            scratch_={}; std::uint32_t enable=0,mode=0,cancel=0,branches=0;
            if (!field(body,"name",scratch_.name.data(),scratch_.name.size()) ||
                !number(body,"enabled",0,1,enable) || !number(body,"mode",0,5,mode) ||
                !number(body,"cancelable",0,1,cancel) || !number(body,"branches",1,4,branches)) result(400,"راجع اسم الكومبو وإعدادات التشغيل");
            else {
                scratch_.enabled=std::uint8_t(enable); scratch_.mode=OagExecution(mode);
                scratch_.cancelable=std::uint8_t(cancel); scratch_.branchCount=std::uint8_t(branches);
                queue(Op::Begin,game-1,slot-1,token);
            }
        } else if (!std::strcmp(path,"/api/oag/combo-branch")) {
            if (!number(body,"branch",0,3,index) || !decode(body,&branch_,sizeof(branch_))) result(400,"بيانات الفرع مش كاملة");
            else queue(Op::Branch,game-1,slot-1,token,index);
        } else if (!std::strcmp(path,"/api/oag/combo-apply")) queue(Op::Apply,game-1,slot-1,token);
        else if (!std::strcmp(path,"/api/oag/combo-save")) queue(Op::SaveCombo,game-1,slot-1,token);
        else result(404,"الطلب ده مش موجود");
    }
    return true;
}
void OagSmartApi::task() {
    if (op_==Op::None) return;
    bool ok=false;
    switch (op_) {
    case Op::Begin: ok=store_.beginCombo(game_,slot_,scratch_,token_); break;
    case Op::Branch: ok=store_.draftMatches(game_,slot_,token_) && store_.branch(index_,branch_,token_); break;
    case Op::Apply: ok=store_.draftMatches(game_,slot_,token_) && store_.applyCombo(token_); break;
    case Op::SaveCombo: ok=store_.saveCombo(game_,slot_,token_); break;
    case Op::Weapon: ok=store_.previewWeapon(game_,slot_,weapon_); break;
    case Op::SaveWeapon: ok=store_.saveWeapon(game_,slot_); break;
    case Op::Cancel: store_.requestCancel(); ok=true; break;
    case Op::Test: store_.requestTest(slot_,index_); ok=true; break;
    case Op::Context: contextGame_=std::uint8_t(game_); contextWeapon_=std::uint8_t(slot_); ok=true; break;
    case Op::DiscardCombo: store_.discardCombo(); ok=true; break;
    case Op::DiscardWeapon: store_.discardWeapon(); ok=true; break;
    case Op::None: break;
    }
    done_=ticket_; success_=ok; error_=ok?"":"التطبيق أو الحفظ ما نجحش. راجع التوقيت والخطوات ومساحة التخزين";
    op_=Op::None;
}
bool OagSmartApi::selectContext(std::uint8_t& g,std::uint8_t& w) {
    if (contextGame_==255) return false;
    g=contextGame_; w=contextWeapon_; contextGame_=255; return true;
}
} // namespace oag
