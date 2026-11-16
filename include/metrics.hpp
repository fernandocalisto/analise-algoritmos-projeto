#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace algolab {

/**
 * @brief Estrutura de métricas empíricas coletadas durante a execução de cada algoritmo.
 *
 * Instrumenta operações primitivas (comparações, trocas/escritas, acessos à memória,
 * chamadas recursivas), uso aproximado de memória auxiliar e tempo de execução em
 * microssegundos e milissegundos.
 */
struct Metrics {
    uint64_t comparisons = 0;
    uint64_t swaps_or_writes = 0;
    uint64_t array_accesses = 0;
    uint64_t recursive_calls = 0;
    size_t extra_memory_bytes = 0;
    double elapsed_us = 0.0;
    double elapsed_ms = 0.0;
    std::string dominant_op;
    uint64_t dominant_count = 0;
    std::string observation;
    std::string result_summary;
    bool correct = true;

    void reset() {
        comparisons = 0;
        swaps_or_writes = 0;
        array_accesses = 0;
        recursive_calls = 0;
        extra_memory_bytes = 0;
        elapsed_us = 0.0;
        elapsed_ms = 0.0;
        dominant_op.clear();
        dominant_count = 0;
        observation.clear();
        result_summary.clear();
        correct = true;
    }
};

}  // namespace algolab
