#include "src/engine/coverage_shm.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace grpc_authz_fuzz::engine {
namespace {

struct ShmHeader {
  uint32_t magic;
  uint32_t bitmap_size;
};

constexpr std::size_t MapSize() {
  return sizeof(ShmHeader) + kCoverageBitmapSize;
}

}  // namespace

CoverageShm::CoverageShm(std::string name, int fd, void* map, std::size_t map_size, uint8_t* bitmap,
                         std::size_t bitmap_size, bool owner)
    : name_(std::move(name)),
      fd_(fd),
      map_(map),
      map_size_(map_size),
      bitmap_(bitmap),
      bitmap_size_(bitmap_size),
      owner_(owner) {}

CoverageShm::~CoverageShm() { Reset(); }

CoverageShm::CoverageShm(CoverageShm&& other) noexcept { *this = std::move(other); }

CoverageShm& CoverageShm::operator=(CoverageShm&& other) noexcept {

  if (this == &other) {
    return *this;
  }

  Reset();

  name_ = std::move(other.name_);
  fd_ = other.fd_;
  map_ = other.map_;
  map_size_ = other.map_size_;
  bitmap_ = other.bitmap_;
  bitmap_size_ = other.bitmap_size_;
  owner_ = other.owner_;
  other.fd_ = -1;
  other.map_ = nullptr;
  other.map_size_ = 0;
  other.bitmap_ = nullptr;
  other.bitmap_size_ = 0;
  other.owner_ = false;

  return *this;

}

void CoverageShm::Reset() {

  if (map_ != nullptr) {
    munmap(map_, map_size_);
    map_ = nullptr;
  }

  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }

  if (owner_ && !name_.empty()) {
    shm_unlink(name_.c_str());
  }

  name_.clear();
  bitmap_ = nullptr;
  bitmap_size_ = 0;
  map_size_ = 0;
  owner_ = false;

}

CoverageShm CoverageShm::Create(const std::string& name) {

  shm_unlink(name.c_str());
  const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);

  if (fd < 0) {
    throw std::runtime_error(std::string("shm_open(create) failed: ") + std::strerror(errno));
  }

  const auto map_size = MapSize();
  if (ftruncate(fd, static_cast<off_t>(map_size)) != 0) {
    const int err = errno;
    close(fd);
    shm_unlink(name.c_str());
    throw std::runtime_error(std::string("ftruncate failed: ") + std::strerror(err));
  }

  void* map = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    const int err = errno;
    close(fd);
    shm_unlink(name.c_str());
    throw std::runtime_error(std::string("mmap failed: ") + std::strerror(err));
  }
  
  auto* header = static_cast<ShmHeader*>(map);
  header->magic = kCoverageShmMagic;
  header->bitmap_size = static_cast<uint32_t>(kCoverageBitmapSize);

  auto* bitmap = reinterpret_cast<uint8_t*>(header + 1);
  std::memset(bitmap, 0, kCoverageBitmapSize);

  return CoverageShm(name, fd, map, map_size, bitmap, kCoverageBitmapSize, true);

}

CoverageShm CoverageShm::Open(const std::string& name) {

  const int fd = shm_open(name.c_str(), O_RDWR, 0600);
  if (fd < 0) {
    throw std::runtime_error(std::string("shm_open(open) failed: ") + std::strerror(errno));
  }

  const auto map_size = MapSize();
  void* map = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    const int err = errno;
    close(fd);
    throw std::runtime_error(std::string("mmap failed: ") + std::strerror(err));
  }

  auto* header = static_cast<ShmHeader*>(map);
  if (header->magic != kCoverageShmMagic || header->bitmap_size != kCoverageBitmapSize) {
    munmap(map, map_size);
    close(fd);
    throw std::runtime_error("coverage SHM header mismatch");
  }

  auto* bitmap = reinterpret_cast<uint8_t*>(header + 1);

  return CoverageShm(name, fd, map, map_size, bitmap, kCoverageBitmapSize, false);

}

void CoverageShm::Clear() {
  if (bitmap_ != nullptr) 
    std::memset(bitmap_, 0, bitmap_size_);
}

void CoverageShm::ObserveMethod(const std::string& method) {

  if (bitmap_ == nullptr || bitmap_size_ == 0) {
    return;
  }

  //TODO: FNV-1a 32-bit => bit index, stub till real sancov
  uint32_t hash = 2166136261u;
  for (unsigned char c : method) {
    hash ^= c;
    hash *= 16777619u;
  }

  const std::size_t index = hash % bitmap_size_;
  bitmap_[index] = 1;
}

uint32_t CoverageShm::CountSetBits() const {

  if (bitmap_ == nullptr) {
    return 0;
  }

  uint32_t n = 0;
  for (std::size_t i = 0; i < bitmap_size_; ++i) {
    n += bitmap_[i] != 0 ? 1u : 0u;
  }

  return n;
}

uint32_t CoverageShm::OrMerge(uint8_t* dst, const uint8_t* src, std::size_t n) {

  uint32_t newly = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (src[i] != 0 && dst[i] == 0) {
      ++newly;
    }
    dst[i] = static_cast<uint8_t>(dst[i] | src[i]);
  }

  return newly;

}

}  // namespace grpc_authz_fuzz::engine
