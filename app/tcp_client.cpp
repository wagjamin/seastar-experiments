#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/smp.hh>
#include <seastar/net/api.hh>
#include <seastar/net/socket_defs.hh>
#include <seastar/util/log.hh>

#include "common.hpp"

constexpr uint16_t PACKET_SIZE = 8000;

/// A sharded client across cores.
class throughput_service
{
public:
  seastar::future<> run_tcp(const std::string& server_ip)
  {
    const uint16_t target_port = 1300 + seastar::this_shard_id();
    std::cout << "Shard " << seastar::this_shard_id() << " writing to TCP port " << target_port << std::endl;

    // Create and connect TCP socket
    return seastar::connect(seastar::make_ipv4_address({server_ip, target_port})).then([this](seastar::connected_socket socket) {
      auto packet = std::vector<char>(PACKET_SIZE, 'A');
      auto output = socket.output();

      return seastar::do_with(std::move(output), std::move(packet), [this](seastar::output_stream<char>& output, auto& packet) {
        return seastar::repeat([this, &output, &packet]() {
          if (interrupted_) {
            return output.close().then([] {
              return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
            });
          }

          return output.write(packet.data(), packet.size())
              .then([&output] {
                return output.flush();
              })
              .then([this] {
                measurements.add_bytes(PACKET_SIZE);
                measurements.add_packets(1);
                return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
              });
        });
      });
    });
  }

  seastar::future<> stop()
  {
    std::cout << "Stopping seastar service on " << seastar::this_shard_id() << "\n";
    // Mark the state as interrupted.
    interrupted_ = true;
    return seastar::make_ready_future<>();
  }

  seastar::future<> setup_reporter()
  {
    timer.set_callback([this]() {
      measurements.tick();
    });
    timer.arm_periodic(std::chrono::seconds(1));
    return seastar::make_ready_future<>();
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
  app.add_options()("server_ip", boost::program_options::value<std::string>()->default_value("127.0.0.1"), "IP address of the server to connect to");

  return app.run(argc, argv, [&app] {
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
          auto& config = app.configuration();
          auto server_ip = config["server_ip"].as<std::string>();
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
