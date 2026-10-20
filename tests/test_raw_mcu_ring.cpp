#define PANDABRIDGE_LIBRARY_ONLY 1
#include "../native/panda_bridge.cpp"
#include "../native/raw_ring.h"
#include <cassert>
#include <sys/types.h>

int main() {
    char path[] = "/tmp/screenmate-raw-ring-XXXXXX";
    int writer = mkstemp(path);
    assert(writer >= 0);
    unlink(path);
    constexpr uint32_t cap = 128;
    assert(ftruncate(writer, 32 + cap * 16) == 0);
    uint8_t header[32]{};
    put32(header, cap); put32(header + 4, 16);
    assert(pwrite(writer, header, 32, 0) == 32);
    RawMcuRing mismatch;
    assert(!mismatch.attach_fd(dup(writer), 256)); // AIDL metadata/header mismatch fails closed
    RawMcuRing ring;
    assert(ring.attach_fd(dup(writer), cap));
    assert(ring.capacity() == cap);
    uint8_t slot[16]{};
    slot[0] = 0; slot[1] = 8;
    put32(slot + 4, 0x401);
    for (unsigned i=0; i<8; ++i) slot[8+i] = uint8_t(i+1);
    unsigned sum = slot[1] + 0x401;
    for (unsigned i=0; i<8; ++i) sum += slot[8+i];
    slot[2] = uint8_t(sum);
    assert(pwrite(writer, slot, 16, 32) == 16);
    uint64_t seq = 1;
    assert(pwrite(writer, &seq, 8, 8) == 8);
    std::vector<CanFrame> frames;
    ring.drain([&](const CanFrame& f) { frames.push_back(f); });
    assert(frames.size()==1 && frames[0].id==0x401 && frames[0].bus==0 && frames[0].dlc==8);
    auto panda=pack_panda(frames[0].id, frames[0].bus, frames[0].dlc, frames[0].payload.data());
    assert(le32(panda.data()) == 0x401u << 21 && le32(panda.data()+4) == 8);
    assert(panda[8]==1 && panda[15]==8 && ring.overruns()==0 && ring.rejects()==0);
    // A different MCU bus must survive without remapping; malformed slots fail closed.
    uint8_t slot2[16]{};
    slot2[0] = 3; slot2[1] = 2;
    put32(slot2 + 4, (3u << 12) | 0x7ffu);
    slot2[8] = 0x42; slot2[9] = 0x43;
    slot2[2] = uint8_t((slot2[1] + ((3u << 12) | 0x7ffu) + 0x42 + 0x43) & 255);
    assert(pwrite(writer, slot2, 16, 48) == 16);
    seq = 2; assert(pwrite(writer, &seq, 8, 8) == 8);
    ring.drain([&](const CanFrame& f) { frames.push_back(f); });
    assert(frames.size()==2 && frames[1].bus==3 && frames[1].id==0x7ff && frames[1].dlc==2);
    auto panda2=pack_panda(frames[1].id, frames[1].bus, frames[1].dlc, frames[1].payload.data());
    assert(le32(panda2.data()+4) == (3u << 4 | 2u));
    slot2[2] ^= 0xff;
    assert(pwrite(writer, slot2, 16, 64) == 16);
    seq = 3; assert(pwrite(writer, &seq, 8, 8) == 8);
    ring.drain([&](const CanFrame& f) { frames.push_back(f); });
    assert(frames.size()==2 && ring.rejects()==1);
    // Once the producer has lapped the consumer, do not send any later slots:
    // the feed is incomplete and the supervising process must stop.
    seq = cap + 4; assert(pwrite(writer, &seq, 8, 8) == 8);
    ring.drain([&](const CanFrame& f) { frames.push_back(f); });
    assert(ring.overruns()>0 && frames.size()==2);
    close(writer);
    puts("separate raw ring -> Panda 0x401 bus0 and 0x7ff bus3: PASS (synthetic only)");
}
