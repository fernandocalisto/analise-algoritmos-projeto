#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "benchmark_runner.hpp"
#include "web_ui_assets.hpp"

namespace fs = std::filesystem;

namespace {

std::mutex g_state_mutex;
std::string g_cached_json;
std::atomic<bool> g_running{true};
std::atomic<bool> g_client_seen{false};
std::atomic<long long> g_last_heartbeat_ms{0};

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string extract_json_string(const std::string& body, const std::string& key, const std::string& def = "") {
    std::string pattern = "\"" + key + "\"";
    std::size_t pos = body.find(pattern);
    if (pos == std::string::npos) return def;
    pos = body.find(':', pos + pattern.size());
    if (pos == std::string::npos) return def;
    std::size_t q1 = body.find('"', pos + 1);
    if (q1 == std::string::npos) return def;
    std::size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) return def;
    return body.substr(q1 + 1, q2 - q1 - 1);
}

std::size_t extract_json_size(const std::string& body, const std::string& key, std::size_t def = 1000) {
    std::string pattern = "\"" + key + "\"";
    std::size_t pos = body.find(pattern);
    if (pos == std::string::npos) return def;
    pos = body.find(':', pos + pattern.size());
    if (pos == std::string::npos) return def;
    std::size_t start = body.find_first_of("0123456789", pos + 1);
    if (start == std::string::npos) return def;
    std::size_t end = body.find_first_not_of("0123456789", start);
    try {
        return static_cast<std::size_t>(std::stoull(body.substr(start, end - start)));
    } catch (...) {
        return def;
    }
}

bool write_text_file(const fs::path& path, const std::string& content) {
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

void ensure_benchmark_cache() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!g_cached_json.empty()) return;

    if (algolab::assets::kEmbeddedBenchmarkJsonLen > 10) {
        g_cached_json.assign(
            reinterpret_cast<const char*>(algolab::assets::kEmbeddedBenchmarkJsonBytes),
            algolab::assets::kEmbeddedBenchmarkJsonLen);
        return;
    }

    auto suite = algolab::run_all_benchmarks(false);
    g_cached_json = algolab::serialize_benchmarks_to_json(suite);
}

#ifdef _WIN32

void send_http_response(SOCKET client, const std::string& status,
                        const std::string& content_type, const char* body, std::size_t len) {
    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << len << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    std::string h = hdr.str();
    send(client, h.data(), static_cast<int>(h.size()), 0);
    std::size_t sent = 0;
    while (sent < len) {
        int chunk = send(client, body + sent, static_cast<int>(std::min<std::size_t>(len - sent, 65536)), 0);
        if (chunk <= 0) break;
        sent += static_cast<std::size_t>(chunk);
    }
}

void handle_client(SOCKET client) {
    std::string raw;
    char buf[4096];
    int received = 0;
    std::size_t header_end = std::string::npos;
    std::size_t content_length = 0;

    while ((received = recv(client, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, static_cast<std::size_t>(received));
        if (header_end == std::string::npos) {
            header_end = raw.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string headers = raw.substr(0, header_end);
                std::size_t cl_pos = headers.find("Content-Length:");
                if (cl_pos == std::string::npos) cl_pos = headers.find("content-length:");
                if (cl_pos != std::string::npos) {
                    std::size_t num_start = headers.find_first_of("0123456789", cl_pos);
                    if (num_start != std::string::npos) {
                        content_length = static_cast<std::size_t>(std::stoull(headers.substr(num_start)));
                    }
                }
            }
        }
        if (header_end != std::string::npos) {
            std::size_t body_len = raw.size() - (header_end + 4);
            if (body_len >= content_length) break;
        }
        if (raw.size() > 1048576) break;
    }

    if (raw.empty()) {
        closesocket(client);
        return;
    }

    std::size_t line_end = raw.find("\r\n");
    std::string req_line = (line_end == std::string::npos) ? raw : raw.substr(0, line_end);
    std::istringstream iss(req_line);
    std::string method, full_path;
    iss >> method >> full_path;

    std::string path = full_path;
    std::size_t qmark = path.find('?');
    if (qmark != std::string::npos) path = path.substr(0, qmark);

    std::string body = (header_end != std::string::npos && raw.size() >= header_end + 4)
                           ? raw.substr(header_end + 4)
                           : "";

    if (method == "GET" && (path == "/" || path == "/index.html")) {
        g_client_seen = true;
        g_last_heartbeat_ms = now_ms();
        send_http_response(client, "200 OK", "text/html; charset=utf-8",
                           reinterpret_cast<const char*>(algolab::assets::kEmbeddedHtmlBytes),
                           algolab::assets::kEmbeddedHtmlLen);
    } else if (method == "GET" && path == "/api/benchmarks") {
        ensure_benchmark_cache();
        std::string payload;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            payload = g_cached_json;
        }
        send_http_response(client, "200 OK", "application/json; charset=utf-8", payload.data(), payload.size());
    } else if (method == "POST" && path == "/api/rebenchmark") {
        auto suite = algolab::run_all_benchmarks(false);
        std::string payload = algolab::serialize_benchmarks_to_json(suite);
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_cached_json = payload;
        }
        send_http_response(client, "200 OK", "application/json; charset=utf-8", payload.data(), payload.size());
    } else if (method == "POST" && path == "/api/run-custom") {
        std::string suite = extract_json_string(body, "suite", "sorting");
        std::string algo = extract_json_string(body, "algo_key", "quick_median3");
        std::string fmt = extract_json_string(body, "format_key", "random");
        std::string extra = extract_json_string(body, "extra_param", "");
        std::size_t n = extract_json_size(body, "n", 1000);
        std::string payload = algolab::run_single_experiment_json(suite, algo, n, fmt, extra);
        send_http_response(client, "200 OK", "application/json; charset=utf-8", payload.data(), payload.size());
    } else if (method == "POST" && path == "/api/heartbeat") {
        g_client_seen = true;
        g_last_heartbeat_ms = now_ms();
        const char ok[] = "{\"ok\":true}";
        send_http_response(client, "200 OK", "application/json", ok, sizeof(ok) - 1);
    } else if (method == "POST" && path == "/api/shutdown") {
        const char ok[] = "{\"closing\":true}";
        send_http_response(client, "200 OK", "application/json", ok, sizeof(ok) - 1);
        g_running = false;
    } else {
        const char not_found[] = "Not Found";
        send_http_response(client, "404 Not Found", "text/plain", not_found, sizeof(not_found) - 1);
    }

    closesocket(client);
}

std::string find_msedge_path() {
    const char* candidates[] = {
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe"
    };
    for (const char* p : candidates) {
        if (fs::exists(p)) return std::string(p);
    }
    return "";
}

bool launch_desktop_window(int port, PROCESS_INFORMATION& out_pi) {
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::string edge = find_msedge_path();

    if (!edge.empty()) {
        char temp_dir[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_dir);
        std::string profile_dir = std::string(temp_dir) + "AlgoLabEdgeProfile";

        std::string cmd = "\"" + edge + "\" --app=" + url +
                          " --window-size=1440,920" +
                          " --user-data-dir=\"" + profile_dir + "\"" +
                          " --no-first-run --no-default-browser-check";

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        ZeroMemory(&out_pi, sizeof(out_pi));
        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        if (CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &out_pi)) {
            return true;
        }
    }

    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return false;
}

int run_server(int requested_port, bool launch_ui, bool auto_exit_on_idle) {
    ensure_benchmark_cache();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Erro ao inicializar WinSock2.\n";
        return 1;
    }

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(requested_port));

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(srv);
        WSACleanup();
        std::cerr << "Falha ao vincular porta local.\n";
        return 1;
    }

    sockaddr_in bound{};
    int bound_len = sizeof(bound);
    getsockname(srv, reinterpret_cast<sockaddr*>(&bound), &bound_len);
    int actual_port = ntohs(bound.sin_port);

    if (listen(srv, 16) == SOCKET_ERROR) {
        closesocket(srv);
        WSACleanup();
        return 1;
    }

    std::cout << "AlgoLab Server ativo em http://127.0.0.1:" << actual_port << "/\n" << std::flush;

    PROCESS_INFORMATION pi{};
    bool has_edge_proc = false;
    if (launch_ui) {
        has_edge_proc = launch_desktop_window(actual_port, pi);
    }

    // Thread de aceitação de conexões
    std::thread accept_thread([&]() {
        while (g_running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(srv, &fds);
            timeval tv{0, 250000};
            int sel = select(0, &fds, nullptr, nullptr, &tv);
            if (sel > 0 && FD_ISSET(srv, &fds)) {
                SOCKET client = accept(srv, nullptr, nullptr);
                if (client != INVALID_SOCKET) {
                    std::thread(handle_client, client).detach();
                }
            }
        }
    });

    const long long start_time = now_ms();
    g_last_heartbeat_ms = start_time;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (auto_exit_on_idle) {
            long long now = now_ms();
            if (g_client_seen) {
                // Encerra após 12s sem heartbeat (permite refresh F5 tranquilo, mas fecha ao fechar janela)
                if (now - g_last_heartbeat_ms.load() > 12000) {
                    g_running = false;
                }
            } else if (now - start_time > 45000) {
                g_running = false;
            }
        }
    }

    closesocket(srv);
    if (accept_thread.joinable()) accept_thread.join();
    if (has_edge_proc) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    WSACleanup();
    return 0;
}

#endif  // _WIN32

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    bool cli_mode = false;
    bool serve_only = false;
    int serve_port = 0;
    std::string json_out_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli") {
            cli_mode = true;
        } else if (arg == "--benchmark-json" && i + 1 < argc) {
            json_out_path = argv[++i];
        } else if (arg == "--serve-only" && i + 1 < argc) {
            serve_only = true;
            serve_port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "AlgoLab C++20 - Projeto Prático de Análise de Algoritmos\n"
                      << "Uso:\n"
                      << "  AlgoLab.exe                      Inicia interface desktop minimalista\n"
                      << "  AlgoLab.exe --cli                Executa experimentos e imprime tabelas\n"
                      << "  AlgoLab.exe --benchmark-json <f> Executa experimentos e salva JSON\n"
                      << "  AlgoLab.exe --serve-only <porta> Inicia servidor HTTP local sem abrir janela\n";
            return 0;
        }
    }

    if (cli_mode || !json_out_path.empty()) {
        auto suite = algolab::run_all_benchmarks(cli_mode);
        if (cli_mode) {
            algolab::print_cli_tables(suite);
        }
        std::string json = algolab::serialize_benchmarks_to_json(suite);
        std::string target_path = json_out_path.empty() ? "data/benchmark_results.json" : json_out_path;
        if (write_text_file(target_path, json)) {
            std::cout << "JSON empírico salvo em: " << target_path << "\n";
        }
        return 0;
    }

#ifdef _WIN32
    if (serve_only) {
        return run_server(serve_port, false, false);
    }
    {
        DWORD procs[2];
        if (GetConsoleProcessList(procs, 2) == 1) {
            HWND hwnd = GetConsoleWindow();
            if (hwnd) ShowWindow(hwnd, SW_HIDE);
        }
    }
    return run_server(0, true, true);
#else
    auto suite = algolab::run_all_benchmarks(true);
    algolab::print_cli_tables(suite);
    return 0;
#endif
}
