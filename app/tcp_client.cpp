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

constexpr uint16_t PACKET_SIZE = 8000;
/// The number of concurrent TCP connections per shard.
constexpr uint16_t DEFAULT_CONCURRENT_CONNECTIONS = 1;
uint16_t CONCURRENT_CONNECTIONS;

/// A sharded client across cores.
class throughput_service
{
public:
  seastar::future<> run_tcp(const std::string& server_ip)
  {
    std::cout << "Shard " << seastar::this_shard_id() << " writing to TCP port " << TCP_SERVER_PORT << std::endl;

    // Create multiple concurrent connections
    for (uint16_t i = 0; i < CONCURRENT_CONNECTIONS; ++i) {
      std::cout << "Starting task " << i << " for shard " << seastar::this_shard_id() << std::endl;
      (void)seastar::with_gate(gate_, [this, i, &server_ip]() {
        return seastar::connect(seastar::make_ipv4_address({server_ip, TCP_SERVER_PORT}))
            .then([this, i](seastar::connected_socket socket) {
              auto packet = std::vector<char>(PACKET_SIZE, 'A');
              auto output = socket.output();

              return seastar::do_with(std::move(output), std::move(packet), [this, i](seastar::output_stream<char>& output, auto& packet) {
                return seastar::repeat([this, &output, &packet, i]() {
                  if (interrupted_) {
                    return output.close().then([] {
                      return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
                    });
                  }

                  return output
                      .write(packet.data(), packet.size())
                      // .then([&output] {
                      //   return output.flush();
                      // })
                      // .then([&output] {
                      //   return seastar::sleep(std::chrono::milliseconds(10));
                      // })
                      .then([this] {
                        measurements.add_bytes(PACKET_SIZE);
                        measurements.add_packets(1);
                        return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
                      });
                });
              });
            })
            .handle_exception([i](std::exception_ptr ep) {
              try {
                std::rethrow_exception(ep);
              } catch (const std::exception& e) {
                std::cerr << "Connection " << i << " failed: " << e.what() << std::endl;
              }
              return seastar::make_ready_future<>();
            });
      });
    }

    return gate_.close();
  }

  seastar::future<> stop()
  {
    std::cout << "Stopping seastar service on " << seastar::this_shard_id() << "\n";
    interrupted_ = true;
    return gate_.close();
  }

  seastar::future<> setup_reporter()
  {
    // Interleave shards by 10 milliseconds to ensure that the report printing in cout doesn't
    // interleave.
    return seastar::sleep(std::chrono::milliseconds(10* seastar::this_shard_id())).then([this]() {
      timer.set_callback([this]() {
        measurements.tick();
      });
      timer.arm_periodic(std::chrono::seconds(1));
      return seastar::make_ready_future<>();
    });
  }

  std::vector<Measurement> get_measurements() const
  {
    return measurements.get_history();
  };

private:
  // Interrupt gate to know when we need to stop working.
  bool interrupted_ = false;
  // Core-local timer to report throughput.
  seastar::timer<> timer;
  // The throughput measurements for this shard.
  MeasurementDevice measurements;
  seastar::gate gate_;
};

// Write the benchmark report.
seastar::future<> write_throughput_report(seastar::sharded<throughput_service>& service)
{
  return service
      // First, retrieve the measurements from every shard.
      .map([](throughput_service& service) {
        return service.get_measurements();
      })
      // Then, take the measurements and dump them to a file.
      .then([](auto shard_measurements) {
        dump_measurements("client_report.csv", shard_measurements);
        return seastar::make_ready_future<>();
      });
}

int main(int argc, char** argv)
{
  seastar::app_template app;
  app.add_options()("server_ip", boost::program_options::value<std::string>()->default_value("127.0.0.1"), "IP address of the server to connect to")(
      "connections",
      boost::program_options::value<uint16_t>()->default_value(DEFAULT_CONCURRENT_CONNECTIONS),
      "Number of concurrent connections per core")(
      "client_offset", boost::program_options::value<uint16_t>()->default_value(0), "Client offset for the server shard to connect to");

  return app.run(argc, argv, [&app] {
    CONCURRENT_CONNECTIONS = app.configuration()["connections"].as<uint16_t>();
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
          auto server_ip = app.configuration()["server_ip"].as<std::string>();
          return service->invoke_on_all(&throughput_service::run_tcp, server_ip);
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
