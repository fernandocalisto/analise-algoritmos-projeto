#include "benchmark_runner.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "data_generator.hpp"
#include "greedy_dp_algorithms.hpp"
#include "recursion_dc_algorithms.hpp"
#include "search_algorithms.hpp"
#include "sorting_algorithms.hpp"

namespace algolab {

namespace {

volatile std::uint64_t g_benchmark_sink = 0;

std::string json_escape(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b";  break;
            case '\f': oss << "\\f";  break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec;
                } else {
                    oss << static_cast<char>(c);
                }
                break;
        }
    }
    return oss.str();
}

std::string current_iso_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    return std::string(buf);
}

std::string detect_compiler_info() {
    std::ostringstream oss;
#if defined(__GNUC__)
    oss << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << " (C++20 -O3 -static)";
#elif defined(_MSC_VER)
    oss << "MSVC " << _MSC_VER << " (C++20)";
#else
    oss << "C++20";
#endif
    return oss.str();
}

template <typename Fn>
double measure_avg_us(int reps, Fn&& fn) {
    using clock = std::chrono::high_resolution_clock;
    // Aquecimento antes da medição
    fn();
    auto start = clock::now();
    for (int i = 0; i < reps; ++i) {
        fn();
    }
    auto stop = clock::now();
    double total_ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return (total_ns / static_cast<double>(reps)) / 1000.0;
}

template <typename SortFn>
ExperimentRecord benchmark_sort(const std::string& algo_name,
                                const std::string& algo_key,
                                std::size_t n,
                                InputFormat fmt,
                                const std::vector<int>& base,
                                SortFn&& sort_fn,
                                const std::string& obs_note,
                                bool fast_interactive = false) {
    ExperimentRecord rec;
    rec.suite = "sorting";
    rec.problem = "Ordenação de Vetor";
    rec.algorithm = algo_name;
    rec.algo_key = algo_key;
    rec.n = n;
    rec.input_format = input_format_label(fmt);
    rec.format_key = input_format_key(fmt);

    using clock = std::chrono::high_resolution_clock;

    // 1 execução instrumentada para contagem exata das operações primitivas
    Metrics m;
    std::vector<int> instrumented = base;
    auto t0 = clock::now();
    sort_fn(instrumented, m);
    auto t1 = clock::now();
    if (!instrumented.empty()) {
        g_benchmark_sink ^= static_cast<std::uint64_t>(instrumented.front()) ^ static_cast<std::uint64_t>(instrumented.back());
    }
    rec.comparisons = m.comparisons;
    rec.swaps_or_writes = m.swaps_or_writes;
    rec.array_accesses = m.array_accesses;
    rec.recursive_calls = m.recursive_calls;
    rec.extra_memory_bytes = m.extra_memory_bytes;
    rec.dominant_op = m.dominant_op;
    rec.dominant_count = m.dominant_count;
    rec.correct = m.correct;
    rec.result_summary = m.correct ? "Ordenado corretamente" : "ERRO de ordenação";

    if (fast_interactive && n >= 10000) {
        double total_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        rec.elapsed_us = total_ns / 1000.0;
        rec.elapsed_ms = rec.elapsed_us / 1000.0;
        rec.observation = obs_note;
        return rec;
    }

    // Múltiplas execuções com vetores pré-copiados fora do cronômetro
    int reps = 3;
    if (n <= 100) reps = fast_interactive ? 15 : 50;
    else if (n <= 500) reps = fast_interactive ? 8 : 20;
    else if (n <= 1000) reps = fast_interactive ? 5 : 10;
    else if (n <= 5000) reps = fast_interactive ? 2 : 5;
    else if (fast_interactive) reps = 1;

    std::vector<std::vector<int>> copies(static_cast<std::size_t>(reps), base);
    Metrics dummy;
    auto start = clock::now();
    for (int r = 0; r < reps; ++r) {
        sort_fn(copies[static_cast<std::size_t>(r)], dummy);
        g_benchmark_sink ^= static_cast<std::uint64_t>(copies[static_cast<std::size_t>(r)].front());
        g_benchmark_sink ^= static_cast<std::uint64_t>(copies[static_cast<std::size_t>(r)].back());
    }
    auto stop = clock::now();

    double total_ns = std::chrono::duration<double, std::nano>(stop - start).count();
    rec.elapsed_us = (total_ns / static_cast<double>(reps)) / 1000.0;
    rec.elapsed_ms = rec.elapsed_us / 1000.0;
    rec.observation = obs_note;
    return rec;
}

template <typename SearchFn>
ExperimentRecord benchmark_search(const std::string& algo_name,
                                  const std::string& algo_key,
                                  std::size_t n,
                                  SearchTargetPos pos,
                                  const SearchCase& sc,
                                  SearchFn&& search_fn,
                                  bool is_binary,
                                  const std::string& obs_note) {
    ExperimentRecord rec;
    rec.suite = "search";
    rec.problem = "Busca em Vetor Ordenado";
    rec.algorithm = algo_name;
    rec.algo_key = algo_key;
    rec.n = n;
    rec.input_format = search_pos_label(pos);
    rec.format_key = search_pos_key(pos);

    Metrics m;
    int idx = search_fn(sc.data, sc.target, m);
    rec.comparisons = m.comparisons;
    rec.swaps_or_writes = m.swaps_or_writes;
    rec.array_accesses = m.array_accesses;
    rec.recursive_calls = m.recursive_calls;
    rec.extra_memory_bytes = m.extra_memory_bytes;
    rec.dominant_op = m.dominant_op;
    rec.dominant_count = m.dominant_count;
    rec.correct = (idx == sc.expected_index);
    rec.result_summary = m.result_summary;

    int reps = is_binary ? 5000 : (n <= 10000 ? 2000 : (n <= 100000 ? 300 : 50));
    Metrics dummy;
    rec.elapsed_us = measure_avg_us(reps, [&]() {
        int res = search_fn(sc.data, sc.target, dummy);
        g_benchmark_sink ^= static_cast<std::uint64_t>(res + 2);
    });
    rec.elapsed_ms = rec.elapsed_us / 1000.0;
    rec.observation = obs_note;
    return rec;
}

std::vector<KnapsackItem> generate_knapsack_items(std::size_t n, std::uint32_t seed = 2026) {
    std::vector<KnapsackItem> items(n);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> w_dist(2, 25);
    std::uniform_int_distribution<int> v_dist(15, 180);
    for (std::size_t i = 0; i < n; ++i) {
        items[i].id = static_cast<int>(i + 1);
        items[i].weight = w_dist(rng);
        items[i].value = static_cast<double>(v_dist(rng));
    }
    return items;
}

}  // namespace

BenchmarkSuiteResult run_all_benchmarks(bool print_progress, bool fast_interactive) {
    BenchmarkSuiteResult suite;
    suite.generated_at = current_iso_timestamp();
    suite.compiler_info = detect_compiler_info();

    // =========================================================================
    // 1. BUSCA: Sequencial vs Binária Iterativa vs Binária Recursiva
    // =========================================================================
    if (print_progress) std::cout << "[1/4] Executando experimentos de Busca...\n";
    const std::vector<std::size_t> search_sizes = {1000, 10000, 100000, 500000, 1000000};
    const std::vector<SearchTargetPos> search_positions = {
        SearchTargetPos::START, SearchTargetPos::MIDDLE, SearchTargetPos::END, SearchTargetPos::ABSENT};

    for (std::size_t n : search_sizes) {
        for (SearchTargetPos pos : search_positions) {
            SearchCase sc = generate_search_case(n, pos);
            suite.records.push_back(benchmark_search(
                "Busca Sequencial", "seq_search", n, pos, sc, sequential_search, false,
                pos == SearchTargetPos::START ? "O(1) quando chave está no início"
                                              : "Varredura linear proporcional à posição"));
            suite.records.push_back(benchmark_search(
                "Busca Binária (Iterativa)", "bin_search_iter", n, pos, sc, binary_search_iterative, true,
                "Espaço auxiliar O(1); reduz intervalo pela metade"));
            suite.records.push_back(benchmark_search(
                "Busca Binária (Recursiva)", "bin_search_rec", n, pos, sc, binary_search_recursive, true,
                "Pilha recursiva O(log n); sobrecarga de chamadas"));
        }
    }

    // =========================================================================
    // 2. ORDENAÇÃO: Quick Sort (Mediana3 & Pivô Fixo), Merge Sort, Insertion, Bubble
    // =========================================================================
    if (print_progress) std::cout << "[2/4] Executando experimentos de Ordenação...\n";
    const std::vector<std::size_t> sort_sizes = {100, 500, 1000, 2500, 5000, 10000, 25000};
    const std::vector<InputFormat> sort_formats = {
        InputFormat::RANDOM, InputFormat::SORTED_ASC, InputFormat::SORTED_DESC,
        InputFormat::NEARLY_SORTED, InputFormat::MANY_DUPLICATES};

    for (std::size_t n : sort_sizes) {
        for (InputFormat fmt : sort_formats) {
            std::vector<int> base = generate_array(n, fmt);

            suite.records.push_back(benchmark_sort(
                "Quick Sort (Mediana de 3)", "quick_median3", n, fmt, base, quick_sort_median3,
                "Hoare + Mediana de 3 evita degradação O(n^2)", fast_interactive));

            suite.records.push_back(benchmark_sort(
                "Quick Sort (Pivô Fixo)", "quick_fixed", n, fmt, base, quick_sort_fixed_pivot,
                (fmt == InputFormat::SORTED_ASC || fmt == InputFormat::SORTED_DESC ||
                 fmt == InputFormat::MANY_DUPLICATES)
                    ? "Pior caso O(n^2) por partição desbalanceada"
                    : "Caso médio O(n log n) com pivô no fim", fast_interactive));

            suite.records.push_back(benchmark_sort(
                "Merge Sort", "merge_sort", n, fmt, base, merge_sort,
                "Theta(n log n) garantido; requer buffer auxiliar O(n)", fast_interactive));

            suite.records.push_back(benchmark_sort(
                "Insertion Sort", "insertion_sort", n, fmt, base, insertion_sort,
                (fmt == InputFormat::SORTED_ASC || fmt == InputFormat::NEARLY_SORTED)
                    ? "Adaptativo: quase linear O(n) em vetores ordenados"
                    : "Custo quadrático O(n^2) em entradas inversas/aleatórias", fast_interactive));

            suite.records.push_back(benchmark_sort(
                "Bubble Sort Otimizado", "bubble_sort", n, fmt, base, bubble_sort_optimized,
                (fmt == InputFormat::SORTED_ASC)
                    ? "Flag de parada antecipada encerra em O(n)"
                    : "Alto número de trocas e comparações O(n^2)", fast_interactive));
        }
    }

    // =========================================================================
    // 3. ITERATIVO vs RECURSIVO e DIVISÃO E CONQUISTA
    // =========================================================================
    if (print_progress) std::cout << "[3/4] Executando experimentos Iterativo vs Recursivo & D&C...\n";
    const std::vector<int> fib_sizes = {10, 20, 30, 35, 40};
    for (int n : fib_sizes) {
        {
            Metrics m;
            std::uint64_t val = fibonacci_iterative(n, m);
            double us = measure_avg_us(5000, [&]() {
                Metrics d;
                g_benchmark_sink ^= fibonacci_iterative(n, d);
            });
            ExperimentRecord r;
            r.suite = "recursion";
            r.problem = "Fibonacci (Iterativo vs Recursivo vs PD)";
            r.algorithm = "Fibonacci Iterativo";
            r.algo_key = "fib_iter";
            r.n = static_cast<std::size_t>(n);
            r.input_format = "Escalar n";
            r.format_key = "scalar";
            r.elapsed_us = us;
            r.elapsed_ms = us / 1000.0;
            r.dominant_op = m.dominant_op;
            r.dominant_count = m.dominant_count;
            r.comparisons = m.comparisons;
            r.swaps_or_writes = m.swaps_or_writes;
            r.recursive_calls = 0;
            r.extra_memory_bytes = m.extra_memory_bytes;
            r.result_summary = "F(" + std::to_string(n) + ") = " + std::to_string(val);
            r.observation = "Tempo O(n), espaço O(1) sem sobrecarga de pilha";
            suite.records.push_back(r);
        }
        {
            Metrics m;
            std::uint64_t val = fibonacci_dp_memo(n, m);
            double us = measure_avg_us(1000, [&]() {
                Metrics d;
                g_benchmark_sink ^= fibonacci_dp_memo(n, d);
            });
            ExperimentRecord r;
            r.suite = "recursion";
            r.problem = "Fibonacci (Iterativo vs Recursivo vs PD)";
            r.algorithm = "Fibonacci PD (Memoização)";
            r.algo_key = "fib_dp";
            r.n = static_cast<std::size_t>(n);
            r.input_format = "Escalar n";
            r.format_key = "scalar";
            r.elapsed_us = us;
            r.elapsed_ms = us / 1000.0;
            r.dominant_op = m.dominant_op;
            r.dominant_count = m.dominant_count;
            r.comparisons = m.comparisons;
            r.swaps_or_writes = m.swaps_or_writes;
            r.recursive_calls = m.recursive_calls;
            r.extra_memory_bytes = m.extra_memory_bytes;
            r.result_summary = "F(" + std::to_string(n) + ") = " + std::to_string(val);
            r.observation = "Elimina subproblemas repetidos: O(n) tempo e O(n) espaço";
            suite.records.push_back(r);
        }
        {
            Metrics m;
            auto f0 = std::chrono::high_resolution_clock::now();
            std::uint64_t val = fibonacci_recursive(n, m);
            auto f1 = std::chrono::high_resolution_clock::now();
            g_benchmark_sink ^= val;
            double us = 0.0;
            if (fast_interactive && n >= 35) {
                us = std::chrono::duration<double, std::nano>(f1 - f0).count() / 1000.0;
            } else {
                int reps = (n <= 20) ? 200 : (n <= 30 ? 20 : (n <= 35 ? 3 : 1));
                us = measure_avg_us(reps, [&]() {
                    Metrics d;
                    g_benchmark_sink ^= fibonacci_recursive(n, d);
                });
            }
            ExperimentRecord r;
            r.suite = "recursion";
            r.problem = "Fibonacci (Iterativo vs Recursivo vs PD)";
            r.algorithm = "Fibonacci Recursivo Ingênuo";
            r.algo_key = "fib_rec";
            r.n = static_cast<std::size_t>(n);
            r.input_format = "Escalar n";
            r.format_key = "scalar";
            r.elapsed_us = us;
            r.elapsed_ms = us / 1000.0;
            r.dominant_op = m.dominant_op;
            r.dominant_count = m.dominant_count;
            r.comparisons = m.comparisons;
            r.swaps_or_writes = m.swaps_or_writes;
            r.recursive_calls = m.recursive_calls;
            r.extra_memory_bytes = m.extra_memory_bytes;
            r.result_summary = "F(" + std::to_string(n) + ") = " + std::to_string(val);
            r.observation = "Explosão exponencial O(phi^n) por recomputação";
            suite.records.push_back(r);
        }
    }

    const std::vector<int> pow_exps = {100, 1000, 10000, 100000, 1000000};
    const double base_val = 1.000001;
    for (int exp : pow_exps) {
        {
            Metrics m;
            double val = power_iterative(base_val, exp, m);
            int reps = (exp <= 10000) ? 1000 : 100;
            double us = measure_avg_us(reps, [&]() {
                Metrics d;
                double v = power_iterative(base_val, exp, d);
                g_benchmark_sink ^= static_cast<std::uint64_t>(v * 1000.0);
            });
            ExperimentRecord r;
            r.suite = "recursion";
            r.problem = "Exponenciação (Iterativa vs Divisão e Conquista)";
            r.algorithm = "Exponenciação Iterativa";
            r.algo_key = "pow_iter";
            r.n = static_cast<std::size_t>(exp);
            r.input_format = "Expoente n";
            r.format_key = "exponent";
            r.elapsed_us = us;
            r.elapsed_ms = us / 1000.0;
            r.dominant_op = m.dominant_op;
            r.dominant_count = m.dominant_count;
            r.comparisons = m.comparisons;
            r.swaps_or_writes = m.swaps_or_writes;
            r.extra_memory_bytes = m.extra_memory_bytes;
            r.result_summary = "1.000001^n = " + std::to_string(val);
            r.observation = "Crescimento linear Theta(n) multiplicações";
            suite.records.push_back(r);
        }
        {
            Metrics m;
            double val = power_divide_and_conquer(base_val, exp, m);
            double us = measure_avg_us(5000, [&]() {
                Metrics d;
                double v = power_divide_and_conquer(base_val, exp, d);
                g_benchmark_sink ^= static_cast<std::uint64_t>(v * 1000.0);
            });
            ExperimentRecord r;
            r.suite = "recursion";
            r.problem = "Exponenciação (Iterativa vs Divisão e Conquista)";
            r.algorithm = "Exponenciação Rápida (D&C)";
            r.algo_key = "pow_dc";
            r.n = static_cast<std::size_t>(exp);
            r.input_format = "Expoente n";
            r.format_key = "exponent";
            r.elapsed_us = us;
            r.elapsed_ms = us / 1000.0;
            r.dominant_op = m.dominant_op;
            r.dominant_count = m.dominant_count;
            r.comparisons = m.comparisons;
            r.swaps_or_writes = m.swaps_or_writes;
            r.recursive_calls = m.recursive_calls;
            r.extra_memory_bytes = m.extra_memory_bytes;
            r.result_summary = "1.000001^n = " + std::to_string(val);
            r.observation = "Divisão e conquista reduz custo para Theta(log n)";
            suite.records.push_back(r);
        }
    }

    // =========================================================================
    // 4. ALGORITMOS GULOSOS vs PROGRAMAÇÃO DINÂMICA
    // =========================================================================
    if (print_progress) std::cout << "[4/4] Executando experimentos Guloso vs Programação Dinâmica...\n";

    // 4a. Problema do Troco — Sistema Canônico BRL e Contraexemplo Não-Canônico
    struct CoinTestSpec {
        std::vector<int> coins;
        std::string label;
        std::string key;
        std::vector<int> amounts;
    };
    const std::vector<CoinTestSpec> coin_specs = {
        {{1, 5, 10, 25, 50, 100}, "Sistema Canônico BRL [1,5,10,25,50,100]", "brl", {60, 250, 1000, 5000}},
        {{1, 3, 4}, "Contraexemplo Não-Canônico [1,3,4]", "non_canonical", {6, 10, 18, 60}}
    };

    for (const auto& spec : coin_specs) {
        for (int amount : spec.amounts) {
            Metrics mg, mdp;
            CoinChangeResult rg = coin_change_greedy(spec.coins, amount, mg);
            CoinChangeResult rdp = coin_change_dp(spec.coins, amount, mdp);

            double us_g = measure_avg_us(2000, [&]() {
                Metrics d;
                auto res = coin_change_greedy(spec.coins, amount, d);
                g_benchmark_sink ^= static_cast<std::uint64_t>(res.total_coins);
            });
            double us_dp = measure_avg_us(amount <= 1000 ? 1000 : 200, [&]() {
                Metrics d;
                auto res = coin_change_dp(spec.coins, amount, d);
                g_benchmark_sink ^= static_cast<std::uint64_t>(res.total_coins);
            });

            bool greedy_optimal = (rg.total_coins == rdp.total_coins);

            ExperimentRecord rec_g;
            rec_g.suite = "greedy_dp";
            rec_g.problem = "Problema do Troco";
            rec_g.algorithm = "Troco Guloso";
            rec_g.algo_key = "coin_greedy";
            rec_g.n = static_cast<std::size_t>(amount);
            rec_g.input_format = spec.label;
            rec_g.format_key = spec.key;
            rec_g.elapsed_us = us_g;
            rec_g.elapsed_ms = us_g / 1000.0;
            rec_g.dominant_op = mg.dominant_op;
            rec_g.dominant_count = mg.dominant_count;
            rec_g.comparisons = mg.comparisons;
            rec_g.swaps_or_writes = mg.swaps_or_writes;
            rec_g.extra_memory_bytes = mg.extra_memory_bytes;
            rec_g.correct = greedy_optimal;
            rec_g.result_summary = std::to_string(rg.total_coins) + " moedas" +
                                   (greedy_optimal ? " (ótimo)" : " (SUBÓTIMO!)");
            rec_g.observation = greedy_optimal
                                    ? "Sistema canônico permite decisão local ótima em O(k log k)"
                                    : "Falha gulosa: ótimo exige " + std::to_string(rdp.total_coins) +
                                          " moedas vs " + std::to_string(rg.total_coins) + " do guloso";
            suite.records.push_back(rec_g);

            ExperimentRecord rec_dp;
            rec_dp.suite = "greedy_dp";
            rec_dp.problem = "Problema do Troco";
            rec_dp.algorithm = "Troco Programação Dinâmica";
            rec_dp.algo_key = "coin_dp";
            rec_dp.n = static_cast<std::size_t>(amount);
            rec_dp.input_format = spec.label;
            rec_dp.format_key = spec.key;
            rec_dp.elapsed_us = us_dp;
            rec_dp.elapsed_ms = us_dp / 1000.0;
            rec_dp.dominant_op = mdp.dominant_op;
            rec_dp.dominant_count = mdp.dominant_count;
            rec_dp.comparisons = mdp.comparisons;
            rec_dp.swaps_or_writes = mdp.swaps_or_writes;
            rec_dp.array_accesses = mdp.array_accesses;
            rec_dp.extra_memory_bytes = mdp.extra_memory_bytes;
            rec_dp.correct = true;
            rec_dp.result_summary = std::to_string(rdp.total_coins) + " moedas (ótimo global)";
            rec_dp.observation = "Sempre encontra o mínimo ótimo em Theta(A * k) com memória Theta(A)";
            suite.records.push_back(rec_dp);
        }
    }

    // 4b. Problema da Mochila: Fracionária (Guloso), 0/1 Heurística Gulosa vs 0/1 Prog. Dinâmica
    {
        // Contraexemplo clássico de 3 itens onde a heurística gulosa 0/1 perde para a PD 0/1
        std::vector<KnapsackItem> counter_items = {{1, 10, 60.0}, {2, 20, 100.0}, {3, 30, 120.0}};
        int cap = 50;
        Metrics mf, mg, mdp;
        auto rf = knapsack_fractional_greedy(counter_items, cap, mf);
        auto rg = knapsack_01_greedy_heuristic(counter_items, cap, mg);
        auto rdp = knapsack_01_dp(counter_items, cap, mdp);

        double us_f = measure_avg_us(3000, [&]() {
            Metrics d;
            g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_fractional_greedy(counter_items, cap, d).total_value);
        });
        double us_g = measure_avg_us(3000, [&]() {
            Metrics d;
            g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_01_greedy_heuristic(counter_items, cap, d).total_value);
        });
        double us_dp = measure_avg_us(2000, [&]() {
            Metrics d;
            g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_01_dp(counter_items, cap, d).total_value);
        });

        ExperimentRecord rf_rec{"greedy_dp", "Problema da Mochila", "Mochila Fracionária (Guloso)",
                                "knap_frac", 3, "Contraexemplo clássico (W=50)", "counterexample",
                                us_f, us_f / 1000.0, mf.dominant_op, mf.dominant_count, mf.comparisons,
                                mf.swaps_or_writes, mf.array_accesses, 0, mf.extra_memory_bytes,
                                "Valor = " + std::to_string(static_cast<int>(rf.total_value)),
                                "Permite fracionar item 3; ótimo exato para versão fracionária", true};
        ExperimentRecord rg_rec{"greedy_dp", "Problema da Mochila", "Mochila 0/1 (Heurística Gulosa)",
                                "knap_01_greedy", 3, "Contraexemplo clássico (W=50)", "counterexample",
                                us_g, us_g / 1000.0, mg.dominant_op, mg.dominant_count, mg.comparisons,
                                mg.swaps_or_writes, mg.array_accesses, 0, mg.extra_memory_bytes,
                                "Valor = " + std::to_string(static_cast<int>(rg.total_value)) + " (SUBÓTIMO)",
                                "Escolhe itens 1+2 (valor 160), deixando 20 de capacidade ociosa", false};
        ExperimentRecord rdp_rec{"greedy_dp", "Problema da Mochila", "Mochila 0/1 (Prog. Dinâmica)",
                                 "knap_01_dp", 3, "Contraexemplo clássico (W=50)", "counterexample",
                                 us_dp, us_dp / 1000.0, mdp.dominant_op, mdp.dominant_count, mdp.comparisons,
                                 mdp.swaps_or_writes, mdp.array_accesses, 0, mdp.extra_memory_bytes,
                                 "Valor = " + std::to_string(static_cast<int>(rdp.total_value)) + " (ótimo)",
                                 "Combina itens 2+3 (valor 220), maximizando a ocupação da capacidade", true};
        suite.records.push_back(rf_rec);
        suite.records.push_back(rg_rec);
        suite.records.push_back(rdp_rec);
    }

    const std::vector<std::size_t> knap_sizes = {10, 25, 50, 100, 200};
    for (std::size_t n : knap_sizes) {
        int cap = static_cast<int>(n * 5);
        std::vector<KnapsackItem> items = generate_knapsack_items(n);

        Metrics mf, mg, mdp;
        auto rf = knapsack_fractional_greedy(items, cap, mf);
        auto rg = knapsack_01_greedy_heuristic(items, cap, mg);
        auto rdp = knapsack_01_dp(items, cap, mdp);

        double us_f = measure_avg_us(1000, [&]() {
            Metrics d;
            g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_fractional_greedy(items, cap, d).total_value);
        });
        double us_g = measure_avg_us(1000, [&]() {
            Metrics d;
            g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_01_greedy_heuristic(items, cap, d).total_value);
        });
        double us_dp = measure_avg_us(n <= 50 ? 200 : 30, [&]() {
            Metrics d;
            g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_01_dp(items, cap, d).total_value);
        });

        bool heuristic_is_opt = (static_cast<int>(rg.total_value) == static_cast<int>(rdp.total_value));
        std::string fmt_desc = "Itens aleatórios (W=" + std::to_string(cap) + ")";

        suite.records.push_back({"greedy_dp", "Problema da Mochila", "Mochila Fracionária (Guloso)",
                                 "knap_frac", n, fmt_desc, "scaled", us_f, us_f / 1000.0,
                                 mf.dominant_op, mf.dominant_count, mf.comparisons, mf.swaps_or_writes,
                                 mf.array_accesses, 0, mf.extra_memory_bytes,
                                 "Valor = " + std::to_string(static_cast<int>(rf.total_value)),
                                 "Ordenação O(n log n); limite superior contínuo", true});

        suite.records.push_back({"greedy_dp", "Problema da Mochila", "Mochila 0/1 (Heurística Gulosa)",
                                 "knap_01_greedy", n, fmt_desc, "scaled", us_g, us_g / 1000.0,
                                 mg.dominant_op, mg.dominant_count, mg.comparisons, mg.swaps_or_writes,
                                 mg.array_accesses, 0, mg.extra_memory_bytes,
                                 "Valor = " + std::to_string(static_cast<int>(rg.total_value)) +
                                     (heuristic_is_opt ? " (ótimo)" : " (subótimo)"),
                                 "Rápida O(n log n), sem garantia de otimalidade em 0/1", heuristic_is_opt});

        suite.records.push_back({"greedy_dp", "Problema da Mochila", "Mochila 0/1 (Prog. Dinâmica)",
                                 "knap_01_dp", n, fmt_desc, "scaled", us_dp, us_dp / 1000.0,
                                 mdp.dominant_op, mdp.dominant_count, mdp.comparisons, mdp.swaps_or_writes,
                                 mdp.array_accesses, 0, mdp.extra_memory_bytes,
                                 "Valor = " + std::to_string(static_cast<int>(rdp.total_value)) + " (ótimo)",
                                 "Matriz n x W garante otimalidade; custo pseudo-polinomial", true});
    }

    return suite;
}

std::string serialize_benchmarks_to_json(const BenchmarkSuiteResult& suite) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{\n";
    oss << "  \"generated_at\": \"" << json_escape(suite.generated_at) << "\",\n";
    oss << "  \"compiler_info\": \"" << json_escape(suite.compiler_info) << "\",\n";
    oss << "  \"total_records\": " << suite.records.size() << ",\n";
    oss << "  \"records\": [\n";
    for (std::size_t i = 0; i < suite.records.size(); ++i) {
        const auto& r = suite.records[i];
        oss << "    {"
            << "\"suite\":\"" << json_escape(r.suite) << "\","
            << "\"problem\":\"" << json_escape(r.problem) << "\","
            << "\"algorithm\":\"" << json_escape(r.algorithm) << "\","
            << "\"algo_key\":\"" << json_escape(r.algo_key) << "\","
            << "\"n\":" << r.n << ","
            << "\"input_format\":\"" << json_escape(r.input_format) << "\","
            << "\"format_key\":\"" << json_escape(r.format_key) << "\","
            << "\"elapsed_us\":" << r.elapsed_us << ","
            << "\"elapsed_ms\":" << r.elapsed_ms << ","
            << "\"dominant_op\":\"" << json_escape(r.dominant_op) << "\","
            << "\"dominant_count\":" << r.dominant_count << ","
            << "\"comparisons\":" << r.comparisons << ","
            << "\"swaps_or_writes\":" << r.swaps_or_writes << ","
            << "\"array_accesses\":" << r.array_accesses << ","
            << "\"recursive_calls\":" << r.recursive_calls << ","
            << "\"extra_memory_bytes\":" << r.extra_memory_bytes << ","
            << "\"result_summary\":\"" << json_escape(r.result_summary) << "\","
            << "\"observation\":\"" << json_escape(r.observation) << "\","
            << "\"correct\":" << (r.correct ? "true" : "false")
            << "}";
        if (i + 1 < suite.records.size()) oss << ",";
        oss << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

void print_cli_tables(const BenchmarkSuiteResult& suite) {
    std::cout << "\n=========================================================================================================\n";
    std::cout << "  ALGOLAB C++20 - RESULTADOS EMPIRICOS DO PROJETO PRATICO DE ANALISE DE ALGORITMOS\n";
    std::cout << "  Compilador: " << suite.compiler_info << " | Gerado em: " << suite.generated_at << "\n";
    std::cout << "=========================================================================================================\n";

    auto print_section = [&](const std::string& title, const std::string& target_suite, std::size_t max_rows) {
        std::cout << "\n--- " << title << " ---\n";
        std::cout << std::left << std::setw(32) << "Algoritmo"
                  << std::right << std::setw(10) << "n" << "  "
                  << std::left << std::setw(28) << "Formato da entrada"
                  << std::right << std::setw(12) << "Tempo (us)" << "  "
                  << std::left << std::setw(20) << "Operacao dominante"
                  << std::right << std::setw(14) << "Quantidade" << "\n";
        std::cout << std::string(118, '-') << "\n";

        std::size_t shown = 0;
        for (const auto& r : suite.records) {
            if (r.suite != target_suite) continue;
            if (max_rows > 0 && shown >= max_rows) break;
            std::cout << std::left << std::setw(32) << r.algorithm.substr(0, 31)
                      << std::right << std::setw(10) << r.n << "  "
                      << std::left << std::setw(28) << r.input_format.substr(0, 27)
                      << std::right << std::setw(12) << std::fixed << std::setprecision(2) << r.elapsed_us << "  "
                      << std::left << std::setw(20) << r.dominant_op.substr(0, 19)
                      << std::right << std::setw(14) << r.dominant_count << "\n";
            ++shown;
        }
    };

    print_section("1. BUSCA (SEQUENCIAL vs BINARIA ITERATIVA/RECURSIVA)", "search", 24);
    print_section("2. ORDENACAO (QUICK, MERGE, INSERTION, BUBBLE)", "sorting", 35);
    print_section("3. ITERATIVO vs RECURSIVO & DIVISAO E CONQUISTA", "recursion", 30);
    print_section("4. GULOSO vs PROGRAMACAO DINAMICA (TROCO E MOCHILA)", "greedy_dp", 40);
    std::cout << "\nTotal de experimentos executados: " << suite.records.size() << "\n";
}

std::string run_single_experiment_json(const std::string& suite,
                                       const std::string& algo_key,
                                       std::size_t n,
                                       const std::string& format_key,
                                       const std::string& extra_param) {
    if (n == 0) n = 100;
    Metrics m;
    double elapsed_us = 0.0;
    std::string algo_name = algo_key;
    std::string fmt_label = format_key;
    std::string extra_json = "{}";

    if (suite == "sorting") {
        if (n > 50000) n = 50000;
        InputFormat fmt = parse_input_format(format_key);
        fmt_label = input_format_label(fmt);
        std::vector<int> base = generate_array(n, fmt);

        auto run_sort = [&](auto fn) {
            std::vector<int> copy1 = base;
            fn(copy1, m);
            int reps = (n <= 1000) ? 10 : (n <= 5000 ? 5 : 2);
            std::vector<std::vector<int>> copies(static_cast<std::size_t>(reps), base);
            Metrics dummy;
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int r = 0; r < reps; ++r) {
                fn(copies[static_cast<std::size_t>(r)], dummy);
                g_benchmark_sink ^= static_cast<std::uint64_t>(copies[static_cast<std::size_t>(r)].front());
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            elapsed_us = (std::chrono::duration<double, std::nano>(t1 - t0).count() / reps) / 1000.0;
        };

        if (algo_key == "quick_median3") {
            algo_name = "Quick Sort (Mediana de 3)";
            run_sort(quick_sort_median3);
        } else if (algo_key == "quick_fixed") {
            algo_name = "Quick Sort (Pivô Fixo)";
            run_sort(quick_sort_fixed_pivot);
        } else if (algo_key == "merge_sort") {
            algo_name = "Merge Sort";
            run_sort(merge_sort);
        } else if (algo_key == "insertion_sort") {
            algo_name = "Insertion Sort";
            run_sort(insertion_sort);
        } else {
            algo_name = "Bubble Sort Otimizado";
            run_sort(bubble_sort_optimized);
        }
    } else if (suite == "search") {
        if (n > 2000000) n = 2000000;
        SearchTargetPos pos = parse_search_pos(format_key);
        fmt_label = search_pos_label(pos);
        SearchCase sc = generate_search_case(n, pos);
        int found_idx = -1;

        auto run_s = [&](auto fn, int reps) {
            found_idx = fn(sc.data, sc.target, m);
            Metrics dummy;
            elapsed_us = measure_avg_us(reps, [&]() {
                g_benchmark_sink ^= static_cast<std::uint64_t>(fn(sc.data, sc.target, dummy) + 2);
            });
        };

        if (algo_key == "seq_search") {
            algo_name = "Busca Sequencial";
            run_s(sequential_search, n <= 50000 ? 500 : 50);
        } else if (algo_key == "bin_search_rec") {
            algo_name = "Busca Binária (Recursiva)";
            run_s(binary_search_recursive, 3000);
        } else {
            algo_name = "Busca Binária (Iterativa)";
            run_s(binary_search_iterative, 3000);
        }
        m.correct = (found_idx == sc.expected_index);
    } else if (suite == "recursion") {
        if (algo_key == "fib_rec" || algo_key == "fib_iter" || algo_key == "fib_dp") {
            int fn_n = static_cast<int>(std::min<std::size_t>(n, algo_key == "fib_rec" ? 42 : 90));
            fmt_label = "n = " + std::to_string(fn_n);
            if (algo_key == "fib_rec") {
                algo_name = "Fibonacci Recursivo Ingênuo";
                elapsed_us = measure_avg_us(fn_n <= 28 ? 20 : 1, [&]() {
                    g_benchmark_sink ^= fibonacci_recursive(fn_n, m);
                });
            } else if (algo_key == "fib_dp") {
                algo_name = "Fibonacci PD (Memoização)";
                elapsed_us = measure_avg_us(500, [&]() {
                    g_benchmark_sink ^= fibonacci_dp_memo(fn_n, m);
                });
            } else {
                algo_name = "Fibonacci Iterativo";
                elapsed_us = measure_avg_us(2000, [&]() {
                    g_benchmark_sink ^= fibonacci_iterative(fn_n, m);
                });
            }
        } else {
            int exp_n = static_cast<int>(std::min<std::size_t>(n, 2000000));
            fmt_label = "Expoente = " + std::to_string(exp_n);
            if (algo_key == "pow_dc") {
                algo_name = "Exponenciação Rápida (D&C)";
                elapsed_us = measure_avg_us(2000, [&]() {
                    g_benchmark_sink ^= static_cast<std::uint64_t>(power_divide_and_conquer(1.000001, exp_n, m) * 100);
                });
            } else {
                algo_name = "Exponenciação Iterativa";
                elapsed_us = measure_avg_us(exp_n <= 10000 ? 500 : 50, [&]() {
                    g_benchmark_sink ^= static_cast<std::uint64_t>(power_iterative(1.000001, exp_n, m) * 100);
                });
            }
        }
    } else if (suite == "greedy_dp") {
        if (algo_key == "coin_greedy" || algo_key == "coin_dp") {
            int amount = static_cast<int>(std::min<std::size_t>(n, 50000));
            std::vector<int> coins = (extra_param == "non_canonical" || format_key == "non_canonical")
                                         ? std::vector<int>{1, 3, 4}
                                         : std::vector<int>{1, 5, 10, 25, 50, 100};
            fmt_label = (coins.size() == 3) ? "Não-Canônico [1,3,4]" : "Canônico BRL [1,5,10,25,50,100]";
            if (algo_key == "coin_greedy") {
                algo_name = "Troco Guloso";
                auto res = coin_change_greedy(coins, amount, m);
                elapsed_us = measure_avg_us(1000, [&]() {
                    Metrics d;
                    g_benchmark_sink ^= static_cast<std::uint64_t>(coin_change_greedy(coins, amount, d).total_coins);
                });
                std::ostringstream cs;
                cs << "[";
                for (std::size_t i = 0; i < res.chosen_coins.size() && i < 20; ++i) {
                    if (i) cs << ",";
                    cs << res.chosen_coins[i];
                }
                if (res.chosen_coins.size() > 20) cs << ",\"...\"";
                cs << "]";
                extra_json = "{\"chosen_coins\":" + cs.str() + ",\"total_coins\":" + std::to_string(res.total_coins) + "}";
            } else {
                algo_name = "Troco Programação Dinâmica";
                auto res = coin_change_dp(coins, amount, m);
                elapsed_us = measure_avg_us(amount <= 1000 ? 300 : 50, [&]() {
                    Metrics d;
                    g_benchmark_sink ^= static_cast<std::uint64_t>(coin_change_dp(coins, amount, d).total_coins);
                });
                std::ostringstream cs;
                cs << "[";
                for (std::size_t i = 0; i < res.chosen_coins.size() && i < 20; ++i) {
                    if (i) cs << ",";
                    cs << res.chosen_coins[i];
                }
                if (res.chosen_coins.size() > 20) cs << ",\"...\"";
                cs << "]";
                extra_json = "{\"chosen_coins\":" + cs.str() + ",\"total_coins\":" + std::to_string(res.total_coins) + "}";
            }
        } else {
            std::size_t knap_n = std::min<std::size_t>(n, 300);
            int cap = static_cast<int>(knap_n * 5);
            std::vector<KnapsackItem> items = generate_knapsack_items(knap_n);
            fmt_label = "Capacidade W=" + std::to_string(cap);
            if (algo_key == "knap_frac") {
                algo_name = "Mochila Fracionária (Guloso)";
                auto res = knapsack_fractional_greedy(items, cap, m);
                elapsed_us = measure_avg_us(500, [&]() {
                    Metrics d;
                    g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_fractional_greedy(items, cap, d).total_value);
                });
                extra_json = "{\"total_value\":" + std::to_string(res.total_value) + "}";
            } else if (algo_key == "knap_01_greedy") {
                algo_name = "Mochila 0/1 (Heurística Gulosa)";
                auto res = knapsack_01_greedy_heuristic(items, cap, m);
                elapsed_us = measure_avg_us(500, [&]() {
                    Metrics d;
                    g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_01_greedy_heuristic(items, cap, d).total_value);
                });
                extra_json = "{\"total_value\":" + std::to_string(res.total_value) + "}";
            } else {
                algo_name = "Mochila 0/1 (Prog. Dinâmica)";
                auto res = knapsack_01_dp(items, cap, m);
                elapsed_us = measure_avg_us(50, [&]() {
                    Metrics d;
                    g_benchmark_sink ^= static_cast<std::uint64_t>(knapsack_01_dp(items, cap, d).total_value);
                });
                extra_json = "{\"total_value\":" + std::to_string(res.total_value) + "}";
            }
        }
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4)
        << "{"
        << "\"suite\":\"" << json_escape(suite) << "\","
        << "\"algorithm\":\"" << json_escape(algo_name) << "\","
        << "\"algo_key\":\"" << json_escape(algo_key) << "\","
        << "\"n\":" << n << ","
        << "\"input_format\":\"" << json_escape(fmt_label) << "\","
        << "\"elapsed_us\":" << elapsed_us << ","
        << "\"elapsed_ms\":" << (elapsed_us / 1000.0) << ","
        << "\"dominant_op\":\"" << json_escape(m.dominant_op) << "\","
        << "\"dominant_count\":" << m.dominant_count << ","
        << "\"comparisons\":" << m.comparisons << ","
        << "\"swaps_or_writes\":" << m.swaps_or_writes << ","
        << "\"array_accesses\":" << m.array_accesses << ","
        << "\"recursive_calls\":" << m.recursive_calls << ","
        << "\"extra_memory_bytes\":" << m.extra_memory_bytes << ","
        << "\"result_summary\":\"" << json_escape(m.result_summary) << "\","
        << "\"correct\":" << (m.correct ? "true" : "false") << ","
        << "\"extra\":" << extra_json
        << "}";
    return oss.str();
}

std::string generate_input_preview_json(std::size_t n, const std::string& format_key) {
    if (n > 120) n = 120;
    if (n < 4) n = 4;
    InputFormat fmt = parse_input_format(format_key);
    std::vector<int> arr = generate_array(n, fmt);

    std::ostringstream oss;
    oss << "{\"n\":" << n << ",\"format\":\"" << json_escape(input_format_label(fmt)) << "\",\"values\":[";
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (i) oss << ",";
        oss << arr[i];
    }
    oss << "]}";
    return oss.str();
}

}  // namespace algolab
