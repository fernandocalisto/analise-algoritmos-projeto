from pathlib import Path


def format_c_array(name: str, data: bytes) -> str:
    if not data:
        return (
            f"inline constexpr unsigned char {name}[] = {{0x00}};\n"
            f"inline constexpr std::size_t {name[:-5]}Len = 0;\n"
        )
    lines = []
    for i in range(0, len(data), 24):
        chunk = data[i : i + 24]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk))
    body = ",\n".join(lines)
    return (
        f"inline constexpr unsigned char {name}[] = {{\n{body}\n}};\n"
        f"inline constexpr std::size_t {name[:-5]}Len = {len(data)};\n"
    )


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    html_path = root / "src" / "ui" / "index.html"
    json_path = root / "data" / "benchmark_results.json"
    out_path = root / "include" / "web_ui_assets.hpp"

    html_text = html_path.read_text(encoding="utf-8")
    json_bytes = b""
    if json_path.exists():
        json_text = json_path.read_text(encoding="utf-8").strip()
        json_bytes = json_text.encode("utf-8")
        html_text = html_text.replace("__EMBEDDED_BENCHMARK_JSON__", json_text.replace("</", "<\\/"))

    html_bytes = html_text.encode("utf-8")

    content = (
        "#pragma once\n\n"
        "#include <cstddef>\n\n"
        "namespace algolab::assets {\n\n"
        + format_c_array("kEmbeddedHtmlBytes", html_bytes)
        + "\n"
        + format_c_array("kEmbeddedBenchmarkJsonBytes", json_bytes)
        + "\n}  // namespace algolab::assets\n"
    )
    out_path.write_text(content, encoding="utf-8")
    print(f"Generated {out_path} (HTML: {len(html_bytes)} bytes, JSON: {len(json_bytes)} bytes)")


if __name__ == "__main__":
    main()
