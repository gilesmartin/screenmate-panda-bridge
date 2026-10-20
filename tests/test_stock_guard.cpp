#include "../native/stock_guard.h"
#include <cassert>
using namespace stock_guard;

static Snapshot good() {
    Snapshot s;
    s.mcu_pid=11680; s.mcu_binary="exact-hash";
    s.stock_fds.push_back({9,"/dev/ashmem00000000-0000-0000-0000-000000000000","17:856:0"});
    s.vhal_pid=9720; s.vhal_name="android.hardware.automotive.vehicle.service-teslogic"; s.vhal_birth="100";
    s.vhal_maps.push_back({"/dev/ashmem/teslogic_can_stream (deleted)","r--s","1:68644:1056"});
    s.bridge_maps.push_back({"/dev/ashmem/teslogic_can_stream (deleted)","r--s","1:68644:1056"});
    return s;
}
int main() {
    auto b=good(); assert(preflight(b));
    auto x=b; x.stock_fds.clear(); assert(!preflight(x));
    x=b; x.stock_fds.push_back({10,"/dev/ashmem-extra","17:857:0"}); assert(!preflight(x));
    x=b; x.vhal_maps[0].permissions="rw-s"; assert(!preflight(x));
    x=b; x.vhal_maps.push_back(x.vhal_maps[0]); assert(!preflight(x));
    x=b; x.vhal_pid=0; assert(!preflight(x));
    auto delta=b; delta.stock_fds.push_back({11,"/dev/ashmem-new","17:900:512"});
    assert(attest_delta(b,delta,11,true));
    assert(!attest_delta(b,delta,11,false));
    assert(!attest_delta(b,b,11,true));
    auto changed=b; changed.stock_fds[0].stat="17:999:0"; assert(!attest_delta(b,changed,11,true));
    changed=b; changed.stock_fds.clear(); assert(!same_stock(b,changed));
    changed=b; changed.bridge_maps[0].stat="1:999:1056"; assert(!same_stock(b,changed));
    changed=b; changed.stock_fds.push_back(b.stock_fds[0]); assert(!attest_delta(b,changed,11,true));
    changed=delta; changed.stock_fds[1].stat=b.stock_fds[0].stat;
    assert(attest_delta(b,changed,11,true)); // ashmem chardev aliases share stat fingerprint
    changed=delta; changed.stock_fds[1].target=b.stock_fds[0].target;
    assert(attest_delta(b,changed,11,true)); // vendor uses the same ashmem label for each ring
    assert(reply_valid(11,512,16,true));
    assert(!reply_valid(-1,512,16,true));
    assert(!reply_valid(11,513,16,true));
    assert(!reply_valid(11,512,16,false));
    Guard g(b); assert(g.owns_only_if_attested(11,delta,true));
    assert(g.monitor(delta));
    changed=delta; changed.vhal_birth="101"; assert(!g.monitor(changed));
    changed=delta; changed.stock_fds[0].target="/dev/ashmem-replaced"; assert(!g.monitor(changed));
    changed=b; assert(!g.monitor(changed));
    assert(g.may_close(11)); assert(!g.may_close(12));
    g.revoke(); assert(!g.may_close(11));
    assert(!g.owns_only_if_attested(-1,delta,true));
    assert(!g.may_close(11));
    assert(!g.monitor(delta));
    assert(g.monitor(changed));
    assert(preflight(b) && same_stock(b,b));
}
