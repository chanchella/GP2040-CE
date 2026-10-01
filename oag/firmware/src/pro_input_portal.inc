// Included in oag::firmware; shares the portal's checked form parser/response path.
namespace {
void encodeProSettings(char* out, std::size_t cap, const oag::ProInputSettings& s) {
    std::snprintf(out, cap, "{\"sourceDpi\":%lu,\"targetDpi\":%lu,\"multiplier\":%u,\"effectiveQ16\":%ld,\"gainX\":%u,\"gainY\":%u,\"processingHz\":%u,\"curve\":%u,\"inner\":%u,\"outer\":%u,\"fullScale\":%u,\"auto\":%u,\"fractional\":%u,\"dpiSource\":%u}",
        static_cast<unsigned long>(s.sourceDpi), static_cast<unsigned long>(s.targetDpi), s.multiplierPermille,
        static_cast<long>(oag::proDpiGainQ16(s)), s.gainXPermille, s.gainYPermille, s.processingHz,
        s.curvePermille, s.innerDeadzonePermille, s.outerDeadzonePermille, s.mouseFullScaleCounts,
        s.automaticMultiplier, s.fractionalRemainder, static_cast<unsigned>(s.dpiSource));
}
bool decodeProSettings(const char* b, oag::ProInputSettings& s) {
    std::uint32_t v = 0;
    if (!parseUnsigned(b,"sourceDpi",0,64000,v)) return false;
    s.sourceDpi = v;
    if (!parseUnsigned(b,"targetDpi",100,64000,v)) return false;
    s.targetDpi = v;
    if (!parseUnsigned(b,"multiplier",10,32000,v)) return false;
    s.multiplierPermille = v;
    if (!parseUnsigned(b,"gainX",10,8000,v)) return false;
    s.gainXPermille = v;
    if (!parseUnsigned(b,"gainY",10,8000,v)) return false;
    s.gainYPermille = v;
    if (!parseUnsigned(b,"processingHz",125,1000,v)) return false;
    s.processingHz = v;
    if (!parseUnsigned(b,"curve",100,4000,v)) return false;
    s.curvePermille = v;
    if (!parseUnsigned(b,"inner",0,400,v)) return false;
    s.innerDeadzonePermille = v;
    if (!parseUnsigned(b,"outer",0,400,v)) return false;
    s.outerDeadzonePermille = v;
    if (!parseUnsigned(b,"fullScale",1,32767,v)) return false;
    s.mouseFullScaleCounts = v;
    if (!parseUnsigned(b,"auto",0,1,v)) return false;
    s.automaticMultiplier = v;
    if (!parseUnsigned(b,"fractional",0,1,v)) return false;
    s.fractionalRemainder = v;
    if (!parseUnsigned(b,"dpiSource",0,2,v)) return false;
    s.dpiSource = static_cast<oag::ProDpiSource>(v);
    return oag::validProInputSettings(s);
}
bool decodeProKey(const char* b, unsigned& kind, unsigned& scope, oag::DeviceRecord& d) {
    std::uint32_t v = 0;
    if (!parseUnsigned(b,"kind",0,2,v)) return false;
    kind = v;
    if (!parseUnsigned(b,"scope",0,1,v)) return false;
    scope = v;
    if (!scope) return true;
    if (!parseUnsigned(b,"vid",0,65535,v)) return false;
    d.vid = v;
    if (!parseUnsigned(b,"pid",0,65535,v)) return false;
    d.pid = v;
    if (!parseUnsigned(b,"transport",0,2,v)) return false;
    d.transport = static_cast<oag::TransportType>(v);
    return true;
}
}
bool DiamondWifiPortal::handleProInputRequest(void* raw, const char* method, const char* path, const char* body) {
    auto* client = static_cast<tcp_pcb*>(raw);
    const auto reply = [&](const char* status, const char* message) { sendResponse(client,status,"text/plain; charset=utf-8",message); };
    const auto json = [&](const char* value) { sendResponse(client,"200 OK","application/json",value); };
    const bool get = !std::strcmp(method,"GET"), post = !std::strcmp(method,"POST");
    if (get && staticPath(path,"/pro.html")) { sendResponse(client,"200 OK","text/html; charset=utf-8",kProHtml); return true; }
    if (get && staticPath(path,"/pro.css")) { sendResponse(client,"200 OK","text/css; charset=utf-8",kProCss); return true; }
    if (get && staticPath(path,"/pro-form.js")) { sendResponse(client,"200 OK","application/javascript; charset=utf-8",kProFormJs); return true; }
    if (get && staticPath(path,"/pro-monitor.js")) { sendResponse(client,"200 OK","application/javascript; charset=utf-8",kProMonitorJs); return true; }
    auto& config = store_->config().proInput;
    if (get && !std::strncmp(path,"/api/pro-input?",15)) {
        unsigned kind=0,scope=0; oag::DeviceRecord d;
        if (!decodeProKey(path+15,kind,scope,d)) { reply("400 Bad Request","Invalid settings key"); return true; }
        char out[640] {}; encodeProSettings(out,sizeof(out),scope ? oag::proSettingsFor(config,d,static_cast<oag::ProInputKind>(kind)) : config.defaults[kind]); json(out); return true;
    }
    if (get && !std::strcmp(path,"/api/pro-profiles")) {
        char out[1800] {}; int used=std::snprintf(out,sizeof(out),"{\"gameContextInactive\":%u,\"desktop\":%u,\"generation\":%lu,\"profiles\":[",config.gameContextInactive,config.gameContextInactive,static_cast<unsigned long>(store_->generation())); bool comma=false;
        for (const auto& p : config.devices) if (p.enabled) {
            used+=std::snprintf(out+used,sizeof(out)-used,"%s{\"kind\":%u,\"vid\":%u,\"pid\":%u,\"transport\":%u}",comma?",":"",static_cast<unsigned>(p.kind),p.vid,p.pid,static_cast<unsigned>(p.transport)); comma=true;
        }
        std::snprintf(out+used,sizeof(out)-used,"]}"); json(out); return true;
    }
    if (get && !std::strcmp(path,"/api/pro-devices")) {
        if (!proInput_ || !proRegistry_) { reply("503 Service Unavailable","Input preview unavailable"); return true; }
        static char out[5100]; std::size_t used=0;
        const auto append = [&](const char* format, auto... args) {
            int n;
            if constexpr (sizeof...(args) == 0) n=std::snprintf(out+used,sizeof(out)-used,"%s",format);
            else n=std::snprintf(out+used,sizeof(out)-used,format,args...);
            if (n<0 || static_cast<std::size_t>(n)>=sizeof(out)-used) return false;
            used+=n; return true;
        };
        append("{\"mode\":\"config\",\"outputActive\":false,\"devices\":["); bool comma=false;
        for (std::size_t i=0;i<oag::DeviceRegistry::kCapacity;++i) {
            const auto* d=proRegistry_->at(i); if (!d) continue;
            const auto& stored=proInput_->stats(i); const oag::ProInputStats empty{};
            const auto& s=stored.source==d->id?stored:empty;
            const auto& m=oag::proSettingsFor(config,*d,oag::ProInputKind::Mouse);
            const auto& k=oag::proSettingsFor(config,*d,oag::ProInputKind::Keyboard);
            const auto& g=oag::proSettingsFor(config,*d,oag::ProInputKind::Gamepad);
            const bool idle=!s.reports || time_us_64()-s.lastReportUs>=1000000;
            if (!append("%s{\"index\":%u,\"generation\":%u,\"vid\":%u,\"pid\":%u,\"transport\":%u,\"protocol\":%u,\"caps\":%u,\"reports\":%llu,\"reportHz\":%lu,\"cpuUs\":%lu,\"maxCpuUs\":%lu,\"shapeUs\":%lu,\"maxShapeUs\":%lu,\"idle\":%u,\"hz\":[%u,%u,%u],\"dpi\":[%lu,%lu,%ld,%u,%u],\"rawX\":%ld,\"rawY\":%ld,\"outX\":%ld,\"outY\":%ld,\"remainderX\":%lld,\"remainderY\":%lld}",
                comma?",":"",static_cast<unsigned>(i),d->id.generation,d->vid,d->pid,static_cast<unsigned>(d->transport),static_cast<unsigned>(d->protocol),s.capabilities,
                static_cast<unsigned long long>(s.reports),static_cast<unsigned long>(idle?0:s.reportHz),static_cast<unsigned long>(s.lastProcessUs),static_cast<unsigned long>(s.maxProcessUs),static_cast<unsigned long>(s.lastShapeUs),static_cast<unsigned long>(s.maxShapeUs),idle,m.processingHz,k.processingHz,g.processingHz,
                static_cast<unsigned long>(m.sourceDpi),static_cast<unsigned long>(m.targetDpi),static_cast<long>(oag::proDpiGainQ16(m)),m.fractionalRemainder,static_cast<unsigned>(m.dpiSource),static_cast<long>(s.rawX),static_cast<long>(s.rawY),static_cast<long>(s.outputX),static_cast<long>(s.outputY),static_cast<long long>(s.remainderX),static_cast<long long>(s.remainderY))) {
                reply("500 Internal Server Error","Preview overflow"); return true;
            }
            comma=true;
        }
        if (!append("]}")) reply("500 Internal Server Error","Preview overflow"); else json(out); return true;
    }
    if (post && (!std::strcmp(path,"/api/pro-input") || !std::strcmp(path,"/api/pro-reset"))) {
        unsigned kind=0,scope=0; oag::DeviceRecord d; oag::ProInputSettings settings;
        const bool reset=!std::strcmp(path,"/api/pro-reset");
        if (!decodeProKey(body,kind,scope,d) || (!reset && !decodeProSettings(body,settings))) { reply("400 Bad Request","Invalid input settings"); return true; }
        const auto previous=config;
        if (scope) {
            const int slot=oag::proProfileSlot(config,d,static_cast<oag::ProInputKind>(kind));
            if (slot<0) { reply("409 Conflict","All 16 device profiles are occupied"); return true; }
            auto& p=config.devices[slot]; p={};
            if (!reset) { p.enabled=true; p.kind=static_cast<oag::ProInputKind>(kind); p.vid=d.vid; p.pid=d.pid; p.transport=d.transport; p.settings=settings; }
        } else config.defaults[kind]=settings;
        if (!store_->save()) { config=previous; reply("500 Internal Server Error","Flash save failed; previous settings restored"); return true; }
        if (proInput_) proInput_->resetFractions();
        json("{\"saved\":true}"); return true;
    }
    // Preserve the V1 endpoint as an alias for already-cached pages.
    if (post && (!std::strcmp(path,"/api/pro-game-context") || !std::strcmp(path,"/api/pro-desktop"))) {
        std::uint32_t value=0;
        if (!parseUnsigned(body,"enabled",0,1,value)) { reply("400 Bad Request","Invalid game context state"); return true; }
        const bool previous=config.gameContextInactive; config.gameContextInactive=value;
        if (!store_->save()) { config.gameContextInactive=previous; reply("500 Internal Server Error","Flash save failed"); } else json("{\"saved\":true}"); return true;
    }
    if (post && (!std::strcmp(path,"/api/pro-calibrate/start") || !std::strcmp(path,"/api/pro-calibrate/finish"))) {
        std::uint32_t index=0,generation=0,mm=0;
        if (!parseUnsigned(body,"index",0,11,index) || !parseUnsigned(body,"generation",1,65535,generation)) { reply("400 Bad Request","Invalid device"); return true; }
        const oag::DeviceId id {static_cast<std::uint16_t>(index),static_cast<std::uint16_t>(generation)};
        if (!proRegistry_ || !proInput_ || !proRegistry_->find(id) || proInput_->stats(index).source!=id || !(proInput_->stats(index).capabilities&1)) { reply("409 Conflict","Mouse disconnected or changed"); return true; }
        if (!std::strcmp(path,"/api/pro-calibrate/start")) { calibrationDevice_=id; calibrationStartX_=proInput_->stats(index).totalRawX; json("{\"started\":true}"); return true; }
        if (calibrationDevice_!=id) { reply("409 Conflict","Start calibration again"); return true; }
        if (!parseUnsigned(body,"mm",10,1000,mm)) { reply("400 Bad Request","Distance must be 10..1000 mm"); return true; }
        const auto delta=proInput_->stats(index).totalRawX-calibrationStartX_;
        const auto counts=delta<0?-delta:delta;
        const auto dpi=(counts*254+mm*5)/(mm*10); calibrationDevice_={};
        if (dpi<100 || dpi>64000) { reply("400 Bad Request","Move horizontally once across the measured distance"); return true; }
        char out[96] {}; std::snprintf(out,sizeof(out),"{\"sourceDpi\":%lld,\"dpiSource\":2}",static_cast<long long>(dpi)); json(out); return true;
    }
    return false;
}
