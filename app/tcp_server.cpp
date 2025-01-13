#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/timer.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tcp.hh>

#include "common.hpp"

constexpr uint32_t DEFAULT_DATA_SIZE = 64;
uint32_t DATA_SIZE;

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
      std::cout << "Starting TCP server on shard " << seastar::this_shard_id() << " - listening port: " << TCP_SERVER_PORT << std::endl;
      seastar::listen_options opts;
      opts.reuse_address = true;
      // We need to use port-based load balancing to ensure that the connections are distributed evenly across shards.
      // The default load balancing policy causes all connections to be accepted by a single shard.
      opts.lba = seastar::server_socket::load_balancing_algorithm::port;
      return seastar::do_with(seastar::listen(seastar::make_ipv4_address({TCP_SERVER_PORT}), opts), [this](auto& listener) {
        std::cout << "TCP server on shard " << seastar::this_shard_id() << " now listening" << std::endl;
        listener_ = &listener;
        return seastar::repeat([this, &listener]() {
          return listener.accept().then_wrapped([this](auto&& f) {
            if (interrupted_) {
              return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
            }
            auto res = f.get();
            std::cout << "Accepted connection from " << res.remote_address << "\n";

            (void)seastar::with_gate(gate_, [this, conn = std::move(res.connection)]() mutable {
              return handle_connection(std::move(conn));
            });

            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
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
  seastar::future<> handle_connection(seastar::connected_socket socket)
  {
    auto packet = std::vector<char>(DATA_SIZE, 'A');
    auto output = socket.output();

    auto write_future = seastar::do_with(std::move(output), std::move(packet), [this](seastar::output_stream<char>& output, auto& packet) {
      return seastar::repeat([this, &output, &packet]() {
        if (interrupted_) {
          return output.close().then([] {
            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
          });
        }

        return output.write(packet.data(), packet.size()).then([this] {
          write_measurements.add_bytes(DATA_SIZE);
          write_measurements.add_packets(1);
          return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
        });
      });
    });

    auto input = socket.input();

    auto read_future = seastar::do_with(std::move(input), [this](seastar::input_stream<char>& input) {
      return seastar::repeat([this, &input]() {
        if (interrupted_) {
          return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
        }
        return input.read_exactly(DATA_SIZE).then([this](seastar::temporary_buffer<char> buf) {
          if (buf.size() > 0) {
            read_measurements.add_bytes(buf.size());
            read_measurements.add_packets(1);
            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
          }
          return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
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

        dump_measurements("server_report_read.csv", read_measurements);
        dump_measurements("server_report_write.csv", write_measurements);
        return seastar::make_ready_future<>();
      });
}

int main(int argc, char** argv)
{
  seastar::app_template app;
  app.add_options()("data_size", boost::program_options::value<uint32_t>()->default_value(DEFAULT_DATA_SIZE), "Size of the data to send in bytes");

  return app.run(argc, argv, [&app] {
    DATA_SIZE = app.configuration()["data_size"].as<uint32_t>();
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
