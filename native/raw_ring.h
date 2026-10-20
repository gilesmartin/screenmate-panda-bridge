#pragma once
// Read-only reader for a separately allocated Teslogic MCU CAN stream ring.
// Does not open a CAN device or create/close a vendor session itself.
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/sharedmem.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>

class RawMcuRing {
    int fd_ = -1;
    const uint8_t* bytes_ = nullptr;
    size_t length_ = 0;
    uint32_t capacity_ = 0;
    uint64_t next_ = 0, overruns_ = 0, rejects_ = 0;
public:
    ~RawMcuRing() { detach(); }
    RawMcuRing(const RawMcuRing&) = delete;
    RawMcuRing& operator=(const RawMcuRing&) = delete;
    RawMcuRing() = default;
    void detach() {
        if (bytes_) munmap(const_cast<uint8_t*>(bytes_), length_);
        if (fd_ >= 0) close(fd_);
        fd_ = -1; bytes_ = nullptr; length_ = 0; capacity_ = 0;
    }
    bool attach_fd(int fd, uint32_t expected_capacity=0) { // takes ownership, including on failure
        detach();
        if (fd < 0) return false;
        struct stat st{};
        if (fstat(fd, &st)) { close(fd); return false; }
        int64_t region_size = st.st_size;
#ifdef __ANDROID__
        // Legacy ashmem reports zero st_size even when its ioctl size and
        // mapped region are valid (confirmed on ScreenMate's Android runtime).
        if (region_size < 32) region_size = ASharedMemory_getSize(fd);
#endif
        if (region_size < 32) { close(fd); return false; }
        // Android ashmem may be mappable yet reject pread(). Read the producer's
        // header through a bounded, read-only mapping instead of fd I/O.
        void* header = mmap(nullptr, 32, PROT_READ, MAP_SHARED, fd, 0);
        if (header == MAP_FAILED) { close(fd); return false; }
        const uint32_t cap = le32(static_cast<const uint8_t*>(header));
        const uint32_t slot = le32(static_cast<const uint8_t*>(header) + 4);
        munmap(header, 32);
        if (cap < 2 || cap > 4096 || (cap & (cap - 1)) || slot != 16 ||
            (expected_capacity && cap != expected_capacity) ||
            region_size < static_cast<int64_t>(32u + size_t(cap) * 16)) {
            close(fd); return false;
        }
        const size_t length = 32u + size_t(cap) * 16;
        void* ptr = mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) { close(fd); return false; }
        const auto* bytes = static_cast<const uint8_t*>(ptr);
        if (le32(bytes) != cap || le32(bytes + 4) != slot) {
            munmap(ptr, length); close(fd); return false;
        }
        fd_ = fd; bytes_ = bytes;
        length_ = length; capacity_ = cap;
        next_ = sequence(bytes_); // join at current write sequence, not old frames
        return true;
    }
    uint32_t capacity() const { return capacity_; }
    uint64_t overruns() const { return overruns_; }
    uint64_t rejects() const { return rejects_; }
    template <typename Sink> void drain(Sink sink) {
        if (!bytes_ || overruns_) return; // incomplete feed is terminal
        uint64_t published = sequence(bytes_);
        if (published < next_) { ++overruns_; return; }
        // Exactly full may already overwrite the oldest slot before the new
        // sequence is published. Conservatively stop rather than skip frames.
        if (published - next_ >= capacity_) {
            overruns_ += published - next_ - capacity_ + 1;
            return;
        }
        unsigned attempts = 0;
        while (next_ < published && attempts++ < capacity_) {
            uint8_t slot[16];
            std::memcpy(slot, bytes_ + 32u + (next_ & (capacity_ - 1u)) * 16u, 16);
            const uint64_t after = sequence(bytes_);
            if (after < next_) { ++overruns_; return; }
            if (after - next_ >= capacity_) {
                overruns_ += after - next_ - capacity_ + 1;
                return;
            }
            CanFrame frame{};
            if (decode(slot, frame)) sink(frame); else ++rejects_;
            ++next_;
            published = after;
        }
    }
};
