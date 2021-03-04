/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Firestore/core/src/model/values.h"
#include "Firestore/core/src/model/database_id.h"
#include "Firestore/core/src/model/field_value.h"
#include "Firestore/core/src/remote/serializer.h"
#include "Firestore/core/src/util/comparison.h"
#include "Firestore/core/test/unit/testutil/equals_tester.h"
#include "Firestore/core/test/unit/testutil/testutil.h"
#include "Firestore/core/test/unit/testutil/time_testing.h"
#include "absl/base/casts.h"
#include "gtest/gtest.h"

namespace firebase {
namespace firestore {
namespace model {

using testutil::BlobValue;
using testutil::DbId;
using testutil::Key;
using testutil::time_point;
using testutil::Value;
using util::ComparisonResult;

namespace {

double ToDouble(uint64_t value) {
  return absl::bit_cast<double>(value);
}

// All permutations of the 51 other non-MSB significand bits are also NaNs.
const uint64_t kAlternateNanBits = 0x7fff000000000000ULL;

}  // namespace

static time_point kDate1 = testutil::MakeTimePoint(2016, 5, 20, 10, 20, 0);
static Timestamp kTimestamp1{1463739600, 0};

static time_point kDate2 = testutil::MakeTimePoint(2016, 10, 21, 15, 32, 0);
static Timestamp kTimestamp2{1477063920, 0};

class ValuesTest : public ::testing::Test {
 public:
  ValuesTest() : serializer(DbId()) {
  }

  template <typename T>
  google_firestore_v1_Value Wrap(T input) {
    model::FieldValue fv = Value(input);
    return serializer.EncodeFieldValue(fv);
  }

  template <typename... Args>
  google_firestore_v1_Value WrapObject(Args... key_value_pairs) {
    model::FieldValue fv = testutil::WrapObject((key_value_pairs)...);
    return serializer.EncodeFieldValue(fv);
  }

  template <typename... Args>
  google_firestore_v1_Value WrapArray(Args... values) {
    std::vector<model::FieldValue> contents{Value(values)...};
    model::FieldValue fv = model::FieldValue::FromArray(std::move(contents));
    return serializer.EncodeFieldValue(fv);
  }

  google_firestore_v1_Value WrapReference(DatabaseId database_id,
                                          DocumentKey key) {
    google_firestore_v1_Value result{};
    result.which_value_type = google_firestore_v1_Value_reference_value_tag;
    result.reference_value =
        serializer.EncodeResourceName(database_id, key.path());
    return result;
  }

  google_firestore_v1_Value WrapServerTimestamp(
      const model::FieldValue& input) {
    // TODO(mrschmidt): Replace with EncodeFieldValue encoding when available
    return WrapObject("__type__", "server_timestamp", "__local_write_time__",
                      input.server_timestamp_value().local_write_time());
  }

  void VerifyEquals(std::vector<google_firestore_v1_Value>& group) {
    for (size_t i = 0; i < group.size(); ++i) {
      for (size_t j = i; j < group.size(); ++j) {
        EXPECT_TRUE(Values::Equals(group[i], group[j]));
        EXPECT_TRUE(Values::Equals(group[j], group[i]));
      }
    }
  }

  void VerifyNotEquals(std::vector<google_firestore_v1_Value>& left,
                       std::vector<google_firestore_v1_Value>& right) {
    for (size_t i = 0; i < left.size(); ++i) {
      for (size_t j = 0; j < right.size(); ++j) {
        EXPECT_FALSE(Values::Equals(left[i], right[j]));
        EXPECT_FALSE(Values::Equals(right[j], left[i]));
      }
    }
  }

  void VerifyOrdering(std::vector<google_firestore_v1_Value>& left,
                      std::vector<google_firestore_v1_Value>& right,
                      ComparisonResult cmp) {
    for (size_t i = 0; i < left.size(); ++i) {
      for (size_t j = 0; j < right.size(); ++j) {
        EXPECT_EQ(cmp, Values::Compare(left[i], right[j]));
      }
    }
  }

 private:
  remote::Serializer serializer;
};

TEST_F(ValuesTest, Equality) {
  std::vector<std::vector<google_firestore_v1_Value>> equals_group;

  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(nullptr), Wrap(nullptr)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(false), Wrap(false)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(true), Wrap(true)});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      Wrap(std::numeric_limits<double>::quiet_NaN()),
      Wrap(ToDouble(kCanonicalNanBits)), Wrap(ToDouble(kAlternateNanBits)),
      Wrap(std::nan("1")), Wrap(std::nan("2"))});
  // -0.0 and 0.0 compare the same but are not equal.
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{Wrap(-0.0)});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{Wrap(0.0)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(1), Wrap(1LL)});
  // Doubles and Longs aren't equal (even though they compare same).
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(1.0), Wrap(1.0)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(1.1), Wrap(1.1)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(BlobValue(0, 1, 1))});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(BlobValue(0, 1))});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap("string"), Wrap("string")});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap("strin")});
  // latin small letter e + combining acute accent
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap("e\u0301b")});
  // latin small letter e with acute accent
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap("\u00e9a")});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      Wrap(Timestamp::FromTimePoint(kDate1)), Wrap(kTimestamp1)});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      Wrap(Timestamp::FromTimePoint(kDate2)), Wrap(kTimestamp2)});
  // NOTE: ServerTimestampValues can't be parsed via Wrap().
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp1)),
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp1))});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp2))});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      Wrap(GeoPoint(0, 1)), Wrap(GeoPoint(0, 1))});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(GeoPoint(1, 0))});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapReference(DbId(), Key("coll/doc1")),
      WrapReference(DbId(), Key("coll/doc1"))});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapReference(DbId(), Key("coll/doc2"))});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapReference(model::DatabaseId("project", "baz"), Key("coll/doc2"))});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapArray("foo", "bar"), WrapArray("foo", "bar")});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{WrapArray("foo", "bar", "baz")});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{WrapArray("foo")});
  equals_group.emplace_back(std::vector<google_firestore_v1_Value>{
      WrapObject("bar", 1, "foo", 2), WrapObject("foo", 2, "bar", 1)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{WrapObject("bar", 2, "foo", 1)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{WrapObject("bar", 1)});
  equals_group.emplace_back(
      std::vector<google_firestore_v1_Value>{WrapObject("foo", 1)});

  for (size_t i = 0; i < equals_group.size(); ++i) {
    for (size_t j = i; j < equals_group.size(); ++j) {
      if (i == j) {
        VerifyEquals(equals_group[i]);
      } else {
        VerifyNotEquals(equals_group[i], equals_group[j]);
      }
    }
  }
}

TEST_F(ValuesTest, Ordering) {
  std::vector<std::vector<google_firestore_v1_Value>> comparison_groups;

  // null first
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(nullptr)});

  // booleans
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(false)});
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(true)});

  // numbers
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(-1e20)});
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(LLONG_MIN)});
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(-0.1)});
  // Zeros all compare the same.
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(-0.0), Wrap(0.0), Wrap(0L)});
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(0.1)});
  // Doubles and longs Compare() the same.
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(1.0), Wrap(1L)});
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(LLONG_MAX)});
  comparison_groups.emplace_back(
      std::vector<google_firestore_v1_Value>{Wrap(1e20)});

  for (size_t i = 0; i < comparison_groups.size(); ++i) {
    for (size_t j = i; j < comparison_groups.size(); ++j) {
      if (i == j) {
        VerifyOrdering(comparison_groups[i], comparison_groups[i],
                       ComparisonResult::Same);
      } else {
        VerifyOrdering(comparison_groups[i], comparison_groups[j],
                       ComparisonResult::Ascending);
      }
    }
  }
}

TEST_F(ValuesTest, CanonicalId) {
}

}  // namespace model
}  // namespace firestore
}  // namespace firebase
