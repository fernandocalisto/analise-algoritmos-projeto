#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace algolab {

enum class InputFormat {
    RANDOM,
    SORTED_ASC,
    SORTED_DESC,
    NEARLY_SORTED,
    MANY_DUPLICATES
};

enum class SearchTargetPos {
    START,
    MIDDLE,
    END,
    ABSENT
};

inline std::string input_format_key(InputFormat fmt) {
    switch (fmt) {
        case InputFormat::RANDOM:          return "random";
        case InputFormat::SORTED_ASC:      return "sorted_asc";
        case InputFormat::SORTED_DESC:     return "sorted_desc";
        case InputFormat::NEARLY_SORTED:   return "nearly_sorted";
        case InputFormat::MANY_DUPLICATES: return "many_duplicates";
    }
    return "random";
}

inline std::string input_format_label(InputFormat fmt) {
    switch (fmt) {
        case InputFormat::RANDOM:          return "Aleatória";
        case InputFormat::SORTED_ASC:      return "Ordenada crescente";
        case InputFormat::SORTED_DESC:     return "Ordenada decrescente";
        case InputFormat::NEARLY_SORTED:   return "Quase ordenada";
        case InputFormat::MANY_DUPLICATES: return "Com valores repetidos";
    }
    return "Aleatória";
}

inline InputFormat parse_input_format(const std::string& key) {
    if (key == "sorted_asc") return InputFormat::SORTED_ASC;
    if (key == "sorted_desc") return InputFormat::SORTED_DESC;
    if (key == "nearly_sorted") return InputFormat::NEARLY_SORTED;
    if (key == "many_duplicates") return InputFormat::MANY_DUPLICATES;
    return InputFormat::RANDOM;
}

inline std::string search_pos_key(SearchTargetPos pos) {
    switch (pos) {
        case SearchTargetPos::START:  return "start";
        case SearchTargetPos::MIDDLE: return "middle";
        case SearchTargetPos::END:    return "end";
        case SearchTargetPos::ABSENT: return "absent";
    }
    return "middle";
}

inline std::string search_pos_label(SearchTargetPos pos) {
    switch (pos) {
        case SearchTargetPos::START:  return "Início (melhor caso seq.)";
        case SearchTargetPos::MIDDLE: return "Meio (melhor caso bin.)";
        case SearchTargetPos::END:    return "Fim (pior caso seq.)";
        case SearchTargetPos::ABSENT: return "Ausente (pior caso geral)";
    }
    return "Meio";
}

inline SearchTargetPos parse_search_pos(const std::string& key) {
    if (key == "start") return SearchTargetPos::START;
    if (key == "end") return SearchTargetPos::END;
    if (key == "absent") return SearchTargetPos::ABSENT;
    return SearchTargetPos::MIDDLE;
}

/**
 * @brief Gera vetores determinísticos nos 5 formatos especificados na Seção 6 do roteiro.
 *
 * Usa semente fixa (padrão 2026) para assegurar reprodutibilidade e comparação justa
 * entre diferentes algoritmos sobre exatamente as mesmas entradas.
 */
inline std::vector<int> generate_array(std::size_t n, InputFormat format, std::uint32_t seed = 2026) {
    std::vector<int> data(n);
    if (n == 0) return data;

    std::mt19937 rng(seed);

    switch (format) {
        case InputFormat::RANDOM: {
            std::uniform_int_distribution<int> dist(1, static_cast<int>(std::max<std::size_t>(n * 10, 100)));
            for (std::size_t i = 0; i < n; ++i) {
                data[i] = dist(rng);
            }
            break;
        }
        case InputFormat::SORTED_ASC: {
            std::iota(data.begin(), data.end(), 1);
            break;
        }
        case InputFormat::SORTED_DESC: {
            for (std::size_t i = 0; i < n; ++i) {
                data[i] = static_cast<int>(n - i);
            }
            break;
        }
        case InputFormat::NEARLY_SORTED: {
            std::iota(data.begin(), data.end(), 1);
            // Troca aproximadamente 2% dos pares para simular vetor quase ordenado.
            std::size_t swaps = std::max<std::size_t>(1, n / 50);
            std::uniform_int_distribution<std::size_t> idx_dist(0, n - 1);
            for (std::size_t s = 0; s < swaps; ++s) {
                std::size_t a = idx_dist(rng);
                std::size_t b = idx_dist(rng);
                std::swap(data[a], data[b]);
            }
            break;
        }
        case InputFormat::MANY_DUPLICATES: {
            // Domínio restrito a 10 valores distintos para forçar alta taxa de duplicidade.
            static constexpr int kValues[10] = {4, 7, 12, 19, 23, 31, 42, 55, 68, 90};
            std::uniform_int_distribution<int> pick(0, 9);
            for (std::size_t i = 0; i < n; ++i) {
                data[i] = kValues[pick(rng)];
            }
            break;
        }
    }
    return data;
}

struct SearchCase {
    std::vector<int> data;
    int target = 0;
    int expected_index = -1;
};

/**
 * @brief Prepara um conjunto ordenado [1..n] e define a chave de busca na posição solicitada.
 */
inline SearchCase generate_search_case(std::size_t n, SearchTargetPos pos) {
    SearchCase sc;
    sc.data.resize(n);
    std::iota(sc.data.begin(), sc.data.end(), 1);
    if (n == 0) {
        sc.target = 1;
        sc.expected_index = -1;
        return sc;
    }

    switch (pos) {
        case SearchTargetPos::START:
            sc.expected_index = 0;
            sc.target = sc.data[0];
            break;
        case SearchTargetPos::MIDDLE: {
            std::size_t mid = (n - 1) / 2;
            sc.expected_index = static_cast<int>(mid);
            sc.target = sc.data[mid];
            break;
        }
        case SearchTargetPos::END:
            sc.expected_index = static_cast<int>(n - 1);
            sc.target = sc.data[n - 1];
            break;
        case SearchTargetPos::ABSENT:
            sc.expected_index = -1;
            sc.target = static_cast<int>(n) + 999;
            break;
    }
    return sc;
}

}  // namespace algolab
