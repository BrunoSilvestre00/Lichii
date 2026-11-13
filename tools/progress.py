#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import collections
import dataclasses
import json
import re
import sys
import urllib.error
import urllib.request
from collections.abc import Iterator, Sequence
from enum import Enum
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SERVICES = Path("src/core/hle/service")
OPCODES = Path("src/shader_recompiler/frontend/ir/opcodes.inc")
SPIRV = Path("src/shader_recompiler/backend/spirv")
COMPATIBILITY = "https://lichii.zortos.me/api/summary"
REPOSITORY = "zortos293/Lichii"
BRANCH = "nx2"

TABLE = re.compile(r"FunctionInfo\s+\w+\s*\[\s*\]\s*=\s*\{")
ROW = re.compile(r'\{\s*(?:0x[0-9a-fA-F]+|\d+)\s*,\s*(.*?)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
EMITTER = re.compile(r"\b(?:Id|void)\s+Emit(\w+)\s*\([^;{}]*\)\s*\{")
OPCODE = re.compile(r"^OPCODE\((\w+)", re.MULTILINE)
THROW = re.compile(r"throw\s+NotImplementedException\s*\([^;]*\)\s*;")
STUB_HANDLER = re.compile(r"::Stub\w*")
CODE_NOISE = re.compile(r"[\s{}();]|\(void\)\s*\w+")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")

WIDTH = 960
COLUMN = WIDTH // 2
ROW_HEIGHT = 24
BAR_X = 116
BAR_WIDTH = 232
LABEL_X = BAR_X + BAR_WIDTH + 8
PANEL_HEIGHT = 34
GAP = 10
DONE_COLOR = "#2ea043"
TODO_COLOR = "#6e7681"
BORDER = "#0d1117"
TEXT = "#ffffff"
MUTED = "#8b949e"
SOURCE = f"https://github.com/{REPOSITORY}/tree/{BRANCH}/{SERVICES.as_posix()}"
TABLE_HEAD = ("<table><tr><th>Module</th><th>Bound</th><th>Stub handler</th><th>Unbound</th>"
              "<th>Declared</th><th>Bound %</th></tr>")
PAGE_STYLE = (
    "body{background:#0d1117;color:#c9d1d9;font:14px/1.5 -apple-system,Segoe UI,sans-serif;"
    "margin:2rem auto;max-width:60rem;padding:0 1rem}"
    "table{border-collapse:collapse;width:100%;margin:1rem 0}"
    "th,td{border-bottom:1px solid #30363d;padding:.3rem .6rem;text-align:left}"
    "th{color:#8b949e;font-weight:600}code{color:#79c0ff}a{color:#58a6ff}"
)


class Handler(Enum):
    BOUND = "bound"
    STUB = "stub"
    UNBOUND = "unbound"


@dataclasses.dataclass(frozen=True, slots=True)
class Metric:
    done: int
    total: int

    @property
    def percent(self) -> float:
        return round(100 * self.done / self.total, 2) if self.total else 0.0


@dataclasses.dataclass(frozen=True, slots=True)
class Module:
    name: str
    counts: tuple[tuple[Handler, int], ...]

    def count(self, handler: Handler) -> int:
        return dict(self.counts).get(handler, 0)

    @property
    def total(self) -> int:
        return sum(count for _, count in self.counts)

    @property
    def metric(self) -> Metric:
        return Metric(self.count(Handler.BOUND), self.total)


@dataclasses.dataclass(frozen=True, slots=True)
class Services:
    modules: tuple[Module, ...]

    @property
    def metric(self) -> Metric:
        return Metric(sum(module.metric.done for module in self.modules),
                      sum(module.metric.total for module in self.modules))

    def sorted(self) -> tuple[Module, ...]:
        return tuple(sorted(self.modules, key=lambda module: (-module.total, module.name)))


@dataclasses.dataclass(frozen=True, slots=True)
class Shaders:
    implemented: int
    missing: int

    @property
    def metric(self) -> Metric:
        return Metric(self.implemented, self.implemented + self.missing)


@dataclasses.dataclass(frozen=True, slots=True)
class Compatibility:
    games: int
    reports: int
    playable: int
    perfect: int
    build: int


@dataclasses.dataclass(frozen=True, slots=True)
class Report:
    services: Services
    shaders: Shaders
    compatibility: Compatibility | None


def block_end(text: str, start: int) -> int:
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text)


def strip_comments(text: str) -> str:
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def iter_tables(text: str) -> Iterator[str]:
    for match in TABLE.finditer(text):
        brace = match.end() - 1
        yield text[brace:block_end(text, brace) + 1]


def classify(expression: str) -> Handler:
    if expression.strip() == "nullptr":
        return Handler.UNBOUND
    if STUB_HANDLER.search(expression):
        return Handler.STUB
    return Handler.BOUND


def service_files(root: Path) -> tuple[Path, ...]:
    directory = root / SERVICES
    return tuple(sorted(directory.rglob("*.cpp"))) + tuple(sorted(directory.rglob("*.h")))


def module_name(path: Path, root: Path) -> str:
    relative = path.relative_to(root / SERVICES)
    return relative.parts[0] if len(relative.parts) > 1 else relative.stem


def scan_services(root: Path) -> Services:
    tallies: dict[str, collections.Counter[Handler]] = {}
    for path in service_files(root):
        counter = tallies.setdefault(module_name(path, root), collections.Counter())
        text = strip_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for table in iter_tables(text):
            for match in ROW.finditer(table):
                counter[classify(match.group(1))] += 1
    modules = tuple(Module(name, tuple(sorted(counter.items(), key=lambda item: item[0].value)))
                    for name, counter in tallies.items() if counter)
    return Services(modules)


def emitter_bodies(root: Path) -> dict[str, list[str]]:
    bodies: dict[str, list[str]] = {}
    for path in sorted((root / SPIRV).rglob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in EMITTER.finditer(text):
            brace = match.end() - 1
            bodies.setdefault(match.group(1), []).append(text[brace:block_end(text, brace) + 1])
    return bodies


def has_no_work(body: str) -> bool:
    if "NotImplementedException" not in body:
        return False
    return not CODE_NOISE.sub("", THROW.sub("", body))


def scan_shaders(root: Path) -> Shaders:
    text = (root / OPCODES).read_text(encoding="utf-8", errors="ignore")
    opcodes = OPCODE.findall(text)
    bodies = emitter_bodies(root)
    missing = sum(1 for name in opcodes if not bodies.get(name) or all(map(has_no_work, bodies[name])))
    return Shaders(len(opcodes) - missing, missing)


def parse_compatibility(payload: object) -> Compatibility | None:
    if not isinstance(payload, dict):
        return None
    overall = payload.get("overall")
    counts = overall.get("counts") if isinstance(overall, dict) else None
    values = [payload.get("gamesTested"), payload.get("latestBuild"),
              overall.get("total") if isinstance(overall, dict) else None,
              overall.get("playable") if isinstance(overall, dict) else None,
              counts.get("Perfect") if isinstance(counts, dict) else None]
    if not all(isinstance(value, int) for value in values):
        return None
    games, build, total, playable, perfect = (int(value) for value in values)
    return Compatibility(games, total, playable, perfect, build)


def fetch_compatibility(url: str, timeout: float) -> Compatibility | None:
    if not url:
        return None
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return parse_compatibility(json.loads(response.read().decode("utf-8")))
    except (OSError, ValueError, urllib.error.URLError) as error:
        print(f"warning: compatibility numbers left out: {error}", file=sys.stderr)
        return None


def percent_color(percent: float) -> str:
    return "#2ea043" if percent >= 90 else "#97ca00" if percent >= 60 else "#dfb317" if percent >= 30 else "#fe7d37"


def svg_open(width: float, height: float) -> str:
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
            f'width="{width}" height="{height}">')


def heading(text: str, top: float) -> str:
    return f'<text x="0" y="{top + 12}" fill="{TEXT}" font-size="13">{escape(text)}</text>'


def detail_text(text: str, y: float) -> str:
    return f'<text x="0" y="{y}" fill="{MUTED}" font-size="10" font-family="monospace">{escape(text)}</text>'


def badge(label: str, value: str, percent: float) -> str:
    left, right = 10 + 7 * len(label), 10 + 7 * len(value)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{left + right}" height="20" role="img" '
            f'aria-label="{label}: {value}"><rect width="{left}" height="20" fill="#555"/>'
            f'<rect x="{left}" width="{right}" height="20" fill="{percent_color(percent)}"/>'
            f'<g fill="#fff" font-family="Verdana,DejaVu Sans,sans-serif" font-size="11" text-anchor="middle">'
            f'<text x="{left / 2}" y="14">{label}</text><text x="{left + right / 2}" y="14">{value}</text></g></svg>')


def bar(x: float, y: float, width: float, fraction: float, height: float = 12) -> list[str]:
    filled = round(width * max(0.0, min(1.0, fraction)), 2)
    return [f'<rect x="{x}" y="{y}" width="{width}" height="{height}" fill="{TODO_COLOR}" rx="2"/>',
            f'<rect x="{x}" y="{y}" width="{filled}" height="{height}" fill="{DONE_COLOR}" rx="2"/>']


def module_row(module: Module, x: float, y: float) -> list[str]:
    metric = module.metric
    label = f'<text x="{x}" y="{y + 10}" fill="{TEXT}" font-size="11" font-family="monospace">{escape(module.name)}</text>'
    counts = f'<text x="{x + LABEL_X}" y="{y + 10}" fill="{MUTED}" font-size="10" font-family="monospace">'
    return [label, *bar(x + BAR_X, y, BAR_WIDTH, metric.percent / 100),
            f"{counts}{module.count(Handler.BOUND)}/{module.total}</text>"]


def services_panel(modules: Sequence[Module], top: float) -> tuple[list[str], float]:
    columns = [modules[index::2] for index in range(2)]
    height = PANEL_HEIGHT + ROW_HEIGHT * max((len(column) for column in columns), default=0)
    parts = [heading("IPC commands bound to a handler, by module", top)]
    for column_index, column in enumerate(columns):
        for row_index, module in enumerate(column):
            parts += module_row(module, column_index * COLUMN, top + PANEL_HEIGHT + row_index * ROW_HEIGHT)
    return parts, top + height


def shaders_panel(shaders: Shaders, top: float) -> tuple[list[str], float]:
    metric = shaders.metric
    detail = f"{metric.done} of {metric.total} ({metric.percent}%), {shaders.missing} throw NotImplementedException"
    return [heading("Shader IR opcodes with a working SPIR-V emitter", top),
            *bar(0, top + 22, WIDTH, metric.percent / 100, 14),
            detail_text(detail, top + 50)], top + 62


def compatibility_panel(compatibility: Compatibility, top: float) -> tuple[list[str], float]:
    detail = (f"{compatibility.reports} reports across {compatibility.games} games on build "
              f"{compatibility.build}, {compatibility.playable} playable, {compatibility.perfect} perfect")
    return [heading("Community compatibility reports", top), detail_text(detail, top + 32)], top + 46


def render_svg(report: Report) -> str:
    parts, top = services_panel(report.services.sorted(), 0)
    block, top = shaders_panel(report.shaders, top + GAP)
    parts += block
    if report.compatibility:
        block, top = compatibility_panel(report.compatibility, top + GAP)
        parts += block
    height = round(top)
    frame = f'<rect width="{WIDTH}" height="{height}" fill="{BORDER}"/>'
    return "\n".join([svg_open(WIDTH, height), frame, *parts, "</svg>"])


def module_rows(modules: Sequence[Module]) -> Iterator[str]:
    for module in modules:
        metric = module.metric
        yield (f'<tr><td><code>{escape(module.name)}</code></td><td>{module.count(Handler.BOUND)}</td>'
               f'<td>{module.count(Handler.STUB)}</td><td>{module.count(Handler.UNBOUND)}</td>'
               f'<td>{metric.total}</td><td>{metric.percent}%</td></tr>')


def render_html(report: Report) -> str:
    services, shaders = report.services.metric, report.shaders.metric
    compatibility = ""
    if report.compatibility:
        compatibility = (f"<p>{report.compatibility.reports} community reports across "
                         f"{report.compatibility.games} games on build {report.compatibility.build}: "
                         f"{report.compatibility.playable} playable, {report.compatibility.perfect} perfect.</p>")
    summary = (f"<p>Services: <strong>{services.percent}%</strong> of declared IPC commands ({services.done} of "
               f"{services.total}) are bound to a handler. Shaders: <strong>{shaders.percent}%</strong> of IR "
               f"opcodes ({shaders.done} of {shaders.total}) have a working SPIR-V emitter.</p>")
    note = ('A row with <code>nullptr</code> declares a command the emulator does not handle, and a row bound '
            "to a <code>Stub*</code> helper is bound but returns a placeholder.</p>")
    return "\n".join([
        "<!DOCTYPE html>", '<html lang="en"><head><meta charset="utf-8">',
        "<title>Lichii progress</title>", f"<style>{PAGE_STYLE}</style></head><body>",
        "<h1>Lichii progress</h1>", summary, compatibility,
        "<h2>System services</h2>", TABLE_HEAD,
        *module_rows(report.services.sorted()), "</table>",
        f'<p>Counted from the <code>FunctionInfo</code> tables in <a href="{SOURCE}">{SERVICES.as_posix()}</a>. {note}',
        '<p><a href="history.html">Progress history</a></p>',
        "</body></html>",
    ])


def site_data(report: Report) -> dict[str, object]:
    services = report.services.metric
    shaders = report.shaders.metric
    data: dict[str, object] = {
        "services": {"bound": services.done, "total": services.total, "percent": services.percent,
                     "modules": [{"name": module.name, "bound": module.count(Handler.BOUND),
                                  "stub": module.count(Handler.STUB),
                                  "unbound": module.count(Handler.UNBOUND)}
                                 for module in report.services.sorted()]},
        "shaders": {"implemented": shaders.done, "total": shaders.total, "percent": shaders.percent},
    }
    if report.compatibility:
        data["compatibility"] = dataclasses.asdict(report.compatibility)
    return data


def write_site(output: Path, report: Report) -> None:
    output.mkdir(parents=True, exist_ok=True)
    services, shaders = report.services.metric, report.shaders.metric
    (output / "progress.json").write_text(json.dumps(site_data(report), indent=2) + "\n", encoding="utf-8")
    (output / "badge-services.svg").write_text(
        badge("services", f"{services.percent}%", services.percent), encoding="utf-8")
    (output / "badge-shaders.svg").write_text(
        badge("shaders", f"{shaders.percent}%", shaders.percent), encoding="utf-8")
    (output / "progress.svg").write_text(render_svg(report), encoding="utf-8")
    (output / "progress.html").write_text(render_html(report), encoding="utf-8")


def snapshot_metric(snapshot: object, key: str, done: str, total: str) -> Metric | None:
    if not isinstance(snapshot, dict):
        return None
    section = snapshot.get(key)
    if not isinstance(section, dict):
        return None
    values = section.get(done), section.get(total)
    if not all(isinstance(value, int) for value in values):
        return None
    return Metric(int(values[0]), int(values[1]))


def format_delta(before: Metric, after: Metric) -> str:
    change = after.done - before.done
    return f"{before.done}/{before.total} -> {after.done}/{after.total} ({change:+d})"


def report(base: object, head: object) -> str:
    lines = []
    for label, key, done in (("IPC commands bound", "services", "bound"),
                             ("Shader IR opcodes", "shaders", "implemented")):
        before = snapshot_metric(base, key, done, "total")
        after = snapshot_metric(head, key, done, "total")
        if before is None or after is None or before.done == after.done:
            continue
        lines.append(f"| {label} | {before.done}/{before.total} | {after.done}/{after.total} | "
                     f"{after.done - before.done:+d} |")
    if not lines:
        return ""
    return "\n".join(["| Measure | Before | After | Delta |", "| --- | --- | --- | --- |", *lines]) + "\n"


def load_snapshot(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--root", type=Path, help="source tree to measure instead of the one containing this script")
    parser.add_argument("--json", action="store_true", help="print progress.json to stdout")
    parser.add_argument("--compare", type=Path, nargs=2, metavar=("BASE", "HEAD"),
                        help="print a markdown report of the changes between two progress.json files")
    parser.add_argument("--compatibility-url", default="", help="compatibility API to read; empty skips it")
    args = parser.parse_args(argv)
    if args.compare:
        try:
            print(report(load_snapshot(args.compare[0]), load_snapshot(args.compare[1])), end="")
        except (OSError, ValueError) as error:
            parser.error(f"{error}")
        return 0
    root = args.root or ROOT
    if not (root / SERVICES).is_dir():
        parser.error(f"{root} is not a source tree: {SERVICES} not found")
    measured = Report(scan_services(root), scan_shaders(root),
                      fetch_compatibility(args.compatibility_url, timeout=15))
    if args.output:
        write_site(args.output, measured)
    if args.json:
        print(json.dumps(site_data(measured), indent=2))
    else:
        services, shaders = measured.services.metric, measured.shaders.metric
        print(f"services {services.done}/{services.total} ({services.percent}%)")
        print(f"shaders {shaders.done}/{shaders.total} ({shaders.percent}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
