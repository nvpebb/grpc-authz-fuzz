#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace grpc_authz_fuzz::engine {

inline constexpr uint32_t kCoverageShmMagic = 0x47414643u;  // GAFC
inline constexpr std::size_t kCoverageBitmapSize = 1u << 16;

class CoverageShm {
 public:
  CoverageShm() = default;
  ~CoverageShm();

  CoverageShm(const CoverageShm&) = delete;
  CoverageShm& operator=(const CoverageShm&) = delete;

  CoverageShm(CoverageShm&& other) noexcept;
  CoverageShm& operator=(CoverageShm&& other) noexcept;

  static CoverageShm Create(const std::string& name);
  static CoverageShm Open(const std::string& name);

  const std::string& name() const { return name_; }
  uint8_t* bitmap() { return bitmap_; }
  const uint8_t* bitmap() const { return bitmap_; }
  std::size_t bitmap_size() const { return bitmap_size_; }

  void Clear();
  void ObserveMethod(const std::string& method);  // stub: hash => set bit
  uint32_t CountSetBits() const;

  static uint32_t OrMerge(uint8_t* dst, const uint8_t* src, std::size_t n);

 private:
  CoverageShm(std::string name, int fd, void* map, std::size_t map_size, uint8_t* bitmap,
              std::size_t bitmap_size, bool owner);

  void Reset();

  std::string name_;
  int fd_{-1};
  void* map_{nullptr};
  std::size_t map_size_{0};
  uint8_t* bitmap_{nullptr};
  std::size_t bitmap_size_{0};
  bool owner_{false};
};

}  // namespace grpc_authz_fuzz::engine
