#include <disk_analyzer/analyzer.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <ranges>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

// ── Implementation ──

struct DiskAnalyzer::Impl {
  // Cache device types (rotational vs SSD) for lookup
};

DiskAnalyzer::DiskAnalyzer()
  : impl_(std::make_unique<Impl>()) {}

DiskAnalyzer::~DiskAnalyzer() = default;

// ── JSON serialization ──

json DiskStats::to_json() const {
  json j;
  j["device"]             = device;
  j["reads_completed"]    = reads_completed;
  j["reads_merged"]       = reads_merged;
  j["read_sectors"]       = read_sectors;
  j["read_time_ms"]       = read_time_ms;
  j["writes_completed"]   = writes_completed;
  j["writes_merged"]      = writes_merged;
  j["write_sectors"]      = write_sectors;
  j["write_time_ms"]      = write_time_ms;
  j["io_in_progress"]     = io_in_progress;
  j["io_time_ms"]         = io_time_ms;
  j["weighted_io_time_ms"]   = weighted_io_time_ms;
  j["discard_completed"]  = discard_completed;
  j["discard_merged"]     = discard_merged;
  j["discard_sectors"]    = discard_sectors;
  j["discard_time_ms"]    = discard_time_ms;
  j["read_bytes"]         = read_bytes();
  j["write_bytes"]        = write_bytes();
  return j;
}

// ── /proc/diskstats parser ──

Result<std::vector<DiskStats>> DiskAnalyzer::read_stats() {
  std::ifstream f("/proc/diskstats");
  if (!f.is_open()) {
    return std::unexpected(Error{500, std::format("Cannot open /proc/diskstats: {}", std::strerror(errno))});
  }

  std::vector<DiskStats> stats;
  std::string line;

  while (std::getline(f, line)) {
    std::istringstream ss(line);
    DiskStats ds;
    int major, minor;
    std::string device;

    // Format: major minor device rd_completed rd_merged rd_sectors rd_time
    //         wr_completed wr_merged wr_sectors wr_time io_progress io_time
    //         weighted_io_time
    // Optional: discard_completed discard_merged discard_sectors discard_time
    if (!(ss >> major >> minor >> ds.device)) continue;

    ss >> ds.reads_completed >> ds.reads_merged >> ds.read_sectors >> ds.read_time_ms
       >> ds.writes_completed >> ds.writes_merged >> ds.write_sectors >> ds.write_time_ms
       >> ds.io_in_progress >> ds.io_time_ms >> ds.weighted_io_time_ms;

    // Optional discard fields
    if (ss >> ds.discard_completed) {
      ss >> ds.discard_merged >> ds.discard_sectors >> ds.discard_time_ms;
    }

    stats.push_back(std::move(ds));
  }

  return stats;
}

// ── Device info ──

Result<DeviceInfo> DiskAnalyzer::get_device_info(const std::string& device) {
  DeviceInfo info;
  info.device = device;

  std::string base = std::format("/sys/block/{}", device);

  // Check if device exists
  if (!fs::exists(base)) {
    return std::unexpected(Error{404, std::format("Device '{}' not found", device)});
  }

  // Read size
  std::ifstream size_file(std::format("{}/size", base));
  if (size_file.is_open()) {
    size_file >> info.size_sectors;
  }

  // Read rotational flag
  std::ifstream rot_file(std::format("{}/queue/rotational", base));
  if (rot_file.is_open()) {
    rot_file >> info.rotational;
  }

  // Try to read model name
  std::ifstream model_file(std::format("{}/device/model", base));
  if (model_file.is_open()) {
    std::getline(model_file, info.model);
  }

  return info;
}

// ── Dynamic device list ──

std::vector<std::string> DiskAnalyzer::list_devices() {
  std::vector<std::string> devices;
  if (!fs::exists("/sys/block")) return devices;

  for (const auto& entry : fs::directory_iterator("/sys/block")) {
    std::string name = entry.path().filename().string();
    // Skip RAM disks, loopback, etc.
    if (name.starts_with("ram") || name.starts_with("loop") || name == "sr0") continue;
    devices.push_back(name);
  }
  std::ranges::sort(devices);
  return devices;
}

// ── Snapshot ──

Result<std::vector<DiskStats>> DiskAnalyzer::snapshot() {
  return read_stats();
}

// ── Bandwidth calculation ──

std::vector<BandwidthDelta> DiskAnalyzer::calculate_bandwidth(
  const std::vector<DiskStats>& prev,
  const std::vector<DiskStats>& curr,
  double interval_sec)
{
  std::vector<BandwidthDelta> deltas;

  for (const auto& cur : curr) {
    auto it = std::ranges::find_if(prev, [&](const DiskStats& p) {
      return p.device == cur.device;
    });

    if (it == prev.end()) continue;

    const auto& prv = *it;
    BandwidthDelta d;
    d.device = cur.device;

    double dr_sectors   = static_cast<double>(cur.read_sectors - prv.read_sectors);
    double dw_sectors   = static_cast<double>(cur.write_sectors - prv.write_sectors);
    double dr_completed = static_cast<double>(cur.reads_completed - prv.reads_completed);
    double dw_completed = static_cast<double>(cur.writes_completed - prv.writes_completed);

    // Convert sectors (512 bytes) to KB
    d.read_kbps  = (dr_sectors * 512.0) / (1024.0 * interval_sec);
    d.write_kbps = (dw_sectors * 512.0) / (1024.0 * interval_sec);
    d.read_iops  = dr_completed / interval_sec;
    d.write_iops = dw_completed / interval_sec;

    deltas.push_back(std::move(d));
  }

  return deltas;
}
