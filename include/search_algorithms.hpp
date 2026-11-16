#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "metrics.hpp"

namespace algolab {

/**
 * @brief Busca Sequencial (Linear) iterativa.
 * Complexidade: Melhor caso O(1), Caso médio Theta(n), Pior caso O(n), Espaço O(1).
 */
inline int sequential_search(const std::vector<int>& arr, int target, Metrics& m) {
    m.reset();
    m.extra_memory_bytes = sizeof(std::size_t);

    for (std::size_t i = 0; i < arr.size(); ++i) {
        m.array_accesses++;
        m.comparisons++;
        if (arr[i] == target) {
            m.dominant_op = "Comparações";
            m.dominant_count = m.comparisons;
            m.result_summary = "Encontrado no índice " + std::to_string(i);
            return static_cast<int>(i);
        }
    }

    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.result_summary = "Elemento ausente (-1)";
    return -1;
}

/**
 * @brief Busca Binária Iterativa (Divisão e Conquista iterativa em vetor ordenado).
 * Complexidade: Melhor caso O(1), Caso médio e pior caso Theta(log n), Espaço O(1).
 */
inline int binary_search_iterative(const std::vector<int>& arr, int target, Metrics& m) {
    m.reset();
    m.extra_memory_bytes = 3 * sizeof(int);

    int low = 0;
    int high = static_cast<int>(arr.size()) - 1;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        m.array_accesses++;
        int val = arr[static_cast<std::size_t>(mid)];

        m.comparisons++;
        if (val == target) {
            m.dominant_op = "Comparações";
            m.dominant_count = m.comparisons;
            m.result_summary = "Encontrado no índice " + std::to_string(mid);
            return mid;
        }

        m.comparisons++;
        if (val < target) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.result_summary = "Elemento ausente (-1)";
    return -1;
}

namespace detail {

inline int binary_search_rec_impl(const std::vector<int>& arr, int target, int low, int high,
                                  int depth, int& max_depth, Metrics& m) {
    m.recursive_calls++;
    if (depth > max_depth) max_depth = depth;

    if (low > high) {
        return -1;
    }

    int mid = low + (high - low) / 2;
    m.array_accesses++;
    int val = arr[static_cast<std::size_t>(mid)];

    m.comparisons++;
    if (val == target) {
        return mid;
    }

    m.comparisons++;
    if (val < target) {
        return binary_search_rec_impl(arr, target, mid + 1, high, depth + 1, max_depth, m);
    }
    return binary_search_rec_impl(arr, target, low, mid - 1, depth + 1, max_depth, m);
}

}  // namespace detail

/**
 * @brief Busca Binária Recursiva (Divisão e Conquista recursiva).
 * Complexidade: Tempo Theta(log n), Espaço auxiliar O(log n) pela pilha de recursão.
 */
inline int binary_search_recursive(const std::vector<int>& arr, int target, Metrics& m) {
    m.reset();
    int max_depth = 0;
    int idx = detail::binary_search_rec_impl(
        arr, target, 0, static_cast<int>(arr.size()) - 1, 1, max_depth, m);

    // Estimativa conservadora de frame de ativação (~48 bytes por nível na pilha).
    m.extra_memory_bytes = static_cast<std::size_t>(max_depth) * 48;
    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.result_summary = (idx >= 0) ? ("Encontrado no índice " + std::to_string(idx))
                                  : "Elemento ausente (-1)";
    return idx;
}

}  // namespace algolab
