#include <disk_analyzer/analyzer.hpp>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <print>
#include <ranges>
#include <thread>

using namespace std::chrono_literals;

// ── Format helpers ──

static std::string human_size(uint64_t bytes) {
  const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double val = static_cast<double>(bytes);
  int unit = 0;
  while (val >= 1024.0 && unit < 4) { val /= 1024.0; unit++; }
  return std::format("{:.1f} {}", val, units[unit]);
}

static std::string human_rate(double kbps) {
  if (kbps < 1.0) return std::format("{:.1f} B/s", kbps * 1024.0);
  if (kbps < 1024.0) return std::format("{:.1f} KB/s", kbps);
  if (kbps < 1024.0 * 1024.0) return std::format("{:.2f} MB/s", kbps / 1024.0);
  return std::format("{:.2f} GB/s", kbps / (1024.0 * 1024.0));
}

// ── Display functions ──

static void print_disk_table(const std::vector<DiskStats>& disks) {
  std::println(std::cout, "{:<12} {:>12} {:>12} {:>12} {:>12} {:>10} {:>8} {:>8}",
               "DEVICE", "READ_BYTES", "WRITE_BYTES", "READ_TIME",
               "WRITE_TIME", "IO_IN_PROG", "IOPS_R", "IOPS_W");

  std::println(std::cout, "{}", std::string(88, '-'));

  for (const auto& d : disks) {
    std::println(std::cout, "{:<12} {:>12} {:>12} {:>12} {:>12} {:>10} {:>8} {:>8}",
                 d.device,
                 human_size(d.read_bytes()),
                 human_size(d.write_bytes()),
                 std::format("{}ms", d.read_time_ms),
                 std::format("{}ms", d.write_time_ms),
                 d.io_in_progress,
                 d.reads_completed,
                 d.writes_completed);
  }
}

static void print_device_info_table(const std::vector<DeviceInfo>& devices) {
  std::println(std::cout, "{:<12} {:>25} {:>12} {:>10}",
               "DEVICE", "MODEL", "SIZE", "TYPE");
  std::println(std::cout, "{}", std::string(62, '-'));

  for (const auto& d : devices) {
    std::string size_str = d.size_sectors > 0
      ? human_size(d.size_sectors * 512)
      : "-";
    std::string type = d.rotational == "1" ? "HDD" :
                       d.rotational == "0" ? "SSD" : "-";
    std::println(std::cout, "{:<12} {:>25} {:>12} {:>10}",
                 d.device,
                 d.model.empty() ? "-" : d.model,
                 size_str,
                 type);
  }
}

static void print_bandwidth_table(const std::vector<BandwidthDelta>& deltas) {
  std::println(std::cout, "{:<12} {:>14} {:>14} {:>14} {:>10} {:>10}",
               "DEVICE", "READ_RATE", "WRITE_RATE", "TOTAL_RATE",
               "IOPS_R", "IOPS_W");
  std::println(std::cout, "{}", std::string(77, '-'));

  double total_rx = 0, total_tx = 0;
  for (const auto& d : deltas) {
    std::println(std::cout, "{:<12} {:>14} {:>14} {:>14} {:>10.0f} {:>10.0f}",
                 d.device,
                 human_rate(d.read_kbps),
                 human_rate(d.write_kbps),
                 human_rate(d.read_kbps + d.write_kbps),
                 d.read_iops,
                 d.write_iops);
    total_rx += d.read_kbps;
    total_tx += d.write_kbps;
  }

  std::println(std::cout, "{}", std::string(77, '-'));
  std::println(std::cout, "{:<12} {:>14} {:>14} {:>14}",
               "TOTAL", human_rate(total_rx), human_rate(total_tx),
               human_rate(total_rx + total_tx));
}

// ── Main ──

int main(int argc, char** argv) {
  CLI::App app{"Disk Analyzer — Linux disk I/O monitoring tool (/proc/diskstats)"};

  bool show_json = false;
  bool show_info = false;
  bool monitor_mode = false;
  int monitor_interval = 2;
  std::string filter_device;

  app.add_flag("-j,--json", show_json, "JSON output mode");
  app.add_flag("-i,--info", show_info, "Show device info from /sys/block");
  app.add_flag("-m,--monitor", monitor_mode, "Real-time bandwidth monitoring mode");
  app.add_option("--interval", monitor_interval, "Monitor refresh interval (seconds)")->default_val(2);
  app.add_option("-f,--filter", filter_device, "Filter by device name (e.g., sda, nvme0n1)");

  CLI11_PARSE(app, argc, argv);

  // Set up color logging
  auto console = spdlog::stdout_color_mt("console");
  spdlog::set_level(spdlog::level::warn);

  DiskAnalyzer analyzer;

  // ── Device info mode ──
  if (show_info) {
    auto devices_list = DiskAnalyzer::list_devices();
    if (devices_list.empty()) {
      std::println(std::cerr, "Error: no block devices found");
      return 1;
    }

    std::vector<DeviceInfo> infos;
    for (const auto& dev : devices_list) {
      if (!filter_device.empty() && dev != filter_device) continue;
      auto info = analyzer.get_device_info(dev);
      if (info) {
        infos.push_back(std::move(*info));
      }
    }

    if (show_json) {
      json arr = json::array();
      for (const auto& inf : infos) {
        arr.push_back({
          {"device", inf.device},
          {"model", inf.model},
          {"size_bytes", inf.size_sectors * 512},
          {"rotational", inf.rotational},
          {"type", inf.rotational == "1" ? "HDD" : inf.rotational == "0" ? "SSD" : "unknown"}
        });
      }
      std::println(std::cout, "{}", arr.dump(2));
    } else {
      print_device_info_table(infos);
    }
    return 0;
  }

  // ── Monitor mode ──
  if (monitor_mode) {
    auto prev = analyzer.snapshot();
    if (!prev) {
      std::println(std::cerr, "Error: {}", prev.error().message);
      return 1;
    }

    std::println(std::cout, "Disk Analyzer — Monitor Mode (refresh every {}s)\n", monitor_interval);

    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(monitor_interval));
      auto curr = analyzer.snapshot();
      if (!curr) break;

      auto deltas = DiskAnalyzer::calculate_bandwidth(*prev, *curr, monitor_interval);

      // Filter
      if (!filter_device.empty()) {
        std::erase_if(deltas, [&](const auto& d) { return d.device != filter_device; });
      }

      // Exclude RAM disks and loopback
      std::erase_if(deltas, [](const auto& d) {
        return d.device.starts_with("ram") || d.device.starts_with("loop");
      });

      if (deltas.empty()) {
        std::println(std::cout, "\033[2J\033[H");
        std::println(std::cout, "Disk Analyzer — Monitor Mode (refresh every {}s)\n", monitor_interval);
        std::println(std::cout, "No disk activity detected (excluding RAM/loop devices)");
        prev = std::move(*curr);
        continue;
      }

      // Clear screen (ANSI)
      std::println(std::cout, "\033[2J\033[H");
      std::println(std::cout, "Disk Analyzer — Monitor Mode (refresh every {}s)\n", monitor_interval);

      if (show_json) {
        json arr = json::array();
        for (const auto& d : deltas) {
          arr.push_back({
            {"device", d.device},
            {"read_kbps", d.read_kbps},
            {"write_kbps", d.write_kbps},
            {"read_iops", d.read_iops},
            {"write_iops", d.write_iops}
          });
        }
        std::println(std::cout, "{}", arr.dump(2));
      } else {
        print_bandwidth_table(deltas);
      }

      prev = std::move(*curr);
    }
    return 0;
  }

  // ── Default: show disk stats ──
  auto stats = analyzer.snapshot();
  if (!stats) {
    std::println(std::cerr, "Error reading disk stats: {}", stats.error().message);
    return 1;
  }

  if (!filter_device.empty()) {
    std::erase_if(*stats, [&](const DiskStats& d) { return d.device != filter_device; });
  }

  // Exclude RAM disks and loopback
  std::erase_if(*stats, [](const DiskStats& d) {
    return d.device.starts_with("ram") || d.device.starts_with("loop");
  });

  if (show_json) {
    json arr = json::array();
    for (const auto& d : *stats) arr.push_back(d.to_json());
    std::println(std::cout, "{}", arr.dump(2));
  } else {
    std::println(std::cout, "Disk Analyzer — I/O Statistics\n");
    print_disk_table(*stats);
  }

  return 0;
}
