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
struct Measurement
{
  seastar::shard_id shard_id;
  std::chrono::time_point<std::chrono::system_clock> time;
  double received_gbits;
  double sent_gbits;
  size_t received_pps;
  size_t sent_pps;
};

// TODO(benjamin): no prize for this name \_(-.-)_/
struct MeasurementDevice
{
public:
  // Attach one more measurement to the device history.
  void tick()
  {
    const size_t bytes_received_in_last_second = bytes_received - last_bytes_received;
    last_bytes_received = bytes_received;
    const size_t pps_received = packets_received - last_packets_received;
    last_packets_received = packets_received;

    const size_t bytes_sent_in_last_second = bytes_sent - last_bytes_sent;
    last_bytes_sent = bytes_sent;
    const size_t pps_sent = packets_sent - last_packets_sent;
    last_packets_sent = packets_sent;

    const double throughput_received_in_gbps = bytes_received_in_last_second * 8 / 1000.0 / 1000.0 / 1000.0;
    const double throughput_sent_in_gbps = bytes_sent_in_last_second * 8 / 1000.0 / 1000.0 / 1000.0;

    // Add the measurement to the backing tracker.
    measurements.push_back(Measurement{
        .shard_id = seastar::this_shard_id(),
        .time = std::chrono::system_clock::now(),
        .received_gbits = throughput_received_in_gbps,
        .sent_gbits = throughput_sent_in_gbps,
        .received_pps = pps_received,
        .sent_pps = pps_sent,
    });

    std::cout << "Throughput on shard " << seastar::this_shard_id() << ": " << "Received: " << throughput_received_in_gbps << " gbps ("
              << pps_received << " pps), " << "Sent: " << throughput_sent_in_gbps << " gbps (" << pps_sent << " pps)" << std::endl;
  };

  void add_bytes_received(size_t bytes)
  {
    bytes_received += bytes;
    ++packets_received;
  };

  void add_bytes_sent(size_t bytes)
  {
    bytes_sent += bytes;
    ++packets_sent;
  };

  std::vector<Measurement> get_history() const
  {
    return measurements;
  }

private:
  // Core-local bytes received.
  size_t bytes_received = 0;
  // Core-local packets received.
  size_t packets_received = 0;
  // Bytes received at the last check-in of the prober.
  size_t last_bytes_received = 0;
  // Packets received at the last check-in of the prober.
  size_t last_packets_received = 0;

  // Core-local bytes sent.
  size_t bytes_sent = 0;
  // Core-local packets sent.
  size_t packets_sent = 0;
  // Bytes sent at the last check-in of the prober.
  size_t last_bytes_sent = 0;
  // Packets sent at the last check-in of the prober.
  size_t last_packets_sent = 0;

  // Historical throughput measurements.
  std::vector<Measurement> measurements;
};

// Dump measurements from across shards into a target file.
inline void dump_measurements(const std::string& fname, const std::vector<std::vector<Measurement>>& shard_measurements)
{
  std::ofstream file(fname);
  if (!file.is_open()) {
    std::cerr << "Can't open benchmark report file" << std::endl;
    return;
  }

  file << "timestamp,shard,received_gbits,received_pps,sent_gbits,sent_pps" << std::endl;

  for (const auto& shard : shard_measurements) {
    for (const auto& measurement : shard) {
      const auto dur = measurement.time.time_since_epoch();
      file << std::chrono::duration_cast<std::chrono::milliseconds>(dur).count() << "," << measurement.shard_id << "," << measurement.received_gbits
           << "," << measurement.received_pps << "," << measurement.sent_gbits << "," << measurement.sent_pps << std::endl;
    }
  }

  file.close();
}
