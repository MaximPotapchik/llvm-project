//===-- PerfHelperTest.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PerfHelper.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace llvm {
namespace exegesis {

namespace {

TEST(ParseRawCounterTest, RForm) {
  auto Result = pfm::parseRawCounter("r003c");
  ASSERT_TRUE(bool(Result));
  EXPECT_EQ(Result->Config, 0x003cULL);
  EXPECT_EQ(Result->Type, 4U);
}

TEST(ParseRawCounterTest, FieldForm) {
  auto Result = pfm::parseRawCounter("event=0x3c,umask=0x00");
  ASSERT_TRUE(bool(Result));
  EXPECT_EQ(Result->Config, 0x003cULL);
}

TEST(ParseRawCounterTest, InvalidHex) {
  auto Result = pfm::parseRawCounter("rZZZZ");
  EXPECT_FALSE(bool(Result));
  consumeError(Result.takeError());
}

TEST(ParseRawCounterTest, UpperCaseR) {
  auto Result = pfm::parseRawCounter("R003c");
  ASSERT_TRUE(bool(Result));
  EXPECT_EQ(Result->Config, 0x003cULL);
}

TEST(ParseRawCounterTest, MissingEventField) {
  auto Result = pfm::parseRawCounter("umask=0x00");
  EXPECT_FALSE(bool(Result));
  consumeError(Result.takeError());
}

TEST(ParseRawCounterTest, UnknownField) {
  auto Result = pfm::parseRawCounter("event=0x3c,bogus=0x00");
  EXPECT_FALSE(bool(Result));
  consumeError(Result.takeError());
}

} // namespace
} // namespace exegesis
} // namespace llvm
