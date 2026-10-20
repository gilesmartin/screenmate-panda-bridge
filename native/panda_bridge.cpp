// ScreenMate Panda bridge: passively reads the stock VHAL's existing shared-memory
// mapping. No vendor Binder transactions, CAN writes, MCU controls, or session
// lifecycle operations exist in this program.
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
volatile sig_atomic_t running = 1;
[[maybe_unused]] void stop(int) { running = 0; }
constexpr size_t kCapacity = 64, kSlotSize = 16, kHeaderSize = 32;
constexpr size_t kRingSize = kHeaderSize + kCapacity * kSlotSize;
constexpr const char* kVhalName = "android.hardware.automotive.vehicle.service-teslogic";
constexpr const char* kMapName = "/dev/ashmem/teslogic_can_stream (deleted)";

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (i * 8));
}
uint64_t sequence(const uint8_t* p) {
    return __atomic_load_n(reinterpret_cast<const uint64_t*>(p + 8), __ATOMIC_ACQUIRE);
}
struct CanFrame {
    uint16_t id;
    uint8_t bus, dlc;
    std::array<uint8_t, 8> payload;
};
bool decode(const uint8_t* p, CanFrame& frame) {
    const uint32_t raw = le32(p + 4);
    if (raw > 0xffff || p[1] > 8 || p[0] > 14 || (raw >> 12) != p[0] || (raw & 0xfff) > 0x7ff) return false;
    unsigned checksum = p[1] + raw;
    for (int i = 0; i < 8; ++i) checksum += p[8+i];
    if (uint8_t(checksum) != p[2]) return false;
    frame.id = uint16_t(raw & 0xfff);
    frame.bus = p[0];
    frame.dlc = p[1];
    std::copy(p + 8, p + 16, frame.payload.begin());
    return true;
}
std::array<uint8_t, 16> pack_panda(uint16_t id, uint8_t bus, uint8_t dlc, const uint8_t* payload) {
    std::array<uint8_t, 16> p{};
    put32(p.data(), uint32_t(id) << 21);
    put32(p.data() + 4, uint32_t(bus) << 4 | dlc);
    if (payload) std::copy(payload, payload + 8, p.begin() + 8);
    return p;
}
bool numeric(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) if (*s < '0' || *s > '9') return false;
    return true;
}
pid_t find_vhal() {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    pid_t result = -1;
    while (auto* ent = readdir(dir)) {
        if (!numeric(ent->d_name)) continue;
        const std::string base = std::string("/proc/") + ent->d_name;
        std::ifstream cmd(base + "/cmdline", std::ios::binary);
        std::string name;
        std::getline(cmd, name, '\0');
        const auto pos = name.find_last_of('/');
        if (name.substr(pos == std::string::npos ? 0 : pos + 1) == kVhalName) {
            result = pid_t(std::atoi(ent->d_name));
            break;
        }
    }
    closedir(dir);
    return result;
}
std::string mapped_ring(pid_t pid) {
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    std::string line, address, candidate;
    while (std::getline(maps, line)) {
        if (line.find(kMapName) == std::string::npos) continue;
        std::istringstream entry(line);
        std::string permissions;
        entry >> address >> permissions;
        if (permissions != "r--s" || !candidate.empty()) return {};
        candidate = "/proc/" + std::to_string(pid) + "/map_files/" + address;
    }
    if (candidate.empty()) return {};
    char target[256]{};
    const ssize_t n = readlink(candidate.c_str(), target, sizeof(target)-1);
    if (n < 0 || std::string(target, size_t(n)) != kMapName) return {};
    return candidate;
}
class Ring {
    int fd_ = -1;
    const uint8_t* bytes_ = nullptr;
    pid_t pid_ = -1;
    uint64_t next_ = 0;
    uint64_t rejected_ = 0, overrun_ = 0;
    std::string path_;
public:
    ~Ring() { detach(); }
    void detach() {
        if (bytes_) munmap(const_cast<uint8_t*>(bytes_), kRingSize);
        if (fd_ >= 0) close(fd_);
        bytes_ = nullptr; fd_ = -1; pid_ = -1; path_.clear();
    }
    bool attach(const std::string& path, pid_t pid) {
        detach();
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        // map_files is necessarily a procfs symlink; O_NOFOLLOW rejects it.
        if (fd < 0 && errno == ELOOP) fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size < off_t(kRingSize)) { close(fd); return false; }
        void* ptr = mmap(nullptr, kRingSize, PROT_READ, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) { close(fd); return false; }
        const auto* p = static_cast<const uint8_t*>(ptr);
        if (le32(p) != kCapacity || le32(p + 4) != kSlotSize) {
            munmap(ptr, kRingSize); close(fd); return false;
        }
        fd_ = fd; bytes_ = p; pid_ = pid; path_ = path; next_ = sequence(p);
        std::fprintf(stderr, "read-only stock ring attached pid=%d sequence=%llu\n", int(pid),
                     static_cast<unsigned long long>(next_));
        return true;
    }
    void refresh(const std::string& fixture) {
#ifdef PANDABRIDGE_TEST_SOURCE
        if (!fixture.empty()) {
            if (!bytes_) attach(fixture, 0);
            return;
        }
#else
        (void)fixture;
#endif
        const pid_t pid = find_vhal();
        if (pid <= 0) { detach(); return; }
        const std::string path = mapped_ring(pid);
        if (path.empty()) { detach(); return; }
        if (bytes_ && (pid != pid_ || path != path_)) detach();
        if (!bytes_) attach(path, pid);
    }
    template<typename Fn> void drain(Fn send) {
        if (!bytes_) return;
        uint64_t published = sequence(bytes_);
        if (published < next_) { next_ = published; return; }
        if (published - next_ > kCapacity) { overrun_ += published - next_ - kCapacity; next_ = published - kCapacity; }
        unsigned attempts = 0;
        while (next_ < published && attempts++ < kCapacity) {
            const uint64_t index = next_ & (kCapacity - 1);
            uint8_t copy[kSlotSize];
            std::memcpy(copy, bytes_ + kHeaderSize + index * kSlotSize, kSlotSize);
            const uint64_t after = sequence(bytes_);
            if (after < next_) { next_ = after; break; }
            if (after - next_ > kCapacity) { overrun_ += after - next_ - kCapacity; next_ = after - kCapacity; published = after; continue; }
            CanFrame frame{};
            if (decode(copy, frame)) send(frame); else ++rejected_;
            ++next_;
            published = after;
        }
    }
};
struct Listener { std::string iface; in_addr addr{}; int fd=-1; };
struct Peer {
    int fd=-1;
    sockaddr_in address{};
    Clock::time_point seen{};
    unsigned version=1;
    bool all=false;
    std::set<std::pair<uint8_t, uint16_t>> filters;
};
class PandaServer {
    std::vector<std::string> interfaces_;
    std::vector<Listener> listeners_;
    std::vector<Peer> peers_;
    uint16_t port_;
    bool force_all_;
    static bool same_peer(const Peer& p, int fd, const sockaddr_in& a) {
        return p.fd == fd && p.address.sin_addr.s_addr == a.sin_addr.s_addr && p.address.sin_port == a.sin_port;
    }
public:
    PandaServer(std::vector<std::string> names, uint16_t port, bool force_all=false)
        : interfaces_(std::move(names)), port_(port), force_all_(force_all) {}
    ~PandaServer() { for (auto& l : listeners_) close(l.fd); }
    bool has_listeners() const { return !listeners_.empty(); }
    void refresh() {
        std::map<std::string, in_addr> addresses;
        ifaddrs* head=nullptr;
        if (getifaddrs(&head) != 0) return;
        for (auto* p=head; p; p=p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || !(p->ifa_flags & IFF_UP)) continue;
            const std::string name(p->ifa_name);
            if (std::find(interfaces_.begin(), interfaces_.end(), name) != interfaces_.end())
                addresses[name] = reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr;
        }
        freeifaddrs(head);
        bool unchanged = listeners_.size() == addresses.size();
        if (unchanged) for (auto& l:listeners_) {
            auto it=addresses.find(l.iface);
            if (it == addresses.end() || it->second.s_addr != l.addr.s_addr) { unchanged=false; break; }
        }
        if (unchanged) return;
        for (auto& l:listeners_) close(l.fd);
        listeners_.clear(); peers_.clear();
        for (const auto& [name,addr]:addresses) {
            int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
            if (fd<0) continue;
#ifdef SO_BINDTODEVICE
            if (setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,name.c_str(),socklen_t(name.size()+1)) != 0) {
                std::fprintf(stderr,"interface bind restriction failed for %s errno=%d; not listening\n",name.c_str(),errno);
                close(fd); continue;
            }
#else
            std::fprintf(stderr,"SO_BINDTODEVICE unavailable; not listening on %s\n",name.c_str());
            close(fd); continue;
#endif
            sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons(port_); local.sin_addr=addr;
            if (bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local)) != 0) {
                std::fprintf(stderr,"UDP bind failed on %s errno=%d\n",name.c_str(),errno);
                close(fd); continue;
            }
            fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
            listeners_.push_back({name,addr,fd});
            char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&addr,ip,sizeof(ip));
            std::fprintf(stderr,"Panda UDP listening on %s %s:%u\n",name.c_str(),ip,unsigned(port_));
        }
    }
    void handle(int fd) {
        for (int i=0;i<16;++i) {
            uint8_t data[256]{}; sockaddr_in addr{}; socklen_t addrlen=sizeof(addr);
            const ssize_t n=recvfrom(fd,data,sizeof(data),0,reinterpret_cast<sockaddr*>(&addr),&addrlen);
            if (n<0) { if (errno!=EAGAIN && errno!=EWOULDBLOCK) std::fprintf(stderr,"UDP read errno=%d\n",errno); break; }
            if (addrlen!=sizeof(addr) || addr.sin_family!=AF_INET || n==0) continue;
            auto it=std::find_if(peers_.begin(),peers_.end(),[&](const Peer& p){return same_peer(p,fd,addr);});
            const bool hello=n==5 && std::memcmp(data,"hello",5)==0;
            const bool ehllo=n==5 && std::memcmp(data,"ehllo",5)==0;
            if (hello || ehllo) {
                if (it==peers_.end()) {
                    if (peers_.size()>=8) {
                        const auto no=pack_panda(7,15,0,nullptr);
                        sendto(fd,no.data(),no.size(),0,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
                        continue;
                    }
                    peers_.push_back({}); it=peers_.end()-1;
                    it->fd=fd; it->address=addr;
                }
                it->version=ehllo?2:1; it->seen=Clock::now();
                const auto ack=pack_panda(6,15,0,nullptr);
                sendto(fd,ack.data(),ack.size(),0,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
                continue;
            }
            if (it==peers_.end() || it->version!=2) continue;
            if (n==3 && std::memcmp(data,"bye",3)==0) { peers_.erase(it); continue; }
            if (n==1 && data[0]==0x18) { it->all=false; it->filters.clear(); }
            else if (n==1 && data[0]==0x0c) it->all=true;
            else if (n>=4 && (n-1)%3==0 && (data[0]==0x0f || data[0]==0x0e)) {
                bool valid=true;
                for (ssize_t off=1;off<n;off+=3)
                    if ((data[off]>14 && data[off]!=0xff) || (uint16_t(data[off+1])<<8 | data[off+2])>0x7ff) valid=false;
                if (!valid) continue;
                for (ssize_t off=1;off<n;off+=3) {
                    const auto key=std::make_pair(data[off],uint16_t(uint16_t(data[off+1])<<8 | data[off+2]));
                    if (data[0]==0x0f) it->filters.insert(key); else it->filters.erase(key);
                }
            } else continue; // Never forward control packets to CAN or MCU.
            it->seen=Clock::now();
        }
    }
    void expire() {
        const auto now=Clock::now();
        peers_.erase(std::remove_if(peers_.begin(),peers_.end(),[&](const Peer& p){return now-p.seen>std::chrono::seconds(10);}),peers_.end());
    }
    void send(const CanFrame& f) {
        if (peers_.empty()) return;
        const auto packet=pack_panda(f.id,f.bus,f.dlc,f.payload.data());
        for (auto& p:peers_) {
            if (!force_all_ && p.version==2 && !p.all && !p.filters.count({f.bus,f.id}) && !p.filters.count({0xff,f.id})) continue;
            sendto(p.fd,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&p.address),sizeof(p.address));
        }
    }
    void poll_inputs() {
        std::vector<pollfd> fds;
        for (const auto& l:listeners_) fds.push_back({l.fd,POLLIN,0});
        const int rc=poll(fds.data(),fds.size(),5);
        if (rc>0) for (const auto& f:fds) if (f.revents&POLLIN) handle(f.fd);
    }
};
[[maybe_unused]] std::vector<std::string> split_interfaces(const std::string& s) {
    std::vector<std::string> result; std::stringstream stream(s); std::string part;
    while (std::getline(stream,part,',')) {
        if (part.empty() || part.size()>IFNAMSIZ-1 || part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")!=std::string::npos) return {};
        if (std::find(result.begin(),result.end(),part)==result.end()) result.push_back(part);
    }
    return result;
}
} // namespace
#ifndef PANDABRIDGE_LIBRARY_ONLY
int main(int argc,char** argv) {
    std::string names="ap0,tun1", fixture, pidfile;
    uint16_t port=1338;
    for (int i=1;i<argc;++i) {
        if (i+1>=argc) { std::fprintf(stderr,"missing value for %s\n",argv[i]); return 2; }
        const std::string opt=argv[i]; const std::string val=argv[++i];
        if (opt=="--interfaces") names=val;
        else if (opt=="--pid-file") pidfile=val;
        else if (opt=="--port") {
            char* end=nullptr; const unsigned long n=std::strtoul(val.c_str(),&end,10);
            if (!end || *end || n<1 || n>65535) return 2;
            port=uint16_t(n);
        }
#ifdef PANDABRIDGE_TEST_SOURCE
        else if (opt=="--source-file") fixture=val;
#endif
        else { std::fprintf(stderr,"unsupported option %s\n",opt.c_str()); return 2; }
    }
#ifndef PANDABRIDGE_TEST_SOURCE
    if (geteuid()!=0) { std::fprintf(stderr,"root required for read-only VHAL map_files access\n"); return 3; }
#endif
    const auto interfaces=split_interfaces(names);
    if (interfaces.empty()) return 2;
    int lock=-1;
    if (!pidfile.empty()) {
        lock=open(pidfile.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0644);
        if (lock<0 || flock(lock,LOCK_EX|LOCK_NB)!=0) { std::fprintf(stderr,"bridge already running or pidfile unavailable\n"); return 4; }
        ftruncate(lock,0); const auto pid=std::to_string(getpid())+"\n";
        if (write(lock,pid.c_str(),pid.size()) != ssize_t(pid.size())) return 4;
    }
    signal(SIGTERM,stop); signal(SIGINT,stop);
    Ring ring;
    PandaServer server(interfaces,port);
    auto next_refresh=Clock::time_point::min();
    while (running) {
        if (Clock::now()>=next_refresh) {
            server.refresh(); ring.refresh(fixture); server.expire();
            next_refresh=Clock::now()+std::chrono::seconds(1);
        }
        server.poll_inputs();
        ring.drain([&](const CanFrame& frame){server.send(frame);});
    }
    if (lock>=0) { ftruncate(lock,0); close(lock); }
    return 0;
}
#endif // PANDABRIDGE_LIBRARY_ONLY
