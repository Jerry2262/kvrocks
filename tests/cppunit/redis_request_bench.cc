#include <event2/buffer.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "server/redis_request.h"
#include "server/server.h"
#include "storage/storage.h"

namespace {

std::string RespArray(const std::vector<std::string_view> &args) {
  std::string out;
  out.reserve(64);
  out += '*';
  out += std::to_string(args.size());
  out += "\r\n";
  for (const auto arg : args) {
    out += '$';
    out += std::to_string(arg.size());
    out += "\r\n";
    out.append(arg.data(), arg.size());
    out += "\r\n";
  }
  return out;
}

std::string RepeatPayload(const std::string &payload, int count) {
  std::string out;
  out.reserve(payload.size() * count);
  for (int i = 0; i < count; ++i) {
    out += payload;
  }
  return out;
}

std::vector<std::string_view> HmgetAllFieldsArgs(std::string_view key) {
  return {"HMGET", key, "field0", "field1", "field2", "field3", "field4", "field5", "field6", "field7", "field8",
          "field9"};
}

class TokenizeBenchFixture {
 public:
  TokenizeBenchFixture() : storage_(&config_), server_(&storage_, &config_), request_(&server_) {}

  void RunTokenizeBench(std::string_view name, const std::string &payload, int commands_per_payload, int loops) {
    evbuffer *input = evbuffer_new();
    ASSERT_NE(input, nullptr);

    constexpr int kWarmupCommands = 100000;
    int warmup_loops = std::max(1, kWarmupCommands / commands_per_payload);
    for (int i = 0; i < warmup_loops; ++i) {
      evbuffer_add(input, payload.data(), payload.size());
      auto s = request_.Tokenize(input);
      ASSERT_TRUE(s.IsOK()) << s.Msg();
      consumed_commands_ += request_.GetCommands()->size();
      request_.GetCommands()->clear();
    }

    auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < loops; ++i) {
      evbuffer_add(input, payload.data(), payload.size());
      auto s = request_.Tokenize(input);
      if (!s.IsOK()) {
        std::cerr << "Tokenize failed: " << s.Msg() << std::endl;
        std::abort();
      }
      consumed_commands_ += request_.GetCommands()->size();
      request_.GetCommands()->clear();
    }
    auto end = std::chrono::steady_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    auto total_commands = static_cast<int64_t>(loops) * commands_per_payload;
    PrintResult(name, static_cast<double>(ns) / static_cast<double>(total_commands), total_commands, payload.size());

    evbuffer_free(input);
  }

  void RunEvbufferBaseline(std::string_view name, const std::string &payload, int commands_per_payload, int loops) {
    evbuffer *input = evbuffer_new();
    ASSERT_NE(input, nullptr);

    auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < loops; ++i) {
      evbuffer_add(input, payload.data(), payload.size());
      evbuffer_drain(input, payload.size());
    }
    auto end = std::chrono::steady_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    auto total_commands = static_cast<int64_t>(loops) * commands_per_payload;
    PrintResult(name, static_cast<double>(ns) / static_cast<double>(total_commands), total_commands, payload.size());

    evbuffer_free(input);
  }

  uint64_t ConsumedCommands() const { return consumed_commands_; }

 private:
  static void PrintResult(std::string_view name, double ns_per_command, int64_t commands, size_t payload_bytes) {
    std::cout << std::left << std::setw(34) << name << " " << std::right << std::fixed << std::setprecision(2)
              << ns_per_command << " ns/op"
              << "  commands=" << commands << " payload_bytes=" << payload_bytes << std::endl;
  }

  Config config_;
  Engine::Storage storage_;
  Server server_;
  Redis::Request request_;
  uint64_t consumed_commands_ = 0;
};

}  // namespace

TEST(RedisRequestBench, DISABLED_TokenizeYCSBWorkloadC) {
  // YCSB Redis binding maps records to Redis hashes. With workloadc's default
  // readallfields=true and readallfieldsbyname=false, READ is HGETALL key.
  constexpr std::string_view kYcsbKey = "user6284781860667377211";

  const auto hgetall = RespArray({"HGETALL", kYcsbKey});
  const auto hgetall_pipeline16 = RepeatPayload(hgetall, 16);
  const auto hgetall_pipeline64 = RepeatPayload(hgetall, 64);
  const auto hmget_one_field = RespArray({"HMGET", kYcsbKey, "field0"});
  const auto hmget_all_fields = RespArray(HmgetAllFieldsArgs(kYcsbKey));

  TokenizeBenchFixture bench;

  std::cout << "\nRedis::Request::Tokenize microbenchmarks" << std::endl;
  std::cout << "YCSB workloadc default Redis READ is HGETALL key." << std::endl;

  bench.RunEvbufferBaseline("baseline add+drain HGETALL", hgetall, 1, 1000000);
  bench.RunTokenizeBench("Tokenize HGETALL x1", hgetall, 1, 1000000);
  bench.RunTokenizeBench("Tokenize HGETALL pipeline x16", hgetall_pipeline16, 16, 200000);
  bench.RunTokenizeBench("Tokenize HGETALL pipeline x64", hgetall_pipeline64, 64, 50000);

  // These two are not workloadc defaults, but are useful for comparing against
  // readallfields=false and readallfieldsbyname=true runs.
  bench.RunTokenizeBench("Tokenize HMGET one field", hmget_one_field, 1, 1000000);
  bench.RunTokenizeBench("Tokenize HMGET all 10 fields", hmget_all_fields, 1, 1000000);

  ASSERT_GT(bench.ConsumedCommands(), 0);
}
