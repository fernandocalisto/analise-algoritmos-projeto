#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "metrics.hpp"

namespace algolab {

/**
 * @brief Bubble Sort Otimizado com detecção de parada antecipada.
 * Melhor caso (ordenado): O(n) comparações, 0 trocas.
 * Caso médio e pior caso: Theta(n^2). Espaço auxiliar: O(1).
 */
inline void bubble_sort_optimized(std::vector<int>& arr, Metrics& m) {
    m.reset();
    m.extra_memory_bytes = sizeof(std::size_t) * 2 + sizeof(bool);
    const std::size_t n = arr.size();
    if (n <= 1) {
        m.dominant_op = "Comparações";
        return;
    }

    for (std::size_t i = 0; i < n - 1; ++i) {
        bool swapped = false;
        for (std::size_t j = 0; j < n - 1 - i; ++j) {
            m.array_accesses += 2;
            m.comparisons++;
            if (arr[j] > arr[j + 1]) {
                std::swap(arr[j], arr[j + 1]);
                m.array_accesses += 2;
                m.swaps_or_writes += 3;
                swapped = true;
            }
        }
        if (!swapped) {
            break;
        }
    }

    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.correct = std::is_sorted(arr.begin(), arr.end());
}

/**
 * @brief Insertion Sort (Ordenação por Inserção).
 * Melhor caso (ordenado): O(n). Quase ordenado: quase linear O(n + k).
 * Pior caso (decrescente): O(n^2). Espaço auxiliar: O(1). Estável e adaptativo.
 */
inline void insertion_sort(std::vector<int>& arr, Metrics& m) {
    m.reset();
    m.extra_memory_bytes = sizeof(std::size_t) * 2 + sizeof(int);
    const std::size_t n = arr.size();
    if (n <= 1) {
        m.dominant_op = "Comparações";
        return;
    }

    for (std::size_t i = 1; i < n; ++i) {
        m.array_accesses++;
        int key = arr[i];
        std::size_t j = i;
        while (j > 0) {
            m.array_accesses++;
            m.comparisons++;
            if (arr[j - 1] > key) {
                arr[j] = arr[j - 1];
                m.array_accesses++;
                m.swaps_or_writes++;
                --j;
            } else {
                break;
            }
        }
        arr[j] = key;
        m.swaps_or_writes++;
    }

    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.correct = std::is_sorted(arr.begin(), arr.end());
}

namespace detail {

inline void merge_ranges(std::vector<int>& arr, std::vector<int>& aux,
                         std::size_t left, std::size_t mid, std::size_t right, Metrics& m) {
    for (std::size_t k = left; k <= right; ++k) {
        aux[k] = arr[k];
        m.array_accesses += 2;
        m.swaps_or_writes++;
    }

    std::size_t i = left;
    std::size_t j = mid + 1;
    for (std::size_t k = left; k <= right; ++k) {
        if (i > mid) {
            arr[k] = aux[j++];
            m.array_accesses += 2;
            m.swaps_or_writes++;
        } else if (j > right) {
            arr[k] = aux[i++];
            m.array_accesses += 2;
            m.swaps_or_writes++;
        } else {
            m.array_accesses += 2;
            m.comparisons++;
            if (aux[i] <= aux[j]) {
                arr[k] = aux[i++];
            } else {
                arr[k] = aux[j++];
            }
            m.array_accesses += 2;
            m.swaps_or_writes++;
        }
    }
}

inline void merge_sort_rec(std::vector<int>& arr, std::vector<int>& aux,
                           std::size_t left, std::size_t right,
                           int depth, int& max_depth, Metrics& m) {
    m.recursive_calls++;
    if (depth > max_depth) max_depth = depth;
    if (left >= right) return;

    std::size_t mid = left + (right - left) / 2;
    merge_sort_rec(arr, aux, left, mid, depth + 1, max_depth, m);
    merge_sort_rec(arr, aux, mid + 1, right, depth + 1, max_depth, m);
    merge_ranges(arr, aux, left, mid, right, m);
}

inline int partition_lomuto_last(std::vector<int>& arr, int low, int high, Metrics& m) {
    m.array_accesses++;
    int pivot = arr[static_cast<std::size_t>(high)];
    int i = low - 1;

    for (int j = low; j < high; ++j) {
        m.array_accesses++;
        m.comparisons++;
        if (arr[static_cast<std::size_t>(j)] <= pivot) {
            ++i;
            if (i != j) {
                std::swap(arr[static_cast<std::size_t>(i)], arr[static_cast<std::size_t>(j)]);
                m.array_accesses += 4;
                m.swaps_or_writes += 3;
            }
        }
    }
    if (i + 1 != high) {
        std::swap(arr[static_cast<std::size_t>(i + 1)], arr[static_cast<std::size_t>(high)]);
        m.array_accesses += 4;
        m.swaps_or_writes += 3;
    }
    return i + 1;
}

inline void quick_sort_fixed_rec(std::vector<int>& arr, int low, int high,
                                 int depth, int& max_depth, Metrics& m) {
    // Eliminação de recursão de cauda no lado maior evita Stack Overflow em O(n^2) mantendo custo quadrático.
    while (low < high) {
        m.recursive_calls++;
        if (depth > max_depth) max_depth = depth;

        int p = partition_lomuto_last(arr, low, high, m);
        if (p - low < high - p) {
            quick_sort_fixed_rec(arr, low, p - 1, depth + 1, max_depth, m);
            low = p + 1;
        } else {
            quick_sort_fixed_rec(arr, p + 1, high, depth + 1, max_depth, m);
            high = p - 1;
        }
        ++depth;
    }
}

inline int median_of_three_pivot(std::vector<int>& arr, int low, int high, Metrics& m) {
    int mid = low + (high - low) / 2;
    m.array_accesses += 3;
    m.comparisons++;
    if (arr[static_cast<std::size_t>(mid)] < arr[static_cast<std::size_t>(low)]) {
        std::swap(arr[static_cast<std::size_t>(mid)], arr[static_cast<std::size_t>(low)]);
        m.swaps_or_writes += 3;
    }
    m.comparisons++;
    if (arr[static_cast<std::size_t>(high)] < arr[static_cast<std::size_t>(low)]) {
        std::swap(arr[static_cast<std::size_t>(high)], arr[static_cast<std::size_t>(low)]);
        m.swaps_or_writes += 3;
    }
    m.comparisons++;
    if (arr[static_cast<std::size_t>(high)] < arr[static_cast<std::size_t>(mid)]) {
        std::swap(arr[static_cast<std::size_t>(high)], arr[static_cast<std::size_t>(mid)]);
        m.swaps_or_writes += 3;
    }
    return arr[static_cast<std::size_t>(mid)];
}

inline int partition_hoare_median3(std::vector<int>& arr, int low, int high, Metrics& m) {
    int pivot = median_of_three_pivot(arr, low, high, m);
    int i = low - 1;
    int j = high + 1;

    while (true) {
        do {
            ++i;
            m.array_accesses++;
            m.comparisons++;
        } while (arr[static_cast<std::size_t>(i)] < pivot);

        do {
            --j;
            m.array_accesses++;
            m.comparisons++;
        } while (arr[static_cast<std::size_t>(j)] > pivot);

        if (i >= j) {
            return j;
        }
        std::swap(arr[static_cast<std::size_t>(i)], arr[static_cast<std::size_t>(j)]);
        m.array_accesses += 4;
        m.swaps_or_writes += 3;
    }
}

inline void quick_sort_median3_rec(std::vector<int>& arr, int low, int high,
                                   int depth, int& max_depth, Metrics& m) {
    while (low < high) {
        m.recursive_calls++;
        if (depth > max_depth) max_depth = depth;

        int p = partition_hoare_median3(arr, low, high, m);
        if (p - low < high - p) {
            quick_sort_median3_rec(arr, low, p, depth + 1, max_depth, m);
            low = p + 1;
        } else {
            quick_sort_median3_rec(arr, p + 1, high, depth + 1, max_depth, m);
            high = p;
        }
        ++depth;
    }
}

}  // namespace detail

/**
 * @brief Merge Sort (Divisão e Conquista estável).
 * Melhor, médio e pior caso: Theta(n log n). Espaço auxiliar: Theta(n).
 */
inline void merge_sort(std::vector<int>& arr, Metrics& m) {
    m.reset();
    if (arr.size() <= 1) {
        m.dominant_op = "Comparações";
        return;
    }

    std::vector<int> aux(arr.size());
    int max_depth = 0;
    detail::merge_sort_rec(arr, aux, 0, arr.size() - 1, 1, max_depth, m);

    m.extra_memory_bytes = aux.size() * sizeof(int) + static_cast<std::size_t>(max_depth) * 48;
    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.correct = std::is_sorted(arr.begin(), arr.end());
}

/**
 * @brief Quick Sort Clássico com Pivô Fixo (último elemento - Lomuto).
 * Caso médio: Theta(n log n). Pior caso (ordenado/inverso/repetidos): Theta(n^2).
 */
inline void quick_sort_fixed_pivot(std::vector<int>& arr, Metrics& m) {
    m.reset();
    if (arr.size() <= 1) {
        m.dominant_op = "Comparações";
        return;
    }

    int max_depth = 0;
    detail::quick_sort_fixed_rec(arr, 0, static_cast<int>(arr.size()) - 1, 1, max_depth, m);

    m.extra_memory_bytes = static_cast<std::size_t>(max_depth) * 48;
    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.correct = std::is_sorted(arr.begin(), arr.end());
}

/**
 * @brief Quick Sort Otimizado com Mediana de Três e Partição de Hoare.
 * Mantém O(n log n) inclusive em entradas ordenadas, reversas e com repetição.
 */
inline void quick_sort_median3(std::vector<int>& arr, Metrics& m) {
    m.reset();
    if (arr.size() <= 1) {
        m.dominant_op = "Comparações";
        return;
    }

    int max_depth = 0;
    detail::quick_sort_median3_rec(arr, 0, static_cast<int>(arr.size()) - 1, 1, max_depth, m);

    m.extra_memory_bytes = static_cast<std::size_t>(max_depth) * 48;
    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.correct = std::is_sorted(arr.begin(), arr.end());
}

}  // namespace algolab
