#include "tuner.h"

#include "simulator.h"
#include "tiling_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t MAX_TILE_COUNT = 10'000'000;

struct Candidate {
    std::size_t strategy_index = 0;
    std::uint64_t m = 1;
    std::uint64_t n = 1;
    std::uint64_t k = 1;
};

struct EvaluatedCandidate {
    Candidate candidate;
    Metrics metrics;
    EnergyResult energy;
};

using CandidateKey = std::tuple<std::size_t, std::uint64_t, std::uint64_t, std::uint64_t>;

bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
    out = a * b;
    return true;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) return false;
    out = a + b;
    return true;
}

std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) {
    return a / b + (a % b != 0 ? 1 : 0);
}

std::uint64_t extent(std::uint64_t dimension) {
    return std::max<std::uint64_t>(1, dimension);
}

std::uint64_t clamp_axis(std::uint64_t value, std::uint64_t dimension) {
    return std::max<std::uint64_t>(1, std::min(value, extent(dimension)));
}

std::uint64_t square_base(const HardwareParams& p, std::size_t strategy_index) {
    if (p.element_bytes == 0) return 0;
    const std::uint64_t buffers = strategy_index == 3 ? 2 : 1;
    std::uint64_t denominator = 0;
    if (!checked_mul(buffers, 3, denominator) ||
        !checked_mul(denominator, p.element_bytes, denominator) || denominator == 0) {
        return 0;
    }
    const auto cells = p.scratchpad_bytes / denominator;
    auto t = static_cast<std::uint64_t>(std::sqrt(static_cast<long double>(cells)));
    while (t < std::numeric_limits<std::uint64_t>::max() &&
           static_cast<long double>(t + 1) * static_cast<long double>(t + 1) <= cells) ++t;
    while (static_cast<long double>(t) * static_cast<long double>(t) > cells) --t;
    const auto max_dim = std::min({extent(p.matrix_m), extent(p.matrix_n), extent(p.matrix_k)});
    return std::max<std::uint64_t>(1, std::min(t, max_dim));
}

bool footprint_fits(const HardwareParams& p, const Candidate& c) {
    if (c.m == 0 || c.n == 0 || c.k == 0 ||
        c.m > extent(p.matrix_m) || c.n > extent(p.matrix_n) || c.k > extent(p.matrix_k)) {
        return false;
    }
    std::uint64_t mk = 0, kn = 0, mn = 0, sum = 0, bytes = 0;
    return p.element_bytes > 0 &&
           checked_mul(c.m, c.k, mk) && checked_mul(c.k, c.n, kn) &&
           checked_mul(c.m, c.n, mn) && checked_add(mk, kn, sum) &&
           checked_add(sum, mn, sum) && checked_mul(sum, p.element_bytes, bytes) &&
           bytes <= p.scratchpad_bytes;
}

bool work_count_fits(const HardwareParams& p, const Candidate& c) {
    const auto mt = ceil_div(p.matrix_m, c.m);
    const auto nt = ceil_div(p.matrix_n, c.n);
    const auto kt = ceil_div(p.matrix_k, c.k);
    if (mt == 0 || nt == 0 || kt == 0) return true;
    std::uint64_t count = 0;
    return checked_mul(mt, nt, count) && checked_mul(count, kt, count) &&
           count <= MAX_TILE_COUNT;
}

std::size_t strategy_rank(const std::string& strategy) {
    const auto& names = supported_strategies();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (strategy == names[i]) return i;
    }
    return names.size();
}

long double tile_volume(const HardwareParams& p) {
    return static_cast<long double>(p.tile_m) * static_cast<long double>(p.tile_n) *
           static_cast<long double>(p.tile_k);
}

template <typename T>
int compare_value(const T& lhs, const T& rhs) {
    if (lhs < rhs) return -1;
    if (rhs < lhs) return 1;
    return 0;
}

int compare_results(const TuneResult& lhs, const TuneResult& rhs) {
    int cmp = 0;
    if (lhs.objective == TuneObjective::Cycles) {
        if ((cmp = compare_value(lhs.metrics.total_cycles, rhs.metrics.total_cycles)) != 0) return cmp;
        if ((cmp = compare_value(lhs.energy.energy_pj, rhs.energy.energy_pj)) != 0) return cmp;
        if ((cmp = compare_value(lhs.metrics.dram_bytes, rhs.metrics.dram_bytes)) != 0) return cmp;
    } else if (lhs.objective == TuneObjective::Energy) {
        if ((cmp = compare_value(lhs.energy.energy_pj, rhs.energy.energy_pj)) != 0) return cmp;
        if ((cmp = compare_value(lhs.metrics.total_cycles, rhs.metrics.total_cycles)) != 0) return cmp;
        if ((cmp = compare_value(lhs.metrics.dram_bytes, rhs.metrics.dram_bytes)) != 0) return cmp;
    } else {
        if ((cmp = compare_value(lhs.metrics.dram_bytes, rhs.metrics.dram_bytes)) != 0) return cmp;
        if ((cmp = compare_value(lhs.metrics.total_cycles, rhs.metrics.total_cycles)) != 0) return cmp;
        if ((cmp = compare_value(lhs.energy.energy_pj, rhs.energy.energy_pj)) != 0) return cmp;
    }
    const auto lhs_volume = tile_volume(lhs.params);
    const auto rhs_volume = tile_volume(rhs.params);
    if (lhs_volume != rhs_volume) return lhs_volume > rhs_volume ? -1 : 1;
    if ((cmp = compare_value(strategy_rank(lhs.strategy), strategy_rank(rhs.strategy))) != 0) return cmp;
    if ((cmp = compare_value(lhs.params.tile_m, rhs.params.tile_m)) != 0) return cmp;
    if ((cmp = compare_value(lhs.params.tile_n, rhs.params.tile_n)) != 0) return cmp;
    return compare_value(lhs.params.tile_k, rhs.params.tile_k);
}

TuneResult as_result(const EvaluatedCandidate& value, const HardwareParams& base,
                     TuneObjective objective) {
    TuneResult result;
    result.strategy = supported_strategies()[value.candidate.strategy_index];
    result.params = base;
    result.params.tile_m = value.candidate.m;
    result.params.tile_n = value.candidate.n;
    result.params.tile_k = value.candidate.k;
    result.metrics = value.metrics;
    result.energy = value.energy;
    result.objective = objective;
    return result;
}

bool evaluated_better(const EvaluatedCandidate& lhs, const EvaluatedCandidate& rhs,
                      const HardwareParams& base, TuneObjective objective) {
    return compare_results(as_result(lhs, base, objective), as_result(rhs, base, objective)) < 0;
}

std::vector<Candidate> coarse_candidates(const HardwareParams& p, std::size_t strategy_index,
                                         std::uint64_t base) {
    const std::array<std::uint64_t, 3> extents = {
        extent(p.matrix_m), extent(p.matrix_n), extent(p.matrix_k)
    };
    std::array<std::vector<std::uint64_t>, 3> values;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        values[axis] = {
            clamp_axis(std::max<std::uint64_t>(1, base / 2), extents[axis]),
            clamp_axis(base, extents[axis]),
            clamp_axis(base > extents[axis] / 2 ? extents[axis] : base * 2, extents[axis])
        };
        std::sort(values[axis].begin(), values[axis].end());
        values[axis].erase(std::unique(values[axis].begin(), values[axis].end()), values[axis].end());
    }

    std::set<CandidateKey> local;
    std::vector<Candidate> candidates;
    auto add = [&](std::uint64_t m, std::uint64_t n, std::uint64_t k) {
        Candidate c{strategy_index, m, n, k};
        if (local.emplace(strategy_index, m, n, k).second) candidates.push_back(c);
    };
    for (auto m : values[0]) for (auto n : values[1]) for (auto k : values[2]) add(m, n, k);
    for (unsigned mask = 1; mask < 8; ++mask) {
        add(mask & 1 ? extents[0] : clamp_axis(base, extents[0]),
            mask & 2 ? extents[1] : clamp_axis(base, extents[1]),
            mask & 4 ? extents[2] : clamp_axis(base, extents[2]));
    }

    const Candidate center{strategy_index, clamp_axis(base, extents[0]),
                           clamp_axis(base, extents[1]), clamp_axis(base, extents[2])};
    auto distance = [&](const Candidate& c) {
        auto diff = [](std::uint64_t a, std::uint64_t b) { return a > b ? a - b : b - a; };
        return static_cast<long double>(diff(c.m, center.m)) + diff(c.n, center.n) + diff(c.k, center.k);
    };
    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
        const auto da = distance(a), db = distance(b);
        return da != db ? da < db : std::tie(a.m, a.n, a.k) < std::tie(b.m, b.n, b.k);
    });
    return candidates;
}

std::uint64_t adjusted(std::uint64_t value, int delta, std::uint64_t step,
                       std::uint64_t axis_extent) {
    if (delta < 0) value = value > step ? value - step : 1;
    if (delta > 0) value = value > axis_extent - std::min(step, axis_extent)
        ? axis_extent : value + step;
    return clamp_axis(value, axis_extent);
}

} // namespace

const char* tune_objective_name(TuneObjective objective) {
    switch (objective) {
        case TuneObjective::Cycles: return "cycles";
        case TuneObjective::Energy: return "energy";
        case TuneObjective::DramBytes: return "dram_bytes";
    }
    return "cycles";
}

TuneObjective parse_tune_objective(const std::string& name) {
    if (name == "cycles") return TuneObjective::Cycles;
    if (name == "energy") return TuneObjective::Energy;
    if (name == "dram_bytes") return TuneObjective::DramBytes;
    throw std::invalid_argument("unknown tune objective: " + name);
}

bool tune_result_better(const TuneResult& lhs, const TuneResult& rhs) {
    if (lhs.objective != rhs.objective) {
        throw std::invalid_argument("cannot compare tuning results with different objectives");
    }
    return compare_results(lhs, rhs) < 0;
}

TuneResult tune_configuration(const HardwareParams& params, const TuneOptions& options) {
    if (options.max_evaluations < supported_strategies().size()) {
        throw std::invalid_argument("tune budget must be at least 4");
    }
    const bool any_tile = params.tile_m != 0 || params.tile_n != 0 || params.tile_k != 0;
    const bool all_tiles = params.tile_m != 0 && params.tile_n != 0 && params.tile_k != 0;
    if (any_tile && !all_tiles) {
        throw std::invalid_argument("--tile-m/n/k must all be set together or all left at 0 (auto)");
    }
    if (params.element_bytes == 0) {
        throw std::invalid_argument("element_bytes must be greater than zero for tuning");
    }

    TuneStats stats;
    std::set<CandidateKey> seen;
    std::array<std::vector<EvaluatedCandidate>, 4> elites;
    std::optional<EvaluatedCandidate> best;

    auto evaluate = [&](const Candidate& candidate) {
        const CandidateKey key{candidate.strategy_index, candidate.m, candidate.n, candidate.k};
        if (!seen.insert(key).second) return false;
        ++stats.candidates_considered;
        if (!footprint_fits(params, candidate) || !work_count_fits(params, candidate)) {
            ++stats.candidates_rejected;
            return true;
        }
        if (stats.candidates_evaluated >= options.max_evaluations) return false;
        HardwareParams candidate_params = params;
        candidate_params.tile_m = candidate.m;
        candidate_params.tile_n = candidate.n;
        candidate_params.tile_k = candidate.k;
        ++stats.candidates_evaluated;
        try {
            const auto metrics = run_simulation(candidate_params,
                                                supported_strategies()[candidate.strategy_index]);
            EvaluatedCandidate value{candidate, metrics, compute_energy(candidate_params, metrics)};
            if (!best || evaluated_better(value, *best, params, options.objective)) best = value;
            auto& strategy_elites = elites[candidate.strategy_index];
            strategy_elites.push_back(value);
            std::sort(strategy_elites.begin(), strategy_elites.end(), [&](const auto& a, const auto& b) {
                return evaluated_better(a, b, params, options.objective);
            });
            if (strategy_elites.size() > 2) strategy_elites.resize(2);
        } catch (const SimulationLimitError&) {
            ++stats.candidates_rejected;
        } catch (const std::invalid_argument&) {
            ++stats.candidates_rejected;
        } catch (const std::overflow_error&) {
            ++stats.candidates_rejected;
        }
        return true;
    };

    if (all_tiles) {
        for (std::size_t strategy = 0; strategy < supported_strategies().size(); ++strategy) {
            evaluate({strategy, params.tile_m, params.tile_n, params.tile_k});
        }
    } else {
        std::array<std::uint64_t, 4> bases{};
        std::array<std::vector<Candidate>, 4> queues;
        for (std::size_t strategy = 0; strategy < queues.size(); ++strategy) {
            bases[strategy] = square_base(params, strategy);
            queues[strategy] = coarse_candidates(params, strategy, bases[strategy]);
        }

        std::array<std::size_t, 4> positions{};
        bool progressed = true;
        while (stats.candidates_evaluated < options.max_evaluations && progressed) {
            progressed = false;
            for (std::size_t strategy = 0; strategy < queues.size(); ++strategy) {
                if (positions[strategy] < queues[strategy].size()) {
                    evaluate(queues[strategy][positions[strategy]++]);
                    progressed = true;
                    if (stats.candidates_evaluated >= options.max_evaluations) break;
                }
            }
        }

        std::array<std::uint64_t, 4> steps{};
        for (std::size_t strategy = 0; strategy < steps.size(); ++strategy) {
            steps[strategy] = std::max<std::uint64_t>(1, bases[strategy] / 4);
        }

        while (stats.candidates_evaluated < options.max_evaluations) {
            std::array<std::vector<Candidate>, 4> refinement;
            for (std::size_t strategy = 0; strategy < refinement.size(); ++strategy) {
                for (const auto& parent : elites[strategy]) {
                    for (int dm = -1; dm <= 1; ++dm) {
                        for (int dn = -1; dn <= 1; ++dn) {
                            for (int dk = -1; dk <= 1; ++dk) {
                                if (dm == 0 && dn == 0 && dk == 0) continue;
                                refinement[strategy].push_back({
                                    strategy,
                                    adjusted(parent.candidate.m, dm, steps[strategy], extent(params.matrix_m)),
                                    adjusted(parent.candidate.n, dn, steps[strategy], extent(params.matrix_n)),
                                    adjusted(parent.candidate.k, dk, steps[strategy], extent(params.matrix_k))
                                });
                            }
                        }
                    }
                }
                std::sort(refinement[strategy].begin(), refinement[strategy].end(),
                          [](const Candidate& a, const Candidate& b) {
                              return std::tie(a.m, a.n, a.k) < std::tie(b.m, b.n, b.k);
                          });
            }

            std::array<std::size_t, 4> refine_positions{};
            bool generated_unseen = false;
            bool round_progress = true;
            while (stats.candidates_evaluated < options.max_evaluations && round_progress) {
                round_progress = false;
                for (std::size_t strategy = 0; strategy < refinement.size(); ++strategy) {
                    while (refine_positions[strategy] < refinement[strategy].size()) {
                        const auto before = seen.size();
                        evaluate(refinement[strategy][refine_positions[strategy]++]);
                        if (seen.size() != before) {
                            generated_unseen = true;
                            round_progress = true;
                            break;
                        }
                    }
                    if (stats.candidates_evaluated >= options.max_evaluations) break;
                }
            }
            if (!generated_unseen) break;
            for (auto& step : steps) step = std::max<std::uint64_t>(1, step / 2);
        }
    }

    if (!best) {
        throw std::invalid_argument(
            "no valid tuning candidate for matrix " + std::to_string(params.matrix_m) + "x" +
            std::to_string(params.matrix_n) + "x" + std::to_string(params.matrix_k) +
            " with scratchpad " + std::to_string(params.scratchpad_bytes) + " bytes");
    }

    auto result = as_result(*best, params, options.objective);
    result.stats = stats;
    return result;
}
