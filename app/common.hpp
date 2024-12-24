#pragma once

#include <chrono>
#include <fstream>
#include <iostream>
#include <seastar/core/shard_id.hh>
#include <string>
#include <vector>

// Common utilities across the client/server implementations.
constexpr uint16_t TCP_SERVER_PORT = 1300;

// Historical traffic measurements in gbit/s and pps.
struct Measurement {
  seastar::shard_id shard_id;
  std::chrono::time_point<std::chrono::system_clock> time;
  double gbits;
  size_t pps;
};

// TODO(benjamin): no prize for this name \_(-.-)_/
struct MeasurementDevice {
 public:
  // Attach one more measurement to the device history.
  void tick() {
    const size_t bytes_in_last_second = bytes_received - last_bytes_received;
    last_bytes_received = bytes_received;
    const size_t pps = packets_received - last_packets_received;
    last_packets_received = packets_received;

    const double throughput_in_gbps = bytes_in_last_second * 8 / 1000.0 / 1000.0 / 1000.0;

    // Add the measurement to the backing tracker.
    measurements.push_back(Measurement{
        .shard_id = seastar::this_shard_id(),
        .time = std::chrono::system_clock::now(),
        .gbits = throughput_in_gbps,
        .pps = pps,
    });

    std::cout << "Throughput on shard " << seastar::this_shard_id() << ": " << throughput_in_gbps
              << " gbps (" << pps << " pps)" << std::endl;
  };

  void add_bytes(size_t bytes) { bytes_received += bytes; };

  void add_packets(size_t packets) { packets_received += packets; };

  std::vector<Measurement> get_history() const { return measurements; }

 private:
  // Core-local bytes received.
  size_t bytes_received = 0;
  // Core-local packets received.
  size_t packets_received = 0;
  // Bytes received at the last check-in of the prober.
  size_t last_bytes_received = 0;
  // Packets received at the last check-in of the prober.
  size_t last_packets_received = 0;
  // Historical throughput measurements.
  std::vector<Measurement> measurements;
};

// Dump measurements from across shards into a target file.
void dump_measurements(const std::string& fname,
                       const std::vector<std::vector<Measurement>>& shard_measurements) {
  std::ofstream file(fname);
  if (!file.is_open()) {
    std::cerr << "Can't open benchmark report file" << std::endl;
    return;
  }

  file << "timestamp,shard,gbits,pps" << std::endl;

  for (const auto& shard : shard_measurements) {
    for (const auto& measurement : shard) {
      const auto dur = measurement.time.time_since_epoch();
      file << std::chrono::duration_cast<std::chrono::milliseconds>(dur).count() << ","
           << measurement.shard_id << "," << measurement.gbits << "," << measurement.pps
           << std::endl;
    }
  }

  file.close();
}
