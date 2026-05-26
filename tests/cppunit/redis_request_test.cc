/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "server/redis_request.h"

#include <event2/buffer.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "server/server.h"
#include "storage/storage.h"

namespace {

std::string RespArray(const std::vector<std::string_view> &args) {
  std::string out;
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

class RedisRequestTest : public testing::Test {
 protected:
  RedisRequestTest() : storage_(&config_), server_(&storage_, &config_), request_(&server_) {
    input_ = evbuffer_new();
    EXPECT_NE(input_, nullptr);
  }

  ~RedisRequestTest() override { evbuffer_free(input_); }

  Status Tokenize(std::string_view payload) {
    evbuffer_add(input_, payload.data(), payload.size());
    return request_.Tokenize(input_);
  }

  std::deque<Redis::CommandTokens> *Commands() { return request_.GetCommands(); }

 private:
  Config config_;
  Engine::Storage storage_;
  Server server_;

 protected:
  Redis::Request request_;
  evbuffer *input_ = nullptr;
};

}  // namespace

TEST_F(RedisRequestTest, TokenizesRespPipeline) {
  auto payload = RespArray({"HGETALL", "user6284781860667377211"});
  payload += RespArray({"HMGET", "user6284781860667377211", "field0", "field1"});

  auto s = Tokenize(payload);
  ASSERT_TRUE(s.IsOK()) << s.Msg();
  ASSERT_EQ(Commands()->size(), 2);

  ASSERT_EQ((*Commands())[0], Redis::CommandTokens({"HGETALL", "user6284781860667377211"}));
  ASSERT_EQ((*Commands())[1], Redis::CommandTokens({"HMGET", "user6284781860667377211", "field0", "field1"}));
}

TEST_F(RedisRequestTest, KeepsPartialRespCommandBuffered) {
  const auto payload = RespArray({"SET", "ycsb-key", "ycsb-value"});
  auto s = Tokenize(std::string_view(payload).substr(0, payload.size() - 4));
  ASSERT_TRUE(s.IsOK()) << s.Msg();
  ASSERT_TRUE(Commands()->empty());

  s = Tokenize(std::string_view(payload).substr(payload.size() - 4));
  ASSERT_TRUE(s.IsOK()) << s.Msg();
  ASSERT_EQ(Commands()->size(), 1);
  ASSERT_EQ((*Commands())[0], Redis::CommandTokens({"SET", "ycsb-key", "ycsb-value"}));
}

TEST_F(RedisRequestTest, TokenizesInlineCommand) {
  auto s = Tokenize("set ycsb-key ycsb-value\r\n");
  ASSERT_TRUE(s.IsOK()) << s.Msg();
  ASSERT_EQ(Commands()->size(), 1);
  ASSERT_EQ((*Commands())[0], Redis::CommandTokens({"set", "ycsb-key", "ycsb-value"}));
}

TEST_F(RedisRequestTest, TokenizesInlineCommandWithRepeatedWhitespace) {
  auto s = Tokenize("  set\tycsb-key   ycsb-value  \r\n");
  ASSERT_TRUE(s.IsOK()) << s.Msg();
  ASSERT_EQ(Commands()->size(), 1);
  ASSERT_EQ((*Commands())[0], Redis::CommandTokens({"set", "ycsb-key", "ycsb-value"}));
}

TEST_F(RedisRequestTest, RejectsInvalidRespLength) {
  auto s = Tokenize("*2\r\n$3x\r\nGET\r\n$3\r\nkey\r\n");
  ASSERT_FALSE(s.IsOK());
  ASSERT_EQ(s.Msg(), "Protocol error: invalid bulk length");
}
