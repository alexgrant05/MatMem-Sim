#include "energy_model.h"
#include "simulator.h"
#include "test_utils.h"
#include "tiling_engine.h"
#include "tuner.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

int g_failures = 0;

static HardwareParams small_params() {
    HardwareParams p;
    p.matrix_m = 24;
    p.matrix_n = 16;
    p.matrix_k = 20;
    p.scratchpad_bytes = 8 * 1024;
    return p;
}

static TuneResult synthetic(TuneObjective objective, const char* strategy,
                            std::uint64_t cycles, std::uint64_t bytes, double energy,
                            std::uint64_t tile = 8) {
    TuneResult result;
    result.objective = objective;
    result.strategy = strategy;
    result.metrics.total_cycles = cycles;
    result.metrics.dram_bytes = bytes;
    result.energy.energy_pj = energy;
    result.params.tile_m = result.params.tile_n = result.params.tile_k = tile;
    return result;
}

static void test_tuned_winner_replays_exactly() {
    const auto p = small_params();
    const auto tuned = tune_configuration(p, {TuneObjective::Cycles, 48});
    CHECK(!tuned.strategy.empty());
    CHECK(tuned.strategy != "auto");
    CHECK(tuned.params.tile_m > 0 && tuned.params.tile_n > 0 && tuned.params.tile_k > 0);
    CHECK(tuned.params.tile_m <= p.matrix_m);
    CHECK(tuned.params.tile_n <= p.matrix_n);
    CHECK(tuned.params.tile_k <= p.matrix_k);
    CHECK(tuned.stats.candidates_evaluated <= 48);

    const auto replay = run_simulation(tuned.params, tuned.strategy);
    CHECK(replay.total_cycles == tuned.metrics.total_cycles);
    CHECK(replay.dram_bytes == tuned.metrics.dram_bytes);
    CHECK(replay.operations == tuned.metrics.operations);
    CHECK_APPROX(compute_energy(tuned.params, replay).energy_pj, tuned.energy.energy_pj, 1e-9);
}

static void test_tuning_is_deterministic() {
    const auto p = small_params();
    const auto first = tune_configuration(p, {TuneObjective::Cycles, 32});
    const auto second = tune_configuration(p, {TuneObjective::Cycles, 32});
    CHECK(first.strategy == second.strategy);
    CHECK(first.params.tile_m == second.params.tile_m);
    CHECK(first.params.tile_n == second.params.tile_n);
    CHECK(first.params.tile_k == second.params.tile_k);
    CHECK(first.metrics.total_cycles == second.metrics.total_cycles);
    CHECK(first.stats.candidates_considered == second.stats.candidates_considered);
    CHECK(first.stats.candidates_evaluated == second.stats.candidates_evaluated);
    CHECK(first.stats.candidates_rejected == second.stats.candidates_rejected);
}

static void test_tuner_includes_legacy_baselines() {
    const auto p = small_params();
    std::uint64_t best_baseline = std::numeric_limits<std::uint64_t>::max();
    for (const auto* strategy : supported_strategies()) {
        best_baseline = std::min(best_baseline, run_simulation(p, strategy).total_cycles);
    }
    const auto tuned = tune_configuration(p, {TuneObjective::Cycles, 32});
    CHECK(tuned.metrics.total_cycles <= best_baseline);
}

static void test_fixed_tile_tunes_strategy_only() {
    auto p = small_params();
    p.tile_m = 8;
    p.tile_n = 4;
    p.tile_k = 5;
    const auto tuned = tune_configuration(p, {TuneObjective::DramBytes, 4});
    CHECK(tuned.params.tile_m == p.tile_m);
    CHECK(tuned.params.tile_n == p.tile_n);
    CHECK(tuned.params.tile_k == p.tile_k);
    CHECK(tuned.stats.candidates_evaluated == 4);

    TuneResult expected;
    bool have_expected = false;
    for (const auto* strategy : supported_strategies()) {
        TuneResult candidate;
        candidate.strategy = strategy;
        candidate.params = p;
        candidate.metrics = run_simulation(p, strategy);
        candidate.energy = compute_energy(p, candidate.metrics);
        candidate.objective = TuneObjective::DramBytes;
        if (!have_expected || tune_result_better(candidate, expected)) {
            expected = candidate;
            have_expected = true;
        }
    }
    CHECK(tuned.strategy == expected.strategy);
    CHECK(tuned.metrics.dram_bytes == expected.metrics.dram_bytes);
}

static void test_objective_scoring_and_ties() {
    auto fast = synthetic(TuneObjective::Cycles, "output_stationary", 10, 200, 300.0);
    auto efficient = synthetic(TuneObjective::Cycles, "input_stationary", 11, 100, 100.0);
    CHECK(tune_result_better(fast, efficient));

    fast.objective = efficient.objective = TuneObjective::Energy;
    CHECK(tune_result_better(efficient, fast));

    fast.objective = efficient.objective = TuneObjective::DramBytes;
    CHECK(tune_result_better(efficient, fast));

    fast.objective = TuneObjective::Cycles;
    auto large = synthetic(TuneObjective::Cycles, "output_stationary", 10, 200, 300.0, 16);
    CHECK(tune_result_better(large, fast));

    auto row = synthetic(TuneObjective::Cycles, "row_stationary", 10, 200, 300.0, 16);
    CHECK(tune_result_better(row, large));
}

static void test_validation_and_zero_work() {
    auto p = small_params();
    CHECK_THROWS(std::invalid_argument,
                 tune_configuration(p, {TuneObjective::Cycles, 3}));

    p.tile_m = 4;
    CHECK_THROWS(std::invalid_argument,
                 tune_configuration(p, {TuneObjective::Cycles, 8}));

    p = small_params();
    p.scratchpad_bytes = 0;
    CHECK_THROWS(std::invalid_argument,
                 tune_configuration(p, {TuneObjective::Cycles, 16}));

    p = small_params();
    p.matrix_k = 0;
    const auto zero = tune_configuration(p, {TuneObjective::Cycles, 16});
    CHECK(zero.metrics.total_cycles == 0);
    CHECK(zero.metrics.operations == 0);
    CHECK(zero.params.tile_k == 1);
}

static void test_objective_names() {
    CHECK(parse_tune_objective("cycles") == TuneObjective::Cycles);
    CHECK(parse_tune_objective("energy") == TuneObjective::Energy);
    CHECK(parse_tune_objective("dram_bytes") == TuneObjective::DramBytes);
    CHECK_THROWS(std::invalid_argument, parse_tune_objective("unknown"));
}

int main() {
    test_tuned_winner_replays_exactly();
    test_tuning_is_deterministic();
    test_tuner_includes_legacy_baselines();
    test_fixed_tile_tunes_strategy_only();
    test_objective_scoring_and_ties();
    test_validation_and_zero_work();
    test_objective_names();

    if (g_failures == 0) {
        std::cout << "test_tuner: all tests passed\n";
    } else {
        std::cout << "test_tuner: " << g_failures << " test(s) FAILED\n";
    }
    return g_failures > 0 ? 1 : 0;
}
