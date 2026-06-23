#include "hardware_params.h"
#include "simulator.h"

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

// operations = 2 * MACs (FLOPs). Energy is:
//   DRAM:  dram_bytes * E_dram_per_byte
//   SRAM:  (dram_bytes/elem + operations) * E_sram_per_access
//            dram_bytes/elem  = scratchpad I/O elements
//            operations       = 2 operand reads per MAC = 2*(ops/2) = ops
//   MAC:   (operations/2) * E_mac_pj   [ops/2 = number of MACs]
struct EnergyResult { double energy_pj; double ops_per_pj; };

EnergyResult compute_energy(const HardwareParams& p, const Metrics& m) {
    const double ops = static_cast<double>(m.operations);
    const double sram_acc = static_cast<double>(m.dram_bytes) / static_cast<double>(p.element_bytes)
                          + ops;
    const double e = static_cast<double>(m.dram_bytes) * p.energy_dram_pj_per_byte
                   + sram_acc * p.energy_sram_pj_per_access
                   + (ops / 2.0) * p.energy_mac_pj;
    const double opp = e > 0.0 ? ops / e : 0.0;
    return {e, opp};
}

void print_usage() {
    std::cout <<
        "usage: matmem-sim\n"
        "  [--strategy row_stationary|output_stationary|input_stationary|double_buffer]\n"
        "  [--scratchpad-kb N]      scratchpad capacity in KB (default 32)\n"
        "  [--dram-latency N]       DRAM round-trip latency in cycles (default 100)\n"
        "  [--bandwidth N]          DRAM bandwidth in bytes/cycle (default 32)\n"
        "  [--compute-ops N]        compute throughput in ops/cycle (default 256)\n"
        "  [--matrix-m N]           matrix M dimension (default 256)\n"
        "  [--matrix-n N]           matrix N dimension (default 256)\n"
        "  [--matrix-k N]           matrix K dimension (default 256)\n"
        "  [--tile-m N]             tile M size, 0 = auto (default 0)\n"
        "  [--tile-n N]             tile N size, 0 = auto (default 0)\n"
        "  [--tile-k N]             tile K size, 0 = auto (default 0)\n"
        "  [--csv]                  emit CSV row instead of human-readable output\n"
        "  [--trace]                print per-tile Gantt (load/compute/store cycle ranges)\n";
}

} // namespace

int main(int argc, char** argv) {
    HardwareParams params;
    std::string strategy = "row_stationary";
    bool csv = false;
    bool trace = false;

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
            } else if (arg == "--dram-latency") {
                params.dram_latency_cycles = parse_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--bandwidth") {
                params.dram_bandwidth_bytes_per_cycle = parse_pos_uint(value_after(i, argc, argv), arg);
            } else if (arg == "--compute-ops") {
                params.compute_ops_per_cycle = parse_pos_uint(value_after(i, argc, argv), arg);
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

        std::vector<TraceRecord> trace_records;
        const auto metrics = run_simulation(params, strategy, trace ? &trace_records : nullptr);
        const auto energy = compute_energy(params, metrics);

        if (csv) {
            std::cout << "strategy,scratchpad_kb,dram_latency,bandwidth,compute_ops,"
                         "matrix_m,matrix_n,matrix_k,"
                         "total_cycles,compute_cycles,dram_stall_cycles,dram_bytes,"
                         "compute_utilization,arithmetic_intensity,effective_ops_per_cycle,"
                         "energy_pj,ops_per_pj\n";
            std::cout << strategy << ','
                      << params.scratchpad_bytes / 1024 << ','
                      << params.dram_latency_cycles << ','
                      << params.dram_bandwidth_bytes_per_cycle << ','
                      << params.compute_ops_per_cycle << ','
                      << params.matrix_m << ','
                      << params.matrix_n << ','
                      << params.matrix_k << ','
                      << metrics.total_cycles << ','
                      << metrics.compute_cycles << ','
                      << metrics.dram_stall_cycles << ','
                      << metrics.dram_bytes << ','
                      << metrics.compute_utilization() << ','
                      << metrics.arithmetic_intensity() << ','
                      << metrics.effective_ops_per_cycle() << ','
                      << energy.energy_pj << ','
                      << energy.ops_per_pj << '\n';
        } else {
            std::cout << std::fixed << std::setprecision(4);
            std::cout << "strategy: " << strategy << '\n';
            std::cout << "total_cycles: " << metrics.total_cycles << '\n';
            std::cout << "compute_cycles: " << metrics.compute_cycles << '\n';
            std::cout << "dram_stall_cycles: " << metrics.dram_stall_cycles << '\n';
            std::cout << "dram_bytes: " << metrics.dram_bytes << '\n';
            std::cout << "compute_utilization: " << metrics.compute_utilization() << '\n';
            std::cout << "arithmetic_intensity: " << metrics.arithmetic_intensity() << '\n';
            std::cout << "effective_ops_per_cycle: " << metrics.effective_ops_per_cycle() << '\n';
            std::cout << "energy_pj: " << energy.energy_pj << '\n';
            std::cout << "ops_per_pj: " << energy.ops_per_pj << '\n';
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
