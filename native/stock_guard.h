#pragma once
#include <string>
#include <utility>
#include <vector>

// Pure, firmware-scoped ownership predicates. A collector must reject incomplete
// /proc enumerations rather than turning missing entries into an empty snapshot.
namespace stock_guard {
struct Fd { int number=-1; std::string target, stat; };
struct Mapping { std::string target, permissions, stat; };
struct Snapshot {
    int mcu_pid=-1, vhal_pid=-1;
    std::string mcu_binary, vhal_name, vhal_birth;
    std::vector<Fd> stock_fds; // all MCU ashmem/memfd descriptors, not guessed IDs
    std::vector<Mapping> vhal_maps, bridge_maps;
};
inline constexpr const char* kVhalName="android.hardware.automotive.vehicle.service-teslogic";
inline constexpr const char* kMapTarget="/dev/ashmem/teslogic_can_stream (deleted)";
inline bool is_stream_fd(const Fd& f) {
    return f.number>=0 && !f.stat.empty() &&
        (f.target.find("ashmem")!=std::string::npos ||
         f.target.find("memfd")!=std::string::npos);
}
inline bool valid_stock(const Snapshot& s) {
    return s.mcu_pid>0 && !s.mcu_binary.empty() &&
        s.stock_fds.size()==1 && is_stream_fd(s.stock_fds[0]) &&
        s.vhal_pid>0 && s.vhal_name==kVhalName && !s.vhal_birth.empty() && s.vhal_maps.size()==1 &&
        s.vhal_maps[0].target==kMapTarget &&
        s.vhal_maps[0].permissions=="r--s" && !s.vhal_maps[0].stat.empty() &&
        (s.bridge_maps.empty() || (s.bridge_maps.size()==1 &&
         s.bridge_maps[0].target==kMapTarget &&
         s.bridge_maps[0].stat==s.vhal_maps[0].stat));
}
inline bool preflight(const Snapshot& s) { return valid_stock(s); }
inline bool same_stock(const Snapshot& base, const Snapshot& now) {
    if (!valid_stock(base) || now.mcu_pid!=base.mcu_pid ||
        now.mcu_binary!=base.mcu_binary || now.vhal_pid!=base.vhal_pid ||
        now.vhal_name!=base.vhal_name || now.vhal_birth!=base.vhal_birth || now.stock_fds.empty() ||
        now.stock_fds.size()>2 || now.vhal_maps.size()!=1 ||
        now.vhal_maps[0].target!=base.vhal_maps[0].target ||
        now.vhal_maps[0].permissions!=base.vhal_maps[0].permissions ||
        now.vhal_maps[0].stat!=base.vhal_maps[0].stat ||
        now.bridge_maps.size()!=base.bridge_maps.size()) return false;
    if (!base.bridge_maps.empty() &&
        (now.bridge_maps[0].target!=base.bridge_maps[0].target ||
         now.bridge_maps[0].permissions!=base.bridge_maps[0].permissions ||
         now.bridge_maps[0].stat!=base.bridge_maps[0].stat)) return false;
    // Android ashmem aliases share chardev stat and link label; distinguish
    // the added FD by number and cardinality. A matching Binder reply and
    // sustained stock delivery are separate requirements; this procfs delta
    // alone cannot prove a general vendor session-ID allocation guarantee.
    int matching=0;
    for (const auto& f:now.stock_fds) {
        if (!is_stream_fd(f)) return false;
        if (f.number==base.stock_fds[0].number) {
            if (f.target!=base.stock_fds[0].target ||
                f.stat!=base.stock_fds[0].stat) return false;
            ++matching;
        }
    }
    return matching==1;
}
inline bool reply_valid(int id,int capacity,int slot,bool parsed) {
    return parsed && id>0 && capacity>=2 && capacity<=4096 &&
        (capacity&(capacity-1))==0 && slot==16;
}
inline bool attest_delta(const Snapshot& base,const Snapshot& now,int id,bool reply) {
    return id>0 && reply && now.stock_fds.size()==2 && same_stock(base,now);
}
class Guard {
    Snapshot baseline_;
    int owned_id_=-1;
public:
    explicit Guard(Snapshot s):baseline_(std::move(s)){}
    bool owns_only_if_attested(int id,const Snapshot& now,bool valid_reply) {
        owned_id_=attest_delta(baseline_,now,id,valid_reply) ? id : -1;
        return owned_id_>0;
    }
    bool monitor(const Snapshot& now) const {
        return owned_id_>0 ? attest_delta(baseline_,now,owned_id_,true)
                           : (preflight(now) && same_stock(baseline_,now));
    }
    void revoke() { owned_id_=-1; }
    bool may_close(int id) const { return owned_id_>0 && owned_id_==id; }
};
}
