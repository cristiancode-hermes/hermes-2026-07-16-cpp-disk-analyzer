#pragma once

#include <expected>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── Error type ──

struct Error {
  int code;
  std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

// ── Data structures ──

struct DiskStats {
  std::string device;        // e.g., "sda", "nvme0n1"
  uint64_t reads_completed   = 0;
  uint64_t reads_merged      = 0;
  uint64_t read_sectors      = 0;
  uint64_t read_time_ms      = 0;
  uint64_t writes_completed  = 0;
  uint64_t writes_merged     = 0;
  uint64_t write_sectors     = 0;
  uint64_t write_time_ms     = 0;
  uint64_t io_in_progress    = 0;
  uint64_t io_time_ms        = 0;
  uint64_t weighted_io_time_ms = 0;
  uint64_t discard_completed = 0;
  uint64_t discard_merged    = 0;
  uint64_t discard_sectors   = 0;
  uint64_t discard_time_ms   = 0;

  [[nodiscard]] json to_json() const;
  [[nodiscard]] uint64_t iops_read() const { return reads_completed; }
  [[nodiscard]] uint64_t iops_write() const { return writes_completed; }
  [[nodiscard]] uint64_t read_bytes() const { return read_sectors * 512; }
  [[nodiscard]] uint64_t write_bytes() const { return write_sectors * 512; }
};

struct BandwidthDelta {
  std::string device;
  double read_kbps   = 0.0;
  double write_kbps  = 0.0;
  double read_iops   = 0.0;
  double write_iops  = 0.0;
  double read_tps    = 0.0;
  double write_tps   = 0.0;
};

struct DeviceInfo {
  std::string device;
  std::string model;
  uint64_t size_sectors  = 0;
  std::string rotational;     // "0" = SSD, "1" = HDD
};

// ── DiskAnalyzer class ──

class DiskAnalyzer {
public:
  DiskAnalyzer();
  ~DiskAnalyzer();

  // No copy, sí move
  DiskAnalyzer(const DiskAnalyzer&) = delete;
  DiskAnalyzer& operator=(const DiskAnalyzer&) = delete;
  DiskAnalyzer(DiskAnalyzer&&) noexcept = default;
  DiskAnalyzer& operator=(DiskAnalyzer&&) noexcept = default;

  /// Read current disk I/O stats from /proc/diskstats
  [[nodiscard]] Result<std::vector<DiskStats>> read_stats();

  /// Get device info from /sys/block
  [[nodiscard]] Result<DeviceInfo> get_device_info(const std::string& device);

  /// Take a full snapshot of all disk stats
  [[nodiscard]] Result<std::vector<DiskStats>> snapshot();

  /// Calculate bandwidth deltas between two snapshots
  [[nodiscard]] static std::vector<BandwidthDelta> calculate_bandwidth(
    const std::vector<DiskStats>& prev,
    const std::vector<DiskStats>& curr,
    double interval_sec);

  /// Get available block devices from /sys/block
  [[nodiscard]] static std::vector<std::string> list_devices();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
