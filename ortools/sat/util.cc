// Copyright 2010-2021 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ortools/sat/util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "ortools/base/integral_types.h"
#include "ortools/base/logging.h"
#if !defined(__PORTABLE_PLATFORM__)
#include "google/protobuf/descriptor.h"
#endif  // __PORTABLE_PLATFORM__
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/numeric/int128.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/distributions.h"
#include "absl/types/span.h"
#include "ortools/base/mathutil.h"
#include "ortools/base/stl_util.h"
#include "ortools/sat/sat_base.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/saturated_arithmetic.h"
#include "ortools/util/strong_integers.h"

namespace operations_research {
namespace sat {

namespace {
// This will be optimized into one division. I tested that in other places:
//
// Note that I am not 100% sure we need the indirection for the optimization
// to kick in though, but this seemed safer given our weird r[i ^ 1] inputs.
void QuotientAndRemainder(int64_t a, int64_t b, int64_t& q, int64_t& r) {
  q = a / b;
  r = a % b;
}
}  // namespace

void RandomizeDecisionHeuristic(absl::BitGenRef random,
                                SatParameters* parameters) {
#if !defined(__PORTABLE_PLATFORM__)
  // Random preferred variable order.
  const google::protobuf::EnumDescriptor* order_d =
      SatParameters::VariableOrder_descriptor();
  parameters->set_preferred_variable_order(
      static_cast<SatParameters::VariableOrder>(
          order_d->value(absl::Uniform(random, 0, order_d->value_count()))
              ->number()));

  // Random polarity initial value.
  const google::protobuf::EnumDescriptor* polarity_d =
      SatParameters::Polarity_descriptor();
  parameters->set_initial_polarity(static_cast<SatParameters::Polarity>(
      polarity_d->value(absl::Uniform(random, 0, polarity_d->value_count()))
          ->number()));
#endif  // __PORTABLE_PLATFORM__
  // Other random parameters.
  parameters->set_use_phase_saving(absl::Bernoulli(random, 0.5));
  parameters->set_random_polarity_ratio(absl::Bernoulli(random, 0.5) ? 0.01
                                                                     : 0.0);
  parameters->set_random_branches_ratio(absl::Bernoulli(random, 0.5) ? 0.01
                                                                     : 0.0);
}

// Using the extended Euclidian algo, we find a and b such that a x + b m = gcd.
// https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm
int64_t ModularInverse(int64_t x, int64_t m) {
  DCHECK_GE(x, 0);
  DCHECK_LT(x, m);

  int64_t r[2] = {m, x};
  int64_t t[2] = {0, 1};
  int64_t q;

  // We only keep the last two terms of the sequences with the "^1" trick:
  //
  // q = r[i-2] / r[i-1]
  // r[i] = r[i-2] % r[i-1]
  // t[i] = t[i-2] - t[i-1] * q
  //
  // We always have:
  // - gcd(r[i], r[i - 1]) = gcd(r[i - 1], r[i - 2])
  // - x * t[i] + m * t[i - 1] = r[i]
  int i = 0;
  for (; r[i ^ 1] != 0; i ^= 1) {
    QuotientAndRemainder(r[i], r[i ^ 1], q, r[i]);
    t[i] -= t[i ^ 1] * q;
  }

  // If the gcd is not one, there is no inverse, we returns 0.
  if (r[i] != 1) return 0;

  // Correct the result so that it is in [0, m). Note that abs(t[i]) is known to
  // be less than or equal to x / 2, and we have thorough unit-tests.
  if (t[i] < 0) t[i] += m;

  return t[i];
}

int64_t PositiveMod(int64_t x, int64_t m) {
  const int64_t r = x % m;
  return r < 0 ? r + m : r;
}

int64_t ProductWithModularInverse(int64_t coeff, int64_t mod, int64_t rhs) {
  DCHECK_NE(coeff, 0);
  DCHECK_NE(mod, 0);

  mod = std::abs(mod);
  if (rhs == 0 || mod == 1) return 0;
  DCHECK_EQ(std::gcd(std::abs(coeff), mod), 1);

  // Make both in [0, mod).
  coeff = PositiveMod(coeff, mod);
  rhs = PositiveMod(rhs, mod);

  // From X * coeff % mod = rhs
  // We deduce that X % mod = rhs * inverse % mod
  const int64_t inverse = ModularInverse(coeff, mod);
  CHECK_NE(inverse, 0);

  // We make the operation in 128 bits to be sure not to have any overflow here.
  const absl::int128 p = absl::int128{inverse} * absl::int128{rhs};
  return static_cast<int64_t>(p % absl::int128{mod});
}

bool SolveDiophantineEquationOfSizeTwo(int64_t& a, int64_t& b, int64_t& cte,
                                       int64_t& x0, int64_t& y0) {
  CHECK_NE(a, 0);
  CHECK_NE(b, 0);
  CHECK_NE(a, std::numeric_limits<int64_t>::min());
  CHECK_NE(b, std::numeric_limits<int64_t>::min());

  const int64_t gcd = std::gcd(std::abs(a), std::abs(b));
  if (cte % gcd != 0) return false;
  a /= gcd;
  b /= gcd;
  cte /= gcd;

  // The simple case where (0, 0) is a solution.
  if (cte == 0) {
    x0 = y0 = 0;
    return true;
  }

  // We solve a * X + b * Y = cte
  // We take a valid x0 in [0, b) by considering the equation mod b.
  x0 = ProductWithModularInverse(a, b, cte);

  // We choose x0 of the same sign as cte.
  if (cte < 0 && x0 != 0) x0 -= std::abs(b);

  // By plugging X = x0 + b * Z
  // We have a * (x0 + b * Z) + b * Y = cte
  // so a * b * Z + b * Y = cte - a * x0;
  // and y0 = (cte - a * x0) / b (with an exact division by construction).
  const absl::int128 t = absl::int128{cte} - absl::int128{a} * absl::int128{x0};
  DCHECK_EQ(t % absl::int128{b}, absl::int128{0});

  // Overflow-wise, there is two cases for cte > 0:
  // - a * x0 <= cte, in this case y0 will not overflow (<= cte).
  // - a * x0 > cte, in this case y0 will be in (-a, 0].
  const absl::int128 r = t / absl::int128{b};
  DCHECK_LE(r, absl::int128{std::numeric_limits<int64_t>::max()});
  DCHECK_GE(r, absl::int128{std::numeric_limits<int64_t>::min()});

  y0 = static_cast<int64_t>(r);
  return true;
}

// TODO(user): Find better implementation? In pratice passing via double is
// almost always correct, but the CapProd() might be a bit slow. However this
// is only called when we do propagate something.
int64_t FloorSquareRoot(int64_t a) {
  int64_t result =
      static_cast<int64_t>(std::floor(std::sqrt(static_cast<double>(a))));
  while (CapProd(result, result) > a) --result;
  while (CapProd(result + 1, result + 1) <= a) ++result;
  return result;
}

// TODO(user): Find better implementation?
int64_t CeilSquareRoot(int64_t a) {
  int64_t result =
      static_cast<int64_t>(std::ceil(std::sqrt(static_cast<double>(a))));
  while (CapProd(result, result) < a) ++result;
  while ((result - 1) * (result - 1) >= a) --result;
  return result;
}

int64_t ClosestMultiple(int64_t value, int64_t base) {
  if (value < 0) return -ClosestMultiple(-value, base);
  int64_t result = value / base * base;
  if (value - result > base / 2) result += base;
  return result;
}

bool LinearInequalityCanBeReducedWithClosestMultiple(
    int64_t base, const std::vector<int64_t>& coeffs,
    const std::vector<int64_t>& lbs, const std::vector<int64_t>& ubs,
    int64_t rhs, int64_t* new_rhs) {
  // Precompute some bounds for the equation base * X + error <= rhs.
  int64_t max_activity = 0;
  int64_t max_x = 0;
  int64_t min_error = 0;
  const int num_terms = coeffs.size();
  if (num_terms == 0) return false;
  for (int i = 0; i < num_terms; ++i) {
    const int64_t coeff = coeffs[i];
    CHECK_GT(coeff, 0);
    const int64_t closest = ClosestMultiple(coeff, base);
    max_activity += coeff * ubs[i];
    max_x += closest / base * ubs[i];

    const int64_t error = coeff - closest;
    if (error >= 0) {
      min_error += error * lbs[i];
    } else {
      min_error += error * ubs[i];
    }
  }

  if (max_activity <= rhs) {
    // The constraint is trivially true.
    *new_rhs = max_x;
    return true;
  }

  // This is the max error assuming that activity > rhs.
  int64_t max_error_if_invalid = 0;
  const int64_t slack = max_activity - rhs - 1;
  for (int i = 0; i < num_terms; ++i) {
    const int64_t coeff = coeffs[i];
    const int64_t closest = ClosestMultiple(coeff, base);
    const int64_t error = coeff - closest;
    if (error >= 0) {
      max_error_if_invalid += error * ubs[i];
    } else {
      const int64_t lb = std::max(lbs[i], ubs[i] - slack / coeff);
      max_error_if_invalid += error * lb;
    }
  }

  // We have old solution valid =>
  //     base * X + error <= rhs
  //     base * X <= rhs - error
  //     base * X <= rhs - min_error
  //     X <= new_rhs
  *new_rhs = std::min(max_x, MathUtil::FloorOfRatio(rhs - min_error, base));

  // And we have old solution invalid =>
  //     base * X + error >= rhs + 1
  //     base * X >= rhs + 1 - max_error_if_invalid
  //     X >= infeasibility_bound
  const int64_t infeasibility_bound =
      MathUtil::CeilOfRatio(rhs + 1 - max_error_if_invalid, base);

  // If the two bounds can be separated, we have an equivalence !
  return *new_rhs < infeasibility_bound;
}

int MoveOneUnprocessedLiteralLast(
    const absl::btree_set<LiteralIndex>& processed, int relevant_prefix_size,
    std::vector<Literal>* literals) {
  if (literals->empty()) return -1;
  if (!processed.contains(literals->back().Index())) {
    return std::min<int>(relevant_prefix_size, literals->size());
  }

  // To get O(n log n) size of suffixes, we will first process the last n/2
  // literals, we then move all of them first and process the n/2 literals left.
  // We use the same algorithm recursively. The sum of the suffixes' size S(n)
  // is thus S(n/2) + n + S(n/2). That gives us the correct complexity. The code
  // below simulates one step of this algorithm and is made to be "robust" when
  // from one call to the next, some literals have been removed (but the order
  // of literals is preserved).
  int num_processed = 0;
  int num_not_processed = 0;
  int target_prefix_size = literals->size() - 1;
  for (int i = literals->size() - 1; i >= 0; i--) {
    if (processed.contains((*literals)[i].Index())) {
      ++num_processed;
    } else {
      ++num_not_processed;
      target_prefix_size = i;
    }
    if (num_not_processed >= num_processed) break;
  }
  if (num_not_processed == 0) return -1;
  target_prefix_size = std::min(target_prefix_size, relevant_prefix_size);

  // Once a prefix size has been decided, it is always better to
  // enqueue the literal already processed first.
  std::stable_partition(
      literals->begin() + target_prefix_size, literals->end(),
      [&processed](Literal l) { return processed.contains(l.Index()); });
  return target_prefix_size;
}

void IncrementalAverage::Reset(double reset_value) {
  num_records_ = 0;
  average_ = reset_value;
}

void IncrementalAverage::AddData(double new_record) {
  num_records_++;
  average_ += (new_record - average_) / num_records_;
}

void ExponentialMovingAverage::AddData(double new_record) {
  num_records_++;
  average_ = (num_records_ == 1)
                 ? new_record
                 : (new_record + decaying_factor_ * (average_ - new_record));
}

void Percentile::AddRecord(double record) {
  records_.push_front(record);
  if (records_.size() > record_limit_) {
    records_.pop_back();
  }
}

double Percentile::GetPercentile(double percent) {
  CHECK_GT(records_.size(), 0);
  CHECK_LE(percent, 100.0);
  CHECK_GE(percent, 0.0);
  std::vector<double> sorted_records(records_.begin(), records_.end());
  std::sort(sorted_records.begin(), sorted_records.end());
  const int num_records = sorted_records.size();

  const double percentile_rank =
      static_cast<double>(num_records) * percent / 100.0 - 0.5;
  if (percentile_rank <= 0) {
    return sorted_records.front();
  } else if (percentile_rank >= num_records - 1) {
    return sorted_records.back();
  }
  // Interpolate.
  DCHECK_GE(num_records, 2);
  DCHECK_LT(percentile_rank, num_records - 1);
  const int lower_rank = static_cast<int>(std::floor(percentile_rank));
  DCHECK_LT(lower_rank, num_records - 1);
  return sorted_records[lower_rank] +
         (percentile_rank - lower_rank) *
             (sorted_records[lower_rank + 1] - sorted_records[lower_rank]);
}

void CompressTuples(absl::Span<const int64_t> domain_sizes, int64_t any_value,
                    std::vector<std::vector<int64_t>>* tuples) {
  if (tuples->empty()) return;

  // Remove duplicates if any.
  gtl::STLSortAndRemoveDuplicates(tuples);

  const int num_vars = (*tuples)[0].size();

  std::vector<int> to_remove;
  std::vector<int64_t> tuple_minus_var_i(num_vars - 1);
  for (int i = 0; i < num_vars; ++i) {
    const int domain_size = domain_sizes[i];
    if (domain_size == 1) continue;
    absl::flat_hash_map<const std::vector<int64_t>, std::vector<int>>
        masked_tuples_to_indices;
    for (int t = 0; t < tuples->size(); ++t) {
      int out = 0;
      for (int j = 0; j < num_vars; ++j) {
        if (i == j) continue;
        tuple_minus_var_i[out++] = (*tuples)[t][j];
      }
      masked_tuples_to_indices[tuple_minus_var_i].push_back(t);
    }
    to_remove.clear();
    for (const auto& it : masked_tuples_to_indices) {
      if (it.second.size() != domain_size) continue;
      (*tuples)[it.second.front()][i] = any_value;
      to_remove.insert(to_remove.end(), it.second.begin() + 1, it.second.end());
    }
    std::sort(to_remove.begin(), to_remove.end(), std::greater<int>());
    for (const int t : to_remove) {
      (*tuples)[t] = tuples->back();
      tuples->pop_back();
    }
  }
}

void MaxBoundedSubsetSum::Reset(int64_t bound) {
  DCHECK_GE(bound, 0);
  sums_ = {0};
  current_max_ = 0;
  bound_ = bound;
}

void MaxBoundedSubsetSum::Add(int64_t value) {
  DCHECK_GE(value, 0);

  // The max is already reachable or we aborted.
  if (current_max_ == bound_) return;
  if (value > bound_) return;  // Can be ignored.

  // Mode 1: vector of all possible sums (with duplicates).
  if (!sums_.empty() && sums_.size() <= kMaxComplexityPerAdd) {
    const int old_size = sums_.size();
    for (int i = 0; i < old_size; ++i) {
      const int64_t s = sums_[i] + value;
      if (s <= bound_) {
        sums_.push_back(s);
        current_max_ = std::max(current_max_, s);
      }
    }
    return;
  }

  // Mode 2: bitset of all possible sums.
  if (bound_ <= kMaxComplexityPerAdd) {
    if (!sums_.empty()) {
      expanded_sums_.assign(bound_ + 1, false);
      for (const int64_t s : sums_) {
        expanded_sums_[s] = true;
      }
      sums_.clear();
    }

    // The reverse order is important to not add the current value twice.
    for (int i = bound_ - value; i >= 0; --i) {
      if (expanded_sums_[i]) {
        expanded_sums_[i + value] = true;
        current_max_ = std::max(current_max_, i + value);
      }
    }
    return;
  }

  // Abort.
  current_max_ = bound_;
}

}  // namespace sat
}  // namespace operations_research
