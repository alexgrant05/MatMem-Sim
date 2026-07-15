#include "hardware_params.h"
#include "energy_model.h"
#include "simulator.h"
#include "tuner.h"

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string value_after(int& i, int argc, char** argv) {
    if (i + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[i]);
    }
    return argv[++i];
}

std::uint64_t parse_uint(const std::string& s, const std::string& flag) {
    if (s.empty() || s[0] == '-') {
        throw std::invalid_argument("expected non-negative integer for " + flag + ": '" + s + "'");
    }
    std::size_t pos = 0;
    std::uint64_t v;
    try {
        v = std::stoull(s, &pos);
    } catch (const std::exception&) {
        throw std::invalid_argument("expected non-negative integer for " + flag + ": '" + s + "'");
    }
    if (pos != s.size()) {
        throw std::invalid_argument("expected non-negative integer for " + flag + ": '" + s + "'");
    }
    return v;
}

std::uint64_t parse_pos_uint(const std::string& s, const std::string& flag) {
    const auto v = parse_uint(s, flag);
    if (v == 0) {
        throw std::invalid_argument(flag + " must be greater than zero");
    }
    return v;
}

void print_usage() {
    std::cout <<
        "usage: matmem-sim\n"
        "  [--strategy auto|row_stationary|output_stationary|input_stationary|double_buffer]\n"
        "  [--scratchpad-kb N]      scratchpad capacity in KB (default 32)\n"
        "  [--scratchpad-latency N] scratchpad access latency in cycles (default 1)\n"
        "  [--dram-latency N]       DRAM round-trip latency in cycles (default 100)\n"
        "  [--bandwidth N]          DRAM bandwidth in bytes/cycle (default 32)\n"
        "  [--compute-ops N]        compute throughput in ops/cycle (default 256)\n"
        "  [--element-bytes N]      bytes per matrix element (default 4)\n"
        "  [--matrix-m N]           matrix M dimension (default 256)\n"
        "  [--matrix-n N]           matrix N dimension (default 256)\n"
        "  [--matrix-k N]           matrix K dimension (default 256)\n"
        "  [--tile-m N]             tile M size, 0 = auto (default 0)\n"
        "  [--tile-n N]             tile N size, 0 = auto (default 0)\n"
        "  [--tile-k N]             tile K size, 0 = auto (default 0)\n"
        "  [--tune-objective cycles|energy|dram_bytes] (auto strategy only)\n"
        "  [--tune-budget N]        exact simulation budget, minimum 4 (default 256)\n"
        "  [--csv]                  emit CSV row instead of human-readable output\n"
        "  [--trace]                print per-tile Gantt (load/compute/store cycle ranges)\n";
}

} // namespace

int main(int argc, char** argv) {
    HardwareParams params;
    std::string strategy = "row_stationary";
    bool csv = false;
    bool trace = false;
    TuneOptions tune_options;
    bool tune_objective_set = false;
    bool tune_budget_set = false;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--strategy") {
                strategy = value_after(i, argc, argv);
            } else if (arg == "--scratchpad-kb") {
                const auto kb = parse_uint(value_after(i, argc, argv), arg);
                if (kb > std::numeric_limits<std::uint64_t>::max() / 1024) {
                    throw std::invalid_argument("--scratchpad-kb value too large");
                }
                params.scratchpad_bytes = kb * 1024;
            } else if (arg == "--scratchpad-latency") {
                params.scratchpad_latency_cycles = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--dram-latency") {
                params.dram_latency_cycles = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--bandwidth") {
                params.dram_bandwidth_bytes_per_cycle = parse_pos_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--compute-ops") {
                params.compute_ops_per_cycle = parse_pos_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--element-bytes") {
                params.element_bytes = parse_pos_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--matrix-m") {
                params.matrix_m = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--matrix-n") {
                params.matrix_n = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--matrix-k") {
                params.matrix_k = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--tile-m") {
                params.tile_m = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--tile-n") {
                params.tile_n = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--tile-k") {
                params.tile_k = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--tune-objective") {
                tune_options.objective = parse_tune_objective(value_after(i, argc, argv));
                tune_objective_set = true;
            } else if (arg == "--tune-budget") {
                tune_options.max_evaluations = parse_pos_uint(value_after(i, argc, argv), arg);
                tune_budget_set = true;
            } else if (arg == "--csv") {
                csv = true;
            } else if (arg == "--trace") {
                trace = true;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("unknown argument: " + arg);
            }
        }

        const bool tuning = strategy == "auto";
        if (!tuning && (tune_objective_set || tune_budget_set)) {
            throw std::invalid_argument("--tune-objective and --tune-budget require --strategy auto");
        }

        std::vector<TraceRecord> trace_records;
        Metrics metrics;
        EnergyResult energy;
        TuneStats tune_stats;
        HardwareParams result_params = params;
        std::string result_strategy = strategy;
        if (tuning) {
            const auto result = tune_configuration(params, tune_options);
            result_strategy = result.strategy;
            result_params = result.params;
            metrics = result.metrics;
            energy = result.energy;
            tune_stats = result.stats;
            if (trace) {
                metrics = run_simulation(result_params, result_strategy, &trace_records);
                energy = compute_energy(result_params, metrics);
            }
        } else {
            metrics = run_simulation(params, strategy, trace ? &trace_records : nullptr);
            energy = compute_energy(params, metrics);
        }

        if (csv) {
            std::cout << "strategy,scratchpad_kb,scratchpad_latency,dram_latency,bandwidth,compute_ops,"
                         "matrix_m,matrix_n,matrix_k,"
                         "total_cycles,compute_cycles,dram_stall_cycles,scratchpad_stall_cycles,dram_bytes,"
                         "compute_utilization,arithmetic_intensity,effective_ops_per_cycle,"
                         "energy_pj,ops_per_pj,"
                         "a_reuse_factor,b_reuse_factor,c_reuse_factor,"
                         "tuned,tune_objective,tile_m,tile_n,tile_k,"
                         "tune_candidates_considered,tune_candidates_evaluated,"
                         "tune_candidates_rejected\n";
            std::cout << result_strategy << ','
                      << params.scratchpad_bytes / 1024 << ','
                      << params.scratchpad_latency_cycles << ','
                      << params.dram_latency_cycles << ','
                      << params.dram_bandwidth_bytes_per_cycle << ','
                      << params.compute_ops_per_cycle << ','
                      << params.matrix_m << ','
                      << params.matrix_n << ','
                      << params.matrix_k << ','
                      << metrics.total_cycles << ','
                      << metrics.compute_cycles << ','
                      << metrics.dram_stall_cycles << ','
                      << metrics.scratchpad_stall_cycles << ','
                      << metrics.dram_bytes << ','
                      << metrics.compute_utilization() << ','
                      << metrics.arithmetic_intensity() << ','
                      << metrics.effective_ops_per_cycle() << ','
                      << energy.energy_pj << ','
                      << energy.ops_per_pj << ','
                      << metrics.a_reuse_factor() << ','
                      << metrics.b_reuse_factor() << ','
                      << metrics.c_reuse_factor() << ','
                      << (tuning ? 1 : 0) << ',';
            if (tuning) {
                std::cout << tune_objective_name(tune_options.objective);
            }
            std::cout << ',' << result_params.tile_m
                      << ',' << result_params.tile_n
                      << ',' << result_params.tile_k
                      << ',' << tune_stats.candidates_considered
                      << ',' << tune_stats.candidates_evaluated
                      << ',' << tune_stats.candidates_rejected << '\n';
        } else {
            std::cout << std::fixed << std::setprecision(4);
            std::cout << "strategy: " << result_strategy << '\n';
            if (tuning) {
                std::cout << "tuned: true\n";
                std::cout << "tune_objective: " << tune_objective_name(tune_options.objective) << '\n';
                std::cout << "tile_m: " << result_params.tile_m << '\n';
                std::cout << "tile_n: " << result_params.tile_n << '\n';
                std::cout << "tile_k: " << result_params.tile_k << '\n';
                std::cout << "tune_candidates_considered: " << tune_stats.candidates_considered << '\n';
                std::cout << "tune_candidates_evaluated: " << tune_stats.candidates_evaluated << '\n';
                std::cout << "tune_candidates_rejected: " << tune_stats.candidates_rejected << '\n';
            }
            std::cout << "total_cycles: " << metrics.total_cycles << '\n';
            std::cout << "compute_cycles: " << metrics.compute_cycles << '\n';
            std::cout << "dram_stall_cycles: " << metrics.dram_stall_cycles << '\n';
            std::cout << "scratchpad_stall_cycles: " << metrics.scratchpad_stall_cycles << '\n';
            std::cout << "dram_bytes: " << metrics.dram_bytes << '\n';
            std::cout << "compute_utilization: " << metrics.compute_utilization() << '\n';
            std::cout << "arithmetic_intensity: " << metrics.arithmetic_intensity() << '\n';
            std::cout << "effective_ops_per_cycle: " << metrics.effective_ops_per_cycle() << '\n';
            std::cout << "energy_pj: " << energy.energy_pj << '\n';
            std::cout << "ops_per_pj: " << energy.ops_per_pj << '\n';
            std::cout << "a_reuse_factor: " << metrics.a_reuse_factor() << '\n';
            std::cout << "b_reuse_factor: " << metrics.b_reuse_factor() << '\n';
            std::cout << "c_reuse_factor: " << metrics.c_reuse_factor() << '\n';
        }
        if (trace) {
            std::cout << '\n';
            for (std::size_t i = 0; i < trace_records.size(); ++i) {
                const auto& r = trace_records[i];
                std::cout << "tile " << std::setw(6) << i
                          << ": load=[" << r.load_start << ',' << r.load_end << ')'
                          << " compute=[" << r.compute_start << ',' << r.compute_end << ')'
                          << " store=[" << r.store_start << ',' << r.store_end << ")\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        print_usage();
        return 1;
    }

    return 0;
}
