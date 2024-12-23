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
#include <seastar/net/tcp.hh>

#include "common.hpp"

uint16_t SERVER_OFFSET;

/// A sharded service across cores.
class throughput_service
{
public:
  seastar::future<> run_tcp()
  {
    return seastar::with_gate(gate_, [this]() {
      // Building a TCP server on seastar is different from a UDP server.
      // It seems like every shard needs to listen on the same port, and connection
      // requests are load balanced across shards.
      // https://github.com/scylladb/seastar/issues/2183
      const uint16_t port = 1300 + SERVER_OFFSET;
      std::cout << "Starting TCP server on shard " << seastar::this_shard_id() << " - listening port: " << port << std::endl;
      return seastar::do_with(seastar::listen(seastar::make_ipv4_address({port})), [this](auto& listener) {
        std::cout << "TCP server on shard " << seastar::this_shard_id() << " now listening" << std::endl;
        listener_ = &listener;
        return seastar::repeat([this, &listener]() {
          return listener.accept().then_wrapped([this](auto&& f) {
            if (interrupted_) {
              return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
            }
            auto res = f.get();
            std::cout << "Accepted connection from " << res.remote_address << "\n";

            return seastar::with_gate(
                       gate_,
                       [this, conn = std::move(res.connection), addr = res.remote_address]() mutable {
                         return handle_connection(std::move(conn), addr);
                       })
                .then([] {
                  return seastar::stop_iteration::no;
                });
          });
        });
      });
    });
  }

  seastar::future<> stop()
  {
    std::cout << "Stopping seastar service on " << seastar::this_shard_id() << "\n";
    interrupted_ = true;
    if (listener_) {
      listener_->abort_accept();
    }
    return gate_.close();
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
  seastar::future<> handle_connection(seastar::connected_socket conn, seastar::socket_address addr)
  {
    auto input = conn.input();
    return seastar::do_with(std::move(input), [this, &conn](auto& in) {
      return seastar::repeat([this, &conn, &in] {
        if (interrupted_) {
          return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
        }
        return in.read().then([this](seastar::temporary_buffer<char> buf) {
          if (buf.empty()) {
            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
          }
          measurements.add_bytes(buf.size());
          measurements.add_packets(1);
          return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
        });
      });
    });
  }

  // Interrupt gate to know when we need to stop working.
  bool interrupted_ = false;

  // Core-local timer to report throughput.
  seastar::timer<> timer;
  // The throughput measurements for this shard.
  MeasurementDevice measurements;
  // The listener for this shard, used to stop the server from the outside.
  seastar::server_socket* listener_ = nullptr;
  // The gate to track background operations. Ensures that all background operations are completed before stopping the
  // server.
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
        dump_measurements("server_report_" + std::to_string(SERVER_OFFSET) + ".csv", shard_measurements);
        return seastar::make_ready_future<>();
      });
}

int main(int argc, char** argv)
{
  seastar::app_template app;
  app.add_options()("server_offset", boost::program_options::value<uint16_t>()->default_value(0), "Server offset for the server shard to listen on");
  if (seastar::smp::count != 0) {
    throw std::runtime_error("The TCP server must be run on a single core.");
  }

  return app.run(argc, argv, [&app] {
    SERVER_OFFSET = app.configuration()["server_offset"].as<uint16_t>();
    auto service = std::make_shared<seastar::sharded<throughput_service>>();

    seastar::engine().at_exit([service] {
      return service->invoke_on_all(&throughput_service::stop);
    });

    return service->start()
        .then([service] {
          return service->invoke_on_all(&throughput_service::setup_reporter);
        })
        .then([service] {
          return service->invoke_on_all(&throughput_service::run_tcp);
        })
        // We need to write the benchmark report before calling "stop" on the service.
        .then([service] {
          std::cout << "Writing benchmark report\n";
          return write_throughput_report(*service);
        })
        .then([service] {
          return service->stop();
        })
        .then([service] {
          std::cout << "Service stopped on all cores\n";
          return seastar::make_ready_future<>();
        });
  });
}
