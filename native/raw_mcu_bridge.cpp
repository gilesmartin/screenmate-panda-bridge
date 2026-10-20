// Optional, default-OFF ScreenMate MCU raw-CAN -> Panda receiver.
// Never called by the stock app/bridge. No CAN TX, MCU reset, or stock-session close.
// Raw reception requires explicit --enable-mcu-raw and one of
// --allow-absent-stock (diagnostic) or --stock-present (guarded, port 1339).
// Port 1338 additionally requires --production-cutover with --stock-present.
// --check-stock-only is a no-open preflight; guessed stock IDs are disabled.
#define PANDABRIDGE_LIBRARY_ONLY 1
#include "panda_bridge.cpp"
#include "raw_ring.h"
#include "session_inventory.h"
#include "proc_fd_inventory.h"
#include "stock_proc_snapshot.h"
#ifndef PANDABRIDGE_TEST_SOURCE
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <dlfcn.h>
#endif
#include <memory>
#include <atomic>
#include <thread>

namespace {
#ifndef PANDABRIDGE_TEST_SOURCE
constexpr const char* kService = "vendor.teslogic.hardware.mcu.IMcu/default";
constexpr const char* kServiceDescriptor = "vendor.teslogic.hardware.mcu.IMcu";
constexpr const char* kCallback = "vendor.teslogic.hardware.mcu.ICanStreamCallback";
// Decoded from installed BpMcu::openCanStream / closeCanStream (0x14b1c4 / 0x14b3f4).
constexpr transaction_code_t kOpen = 38, kClose = 39;
constexpr uint32_t kFlags = 0x10000000; // generated vendor proxy transact flags
constexpr const char* kMcuHash = "18846138ffe5333e3be636e3bfb91d9b2e4b046fb2107988ed13cb71e8d99617";
bool same_mcu_binary() {
    FILE* hash=popen("/system/bin/sha256sum /odm/bin/hw/vendor.teslogic.hardware.mcu.service", "r");
    if (!hash) return false;
    char line[256]{};
    const bool read=fgets(line,sizeof(line),hash)!=nullptr;
    const int rc=pclose(hash);
    return read && rc==0 && strncmp(line,kMcuHash,64)==0 && line[64]==' ';
}
int32_t current_mcu_pid() {
    FILE* f=popen("/system/bin/pidof vendor.teslogic.hardware.mcu.service", "r");
    if (!f) return -1;
    char line[128]{};
    const bool read=fgets(line,sizeof(line),f)!=nullptr;
    const int rc=pclose(f);
    int32_t pid=-1;
    char extra='\0';
    return read && rc==0 && std::sscanf(line,"%d %c",&pid,&extra)==1 && pid>0 ? pid : -1;
}
bool same_mcu_pid(int32_t expected) {
    return expected>0 && current_mcu_pid()==expected;
}
void* cb_create(void* args) { return args; }
void cb_destroy(void*) {}
binder_status_t cb_transact(AIBinder*, transaction_code_t code, const AParcel*, AParcel*) {
    // Installed BpCanStreamCallback::onFramesReady is transaction 1, one-way
    // (0x10000001). It has no reply parcel and the reader polls its own ring.
    return code==1 ? STATUS_OK : STATUS_UNKNOWN_TRANSACTION;
}
binder_status_t reject_local_service_transact(AIBinder*, transaction_code_t, const AParcel*, AParcel*) {
    return STATUS_UNKNOWN_TRANSACTION; // this class is associated only with a remote IMcu binder
}
class RawSession {
    void* binder_ndk_ = nullptr;
    AIBinder* service_ = nullptr;
    AIBinder* callback_ = nullptr;
    int32_t id_ = -1, stock_id_ = -1, capacity_ = 0;
    stock_guard::Guard* guard_ = nullptr;
public:
    int32_t id() const { return id_; }
    uint32_t capacity() const { return uint32_t(capacity_); }
    explicit RawSession(int32_t stock_id, stock_guard::Guard* guard=nullptr)
        : stock_id_(stock_id), guard_(guard) {}
    RawSession(const RawSession&) = delete;
    RawSession& operator=(const RawSession&) = delete;
    ~RawSession() {
        // Validate the live service/stock/raw identities immediately before a
        // close. A replaced MCU may reuse numeric IDs; fail closed on drift.
        if (guard_ && id_>0) {
            stock_guard::Snapshot now;
            if (!stock_proc::snapshot(now) || !guard_->monitor(now)) guard_->revoke();
        }
        // Close ONLY the id returned by our successful open. Never touch the stock id.
        if (service_ && id_ > 0 &&
            (guard_ ? guard_->may_close(id_) : id_ != stock_id_)) {
            AParcel *request=nullptr, *reply=nullptr;
            if (AIBinder_prepareTransaction(service_, &request)==STATUS_OK &&
                AParcel_writeInt32(request, id_)==STATUS_OK) {
                (void)AIBinder_transact(service_, kClose, &request, &reply, kFlags);
            }
            if (request) AParcel_delete(request);
            if (reply) AParcel_delete(reply);
        }
        if (callback_) AIBinder_decStrong(callback_);
        if (service_) AIBinder_decStrong(service_);
        if (binder_ndk_) dlclose(binder_ndk_);
    }
    int open() { // returns exclusively this session's ashmem fd, or -1
        static const AIBinder_Class* callback_class =
            AIBinder_Class_define(kCallback, cb_create, cb_destroy, cb_transact);
        if (!callback_class) return -1;
        binder_ndk_ = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
        if (!binder_ndk_) return -1;
        using CheckService = AIBinder* (*)(const char*);
        using StartThreadPool = void (*)();
        using MarkVintf = void (*)(AIBinder*);
        auto check = reinterpret_cast<CheckService>(dlsym(binder_ndk_,"AServiceManager_checkService"));
        auto start = reinterpret_cast<StartThreadPool>(dlsym(binder_ndk_,"ABinderProcess_startThreadPool"));
        auto mark_vintf = reinterpret_cast<MarkVintf>(dlsym(binder_ndk_,"AIBinder_markVintfStability"));
        if (!check || !start || !mark_vintf) return -1;
        service_ = check(kService);
        if (!service_) return -1;
        static const AIBinder_Class* service_class =
            AIBinder_Class_define(kServiceDescriptor, cb_create, cb_destroy, reject_local_service_transact);
        if (!service_class || !AIBinder_associateClass(service_,service_class)) {
            std::fprintf(stderr,"IMcu interface-class association failed\n"); return -1;
        }
        start();
        callback_ = AIBinder_new(callback_class, this);
        if (!callback_) return -1;
        mark_vintf(callback_); // matching installed BnCanStreamCallback::createBinder
        AParcel *request=nullptr, *reply=nullptr;
        int fd=-1;
        bool valid=false;
        do {
            const binder_status_t prep=AIBinder_prepareTransaction(service_, &request);
            if (prep!=STATUS_OK) { std::fprintf(stderr,"IMcu prepare failed status=%d\n",prep); break; }
            // Parcelable[] vector length 1; each element first carries presence=1.
            // Installed stub element reader (0x3e12c) consumes it before CanFilter.
            if (AParcel_writeInt32(request, 1)!=STATUS_OK ||
                AParcel_writeInt32(request, 1)!=STATUS_OK) break;
            const int32_t start=AParcel_getDataPosition(request);
            if (AParcel_writeInt32(request, 0)!=STATUS_OK ||
                AParcel_writeByte(request, static_cast<int8_t>(0x81))!=STATUS_OK ||
                AParcel_writeInt32(request, 0)!=STATUS_OK ||
                AParcel_writeInt32(request, 0)!=STATUS_OK) break;
            const int32_t end=AParcel_getDataPosition(request);
            if (AParcel_setDataPosition(request, start)!=STATUS_OK ||
                AParcel_writeInt32(request, end-start)!=STATUS_OK ||
                AParcel_setDataPosition(request, end)!=STATUS_OK) break;
            // Window 128ms => capacity 512 slots (service code at 0x1f2b4-0x1f300).
            if (AParcel_writeInt32(request, 128)!=STATUS_OK ||
                AParcel_writeStrongBinder(request, callback_)!=STATUS_OK) break;
            const binder_status_t tx=AIBinder_transact(service_, kOpen, &request, &reply, kFlags);
            if (tx!=STATUS_OK) { std::fprintf(stderr,"IMcu open raw Binder status=%d\n",tx); break; }
            AStatus* status=nullptr;
            const binder_status_t parsed=AParcel_readStatusHeader(reply, &status);
            if (parsed!=STATUS_OK) { std::fprintf(stderr,"IMcu reply status parse=%d\n",parsed); break; }
            bool ok=status && AStatus_isOk(status);
            if (!ok) std::fprintf(stderr,"IMcu service exception=%d code=%d\n",
                 status?AStatus_getExceptionCode(status):-1,
                 status?AStatus_getServiceSpecificError(status):-1);
            AStatus_delete(status);
            if (!ok) break;
            int32_t present=0, parcel_size=0, id=-1, cap=0, slot=0;
            const binder_status_t presence_rc=AParcel_readInt32(reply, &present);
            if (presence_rc!=STATUS_OK || present!=1) {
                std::fprintf(stderr,"IMcu reply presence rc=%d value=%d\n",presence_rc,present); break;
            }
            const int32_t start_reply=AParcel_getDataPosition(reply);
            const binder_status_t size_rc=AParcel_readInt32(reply, &parcel_size);
            if (size_rc!=STATUS_OK || parcel_size<4) {
                std::fprintf(stderr,"IMcu reply parcel size rc=%d value=%d\n",size_rc,parcel_size); break;
            }
            const binder_status_t id_rc=AParcel_readInt32(reply, &id);
            if (id_rc!=STATUS_OK || id<=0 || id==stock_id_) {
                std::fprintf(stderr,"IMcu reply session id rc=%d value=%d stock=%d\n",id_rc,id,stock_id_); break;
            }
            if (!guard_) id_=id; // legacy absent-stock path: keep parsed non-stock id
            const binder_status_t fd_rc=AParcel_readParcelFileDescriptor(reply, &fd);
            const binder_status_t cap_rc=fd_rc==STATUS_OK?AParcel_readInt32(reply, &cap):-1000;
            const binder_status_t slot_rc=cap_rc==STATUS_OK?AParcel_readInt32(reply, &slot):-1000;
            const binder_status_t seek_rc=slot_rc==STATUS_OK?
                AParcel_setDataPosition(reply, start_reply+parcel_size):-1000;
            std::fprintf(stderr,"IMcu reply id=%d fd_rc=%d fd_ok=%d cap_rc=%d cap=%d slot_rc=%d slot=%d seek_rc=%d\n",
                         id,fd_rc,fd>=0,cap_rc,cap,slot_rc,slot,seek_rc);
            if (fd_rc!=STATUS_OK || cap_rc!=STATUS_OK || slot_rc!=STATUS_OK ||
                seek_rc!=STATUS_OK || fd<0 || cap<2 || cap>4096 ||
                (cap&(cap-1)) || slot!=16) break;
            if (guard_) {
                stock_guard::Snapshot after;
                const bool collected=stock_proc::snapshot(after);
                if (!collected ||
                    !guard_->owns_only_if_attested(id,after,
                         stock_guard::reply_valid(id,cap,slot,true))) {
                    std::fprintf(stderr,"stock/raw FD ownership ambiguous: collected=%d mcu=%d vhal=%d stream_fds=%zu vhal_maps=%zu; no session close authorized\n",
                        int(collected),after.mcu_pid,after.vhal_pid,after.stock_fds.size(),after.vhal_maps.size());
                    for (const auto& entry:after.stock_fds)
                        std::fprintf(stderr,"stream_fd=%d inode_fingerprint=%s label=%s\n",
                            entry.number,entry.stat.c_str(),entry.target.c_str());
                    break;
                }
                id_=id; // only an attested new FD makes this id closable
            }
            capacity_=cap;
            valid=true;
            std::fprintf(stderr,"raw MCU session opened id=%d cap=%d (stock id=%d untouched)\n",
                         id_,cap,stock_id_);
        } while(false);
        if (request) AParcel_delete(request);
        if (reply) AParcel_delete(reply);
        if (!valid && fd>=0) { close(fd); fd=-1; }
        return valid ? fd : -1;
    }
};
#endif
}
int main(int argc, char** argv) {
    bool enabled=false, allow_absent_stock=false, check_stock_only=false, stock_present=false;
    bool production_cutover=false;
    int32_t stock_id=-1;
    uint16_t port=1339; // test endpoint, never displace existing 1338 by default
    std::string names="ap0,tun1", source;
    for (int i=1;i<argc;++i) {
        const std::string opt=argv[i];
        if (opt=="--enable-mcu-raw") { enabled=true; continue; }
        if (opt=="--allow-absent-stock") { allow_absent_stock=true; continue; }
        if (opt=="--stock-present") { stock_present=true; continue; }
        if (opt=="--production-cutover") { production_cutover=true; continue; }
        if (opt=="--check-stock-only") { check_stock_only=true; continue; }
        if (i+1>=argc) return 2;
        const std::string val=argv[++i];
        if (opt=="--stock-session-id") {
            char* end=nullptr; const long n=strtol(val.c_str(),&end,10);
            if (!end || *end || n<=0 || n>INT32_MAX) return 2;
            stock_id=static_cast<int32_t>(n);
        } else if (opt=="--interfaces") names=val;
        else if (opt=="--port") {
            char* end=nullptr; const long n=strtol(val.c_str(),&end,10);
            if (!end || *end || n<1 || n>65535) return 2;
            port=static_cast<uint16_t>(n);
        }
#ifdef PANDABRIDGE_TEST_SOURCE
        else if (opt=="--source-file") source=val;
#endif
        else return 2;
    }
    if (check_stock_only) {
        // A no-open, no-listener probe. Never combine with a receive session.
        if (enabled || allow_absent_stock || stock_present || production_cutover ||
            stock_id>0 || argc!=2) return 2;
#ifndef PANDABRIDGE_TEST_SOURCE
        if (geteuid()!=0 || !same_mcu_binary()) return 7;
        stock_guard::Snapshot first, second;
        if (!stock_proc::snapshot(first) || !stock_guard::preflight(first)) return 7;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!stock_proc::snapshot(second) || !stock_guard::same_stock(first,second)) return 7;
        std::fprintf(stderr,"stock proc preflight OK: mcu=%d vhal=%d fd=%d (no stream opened)\n",
                     first.mcu_pid,first.vhal_pid,first.stock_fds[0].number);
        return 0;
#else
        return 7;
#endif
    }
    if (!enabled || (int(allow_absent_stock)+int(stock_present)+int(stock_id>0))!=1 ||
        (production_cutover && (!stock_present || port!=1338)) ||
        (stock_present && (stock_id>0 || (production_cutover ? port!=1338 : port!=1339))) ||
        split_interfaces(names).empty()) return 2;
#ifdef PANDABRIDGE_TEST_SOURCE
    if (stock_present) return 7; // no synthetic fixture may impersonate stock ownership
#endif
#ifndef PANDABRIDGE_TEST_SOURCE
    if (names!="ap0,tun1") return 2; // never bind the raw feed to cellular/unknown interfaces
    if (geteuid()!=0) return 3;
#else
    if (source.empty()) return 2;
#endif
    PandaServer server(split_interfaces(names),port,true); // raw mode sends ALL received frames to each peer
    server.refresh();
    if (!server.has_listeners()) return 4; // fail closed before session creation
    int fd=-1;
#ifndef PANDABRIDGE_TEST_SOURCE
    // Never use vendor MCU dumpsys: even an isolated preflight was associated
    // with SIGPIPE/service replacement. Stock-present mode requires two stable
    // procfs snapshots, then a distinct FD after a validated raw-open reply.
    if (!same_mcu_binary() || stock_id>0) return 7; // no guessed stock session ID
    int32_t mcu_pid=-1;
    std::unique_ptr<stock_guard::Guard> guard;
    if (stock_present) {
        stock_guard::Snapshot first, second;
        if (!stock_proc::snapshot(first) || !stock_guard::preflight(first)) return 7;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!stock_proc::snapshot(second) || !stock_guard::same_stock(first,second) ||
            !stock_guard::preflight(second)) return 7;
        mcu_pid=first.mcu_pid;
        guard=std::make_unique<stock_guard::Guard>(std::move(first));
    } else {
        mcu_pid=current_mcu_pid();
        if (mcu_pid<=0) return 7;
        const std::string fd_dir="/proc/"+std::to_string(mcu_pid)+"/fd";
        if (!no_can_session_fds(fd_dir) || !same_mcu_pid(mcu_pid)) return 7;
    }
    RawSession session(stock_id,guard.get());
    fd=session.open();
#else
    fd=::open(source.c_str(), O_RDONLY | O_CLOEXEC);
#endif
    if (fd<0) return 5;
#ifndef PANDABRIDGE_TEST_SOURCE
    // No vendor-service dump: it has independently reproduced SIGPIPE during
    // preflight. Protect service identity without polling its dump FD.
    bool identity_ok=same_mcu_pid(mcu_pid);
    if (identity_ok && guard) {
        stock_guard::Snapshot now;
        identity_ok=stock_proc::snapshot(now) && guard->monitor(now);
    }
    if (!identity_ok) {
        if (guard) guard->revoke();
        close(fd); return 8;
    }
#endif
    RawMcuRing ring;
#ifndef PANDABRIDGE_TEST_SOURCE
    if (!ring.attach_fd(fd, session.capacity())) return 6;
#else
    if (!ring.attach_fd(fd)) return 6;
#endif
    signal(SIGINT,stop); signal(SIGTERM,stop);
    uint64_t frames=0,id401=0;
    std::map<uint8_t,uint64_t> bus_counts;
#ifndef PANDABRIDGE_TEST_SOURCE
    std::atomic<bool> stock_ok{true}, monitor_active{true};
    // Poll the original MCU process identity, fail closed if it disappears.
    std::thread stock_monitor([&] {
        while (monitor_active.load(std::memory_order_acquire)) {
            bool healthy=same_mcu_pid(mcu_pid);
            if (healthy && guard) {
                stock_guard::Snapshot now;
                healthy=stock_proc::snapshot(now) && guard->monitor(now);
            }
            if (!healthy) {
                if (guard) guard->revoke();
                stock_ok.store(false, std::memory_order_release);
                std::fprintf(stderr,"MCU/VHAL or stock/raw FD identity changed; stopping raw reader\n");
                return;
            }
            for (int i=0; i<10 && monitor_active.load(std::memory_order_acquire); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
#endif
    auto next_refresh=Clock::time_point::min();
    while (running) {
#ifndef PANDABRIDGE_TEST_SOURCE
        if (!stock_ok.load(std::memory_order_acquire)) break;
#endif
        server.poll_inputs();
        ring.drain([&](const CanFrame& frame){
            ++frames; if (frame.id==0x401) ++id401;
            ++bus_counts[frame.bus]; server.send(frame);
        });
        if (ring.overruns()) {
            std::fprintf(stderr,"raw ring overrun; stopping incomplete feed\n");
            break;
        }
        if (Clock::now()>=next_refresh) {
            server.refresh(); server.expire();
            if (!server.has_listeners()) break;
            std::fprintf(stderr,"raw CAN metadata: frames=%llu id401=%llu rejects=%llu overruns=%llu buses=",
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(id401),
                static_cast<unsigned long long>(ring.rejects()),
                static_cast<unsigned long long>(ring.overruns()));
            for (const auto& bus:bus_counts)
                std::fprintf(stderr,"%u:%llu,", unsigned(bus.first),
                             static_cast<unsigned long long>(bus.second));
            std::fputc('\n',stderr);
            next_refresh=Clock::now()+std::chrono::seconds(1);
        }
    }
#ifndef PANDABRIDGE_TEST_SOURCE
    monitor_active.store(false, std::memory_order_release);
    stock_monitor.join();
#endif
    std::fprintf(stderr,"raw MCU reader stopped: rejects=%llu overruns=%llu\n",
        static_cast<unsigned long long>(ring.rejects()),static_cast<unsigned long long>(ring.overruns()));
#ifndef PANDABRIDGE_TEST_SOURCE
    if (!stock_ok.load(std::memory_order_acquire)) return 9;
#endif
    if (ring.overruns()) return 10;
    return 0;
}
