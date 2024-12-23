/// Benjamin's first attempt at a Seastar server. The server listens
/// for connections, accepts packets, and throws them away.

#include <iomanip>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/timer.hh>
#include <seastar/net/api.hh>

#include "common.hpp"

/// A sharded service across cores.
class throughput_service {
 public:
  // TODO(benjamin): Interrupting doesn't work properly yet
  seastar::future<> run_udp() {
    const uint16_t port = 1200 + seastar::this_shard_id();
    std::cout << "Staring udp seastar service on shard " << seastar::this_shard_id()
              << " - listening port: " << port << std::endl;

    // Create a UDP channel we can listen on.
    auto channel = seastar::make_bound_datagram_channel(seastar::ipv4_addr{port});

    return seastar::do_with(std::move(channel), [this](auto& channel) {
      // Receive datagrams in a loop
      return seastar::repeat([this, &channel] {
        if (interrupted_) {
          return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
        }
        return channel.receive().then([this](seastar::net::udp_datagram datagram) {
          // Increment packet and byte counters in the backing measuring device.
          measurements.add_packets(1);
          measurements.add_bytes(datagram.get_data().len());

          // Output the number of bytes received and the sender's address
          // std::cout << "Received packet with " << datagram.get_data().len() << " bytes "
          // << "from " << datagram.get_src() << "\n";

          return seastar::stop_iteration::no;  // Continue receiving packets
        });
      });
    });
  }

  seastar::future<> stop() {
    std::cout << "Stopping seastar service on " << seastar::this_shard_id() << "\n";
    // Mark the state as interrupted.
    interrupted_ = true;
    return seastar::make_ready_future<>();
  }

  seastar::future<> setup_reporter() {
    timer.set_callback([this]() { measurements.tick(); });
    timer.arm_periodic(std::chrono::seconds(1));
    return seastar::make_ready_future<>();
  }

  std::vector<Measurement> get_measurements() const { return measurements.get_history(); };

 private:
  // Interrupt gate to know when we need to stop working.
  bool interrupted_ = false;

  // Core-local timer to report throughput.
  seastar::timer<> timer;
  // The throughput measurements for this shard.
  MeasurementDevice measurements;
};

// Write the benchmark report.
seastar::future<> write_throughput_report(seastar::sharded<throughput_service>& service) {
  return service
      // First, retrieve the measurements from every shard.
      .map([](throughput_service& service) { return service.get_measurements(); })
      // Then, take the measurements and dump them to a file.
      .then([](auto shard_measurements) {
        dump_measurements("server_report.csv", shard_measurements);
        return seastar::make_ready_future<>();
      });
}

int main(int argc, char** argv) {
  seastar::app_template app;

  return app.run(argc, argv, [] {
    auto service = std::make_shared<seastar::sharded<throughput_service>>();

    seastar::engine().at_exit(
        [service] { return service->invoke_on_all(&throughput_service::stop); });

    return service->start()
        .then([service] { return service->invoke_on_all(&throughput_service::setup_reporter); })
        .then([service] { return service->invoke_on_all(&throughput_service::run_udp); })
        // We need to write the benchmark report before calling "stop" on the service.
        .then([service] {
          std::cout << "Writing benchmark report\n";
          return write_throughput_report(*service);
        })
        .then([service] { return service->stop(); })
        .then([service] {
          std::cout << "Service stopped on all cores\n";
          return seastar::make_ready_future<>();
        });
  });
}
