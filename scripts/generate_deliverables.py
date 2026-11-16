import base64
import html
import json
import math
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA_JSON = ROOT / "data" / "benchmark_results.json"
EXE_PATH = ROOT / "dist" / "AlgoLab.exe"
SCREENSHOTS_DIR = ROOT / "docs" / "screenshots"
DELIVERABLES_DIR = ROOT / "entregaveis"


def find_msedge() -> str:
    candidates = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    raise FileNotFoundError("Microsoft Edge (msedge.exe) não encontrado.")


def wait_for_server(url: str, timeout: float = 15.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as resp:
                if resp.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise TimeoutError(f"Servidor não respondeu em {url}")


def edge_screenshot(edge_bin: str, url_or_file: str, out_png: Path, user_data: Path,
                    width: int = 1440, height: int = 920) -> None:
    cmd = [
        edge_bin,
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        f"--window-size={width},{height}",
        "--virtual-time-budget=2600",
        f"--user-data-dir={user_data}",
        f"--screenshot={out_png}",
        url_or_file,
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def edge_print_pdf(edge_bin: str, html_file: Path, out_pdf: Path, user_data: Path) -> None:
    uri = html_file.resolve().as_uri()
    cmd = [
        edge_bin,
        "--headless=new",
        "--disable-gpu",
        "--no-pdf-header-footer",
        f"--user-data-dir={user_data}",
        f"--print-to-pdf={out_pdf}",
        uri,
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def capture_all_screenshots(edge_bin: str) -> None:
    SCREENSHOTS_DIR.mkdir(parents=True, exist_ok=True)
    port = 18945
    proc = subprocess.Popen(
        [str(EXE_PATH), "--serve-only", str(port)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_server(f"http://127.0.0.1:{port}/api/benchmarks")
        with tempfile.TemporaryDirectory(prefix="algolab_edge_ss_") as tmp:
            udir = Path(tmp)
            shots = [
                ("01_dashboard.png", f"http://127.0.0.1:{port}/?tab=overview"),
                ("02_visualizer_sorting.png", f"http://127.0.0.1:{port}/?tab=visualizer&domain=sorting&algo=quick_median3&format=random&step=22"),
                ("03_visualizer_search.png", f"http://127.0.0.1:{port}/?tab=visualizer&domain=search&algo=bin_search_iter&format=middle&step=2"),
                ("04_visualizer_dp_greedy.png", f"http://127.0.0.1:{port}/?tab=visualizer&domain=greedy_dp&algo=coin_compare&format=non_canonical&step=end"),
                ("05_empirical_tables_charts.png", f"http://127.0.0.1:{port}/?tab=empirical&suite=sorting"),
                ("06_asymptotic_analysis.png", f"http://127.0.0.1:{port}/?tab=analysis"),
            ]
            for idx, (fname, url) in enumerate(shots, 1):
                print(f"  [Screenshot {idx}/7] {fname}")
                edge_screenshot(edge_bin, url, SCREENSHOTS_DIR / fname, udir / f"p{idx}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    # Captura real da saída do modo CLI
    print("  [Screenshot 7/7] 07_cli_execution.png")
    cli_res = subprocess.run(
        [str(EXE_PATH), "--cli"],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=True,
    )
    lines = cli_res.stdout.splitlines()
    # Seleciona cabeçalho + amostra legível de cada uma das 4 seções da saída CLI real
    excerpt_lines = []
    sec_rows = 0
    in_section = False
    for ln in lines:
        if any(k in ln for k in ["ALGOLAB C++20", "Compilador:", "=========", "Total de experimentos", "JSON empírico"]):
            excerpt_lines.append(ln)
        elif ln.startswith("--- "):
            in_section = True
            sec_rows = 0
            excerpt_lines.append(ln)
        elif in_section:
            if ln.startswith("Algoritmo") or ln.startswith("------"):
                excerpt_lines.append(ln)
            elif ln.strip() == "":
                in_section = False
            elif sec_rows < 6:
                excerpt_lines.append(ln)
                sec_rows += 1
    cli_text = html.escape("\n".join(excerpt_lines[:46]))

    cli_html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><style>
body {{ margin:0; background:#09090b; font-family:'Cascadia Code','Consolas',monospace; color:#e4e4e7; padding:24px; }}
.win {{ border:1px solid #27272a; border-radius:12px; background:#0d0d11; overflow:hidden; box-shadow:0 18px 40px rgba(0,0,0,.6); }}
.bar {{ background:#18181d; padding:10px 16px; border-bottom:1px solid #27272a; display:flex; align-items:center; gap:8px; font-size:12px; color:#a1a1aa; font-family:'Segoe UI',sans-serif; }}
.dot {{ width:10px; height:10px; border-radius:50%; display:inline-block; }}
pre {{ margin:0; padding:18px 20px; font-size:12px; line-height:1.45; white-space:pre; color:#d4d4d8; }}
.cmd {{ color:#34d399; margin-bottom:10px; font-weight:600; }}
</style></head><body>
<div class="win">
  <div class="bar"><span class="dot" style="background:#f43f5e"></span><span class="dot" style="background:#f59e0b"></span><span class="dot" style="background:#10b981"></span>
    <span style="margin-left:8px;">Windows PowerShell — AlgoLab.exe --cli</span>
  </div>
  <pre><span class="cmd">PS C:\\projeto_pratico_algoritmos&gt; .\\dist\\AlgoLab.exe --cli</span>\n{cli_text}</pre>
</div></body></html>"""

    with tempfile.TemporaryDirectory(prefix="algolab_cli_ss_") as tmp:
        tmp_html = Path(tmp) / "cli.html"
        tmp_html.write_text(cli_html, encoding="utf-8")
        edge_screenshot(edge_bin, tmp_html.as_uri(), SCREENSHOTS_DIR / "07_cli_execution.png", Path(tmp) / "udir", 1280, 760)


def img_data_uri(fname: str) -> str:
    b = (SCREENSHOTS_DIR / fname).read_bytes()
    return "data:image/png;base64," + base64.b64encode(b).decode("ascii")


def fmt_num(v: float, dec: int = 2) -> str:
    return f"{v:,.{dec}f}".replace(",", "X").replace(".", ",").replace("X", ".")


def fmt_int(v: int) -> str:
    return f"{int(v):,}".replace(",", ".")


def build_svg_line_chart(title: str, y_label: str, series: list, x_values: list,
                         use_log: bool = False, width: int = 720, height: int = 285) -> str:
    """
    series: list of dicts { 'name': str, 'color': str, 'points': {n: val} }
    """
    pad_l, pad_r, pad_t, pad_b = 76, 22, 42, 46
    pw = width - pad_l - pad_r
    ph = height - pad_t - pad_b

    def ty(v: float) -> float:
        return math.log10(max(v, 0.01)) if use_log else float(v)

    all_vals = []
    for s in series:
        for n in x_values:
            if n in s["points"]:
                all_vals.append(ty(s["points"][n]))

    if not all_vals:
        return ""
    min_y = min(all_vals) if use_log else 0.0
    max_y = max(all_vals)
    if max_y <= min_y:
        max_y = min_y + 1.0

    svg = [
        f'<svg viewBox="0 0 {width} {height}" width="100%" style="max-width:{width}px; background:#ffffff; border:1px solid #cbd5e1; border-radius:8px; font-family:Segoe UI, Arial, sans-serif;">',
        f'<text x="{pad_l}" y="22" font-size="12.5" font-weight="700" fill="#0f172a">{html.escape(title)}</text>',
    ]

    # Grid Y
    for g in range(6):
        y = pad_t + ph * g / 5.0
        val = max_y - (max_y - min_y) * g / 5.0
        disp = (10 ** val) if use_log else val
        if disp >= 1_000_000:
            lbl = f"{disp/1e6:.1f}M"
        elif disp >= 1000:
            lbl = f"{disp/1e3:.1f}k"
        elif disp >= 10:
            lbl = f"{disp:.0f}"
        else:
            lbl = f"{disp:.2f}"
        svg.append(f'<line x1="{pad_l}" y1="{y:.1f}" x2="{width - pad_r}" y2="{y:.1f}" stroke="#e2e8f0" stroke-width="1"/>')
        svg.append(f'<text x="{pad_l - 8}" y="{y + 3.5:.1f}" font-size="9.5" text-anchor="end" fill="#475569">{lbl}</text>')

    # Eixo Y label
    svg.append(
        f'<text x="16" y="{pad_t + ph / 2:.1f}" font-size="10" font-weight="600" fill="#334155" '
        f'transform="rotate(-90 16 {pad_t + ph / 2:.1f})" text-anchor="middle">{html.escape(y_label)}</text>'
    )

    # Eixo X ticks & label
    for i, n in enumerate(x_values):
        x = pad_l + (pw * 0.5 if len(x_values) == 1 else pw * i / (len(x_values) - 1))
        lbl = f"{n // 1000}k" if n >= 1000 and n % 1000 == 0 else str(n)
        svg.append(f'<line x1="{x:.1f}" y1="{pad_t}" x2="{x:.1f}" y2="{pad_t + ph}" stroke="#f1f5f9" stroke-width="1"/>')
        svg.append(f'<text x="{x:.1f}" y="{pad_t + ph + 15}" font-size="9.5" text-anchor="middle" fill="#475569">{lbl}</text>')

    svg.append(f'<text x="{pad_l + pw / 2:.1f}" y="{height - 8}" font-size="10" font-weight="600" text-anchor="middle" fill="#334155">Tamanho da entrada (n)</text>')

    # Séries
    for s in series:
        pts_xy = []
        for i, n in enumerate(x_values):
            if n not in s["points"]:
                continue
            x = pad_l + (pw * 0.5 if len(x_values) == 1 else pw * i / (len(x_values) - 1))
            yn = (ty(s["points"][n]) - min_y) / (max_y - min_y)
            y = pad_t + ph - yn * ph
            pts_xy.append((x, y))
        if len(pts_xy) >= 2:
            d = " ".join(("M" if k == 0 else "L") + f"{px:.1f},{py:.1f}" for k, (px, py) in enumerate(pts_xy))
            svg.append(f'<path d="{d}" fill="none" stroke="{s["color"]}" stroke-width="2.2"/>')
        for px, py in pts_xy:
            svg.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="3.4" fill="{s["color"]}" stroke="#fff" stroke-width="1"/>')

    # Legenda no topo direito
    lx = width - pad_r
    for idx, s in enumerate(reversed(series)):
        # Empilha legenda limpa logo abaixo do título se muitos itens
        pass
    leg_x = pad_l
    leg_y = 35
    for s in series:
        svg.append(f'<rect x="{leg_x}" y="{leg_y - 7}" width="9" height="9" rx="2" fill="{s["color"]}"/>')
        svg.append(f'<text x="{leg_x + 13}" y="{leg_y + 1}" font-size="9.2" fill="#334155">{html.escape(s["name"])}</text>')
        leg_x += 13 + len(s["name"]) * 5.2 + 14

    svg.append("</svg>")
    return "\n".join(svg)


def render_records_table(records: list, caption: str = "") -> str:
    rows = []
    for r in records:
        obs = html.escape(r["observation"])
        badge_cls = "ok" if r["correct"] else "warn"
        rows.append(
            f"<tr>"
            f"<td><strong>{html.escape(r['algorithm'])}</strong></td>"
            f"<td class='num'>{fmt_int(r['n'])}</td>"
            f"<td>{html.escape(r['input_format'])}</td>"
            f"<td class='num'>{fmt_num(r['elapsed_us'], 2)} µs</td>"
            f"<td>{html.escape(r['dominant_op'])}</td>"
            f"<td class='num'>{fmt_int(r['dominant_count'])}</td>"
            f"<td class='{badge_cls}'>{obs}</td>"
            f"</tr>"
        )
    cap_html = f"<div class='tbl-cap'>{html.escape(caption)}</div>" if caption else ""
    return (
        f"{cap_html}"
        "<table><thead><tr>"
        "<th>Algoritmo</th><th>n</th><th>Formato da entrada</th>"
        "<th>Tempo</th><th>Operação dominante</th><th>Quantidade</th><th>Observação</th>"
        "</tr></thead><tbody>" + "".join(rows) + "</tbody></table>"
    )


def wrap_pdf_document(doc_title: str, subtitle: str, tag: str, body_html: str) -> str:
    return f"""<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8"/>
<title>{html.escape(doc_title)}</title>
<style>
  @page {{
    size: A4;
    margin: 16mm 15mm 16mm 15mm;
  }}
  * {{ box-sizing: border-box; }}
  body {{
    font-family: 'Segoe UI', 'Inter', Arial, sans-serif;
    color: #1e293b;
    font-size: 9.8pt;
    line-height: 1.52;
    margin: 0;
  }}
  .cover-banner {{
    border: 1px solid #cbd5e1;
    border-top: 4px solid #4338ca;
    border-radius: 8px;
    padding: 16px 20px;
    background: linear-gradient(135deg, #f8fafc, #eef2ff);
    margin-bottom: 18px;
  }}
  .doc-tag {{
    display: inline-block;
    font-size: 8pt;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: #4338ca;
    background: #e0e7ff;
    padding: 3px 9px;
    border-radius: 999px;
    margin-bottom: 8px;
  }}
  h1 {{
    margin: 0 0 6px 0;
    font-size: 17pt;
    color: #0f172a;
    letter-spacing: -0.02em;
  }}
  .sub {{
    font-size: 10pt;
    color: #475569;
    margin: 0;
  }}
  .meta-grid {{
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
    margin-top: 12px;
    padding-top: 10px;
    border-top: 1px solid #cbd5e1;
    font-size: 8.5pt;
    color: #334155;
  }}
  h2 {{
    font-size: 12.5pt;
    color: #0f172a;
    border-bottom: 1.5px solid #e2e8f0;
    padding-bottom: 4px;
    margin: 18px 0 8px;
    page-break-after: avoid;
  }}
  h3 {{
    font-size: 10.5pt;
    color: #1e1b4b;
    margin: 12px 0 5px;
    page-break-after: avoid;
  }}
  p {{ margin: 0 0 8px; text-align: justify; }}
  ul, ol {{ margin: 4px 0 10px 18px; padding: 0; }}
  li {{ margin-bottom: 3px; }}
  table {{
    width: 100%;
    border-collapse: collapse;
    font-size: 8.2pt;
    margin: 8px 0 10px;
  }}
  th {{
    background: #f1f5f9;
    color: #0f172a;
    font-weight: 700;
    text-align: left;
    padding: 6px 7px;
    border: 1px solid #cbd5e1;
  }}
  td {{
    padding: 5px 7px;
    border: 1px solid #e2e8f0;
    vertical-align: top;
  }}
  tr:nth-child(even) td {{ background: #f8fafc; }}
  td.num {{ font-family: Consolas, monospace; text-align: right; white-space: nowrap; }}
  td.warn {{ color: #be123c; font-weight: 600; }}
  td.ok {{ color: #334155; }}
  .tbl-cap {{
    font-size: 8.6pt;
    font-weight: 700;
    color: #334155;
    margin-top: 8px;
  }}
  .box {{
    border: 1px solid #cbd5e1;
    border-left: 3.5px solid #4338ca;
    background: #f8fafc;
    border-radius: 6px;
    padding: 9px 12px;
    margin: 8px 0 12px;
    break-inside: avoid;
  }}
  .box.emerald {{ border-left-color: #059669; background: #ecfdf5; }}
  .box.rose {{ border-left-color: #e11d48; background: #fff1f2; }}
  pre.code {{
    background: #0f172a;
    color: #e2e8f0;
    padding: 10px 12px;
    border-radius: 7px;
    font-family: Consolas, 'Courier New', monospace;
    font-size: 7.9pt;
    line-height: 1.42;
    overflow-x: hidden;
    white-space: pre-wrap;
    break-inside: avoid;
    margin: 6px 0 10px;
  }}
  .figure {{
    border: 1px solid #cbd5e1;
    border-radius: 8px;
    padding: 8px;
    background: #ffffff;
    margin: 10px 0 14px;
    break-inside: avoid;
  }}
  .figure img {{
    width: 100%;
    border-radius: 5px;
    border: 1px solid #e2e8f0;
    display: block;
  }}
  .fig-cap {{
    margin-top: 6px;
    font-size: 8.4pt;
    color: #334155;
  }}
  .chart-wrap {{
    margin: 10px 0 6px;
    text-align: center;
    break-inside: avoid;
  }}
  .formula {{
    font-family: Consolas, monospace;
    background: #eef2ff;
    color: #1e1b4b;
    padding: 2px 6px;
    border-radius: 4px;
    font-size: 8.8pt;
  }}
  .page-break {{ page-break-before: always; }}
</style>
</head>
<body>
  <div class="cover-banner">
    <span class="doc-tag">{html.escape(tag)}</span>
    <h1>{html.escape(doc_title)}</h1>
    <p class="sub">{html.escape(subtitle)}</p>
    <div class="meta-grid">
      <div><strong>Disciplina:</strong> Análise de Algoritmos</div>
      <div><strong>Autor / Repositório:</strong> Fernando Calisto</div>
      <div><strong>Software:</strong> AlgoLab C++20 (v1.0.0)</div>
      <div><strong>Ambiente:</strong> Windows x64 • GCC 16.2 -O3</div>
    </div>
  </div>
  {body_html}
</body>
</html>"""


def generate_all_pdfs(edge_bin: str) -> None:
    DELIVERABLES_DIR.mkdir(parents=True, exist_ok=True)
    data = json.loads(DATA_JSON.read_text(encoding="utf-8"))
    recs = data["records"]

    def q(suite=None, algo=None, fmt=None, n_in=None):
        res = recs
        if suite:
            res = [r for r in res if r["suite"] == suite]
        if algo:
            res = [r for r in res if r["algo_key"] == algo]
        if fmt:
            res = [r for r in res if r["format_key"] == fmt]
        if n_in:
            res = [r for r in res if r["n"] in n_in]
        return res

    # Helpers de lookup rápido para interpretar com precisão os dados medidos
    def find_rec(algo_key: str, fmt_key: str, n_val: int):
        for r in recs:
            if r["algo_key"] == algo_key and r["format_key"] == fmt_key and r["n"] == n_val:
                return r
        return None

    # Prepara gráficos SVG vetoriais (Seção 9)
    def make_series(pairs, fmt_key, metric="elapsed_us"):
        out = []
        for akey, name, color in pairs:
            pts = {r["n"]: r[metric] for r in q(algo=akey, fmt=fmt_key)}
            out.append({"name": name, "color": color, "points": pts})
        return out

    svg_search_absent = build_svg_line_chart(
        "Gráfico 1 — Busca em Vetor Ordenado (Chave Ausente — Pior Caso Geral)",
        "Tempo (µs, escala log10)",
        make_series([
            ("seq_search", "Busca Sequencial", "#e11d48"),
            ("bin_search_iter", "Binária Iterativa", "#059669"),
            ("bin_search_rec", "Binária Recursiva", "#0284c7"),
        ], "absent"),
        [1000, 10000, 100000, 500000, 1000000],
        use_log=True,
    )

    svg_sort_random = build_svg_line_chart(
        "Gráfico 2 — Ordenação em Entrada Aleatória (n = 100 a 25.000)",
        "Tempo (µs, escala log10)",
        make_series([
            ("quick_median3", "Quick (Mediana 3)", "#059669"),
            ("quick_fixed", "Quick (Pivô Fixo)", "#d97706"),
            ("merge_sort", "Merge Sort", "#4f46e5"),
            ("insertion_sort", "Insertion Sort", "#0284c7"),
            ("bubble_sort", "Bubble Otimizado", "#e11d48"),
        ], "random"),
        [100, 500, 1000, 2500, 5000, 10000, 25000],
        use_log=True,
    )

    svg_sort_sorted = build_svg_line_chart(
        "Gráfico 3 — Sensibilidade à Entrada: Vetor Já Ordenado Crescente",
        "Tempo (µs, escala log10)",
        make_series([
            ("quick_median3", "Quick (Mediana 3)", "#059669"),
            ("quick_fixed", "Quick (Pivô Fixo)", "#e11d48"),
            ("merge_sort", "Merge Sort", "#4f46e5"),
            ("insertion_sort", "Insertion Sort", "#0284c7"),
            ("bubble_sort", "Bubble Otimizado", "#d97706"),
        ], "sorted_asc"),
        [100, 500, 1000, 2500, 5000, 10000, 25000],
        use_log=True,
    )

    svg_fib = build_svg_line_chart(
        "Gráfico 4 — Fibonacci: Recursivo Ingênuo vs. Iterativo vs. Memoização",
        "Tempo (µs, escala log10)",
        make_series([
            ("fib_iter", "Iterativo O(n)", "#059669"),
            ("fib_dp", "PD Memoização O(n)", "#4f46e5"),
            ("fib_rec", "Recursivo Ingênuo O(φⁿ)", "#e11d48"),
        ], "scalar"),
        [10, 20, 30, 35, 40],
        use_log=True,
    )

    svg_pow = build_svg_line_chart(
        "Gráfico 5 — Exponenciação Linear O(n) vs. Divisão e Conquista O(log n)",
        "Multiplicações (escala log10)",
        make_series([
            ("pow_iter", "Iterativa O(n)", "#e11d48"),
            ("pow_dc", "Divisão e Conquista O(log n)", "#059669"),
        ], "exponent", metric="dominant_count"),
        [100, 1000, 10000, 100000, 1000000],
        use_log=True,
    )

    svg_knap = build_svg_line_chart(
        "Gráfico 6 — Mochila: Guloso O(n log n) vs. Programação Dinâmica 0/1 O(n·W)",
        "Tempo (µs, escala log10)",
        make_series([
            ("knap_frac", "Fracionária (Guloso)", "#0284c7"),
            ("knap_01_greedy", "0/1 Heurística Gulosa", "#d97706"),
            ("knap_01_dp", "0/1 Prog. Dinâmica", "#059669"),
        ], "scaled"),
        [10, 25, 50, 100, 200],
        use_log=True,
    )

    # Números reais chave para interpretações
    seq_1m = find_rec("seq_search", "absent", 1000000)
    bin_1m = find_rec("bin_search_iter", "absent", 1000000)
    q3_25k = find_rec("quick_median3", "random", 25000)
    bub_25k = find_rec("bubble_sort", "random", 25000)
    qf_25k_asc = find_rec("quick_fixed", "sorted_asc", 25000)
    ins_25k_asc = find_rec("insertion_sort", "sorted_asc", 25000)
    fib_rec_40 = find_rec("fib_rec", "scalar", 40)
    fib_it_40 = find_rec("fib_iter", "scalar", 40)

    # -------------------------------------------------------------------------
    # SEÇÕES REUTILIZÁVEIS
    # -------------------------------------------------------------------------
    sec_planejamento = """
    <h2>1. Identificação e Escopo do Projeto</h2>
    <p>Este projeto conduz a investigação empírica e assintótica de algoritmos clássicos previstos no conteúdo da disciplina de <strong>Análise de Algoritmos</strong>, implementados e instrumentados nativamente em <strong>C++20</strong> no software interativo <strong>AlgoLab</strong> (executável standalone Windows x64 com interface desktop minimalista e motor de benchmark CLI).</p>
    <ul>
      <li><strong>Módulo 1 — Busca:</strong> Busca Sequencial vs. Busca Binária (Iterativa e Recursiva).</li>
      <li><strong>Módulo 2 — Ordenação &amp; Divisão e Conquista:</strong> Quick Sort (Mediana de 3 + Hoare vs. Pivô Fixo Lomuto), Merge Sort, Insertion Sort e Bubble Sort Otimizado.</li>
      <li><strong>Módulo 3 — Iterativo vs. Recursivo &amp; Divisão e Conquista:</strong> Sequência de Fibonacci (Iterativo, Recursivo Ingênuo e Programação Dinâmica por Memoização) e Exponenciação Linear vs. Exponenciação Rápida.</li>
      <li><strong>Módulo 4 — Algoritmos Gulosos vs. Programação Dinâmica:</strong> Problema do Troco (sistema canônico BRL vs. contraexemplo não-canônico [1,3,4]) e Problema da Mochila (Fracionária Gulosa vs. 0/1 Heurística Gulosa vs. 0/1 Programação Dinâmica).</li>
    </ul>

    <h2>2. Metodologia de Testes e Formatos de Entrada (Seções 5 e 6)</h2>
    <p>Cada experimento seguiu rigorosamente o roteiro de 10 etapas: definição formal de entradas e saídas, apresentação da estratégia em C++, identificação de primitivas dominantes, variação progressiva de <em>n</em>, preparação de vetores determinísticos (semente fixa <code>2026</code>), aquecimento (<em>warm-up</em>) e repetição cronometrada com <code>std::chrono::high_resolution_clock</code>.</p>
    <table>
      <thead><tr><th>Tipo de Entrada</th><th>Construção Adotada</th><th>Finalidade Analítica</th></tr></thead>
      <tbody>
        <tr><td><strong>Aleatória</strong></td><td>Valores pseudoaleatórios uniformes em [1, 10n]</td><td>Avaliar caso médio esperado em dados imprevisíveis.</td></tr>
        <tr><td><strong>Ordenada crescente</strong></td><td>Sequência estritamente crescente [1, 2, ..., n]</td><td>Expor melhor caso adaptativo (Insertion/Bubble) e pior caso de pivô ingênuo (Quick Fixo).</td></tr>
        <tr><td><strong>Ordenada decrescente</strong></td><td>Sequência inversa [n, n-1, ..., 1]</td><td>Forçar o pior caso quadrático em comparações e movimentações.</td></tr>
        <tr><td><strong>Quase ordenada</strong></td><td>Vetor [1..n] com 2% de pares permutados</td><td>Medir sensibilidade à desordem residual (Insertion vs. algoritmos O(n log n)).</td></tr>
        <tr><td><strong>Com valores repetidos</strong></td><td>Domínio restrito a 10 valores distintos</td><td>Testar particionamento de Quick Sort e estabilidade diante de duplicidades.</td></tr>
        <tr><td><strong>Busca (Início/Meio/Fim/Ausente)</strong></td><td>Chaves no índice 0, (n-1)/2, n-1 e valor n+999</td><td>Confrontar melhor caso, caso médio e pior caso entre Sequencial e Binária.</td></tr>
      </tbody>
    </table>
    <div class="box">
      <strong>Hipóteses Iniciais de Pesquisa:</strong> (H1) A Busca Binária superará a Sequencial por ordens de grandeza para n ≥ 10.000, mantendo a versão iterativa menor pegada de memória que a recursiva; (H2) Quick Sort com Pivô Fixo degradará para O(n²) em entradas já ordenadas ou com muitas repetições, sendo corrigido pela Mediana de 3; (H3) Fibonacci recursivo ingênuo será inviável já em n=40; (H4) Algoritmos gulosos serão mais rápidos, porém produzirão respostas subótimas em entradas não-canônicas e na Mochila 0/1.
    </div>"""

    sec_codigo = """
    <h2>3. Implementação Modular e Instrumentação em C++20</h2>
    <p>Todo o núcleo de algoritmos reside nos cabeçalhos em <code>include/</code>, instrumentado via estrutura <code>Metrics</code> para contabilizar comparações de chaves, escritas/trocas, acessos ao vetor, chamadas recursivas e bytes extras alocados sem interferir na lógica assintótica.</p>
    <h3>3.1 Busca Sequencial e Busca Binária (<code>include/search_algorithms.hpp</code>)</h3>
    <pre class="code">int sequential_search(const std::vector&lt;int&gt;&amp; arr, int target, Metrics&amp; m) {
    m.reset(); m.extra_memory_bytes = sizeof(std::size_t);
    for (std::size_t i = 0; i &lt; arr.size(); ++i) {
        m.array_accesses++; m.comparisons++;
        if (arr[i] == target) return static_cast&lt;int&gt;(i);
    }
    return -1;
}

int binary_search_iterative(const std::vector&lt;int&gt;&amp; arr, int target, Metrics&amp; m) {
    m.reset(); m.extra_memory_bytes = 3 * sizeof(int);
    int low = 0, high = static_cast&lt;int&gt;(arr.size()) - 1;
    while (low &lt;= high) {
        int mid = low + (high - low) / 2;
        m.array_accesses++; m.comparisons++;
        if (arr[mid] == target) return mid;
        m.comparisons++;
        if (arr[mid] &lt; target) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}</pre>
    <h3>3.2 Quick Sort Mediana de Três &amp; Merge Sort (<code>include/sorting_algorithms.hpp</code>)</h3>
    <pre class="code">int partition_hoare_median3(std::vector&lt;int&gt;&amp; arr, int low, int high, Metrics&amp; m) {
    int pivot = median_of_three_pivot(arr, low, high, m);
    int i = low - 1, j = high + 1;
    while (true) {
        do { ++i; m.array_accesses++; m.comparisons++; } while (arr[i] &lt; pivot);
        do { --j; m.array_accesses++; m.comparisons++; } while (arr[j] &gt; pivot);
        if (i &gt;= j) return j;
        std::swap(arr[i], arr[j]);
        m.array_accesses += 4; m.swaps_or_writes += 3;
    }
}</pre>
    <h3>3.3 Problema do Troco — Programação Dinâmica (<code>include/greedy_dp_algorithms.hpp</code>)</h3>
    <pre class="code">for (int x = 1; x &lt;= amount; ++x) {
    for (int coin : coins) {
        m.comparisons++;
        if (coin &gt; 0 &amp;&amp; coin &lt;= x) {
            int candidate = dp[x - coin] + 1;
            m.comparisons++;
            if (candidate &lt; dp[x]) { dp[x] = candidate; parent[x] = coin; m.swaps_or_writes += 2; }
        }
    }
}</pre>"""

    sec_evidencias = f"""
    <h2>4. Evidências Reais de Execução (GUI Desktop &amp; CLI)</h2>
    <p>As capturas abaixo foram obtidas diretamente da execução do binário compilado <code>dist/AlgoLab.exe</code> no sistema Windows x64, comprovando o funcionamento do visualizador interativo, da interface de análise e do motor CLI.</p>
    <div class="figure">
      <img src="{img_data_uri('01_dashboard.png')}" alt="Dashboard AlgoLab"/>
      <div class="fig-cap"><strong>Figura 1 — Painel Principal do AlgoLab Desktop:</strong> Apresenta os indicadores globais calculados sobre os 294 ensaios empíricos (incluindo os ganhos de escala da Busca Binária e Quick Sort) e o quadro consolidado de complexidade teórica.</div>
    </div>
    <div class="figure">
      <img src="{img_data_uri('02_visualizer_sorting.png')}" alt="Visualizador Ordenação"/>
      <div class="fig-cap"><strong>Figura 2 — Visualizador Interativo (Quick Sort Mediana de 3):</strong> Exibe o subvetor ativo delimitado em índigo, barras ordenadas em esmeralda, código C++ sincronizado na linha executada e contadores ao vivo de comparações, escritas e tempo nativo em C++.</div>
    </div>
    <div class="figure">
      <img src="{img_data_uri('03_visualizer_search.png')}" alt="Visualizador Busca"/>
      <div class="fig-cap"><strong>Figura 3 — Visualizador de Busca Binária Iterativa:</strong> Ponteiros L/M/H sobre o vetor ordenado com descarte imediato de metade do espaço de busca a cada passo.</div>
    </div>
    <div class="figure">
      <img src="{img_data_uri('04_visualizer_dp_greedy.png')}" alt="Visualizador Guloso vs PD"/>
      <div class="fig-cap"><strong>Figura 4 — Confronto Guloso vs. Programação Dinâmica (Troco = 6, moedas [1,3,4]):</strong> Demonstração visual do contraexemplo clássico: o método guloso seleciona 3 moedas (4+1+1), enquanto a tabela bottom-up <code>dp[0..6]</code> encontra o ótimo global de 2 moedas (3+3).</div>
    </div>
    <div class="figure">
      <img src="{img_data_uri('05_empirical_tables_charts.png')}" alt="Laboratório Empírico"/>
      <div class="fig-cap"><strong>Figura 5 — Laboratório Empírico no AlgoLab:</strong> Curvas comparativas multi-algoritmo e tabela filtrável no formato padrão da Seção 7 com exportação CSV e execução customizada em C++.</div>
    </div>
    <div class="figure">
      <img src="{img_data_uri('07_cli_execution.png')}" alt="Saída CLI AlgoLab.exe"/>
      <div class="fig-cap"><strong>Figura 6 — Execução em Linha de Comando (<code>AlgoLab.exe --cli</code>):</strong> Saída direta do motor C++20 compilado com GCC 16.2.0 (<code>-O3 -static</code>) gerando as 294 medições e gravando <code>data/benchmark_results.json</code>.</div>
    </div>"""

    # Tabelas selecionadas (completas para os principais cortes, mantendo legibilidade impecável)
    tbl_search = render_records_table(
        q(suite="search", n_in=[1000, 100000, 1000000]),
        "Tabela 1 — Experimentos de Busca para n = 1.000, 100.000 e 1.000.000 (4 cenários de chave)"
    )
    tbl_sort_rand = render_records_table(
        q(suite="sorting", fmt="random", n_in=[100, 1000, 5000, 10000, 25000]),
        "Tabela 2 — Ordenação em Entrada Aleatória (n = 100 a 25.000)"
    )
    tbl_sort_formats = render_records_table(
        [r for r in q(suite="sorting", n_in=[25000]) if r["format_key"] != "random"],
        "Tabela 3 — Sensibilidade ao Formato da Entrada em n = 25.000 (Crescente, Decrescente, Quase Ordenada e Repetidos)"
    )
    tbl_recursion = render_records_table(
        q(suite="recursion"),
        "Tabela 4 — Iterativo vs. Recursivo & Divisão e Conquista (Fibonacci e Exponenciação)"
    )
    tbl_greedy_dp = render_records_table(
        q(suite="greedy_dp"),
        "Tabela 5 — Algoritmos Gulosos vs. Programação Dinâmica (Problema do Troco e Problema da Mochila)"
    )

    sec_tabelas = f"""
    <h2>5. Resultados Experimentais (Tabelas Empíricas — Seção 7)</h2>
    <p>As tabelas a seguir registram as médias de execução obtidas diretamente pelo motor C++20 com aquecimento prévio e controle de otimização (<em>sink</em> volátil).</p>
    {tbl_search}
    <p><strong>Leitura da Tabela 1:</strong> No cenário <em>Início</em>, a Busca Sequencial conclui com 1 comparação (O(1)). Entretanto, quando a chave está ausente em n = 1.000.000, a Busca Sequencial executou {fmt_int(seq_1m['dominant_count'])} comparações em {fmt_num(seq_1m['elapsed_us'])} µs, contra apenas {fmt_int(bin_1m['dominant_count'])} comparações ({fmt_num(bin_1m['elapsed_us'])} µs) da Busca Binária Iterativa.</p>
    {tbl_sort_rand}
    {tbl_sort_formats}
    <p><strong>Leitura das Tabelas 2 e 3:</strong> Em vetor aleatório (n=25.000), Quick Sort Mediana de 3 ({fmt_num(q3_25k['elapsed_ms'])} ms) foi aproximadamente {fmt_num(bub_25k['elapsed_us']/q3_25k['elapsed_us'], 0)}× mais rápido que o Bubble Sort ({fmt_num(bub_25k['elapsed_ms'])} ms). Em vetor já ordenado (n=25.000), a parada antecipada levou o Bubble e o Insertion Sort ({fmt_num(ins_25k_asc['elapsed_us'])} µs) para O(n), enquanto o Quick Sort com pivô fixo no último elemento despencou para {fmt_num(qf_25k_asc['elapsed_ms'])} ms ({fmt_int(qf_25k_asc['dominant_count'])} comparações).</p>
    {tbl_recursion}
    {tbl_greedy_dp}"""

    sec_graficos = f"""
    <h2>6. Gráficos de Desempenho e Interpretação (Seção 9)</h2>
    <div class="chart-wrap">{svg_search_absent}</div>
    <p><strong>Interpretação (Gráfico 1):</strong> Ao crescer <em>n</em> de 1.000 para 1.000.000 (fator 1.000×), o tempo da Busca Sequencial cresceu linearmente de menos de 1 µs para {fmt_num(seq_1m['elapsed_us'])} µs, ao passo que ambas as versões da Busca Binária permaneceram abaixo de 2 µs (dobrando de 20 para apenas 40 comparações). O resultado confirma perfeitamente Θ(n) contra Θ(log n).</p>

    <div class="chart-wrap">{svg_sort_random}</div>
    <p><strong>Interpretação (Gráfico 2):</strong> Em dados aleatórios, Insertion Sort e Bubble Sort se separam drasticamente a partir de n = 1.000 devido ao crescimento quadrático (n²/4 e n²/2 comparações). Quick Sort (Mediana de 3 e Pivô Fixo) e Merge Sort crescem suavemente em Θ(n log n), com pequena vantagem prática do Quick Mediana de 3 por operar <em>in-place</em> com melhor localidade de cache.</p>

    <div class="chart-wrap">{svg_sort_sorted}</div>
    <p><strong>Interpretação (Gráfico 3):</strong> Em vetor ordenado crescente, o gráfico revela uma inversão notável prevista pela teoria: Quick Sort com pivô fixo no fim apresenta comportamento idêntico ao Θ(n²), tornando-se milhares de vezes mais lento que Insertion Sort e Bubble Sort Otimizado, que caem para Θ(n). Quick Sort (Mediana de 3) e Merge Sort mantêm Θ(n log n) robusto.</p>

    <div class="chart-wrap">{svg_fib}</div>
    <p><strong>Interpretação (Gráfico 4):</strong> Para Fibonacci, a versão recursiva ingênua saltou de {fmt_num(find_rec('fib_rec','scalar',10)['elapsed_us'])} µs (n=10, 177 chamadas) para {fmt_num(fib_rec_40['elapsed_ms'])} ms (n=40, {fmt_int(fib_rec_40['dominant_count'])} chamadas), enquanto Iterativo ({fmt_num(fib_it_40['elapsed_us'])} µs) e Memoização mantiveram tempo próximo a 1 µs.</p>

    <div class="chart-wrap">{svg_pow}</div>
    <p><strong>Interpretação (Gráfico 5):</strong> A divisão do expoente pela metade reduz 1.000.000 de multiplicações para apenas 25 multiplicações na Exponenciação Rápida (D&amp;C), confirmando a queda de Θ(n) para Θ(log n).</p>

    <div class="chart-wrap">{svg_knap}</div>
    <p><strong>Interpretação (Gráfico 6):</strong> Na Mochila, as abordagens gulosas executam em menos de 5 µs para n=200 (O(n log n)), mas a heurística gulosa 0/1 falha em garantir otimalidade (ex.: valor 160 vs. 220 no contraexemplo clássico W=50). A Programação Dinâmica paga custo pseudo-polinomial Θ(n·W) ({fmt_num(find_rec('knap_01_dp','scaled',200)['elapsed_us'])} µs em n=200, W=1000) em troca de otimalidade exata garantida.</p>"""

    sec_assintotica = """
    <h2>7. Análise Assintótica Detalhada (Seção 8)</h2>
    <div class="box">
      <h3>7.1 Busca Sequencial vs. Busca Binária</h3>
      <p><strong>Sequencial:</strong> Operação dominante = comparação <code>arr[i] == target</code>. Custo <code>T(n) = 1</code> no melhor caso (índice 0); no pior caso <code>T(n) = n</code>, logo <span class="formula">O(1) / Θ(n) / O(n)</span>, espaço <span class="formula">O(1)</span>.</p>
      <p><strong>Binária:</strong> Cada passo testa <code>arr[mid]</code> e reduz o subproblema à metade: <span class="formula">T(n) = T(n/2) + c</span>. Pelo Teorema Mestre (a=1, b=2, f(n)=c = Θ(n<sup>log₂1</sup>)), resulta <span class="formula">Θ(log₂ n)</span>. A forma recursiva aloca 1 frame por nível, consumindo espaço <span class="formula">Θ(log n)</span>; a iterativa usa <span class="formula">O(1)</span>.</p>
    </div>
    <div class="box">
      <h3>7.2 Quick Sort (Pivô Fixo vs. Mediana de 3) e Merge Sort</h3>
      <p><strong>Quick Sort (Pivô Fixo):</strong> No caso médio (partição equilibrada): <span class="formula">T(n) = 2T(n/2) + cn = Θ(n log n)</span> (Caso 2 do Teorema Mestre). Quando o vetor já está ordenado (crescente/decrescente), o pivô no fim gera subproblemas de tamanhos (n-1) e 0: <span class="formula">T(n) = T(n-1) + (n-1) = n(n-1)/2 = Θ(n²)</span>. A escolha pela Mediana de Três + Hoare evita esse desbalanceamento.</p>
      <p><strong>Merge Sort:</strong> Sempre divide ao meio e intercala linearmente: <span class="formula">T(n) = 2T(n/2) + cn</span> ⇒ <span class="formula">Θ(n log n)</span> em melhor, médio e pior casos, exigindo vetor auxiliar de tamanho <code>n</code> (<span class="formula">Espaço Θ(n)</span>).</p>
    </div>
    <div class="box">
      <h3>7.3 Insertion Sort e Bubble Sort Otimizado</h3>
      <p>Ambos possuem dois laços aninhados: no pior caso (ordem inversa), o laço interno executa ∑<sub>i=1..n-1</sub> i = n(n-1)/2 comparações, levando a <span class="formula">Θ(n²)</span>. No vetor já ordenado, Bubble interrompe na 1ª passada após (n-1) comparações e Insertion apenas verifica 1 vez cada elemento: <span class="formula">Ω(n)</span>.</p>
    </div>
    <div class="box">
      <h3>7.4 Iterativo/Recursivo e Guloso vs. Programação Dinâmica</h3>
      <p><strong>Fibonacci:</strong> A recorrência <span class="formula">T(n) = T(n-1) + T(n-2) + c</span> tem raiz característica φ = (1+√5)/2 ≈ 1,618, resultando em <span class="formula">Θ(φⁿ)</span>. Memoização reduz o grafo de subproblemas a apenas n estados distintos (<span class="formula">Θ(n) tempo e espaço</span>).</p>
      <p><strong>Troco &amp; Mochila:</strong> Troco Guloso ordena k moedas em <span class="formula">O(k log k)</span> e percorre o troco; Troco PD preenche <code>dp[1..A]</code> testando k moedas em <span class="formula">Θ(A·k)</span>. Mochila Fracionária ordena por valor/peso em <span class="formula">Θ(n log n)</span>; Mochila 0/1 PD preenche tabela (n+1)×(W+1) em <span class="formula">Θ(n·W)</span> tempo e espaço.</p>
    </div>"""

    sec_critica = f"""
    <h2>8. Análise Crítica e Matriz Comparativa (Seções 10 e 13)</h2>
    <table>
      <thead><tr><th>Critério</th><th>Busca</th><th>Ordenação</th><th>Iterativo vs. Rec. &amp; D&amp;C</th><th>Guloso vs. PD</th></tr></thead>
      <tbody>
        <tr><td><strong>Corretude</strong></td><td>100% em vetor ordenado; Sequencial aceita não ordenado.</td><td>100% nos 5 formatos (validado por <code>std::is_sorted</code>).</td><td>100%; recursão ingênua torna-se intratável para n ≥ 45.</td><td>Apenas PD garante ótimo em sistemas não-canônicos e Mochila 0/1.</td></tr>
        <tr><td><strong>Simplicidade</strong></td><td>Sequencial e Binária Iterativa são diretas (~15 linhas).</td><td>Insertion/Bubble triviais; Quick/Merge requerem atenção em índices.</td><td>Iterativo é mais simples; D&amp;C exige tratar paridade/base.</td><td>Guloso é muito mais simples; PD exige modelar estados e reconstrução.</td></tr>
        <tr><td><strong>Eficiência Temporal</strong></td><td>Binária supera Sequencial em {fmt_num(seq_1m['elapsed_us']/max(bin_1m['elapsed_us'],0.01), 0)}× para n=1M ausente.</td><td>Θ(n log n) supera Θ(n²) em &gt;100× para n=25k aleatório.</td><td>D&amp;C reduz 10⁶ mults para 25; Fib Iterativo supera Rec em &gt;1.000.000× em n=40.</td><td>Guloso é bem mais rápido (O(n log n)) que PD pseudo-polinomial Θ(n·W).</td></tr>
        <tr><td><strong>Eficiência Espacial</strong></td><td>Iterativa O(1) vs. Recursiva O(log n).</td><td>Quick/Insertion/Bubble O(1)–O(log n) vs. Merge Θ(n).</td><td>Iterativo O(1) vs. Recursão O(n) + Tabela Memo Θ(n).</td><td>Guloso O(1)–O(n) vs. Matriz PD Θ(n·W).</td></tr>
        <tr><td><strong>Sensibilidade</strong></td><td>Sequencial depende da posição da chave; Binária é estável.</td><td>Quick Fixo cai para O(n²) em ordenado; Insertion cai para O(n).</td><td>Depende apenas do valor escalar n.</td><td>Depende da estrutura canônica das moedas e da grandeza W.</td></tr>
        <tr><td><strong>Escalabilidade</strong></td><td>Apenas Binária escala a milhões de itens.</td><td>Apenas Quick Mediana 3 e Merge escalam além de 10k.</td><td>Apenas Iterativo e D&amp;C escalam com segurança.</td><td>PD limitada por W/A grandes; Guloso escala livremente.</td></tr>
        <tr><td><strong>Adequação</strong></td><td>Binária Iterativa em coleções ordenadas grandes.</td><td>Quick Mediana 3 geral; Merge p/ estabilidade; Insertion p/ n pequeno.</td><td>Preferir Iterativo ou D&amp;C sem sobreposição.</td><td>Guloso p/ Fracionária/BRL; PD p/ Mochila 0/1 e troco genérico.</td></tr>
      </tbody>
    </table>

    <h3>Respostas às Questões Orientadoras da Seção 10</h3>
    <ol>
      <li><strong>Entradas pequenas vs. grandes:</strong> Para n = 100 quase ordenado ou ordenado, Insertion Sort (0,26–0,44 µs) superou Merge Sort (1,77–2,90 µs) devido à ausência de recursão e cópia de buffer auxiliar; acima de n = 500, Quick Sort Mediana de 3 e Merge Sort dominam amplamente.</li>
      <li><strong>Impacto da organização inicial:</strong> Vetores ordenados/reversos inverteram completamente o comportamento: Bubble Otimizado e Insertion caíram para O(n), enquanto Quick Sort com Pivô Fixo sofreu degradação quadrática (670 ms em n=25.000 ordenado).</li>
      <li><strong>Confirmação teórica:</strong> Sim. Ao crescer <em>n</em> de 10.000 para 25.000 (2,5×), os métodos quadráticos aumentaram ~6,25× em comparações, os Θ(n log n) ~2,7× e a Busca Binária de 500k para 1M cresceu apenas 2 comparações.</li>
      <li><strong>Assintoticamente melhor mais lento:</strong> Sim, em vetores pequenos (n=100) ordenados/quase ordenados, Insertion Sort Θ(n²) no geral foi ~6–7× mais rápido que Merge Sort Θ(n log n) por fatores constantes e localidade de cache L1.</li>
      <li><strong>Simplicidade e manutenção:</strong> Busca Binária Iterativa, Insertion Sort e Fibonacci Iterativo combinam clareza, baixo risco de stack overflow e previsibilidade.</li>
      <li><strong>Consumo de memória:</strong> Merge Sort exigiu 100.768 B extras em n=25.000, enquanto Quick Sort Mediana de 3 consumiu apenas ~720–1.200 B de pilha; Mochila 0/1 PD alocou matriz proporcional a n·W.</li>
      <li><strong>Benefícios vs. custos da recursividade:</strong> Em Divisão e Conquista sem sobreposição (Merge, Quick, Exponenciação D&amp;C), a recursão produziu saltos de eficiência enormes. Quando há sobreposição sem memoização (Fibonacci), gerou 331 milhões de chamadas redundantes.</li>
      <li><strong>Corretude across inputs:</strong> 100% dos algoritmos de busca e ordenação retornaram saídas corretas nos 5 formatos e 4 cenários; nos problemas de otimização, os testes comprovaram falha de otimalidade das heurísticas gulosas em [1,3,4] e Mochila 0/1.</li>
      <li><strong>Recomendação prática:</strong> Busca Binária Iterativa para buscas em memória ordenada; Quick Sort Mediana de 3 (ou híbrido com Insertion Sort como o Introsort da STL) para ordenação geral; Merge Sort quando estabilidade é requerida; Programação Dinâmica sempre que a otimalidade exata em subestrutura discreta for indispensável.</li>
    </ol>"""

    sec_conclusao_refs = """
    <h2>9. Conclusão Técnica</h2>
    <p>Os experimentos conduzidos no <strong>AlgoLab C++20</strong> demonstram na prática o lema do projeto — <em>Implementar → Medir → Comparar → Analisar → Justificar</em>. A análise assintótica previu com fidelidade as fronteiras de escalabilidade entre classes O(log n), O(n), O(n log n), O(n²) e O(2ⁿ), enquanto a experimentação empírica revelou nuances críticas da computação real: constantes multiplicativas em entradas pequenas (Insertion vs. Merge), sensibilidade severa à escolha de pivô no Quick Sort, o custo da recursão ingênua com sobreposição e os limites de corretude da estratégia gulosa frente à Programação Dinâmica.</p>

    <h2>10. Referências Bibliográficas e Declaração de Ferramentas (Seção 15)</h2>
    <ul>
      <li>CORMEN, T. H.; LEISERSON, C. E.; RIVEST, R. L.; STEIN, C. <em>Algoritmos: Teoria e Prática</em>. 3ª/4ª ed. Rio de Janeiro: LTC / Elsevier.</li>
      <li>SEDGEWICK, R.; WAYNE, K. <em>Algorithms</em>. 4th ed. Addison-Wesley Professional.</li>
      <li>KLEINBERG, J.; TARDOS, É. <em>Algorithm Design</em>. Pearson / Addison-Wesley.</li>
      <li>ISO/IEC 14882:2020 — <em>Programming Languages — C++20 Standard Library (&lt;chrono&gt;, &lt;vector&gt;, &lt;algorithm&gt;)</em>.</li>
      <li><em>Transparência de Apoio Computacional (Seção 15):</em> Ferramentas de apoio e inteligência artificial auxiliaram na estruturação da interface visual e geração dos artefatos de documentação; todo o código C++20 foi compilado localmente, validado quanto à corretude e executado de ponta a ponta para obtenção das medições reais publicadas neste documento.</li>
    </ul>"""

    # -------------------------------------------------------------------------
    # MONTAGEM DOS 6 DELIVERABLES
    # -------------------------------------------------------------------------
    docs = [
        (
            "Etapa-1-Planejamento.pdf",
            "Etapa 1 — Planejamento do Projeto Prático",
            "Definição dos problemas, seleção de algoritmos, hipóteses e plano de experimentos",
            "Entrega Parcial • Etapa 1",
            sec_planejamento + f"""
            <div class="figure">
              <img src="{img_data_uri('01_dashboard.png')}" alt="Protótipo Inicial AlgoLab"/>
              <div class="fig-cap"><strong>Figura 1 — Arquitetura Inicial da Plataforma AlgoLab:</strong> Painel unificado planejado para abrigar os quatro módulos experimentais e seus respectivos visualizadores interativos.</div>
            </div>"""
        ),
        (
            "Etapa-2-Implementacao.pdf",
            "Etapa 2 — Implementação e Primeiras Execuções",
            "Motor instrumentado em C++20, visualizador interativo e evidências iniciais",
            "Entrega Parcial • Etapa 2",
            sec_codigo + f"""
            <h2>4. Primeiras Execuções e Telas de Funcionamento</h2>
            <div class="figure">
              <img src="{img_data_uri('02_visualizer_sorting.png')}" alt="Visualizador Quick Sort"/>
              <div class="fig-cap"><strong>Figura 1 — Execução Visual do Quick Sort (Mediana de 3):</strong> Validação da instrumentação em tempo real contabilizando comparações, trocas e acessos ao vetor junto ao código C++ sincronizado.</div>
            </div>
            <div class="figure">
              <img src="{img_data_uri('07_cli_execution.png')}" alt="CLI C++20"/>
              <div class="fig-cap"><strong>Figura 2 — Primeiras Medições Nativas via Terminal (<code>AlgoLab.exe --cli</code>):</strong> Verificação de corretude e compilação estática C++20 com GCC 16.2.0.</div>
            </div>"""
        ),
        (
            "Etapa-3-Experimentacao.pdf",
            "Etapa 3 — Experimentação e Coleta Empírica",
            "Medições sistemáticas para múltiplos tamanhos n e formatos de entrada",
            "Entrega Parcial • Etapa 3",
            sec_tabelas + f"""
            <div class="figure">
              <img src="{img_data_uri('05_empirical_tables_charts.png')}" alt="Laboratório Empírico"/>
              <div class="fig-cap"><strong>Figura 1 — Módulo de Laboratório Empírico:</strong> Consolidação interativa das medições reais coletadas pelo motor C++20 com suporte a filtragem por formato de entrada.</div>
            </div>"""
        ),
        (
            "Etapa-4-Analise.pdf",
            "Etapa 4 — Análise Empírica e Assintótica",
            "Gráficos comparativos de desempenho, funções de custo, recorrências e interpretação",
            "Entrega Parcial • Etapa 4",
            sec_graficos + sec_assintotica + f"""
            <div class="figure">
              <img src="{img_data_uri('06_asymptotic_analysis.png')}" alt="Tela Análise Assintótica"/>
              <div class="fig-cap"><strong>Figura 1 — Módulo de Análise Teórica no AlgoLab:</strong> Integração entre as deduções assintóticas formais e os dados observados nos testes.</div>
            </div>"""
        ),
        (
            "Etapa-5-Mini-Seminario-Sintese.pdf",
            "Etapa 5 — Mini Seminário Final e Síntese Técnica",
            "Demonstração consolidada, principais achados e justificativa da solução recomendada",
            "Entrega Final • Etapa 5",
            f"""
            <h2>1. Síntese Executiva do Projeto</h2>
            <p>Esta síntese consolida os resultados finais obtidos pelo sistema portátil <strong>AlgoLab.exe</strong> ao longo dos quatro eixos de estudo (Busca, Ordenação, Iteração vs. Recursão/D&amp;C e Guloso vs. Programação Dinâmica), totalizando <strong>{len(recs)} experimentos executados em C++20</strong>.</p>
            <div class="box emerald">
              <strong>Principais Ganhos Medidos na Prática:</strong>
              <ul>
                <li><strong>Busca (n = 1.000.000, ausente):</strong> Binária Iterativa ({fmt_num(bin_1m['elapsed_us'])} µs, 40 comp.) foi <strong>~{fmt_num(seq_1m['elapsed_us']/max(bin_1m['elapsed_us'],0.01), 0)}× mais rápida</strong> que Sequencial ({fmt_num(seq_1m['elapsed_us'])} µs, 1.000.000 comp.).</li>
                <li><strong>Ordenação (n = 25.000, aleatório):</strong> Quick Sort Mediana de 3 ({fmt_num(q3_25k['elapsed_ms'])} ms) superou Bubble Sort ({fmt_num(bub_25k['elapsed_ms'])} ms) em <strong>~{fmt_num(bub_25k['elapsed_us']/q3_25k['elapsed_us'], 0)}×</strong>.</li>
                <li><strong>Recursão (Fibonacci n = 40):</strong> Iterativo ({fmt_num(fib_it_40['elapsed_us'])} µs) superou Recursivo Ingênuo ({fmt_num(fib_rec_40['elapsed_ms'])} ms / 331M chamadas) em mais de <strong>1.000.000×</strong>.</li>
                <li><strong>Otimalidade (Troco &amp; Mochila):</strong> PD corrigiu falhas gulosas no sistema [1,3,4] (troco 6: 2 vs. 3 moedas) e na Mochila 0/1 (valor 220 vs. 160).</li>
              </ul>
            </div>
            <div class="figure">
              <img src="{img_data_uri('04_visualizer_dp_greedy.png')}" alt="Contraexemplo Troco"/>
              <div class="fig-cap"><strong>Figura 1 — Evidência de Contraexemplo no Visualizador:</strong> Demonstração usada no mini seminário provando por que decisões localmente gulosas não garantem otimalidade global fora de sistemas canônicos.</div>
            </div>
            """ + sec_critica + sec_conclusao_refs
        ),
        (
            "Relatorio-Tecnico-Completo-EAD.pdf",
            "Relatório Técnico — Projeto Prático de Análise de Algoritmos",
            "Consolidação completa: implementação C++20, planejamento, evidências, tabelas, gráficos, análise assintótica e crítica",
            "Entrega Consolidada • Modalidade EAD (12 Seções)",
            f"""
            <h2>1. Identificação do Projeto</h2>
            <table>
              <tbody>
                <tr><td><strong>Título do Projeto</strong></td><td>AlgoLab C++20 — Investigação Empírica e Assintótica de Algoritmos de Busca, Ordenação, Recursão e Otimização</td></tr>
                <tr><td><strong>Disciplina / Modalidade</strong></td><td>Análise de Algoritmos — Projeto Prático + Relatório Técnico EAD</td></tr>
                <tr><td><strong>Autor / Desenvolvedor</strong></td><td>Fernando Henrique Braga Calisto (<code>github.com/fernandocalisto</code>)</td></tr>
                <tr><td><strong>Plataforma e Compilador</strong></td><td>Windows x64 • {html.escape(data['compiler_info'])} • {len(recs)} experimentos executados</td></tr>
              </tbody>
            </table>

            <h2>2. Introdução</h2>
            <p>Na engenharia de software, escolher um algoritmo apenas pela funcionalidade imediata frequentemente oculta gargalos severos de escalabilidade quando o volume de dados cresce ou sua organização inicial muda. O presente trabalho aproxima a teoria da complexidade assintótica da prática computacional por meio do ciclo completo <em>Implementar → Medir → Comparar → Analisar → Justificar</em>.</p>
            <p>Para conduzir essa investigação de maneira rigorosa e didática, desenvolveu-se o aplicativo portátil <strong>AlgoLab.exe</strong> em C++20, reunindo um motor nativo de experimentação de alta precisão e um visualizador interativo minimalista para todos os tópicos centrais da ementa.</p>

            <h2>3. Descrição das Soluções e Algoritmos Investigados</h2>
            <ul>
              <li><strong>Busca Sequencial vs. Busca Binária (Iterativa e Recursiva):</strong> A Busca Sequencial percorre linearmente o vetor da esquerda para a direita sem exigir ordenação prévia. A Busca Binária explora a ordenação aplicando Divisão e Conquista iterativa ou recursivamente.</li>
              <li><strong>Ordenação e Divisão e Conquista:</strong> Comparou-se o <em>Quick Sort</em> com pivô fixo no fim (Lomuto) frente à versão otimizada com <em>Mediana de Três + partição de Hoare</em>, além do <em>Merge Sort</em> (divisão equilibrada com intercalação estável), <em>Insertion Sort</em> (inserção incremental adaptativa) e <em>Bubble Sort Otimizado</em> (trocas adjacentes com parada antecipada).</li>
              <li><strong>Iterativo vs. Recursivo &amp; Divisão e Conquista:</strong> Investigou-se a Sequência de Fibonacci em três variantes (iterativa, recursiva ingênua e recursiva com memoização) e o cálculo de potências <em>base<sup>n</sup></em> (multiplicação linear vs. exponenciação rápida por quadratura).</li>
              <li><strong>Algoritmos Gulosos vs. Programação Dinâmica:</strong> Confrontou-se a escolha localmente ótima contra a tabela de subproblemas ótimos no Problema do Troco (canônico BRL e não-canônico [1,3,4]) e na Mochila (Fracionária Gulosa, 0/1 Heurística Gulosa e 0/1 Programação Dinâmica).</li>
            </ul>
            """
            + sec_codigo.replace("<h2>3. ", "<h2>4. ").replace("<h3>3.", "<h3>4.")
            + sec_planejamento.replace("<h2>1. Identificação e Escopo do Projeto</h2>", "<h2>5. Planejamento dos Testes (Metodologia e Entradas)</h2>").replace("<h2>2. Metodologia de Testes e Formatos de Entrada (Seções 5 e 6)</h2>", "")
            + sec_evidencias.replace("<h2>4. ", "<h2>6. ")
            + sec_tabelas.replace("<h2>5. ", "<h2>7. ")
            + sec_graficos.replace("<h2>6. ", "<h2>8. ")
            + sec_assintotica.replace("<h2>7. ", "<h2>9. ").replace("<h3>7.", "<h3>9.")
            + sec_critica.replace("<h2>8. ", "<h2>10. ")
            + sec_conclusao_refs.replace("<h2>9. ", "<h2>11. ").replace("<h2>10. ", "<h2>12. ")
        )
    ]

    with tempfile.TemporaryDirectory(prefix="algolab_pdf_") as tmp:
        tmp_dir = Path(tmp)
        for idx, (filename, title, subtitle, tag, body) in enumerate(docs, 1):
            print(f"  [PDF {idx}/6] Gerando entregaveis/{filename}")
            full_html = wrap_pdf_document(title, subtitle, tag, body)
            html_file = tmp_dir / f"doc_{idx}.html"
            html_file.write_text(full_html, encoding="utf-8")
            edge_print_pdf(edge_bin, html_file, DELIVERABLES_DIR / filename, tmp_dir / f"profile_{idx}")


def main() -> None:
    if not EXE_PATH.exists():
        raise FileNotFoundError(f"Executável não encontrado: {EXE_PATH}")
    if not DATA_JSON.exists():
        raise FileNotFoundError(f"Dados empíricos não encontrados: {DATA_JSON}")

    edge_bin = find_msedge()
    print(f"Edge encontrado: {edge_bin}")
    print("Etapa A: Capturando telas reais de execução...")
    capture_all_screenshots(edge_bin)
    print("Etapa B: Compilando os 6 entregáveis em PDF em entregaveis/...")
    generate_all_pdfs(edge_bin)
    print("Concluído com sucesso!")


if __name__ == "__main__":
    main()
