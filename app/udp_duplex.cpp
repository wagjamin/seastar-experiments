#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/api.hh>
#include <seastar/net/socket_defs.hh>
#include <seastar/util/log.hh>

#include "common.hpp"

/// The number of concurrent UDP connections per shard.
uint16_t CONCURRENT_CONNECTIONS;
/// The starting port for the UDP server.
constexpr uint16_t START_PORT = 1300;
/// The offset for the UDP server port for this shard.
uint16_t MY_PORT_OFFSET;
/// The offset for the UDP server port for the other shard.
uint16_t OTHER_PORT_OFFSET;

constexpr uint32_t DEFAULT_DATA_SIZE = 64;
uint32_t DATA_SIZE;

/// A sharded client across cores.
class throughput_service
{
public:
  seastar::future<> run_udp(const std::string& server_ip)
  {
    // Create multiple concurrent connections
    for (uint16_t i = 0; i < CONCURRENT_CONNECTIONS; ++i) {
      std::cout << "Starting task " << i << " for shard " << seastar::this_shard_id() << std::endl;
      const uint16_t target_port = START_PORT + (seastar::this_shard_id() * CONCURRENT_CONNECTIONS) + i;

      (void)seastar::with_gate(gate_, [this, i, target_port, server_ip]() {
        return handle_connection(target_port + MY_PORT_OFFSET, target_port + OTHER_PORT_OFFSET, server_ip);
      });
    }

    return gate_.close();
  }

  seastar::future<> stop()
  {
    std::cout << "Stopping seastar service on " << seastar::this_shard_id() << "\n";
    interrupted_ = true;
    return seastar::make_ready_future<>();
  }

  seastar::future<> setup_reporter()
  {
    // Interleave shards by 10 milliseconds to ensure that the report printing in cout doesn't
    // interleave.
    return seastar::sleep(std::chrono::milliseconds(10 * seastar::this_shard_id())).then([this]() {
      timer.set_callback([this]() {
        read_measurements.tick("read");
        write_measurements.tick("write");
      });
      timer.arm_periodic(std::chrono::seconds(1));
      return seastar::make_ready_future<>();
    });
  }

  std::vector<Measurement> get_read_measurements() const
  {
    return read_measurements.get_history();
  };

  std::vector<Measurement> get_write_measurements() const
  {
    return write_measurements.get_history();
  };

private:
  seastar::future<> handle_connection(const uint16_t my_port, const uint16_t other_port, const std::string& other_ip)
  {
    auto other_addr = seastar::socket_address(seastar::ipv4_addr{other_ip, other_port});
    auto packet = std::vector<char>(DATA_SIZE, 'A');
    auto send_channel = seastar::make_unbound_datagram_channel(AF_INET);

    auto write_future =
        seastar::do_with(std::move(send_channel), std::move(packet), [this, &other_addr](seastar::net::datagram_channel& send_channel, auto& packet) {
          return seastar::repeat([this, other_addr = std::move(other_addr), channel = std::move(send_channel), packet = std::move(packet)] mutable {
            if (interrupted_) {
              return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
            }
            // Create a seastar::net::packet from the vector
            seastar::net::packet p = seastar::net::packet::from_static_data(packet.data(), packet.size());
            return channel.send(other_addr, std::move(p)).then([this] {
              write_measurements.add_bytes(DATA_SIZE);
              write_measurements.add_packets(1);
              return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
            });
          });
        });

    auto recv_channel = seastar::make_bound_datagram_channel(seastar::ipv4_addr{my_port});
    auto read_future =
        seastar::do_with(std::move(recv_channel), std::move(packet), [this, &other_addr](seastar::net::datagram_channel& recv_channel, auto& packet) {
          return seastar::repeat([this, &recv_channel] {
            if (interrupted_) {
              return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
            }
            return recv_channel.receive().then([this](seastar::net::udp_datagram datagram) {
              // Increment packet and byte counters in the backing measuring device.
              read_measurements.add_packets(1);
              read_measurements.add_bytes(datagram.get_data().len());
              return seastar::stop_iteration::no;  // Continue receiving packets
            });
          });
        });
    return seastar::when_all_succeed(write_future.discard_result(), read_future.discard_result()).discard_result();
  }

  // Interrupt gate to know when we need to stop working.
  bool interrupted_ = false;
  // Core-local timer to report throughput.
  seastar::timer<> timer;
  // The throughput measurements for this shard.
  MeasurementDevice read_measurements;
  MeasurementDevice write_measurements;
  seastar::gate gate_;
};

// Write the benchmark report.
seastar::future<> write_throughput_report(seastar::sharded<throughput_service>& service)
{
  return service
      // First, retrieve the measurements from every shard.
      .map([](throughput_service& service) {
        return std::make_pair(service.get_read_measurements(), service.get_write_measurements());
      })
      // Then, take the measurements and dump them to a file.
      .then([](auto shard_measurements) {
        std::vector<std::vector<Measurement>> read_measurements;
        std::vector<std::vector<Measurement>> write_measurements;
        for (auto& [read, write] : shard_measurements) {
          read_measurements.push_back(std::move(read));
          write_measurements.push_back(std::move(write));
        }

        dump_measurements("client_report_read.csv", read_measurements);
        dump_measurements("client_report_write.csv", write_measurements);
        return seastar::make_ready_future<>();
      });
}

int main(int argc, char** argv)
{
  seastar::app_template app;
  app.add_options()("other_ip", boost::program_options::value<std::string>()->default_value("127.0.0.1"), "IP address of the server to connect to")(
      "connections", boost::program_options::value<uint16_t>()->default_value(1), "Number of concurrent connections per core")(
      "data_size", boost::program_options::value<uint32_t>()->default_value(DEFAULT_DATA_SIZE), "Size of the data to send in bytes")(
      "my_offset", boost::program_options::value<uint16_t>()->default_value(1024), "Client offset for the server shard to connect to")(
      "other_offset", boost::program_options::value<uint16_t>()->default_value(2048), "Client offset for the server shard to connect to");

  return app.run(argc, argv, [&app] {
    CONCURRENT_CONNECTIONS = app.configuration()["connections"].as<uint16_t>();
    MY_PORT_OFFSET = app.configuration()["my_offset"].as<uint16_t>();
    OTHER_PORT_OFFSET = app.configuration()["other_offset"].as<uint16_t>();
    DATA_SIZE = app.configuration()["data_size"].as<uint32_t>();
    auto service = std::make_shared<seastar::sharded<throughput_service>>();

    seastar::engine().at_exit([service] {
      return service->invoke_on_all(&throughput_service::stop);
    });

    return service->start()
        .then([service] {
          return service->invoke_on_all(&throughput_service::setup_reporter);
        })
        .then([service, &app] {
          // Retrieve and use the server_ip option
          auto other_ip = app.configuration()["other_ip"].as<std::string>();
          return service->invoke_on_all(&throughput_service::run_udp, other_ip);
        })
        // We need to write the benchmark report before calling "stop" on the service.
        .then([service] {
          std::cout << "Writing benchmark report\n";
          return write_throughput_report(*service);
        })
        .then([service] {
          return service->stop();
        })
        .then([] {
          std::cout << "Service stopped on all cores\n";
          return seastar::make_ready_future<>();
        });
  });
}
