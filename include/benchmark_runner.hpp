#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "metrics.hpp"

namespace algolab {

struct ExperimentRecord {
    std::string suite;          // "search", "sorting", "recursion", "greedy_dp"
    std::string problem;        // Nome do problema comparativo
    std::string algorithm;      // Nome legível do algoritmo
    std::string algo_key;       // Identificador estável para filtros/gráficos
    std::size_t n = 0;          // Tamanho da entrada
    std::string input_format;   // Descrição do formato da entrada
    std::string format_key;     // Chave do formato
    double elapsed_us = 0.0;    // Tempo médio em microssegundos
    double elapsed_ms = 0.0;    // Tempo médio em milissegundos
    std::string dominant_op;    // Nome da operação dominante
    std::uint64_t dominant_count = 0;
    std::uint64_t comparisons = 0;
    std::uint64_t swaps_or_writes = 0;
    std::uint64_t array_accesses = 0;
    std::uint64_t recursive_calls = 0;
    std::size_t extra_memory_bytes = 0;
    std::string result_summary;
    std::string observation;
    bool correct = true;
};

struct BenchmarkSuiteResult {
    std::string generated_at;
    std::string compiler_info;
    std::vector<ExperimentRecord> records;
};

/**
 * @brief Executa toda a bateria de experimentos empíricos no motor C++.
 */
BenchmarkSuiteResult run_all_benchmarks(bool print_progress = false, bool fast_interactive = false);

/**
 * @brief Serializa todos os resultados empíricos em JSON estruturado.
 */
std::string serialize_benchmarks_to_json(const BenchmarkSuiteResult& suite);

/**
 * @brief Imprime no terminal tabelas formatadas seguindo o modelo da Seção 7 do PDF.
 */
void print_cli_tables(const BenchmarkSuiteResult& suite);

/**
 * @brief Executa um experimento individual sob demanda (usado pela API HTTP da GUI).
 */
std::string run_single_experiment_json(const std::string& suite,
                                       const std::string& algo_key,
                                       std::size_t n,
                                       const std::string& format_key,
                                       const std::string& extra_param);

/**
 * @brief Retorna dados iniciais e traço simplificado (ou vetor gerado) para o visualizador.
 */
std::string generate_input_preview_json(std::size_t n, const std::string& format_key);

}  // namespace algolab
