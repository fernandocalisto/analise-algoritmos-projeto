#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "metrics.hpp"

namespace algolab {

struct CoinChangeResult {
    int total_coins = -1;
    std::vector<int> chosen_coins;
    bool possible = false;
};

struct KnapsackItem {
    int id = 0;
    int weight = 0;
    double value = 0.0;
};

struct KnapsackResult {
    double total_value = 0.0;
    int total_weight = 0;
    std::vector<int> selected_ids;
};

/**
 * @brief Problema do Troco — Algoritmo Guloso.
 * Ordena moedas em ordem decrescente e escolhe sempre a maior moeda <= restante.
 * Complexidade: O(k log k + k + A/min_coin). Espaço: O(1) além do vetor de saída.
 * Observação crítica: só garante solução ótima para sistemas canônicos (ex.: Real BRL).
 * Para [1, 3, 4] com troco = 6, escolhe 4+1+1 (3 moedas), enquanto o ótimo é 3+3 (2 moedas).
 */
inline CoinChangeResult coin_change_greedy(const std::vector<int>& coins, int amount, Metrics& m) {
    m.reset();
    CoinChangeResult res;
    if (amount == 0) {
        res.total_coins = 0;
        res.possible = true;
        m.dominant_op = "Seleções/Comparações";
        return res;
    }

    std::vector<int> sorted_coins = coins;
    std::sort(sorted_coins.rbegin(), sorted_coins.rend());
    m.extra_memory_bytes = sorted_coins.size() * sizeof(int);

    int remaining = amount;
    for (int coin : sorted_coins) {
        m.comparisons++;
        if (coin <= 0) continue;
        while (remaining >= coin) {
            m.comparisons++;
            remaining -= coin;
            res.chosen_coins.push_back(coin);
            res.total_coins = static_cast<int>(res.chosen_coins.size());
            m.swaps_or_writes++;
        }
    }

    res.possible = (remaining == 0);
    if (!res.possible) res.total_coins = -1;

    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.result_summary = res.possible ? (std::to_string(res.total_coins) + " moedas") : "Sem solução";
    return res;
}

/**
 * @brief Problema do Troco — Programação Dinâmica (Bottom-Up).
 * Recorrência: dp[x] = 1 + min_{c in coins, c <= x} dp[x - c].
 * Complexidade: Theta(amount * k), Espaço: Theta(amount).
 * Garante sempre a quantidade mínima ótima de moedas para qualquer conjunto positivo.
 */
inline CoinChangeResult coin_change_dp(const std::vector<int>& coins, int amount, Metrics& m) {
    m.reset();
    CoinChangeResult res;
    if (amount < 0) return res;

    const int INF = std::numeric_limits<int>::max() / 2;
    std::vector<int> dp(static_cast<std::size_t>(amount) + 1, INF);
    std::vector<int> parent(static_cast<std::size_t>(amount) + 1, -1);
    dp[0] = 0;
    m.swaps_or_writes++;

    for (int x = 1; x <= amount; ++x) {
        for (int coin : coins) {
            m.comparisons++;
            if (coin > 0 && coin <= x) {
                m.array_accesses++;
                int candidate = dp[static_cast<std::size_t>(x - coin)] + 1;
                m.comparisons++;
                if (candidate < dp[static_cast<std::size_t>(x)]) {
                    dp[static_cast<std::size_t>(x)] = candidate;
                    parent[static_cast<std::size_t>(x)] = coin;
                    m.swaps_or_writes += 2;
                }
            }
        }
    }

    m.extra_memory_bytes = (dp.size() + parent.size()) * sizeof(int);
    if (dp[static_cast<std::size_t>(amount)] < INF) {
        res.possible = true;
        res.total_coins = dp[static_cast<std::size_t>(amount)];
        int cur = amount;
        while (cur > 0 && parent[static_cast<std::size_t>(cur)] > 0) {
            int c = parent[static_cast<std::size_t>(cur)];
            res.chosen_coins.push_back(c);
            cur -= c;
        }
    }

    m.dominant_op = "Transições DP";
    m.dominant_count = m.comparisons;
    m.result_summary = res.possible ? (std::to_string(res.total_coins) + " moedas (ótimo)")
                                    : "Sem solução";
    return res;
}

/**
 * @brief Mochila Fracionária — Algoritmo Guloso (Ótimo para frações).
 * Ordena itens pela razão valor/peso decrescente em O(n log n) e preenche a capacidade.
 */
inline KnapsackResult knapsack_fractional_greedy(std::vector<KnapsackItem> items, int capacity, Metrics& m) {
    m.reset();
    KnapsackResult res;
    std::sort(items.begin(), items.end(), [&m](const KnapsackItem& a, const KnapsackItem& b) {
        m.comparisons++;
        return (a.value / a.weight) > (b.value / b.weight);
    });

    int remaining = capacity;
    for (const auto& item : items) {
        m.comparisons++;
        if (remaining <= 0) break;
        if (item.weight <= remaining) {
            remaining -= item.weight;
            res.total_weight += item.weight;
            res.total_value += item.value;
            res.selected_ids.push_back(item.id);
            m.swaps_or_writes++;
        } else {
            double frac = static_cast<double>(remaining) / static_cast<double>(item.weight);
            res.total_value += item.value * frac;
            res.total_weight += remaining;
            res.selected_ids.push_back(item.id);
            remaining = 0;
            m.swaps_or_writes++;
        }
    }

    m.extra_memory_bytes = items.size() * sizeof(KnapsackItem);
    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.result_summary = "Valor = " + std::to_string(static_cast<int>(res.total_value + 0.5));
    return res;
}

/**
 * @brief Mochila 0/1 — Heurística Gulosa por razão valor/peso (pode falhar em garantir ótimo 0/1).
 */
inline KnapsackResult knapsack_01_greedy_heuristic(std::vector<KnapsackItem> items, int capacity, Metrics& m) {
    m.reset();
    KnapsackResult res;
    std::sort(items.begin(), items.end(), [&m](const KnapsackItem& a, const KnapsackItem& b) {
        m.comparisons++;
        return (a.value / a.weight) > (b.value / b.weight);
    });

    int remaining = capacity;
    for (const auto& item : items) {
        m.comparisons++;
        if (item.weight <= remaining) {
            remaining -= item.weight;
            res.total_weight += item.weight;
            res.total_value += item.value;
            res.selected_ids.push_back(item.id);
            m.swaps_or_writes++;
        }
    }

    m.extra_memory_bytes = items.size() * sizeof(KnapsackItem);
    m.dominant_op = "Comparações";
    m.dominant_count = m.comparisons;
    m.result_summary = "Valor = " + std::to_string(static_cast<int>(res.total_value + 0.5));
    return res;
}

/**
 * @brief Mochila 0/1 — Programação Dinâmica (Tabela n x W).
 * Complexidade: Theta(n * W) tempo e espaço (ou O(W) com vetor 1D; mantemos matriz para rastreamento).
 */
inline KnapsackResult knapsack_01_dp(const std::vector<KnapsackItem>& items, int capacity, Metrics& m) {
    m.reset();
    KnapsackResult res;
    const std::size_t n = items.size();
    const std::size_t W = static_cast<std::size_t>(std::max(0, capacity));

    std::vector<std::vector<double>> dp(n + 1, std::vector<double>(W + 1, 0.0));

    for (std::size_t i = 1; i <= n; ++i) {
        const auto& item = items[i - 1];
        for (std::size_t w = 0; w <= W; ++w) {
            m.array_accesses++;
            double skip = dp[i - 1][w];
            m.comparisons++;
            if (static_cast<std::size_t>(item.weight) <= w) {
                m.array_accesses++;
                double take = item.value + dp[i - 1][w - static_cast<std::size_t>(item.weight)];
                m.comparisons++;
                dp[i][w] = (take > skip) ? take : skip;
            } else {
                dp[i][w] = skip;
            }
            m.swaps_or_writes++;
        }
    }

    res.total_value = dp[n][W];
    std::size_t w = W;
    for (std::size_t i = n; i >= 1; --i) {
        if (dp[i][w] != dp[i - 1][w]) {
            res.selected_ids.push_back(items[i - 1].id);
            res.total_weight += items[i - 1].weight;
            w -= static_cast<std::size_t>(items[i - 1].weight);
        }
    }
    std::reverse(res.selected_ids.begin(), res.selected_ids.end());

    m.extra_memory_bytes = (n + 1) * (W + 1) * sizeof(double);
    m.dominant_op = "Células DP";
    m.dominant_count = n * (W + 1);
    m.result_summary = "Valor = " + std::to_string(static_cast<int>(res.total_value + 0.5)) + " (ótimo)";
    return res;
}

}  // namespace algolab
