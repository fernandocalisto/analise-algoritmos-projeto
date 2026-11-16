#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "metrics.hpp"

namespace algolab {

namespace detail {

inline std::uint64_t fib_rec_impl(int n, int depth, int& max_depth, Metrics& m) {
    m.recursive_calls++;
    if (depth > max_depth) max_depth = depth;

    m.comparisons++;
    if (n <= 1) {
        return static_cast<std::uint64_t>(n);
    }
    m.swaps_or_writes++;  // representa a adição F(n-1) + F(n-2)
    return fib_rec_impl(n - 1, depth + 1, max_depth, m) +
           fib_rec_impl(n - 2, depth + 1, max_depth, m);
}

inline std::uint64_t fib_memo_impl(int n, std::vector<std::uint64_t>& memo,
                                   std::vector<bool>& computed,
                                   int depth, int& max_depth, Metrics& m) {
    m.recursive_calls++;
    if (depth > max_depth) max_depth = depth;

    m.comparisons++;
    if (n <= 1) {
        return static_cast<std::uint64_t>(n);
    }

    m.array_accesses++;
    m.comparisons++;
    if (computed[static_cast<std::size_t>(n)]) {
        return memo[static_cast<std::size_t>(n)];
    }

    std::uint64_t res = fib_memo_impl(n - 1, memo, computed, depth + 1, max_depth, m) +
                        fib_memo_impl(n - 2, memo, computed, depth + 1, max_depth, m);
    memo[static_cast<std::size_t>(n)] = res;
    computed[static_cast<std::size_t>(n)] = true;
    m.array_accesses += 2;
    m.swaps_or_writes++;
    return res;
}

inline double pow_dc_impl(double base, int exp, int depth, int& max_depth, Metrics& m) {
    m.recursive_calls++;
    if (depth > max_depth) max_depth = depth;

    m.comparisons++;
    if (exp == 0) return 1.0;

    m.comparisons++;
    if (exp == 1) return base;

    double half = pow_dc_impl(base, exp / 2, depth + 1, max_depth, m);
    m.swaps_or_writes++;  // half * half
    double res = half * half;

    m.comparisons++;
    if (exp % 2 != 0) {
        m.swaps_or_writes++;  // res * base
        res *= base;
    }
    return res;
}

}  // namespace detail

/**
 * @brief Fibonacci Recursivo Ingênuo.
 * Recorrência: T(n) = T(n-1) + T(n-2) + c -> O(phi^n) ~ O(2^n). Espaço de pilha: O(n).
 */
inline std::uint64_t fibonacci_recursive(int n, Metrics& m) {
    m.reset();
    if (n < 0) n = 0;
    int max_depth = 0;
    std::uint64_t val = detail::fib_rec_impl(n, 1, max_depth, m);
    m.extra_memory_bytes = static_cast<std::size_t>(max_depth) * 32;
    m.dominant_op = "Chamadas recursivas";
    m.dominant_count = m.recursive_calls;
    m.result_summary = "F(" + std::to_string(n) + ") = " + std::to_string(val);
    return val;
}

/**
 * @brief Fibonacci Iterativo.
 * Complexidade: Theta(n) tempo, Theta(1) espaço auxiliar.
 */
inline std::uint64_t fibonacci_iterative(int n, Metrics& m) {
    m.reset();
    if (n <= 0) {
        m.extra_memory_bytes = sizeof(std::uint64_t) * 3;
        m.dominant_op = "Somas iterativas";
        m.result_summary = "F(0) = 0";
        return 0;
    }
    if (n == 1) {
        m.comparisons = 1;
        m.extra_memory_bytes = sizeof(std::uint64_t) * 3;
        m.dominant_op = "Somas iterativas";
        m.result_summary = "F(1) = 1";
        return 1;
    }

    std::uint64_t a = 0;
    std::uint64_t b = 1;
    for (int i = 2; i <= n; ++i) {
        m.comparisons++;
        std::uint64_t next = a + b;
        a = b;
        b = next;
        m.swaps_or_writes++;
    }

    m.extra_memory_bytes = sizeof(std::uint64_t) * 3 + sizeof(int);
    m.dominant_op = "Somas iterativas";
    m.dominant_count = m.swaps_or_writes;
    m.result_summary = "F(" + std::to_string(n) + ") = " + std::to_string(b);
    return b;
}

/**
 * @brief Fibonacci com Programação Dinâmica (Memoização Top-Down).
 * Complexidade: Theta(n) tempo, Theta(n) espaço auxiliar (tabela + pilha).
 */
inline std::uint64_t fibonacci_dp_memo(int n, Metrics& m) {
    m.reset();
    if (n < 0) n = 0;
    std::vector<std::uint64_t> memo(static_cast<std::size_t>(n) + 1, 0);
    std::vector<bool> computed(static_cast<std::size_t>(n) + 1, false);

    int max_depth = 0;
    std::uint64_t val = detail::fib_memo_impl(n, memo, computed, 1, max_depth, m);

    m.extra_memory_bytes = memo.size() * (sizeof(std::uint64_t) + 1) +
                           static_cast<std::size_t>(max_depth) * 48;
    m.dominant_op = "Chamadas memoizadas";
    m.dominant_count = m.recursive_calls;
    m.result_summary = "F(" + std::to_string(n) + ") = " + std::to_string(val);
    return val;
}

/**
 * @brief Exponenciação Linear Iterativa: base^exp em Theta(n) multiplicações.
 */
inline double power_iterative(double base, int exp, Metrics& m) {
    m.reset();
    double result = 1.0;
    for (int i = 0; i < exp; ++i) {
        m.comparisons++;
        result *= base;
        m.swaps_or_writes++;
    }
    m.extra_memory_bytes = sizeof(double) * 2 + sizeof(int);
    m.dominant_op = "Multiplicações";
    m.dominant_count = m.swaps_or_writes;
    m.result_summary = "Resultado = " + std::to_string(result);
    return result;
}

/**
 * @brief Exponenciação Rápida (Divisão e Conquista): base^exp em Theta(log n) multiplicações.
 * Recorrência: T(n) = T(n/2) + O(1) -> Theta(log n).
 */
inline double power_divide_and_conquer(double base, int exp, Metrics& m) {
    m.reset();
    int max_depth = 0;
    double result = detail::pow_dc_impl(base, exp, 1, max_depth, m);
    m.extra_memory_bytes = static_cast<std::size_t>(max_depth) * 48;
    m.dominant_op = "Multiplicações";
    m.dominant_count = m.swaps_or_writes;
    m.result_summary = "Resultado = " + std::to_string(result);
    return result;
}

}  // namespace algolab
