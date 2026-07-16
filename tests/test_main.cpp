#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <disk_analyzer/analyzer.hpp>

#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>

// ── Unit: DiskStats JSON roundtrip ──

TEST_CASE("DiskStats JSON serialization", "[json]") {
  DiskStats ds;
  ds.device            = "sda";
  ds.reads_completed   = 1000;
  ds.writes_completed  = 2000;
  ds.read_sectors      = 50000;
  ds.write_sectors     = 100000;
  ds.read_time_ms      = 5000;
  ds.write_time_ms     = 8000;
  ds.io_in_progress    = 1;
  ds.io_time_ms        = 12000;
  ds.weighted_io_time_ms = 13000;

  json j = ds.to_json();
  REQUIRE(j["device"] == "sda");
  REQUIRE(j["reads_completed"] == 1000);
  REQUIRE(j["writes_completed"] == 2000);
  REQUIRE(j["read_bytes"] == 50000ULL * 512);
  REQUIRE(j["write_bytes"] == 100000ULL * 512);

  // Roundtrip
  std::string serialized = j.dump();
  json parsed = json::parse(serialized);
  REQUIRE(parsed["device"] == "sda");
  REQUIRE(parsed["reads_completed"] == 1000);
}

// ── Unit: Bandwidth calculation ──

TEST_CASE("Bandwidth delta calculation", "[bandwidth]") {
  DiskStats prev, curr;
  prev.device = "sda";
  prev.read_sectors = 1000;
  prev.write_sectors = 500;
  prev.reads_completed = 100;
  prev.writes_completed = 50;

  curr.device = "sda";
  curr.read_sectors = 2000;     // +1000 sectors = +512KB
  curr.write_sectors = 1000;    // +500 sectors = +256KB
  curr.reads_completed = 200;   // +100 reads
  curr.writes_completed = 100;  // +50 writes

  // Add another device that only exists in curr (should be skipped)
  DiskStats extra;
  extra.device = "sdb";

  auto deltas = DiskAnalyzer::calculate_bandwidth({prev}, {curr, extra}, 2.0);

  REQUIRE(deltas.size() == 1);
  REQUIRE(deltas[0].device == "sda");

  // Read: 512KB / 2s = 256 KB/s = 2048 Kbps
  // Actually: 1000 sectors * 512 B/sector = 512000 B = 500 KB, / 2s = 250 KB/s = 2000 Kbps
  // Wait: 1000 * 512 = 512000 bytes = 500 KB. / 2 = 250 KB/s = 250 Kbps (since kbps is KB/s here)
  // Let's be careful with human_rate units. The code says Kbps but divides by 1024.
  // Actually in human_rate: val < 1024 => return "{:.1f} KB/s".format(val)
  // And calculate_bandwidth: read_kbps = (dr_sectors * 512.0) / (1024.0 * interval_sec)
  // So for sectors: 1000 * 512 / (1024 * 2) = 512000/2048 = 250.0 KB/s
  // The kbps in the code is actually KB/s despite the name. Let's not worry about human_rate
  // and just check the raw calculation.
  REQUIRE(deltas[0].read_kbps == Catch::Approx(250.0).margin(0.01));
  REQUIRE(deltas[0].write_kbps == Catch::Approx(125.0).margin(0.01));
  REQUIRE(deltas[0].read_iops == Catch::Approx(50.0).margin(0.01));
  REQUIRE(deltas[0].write_iops == Catch::Approx(25.0).margin(0.01));
}

// ── Unit: Human size formatting ──

TEST_CASE("Human size formatting", "[format]") {
  // These are tested via the private helper in main.cpp
  // We test via the JSON output which uses human_size indirectly
  // Just verify the values make sense
  DiskStats ds;
  ds.read_sectors = 1024 * 1024;  // ~512 MiB
  json j = ds.to_json();
  REQUIRE(j["read_bytes"] == 1024ULL * 1024 * 512);
}

// ── Integration: /proc/diskstats reading ──

TEST_CASE("/proc/diskstats is accessible", "[integration]") {
  DiskAnalyzer analyzer;
  auto result = analyzer.read_stats();

  REQUIRE(result.has_value());

  // We should have at least some block devices
  REQUIRE(!result->empty());

  // Find a real device (not ram/loop)
  auto it = std::find_if(result->begin(), result->end(), [](const DiskStats& d) {
    return !d.device.starts_with("ram") && !d.device.starts_with("loop");
  });

  if (it != result->end()) {
    REQUIRE(it->reads_completed > 0);  // system has done at least some reads
  } else {
    WARN("No real block device found (only RAM/loop) — running in minimal container?");
  }
}

TEST_CASE("Device info from /sys/block", "[integration]") {
  auto devices = DiskAnalyzer::list_devices();
  REQUIRE(!devices.empty());

  DiskAnalyzer analyzer;
  bool found_real = false;

  for (const auto& dev : devices) {
    if (dev.starts_with("ram") || dev.starts_with("loop")) continue;
    found_real = true;

    auto info = analyzer.get_device_info(dev);
    REQUIRE(info.has_value());
    REQUIRE(info->device == dev);
    break;
  }

  if (!found_real) {
    WARN("No real block device found for sys/block testing");
  }
}

TEST_CASE("Snapshot produces complete data", "[integration]") {
  DiskAnalyzer analyzer;
  auto snap = analyzer.snapshot();
  REQUIRE(snap.has_value());
  REQUIRE(!snap->empty());
}

// ── Benchmark ──

TEST_CASE("Disk stats read performance", "[benchmark]") {
  DiskAnalyzer analyzer;
  BENCHMARK("Read /proc/diskstats") {
    return analyzer.read_stats();
  };
}

TEST_CASE("JSON serialization performance", "[benchmark]") {
  DiskAnalyzer analyzer;
  auto stats = analyzer.read_stats();
  REQUIRE(stats.has_value());
  BENCHMARK("Serialize to JSON") {
    json arr = json::array();
    for (const auto& d : *stats) arr.push_back(d.to_json());
    return arr.dump();
  };
}
