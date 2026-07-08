from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


COLS = 152
ROWS = 44

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "docs" / "tegrastats-tui-preview"

PALETTE = {
    "bg": (10, 14, 18),
    "panel": (16, 21, 28),
    "panel2": (20, 27, 35),
    "line": (76, 88, 104),
    "muted": (128, 141, 157),
    "text": (220, 229, 238),
    "green": (75, 196, 118),
    "yellow": (232, 184, 72),
    "red": (238, 83, 80),
    "cyan": (80, 190, 220),
    "blue": (104, 148, 255),
    "magenta": (204, 136, 255),
    "empty": (43, 51, 62),
}

ANSI = {
    name: f"\x1b[38;2;{r};{g};{b}m" for name, (r, g, b) in PALETTE.items()
}
ANSI_BG = {
    name: f"\x1b[48;2;{r};{g};{b}m" for name, (r, g, b) in PALETTE.items()
}
RESET = "\x1b[0m"


@dataclass
class Cell:
    ch: str = " "
    fg: str = "text"
    bg: str = "bg"


class Term:
    def __init__(self, cols: int, rows: int) -> None:
        self.cols = cols
        self.rows = rows
        self.cells = [[Cell() for _ in range(cols)] for _ in range(rows)]

    def put(self, x: int, y: int, text: str, fg: str = "text", bg: str | None = None) -> None:
        if y < 0 or y >= self.rows:
            return
        for i, ch in enumerate(text):
            xx = x + i
            if 0 <= xx < self.cols:
                cell = self.cells[y][xx]
                cell.ch = ch
                cell.fg = fg
                if bg is not None:
                    cell.bg = bg

    def fill(self, x: int, y: int, w: int, h: int, bg: str) -> None:
        for yy in range(y, min(y + h, self.rows)):
            for xx in range(x, min(x + w, self.cols)):
                self.cells[yy][xx].bg = bg

    def box(self, x: int, y: int, w: int, h: int, title: str = "", fg: str = "line", bg: str = "panel") -> None:
        self.fill(x, y, w, h, bg)
        self.put(x, y, "┌" + "─" * (w - 2) + "┐", fg, bg)
        for yy in range(y + 1, y + h - 1):
            self.put(x, yy, "│", fg, bg)
            self.put(x + w - 1, yy, "│", fg, bg)
        self.put(x, y + h - 1, "└" + "─" * (w - 2) + "┘", fg, bg)
        if title:
            label = f" {title} "
            self.put(x + 2, y, label[: w - 4], "cyan", bg)

    def plain_text(self) -> str:
        return "\n".join("".join(cell.ch for cell in row).rstrip() for row in self.cells)

    def ansi_text(self) -> str:
        lines: list[str] = []
        for row in self.cells:
            parts: list[str] = []
            fg = bg = None
            for cell in row:
                if cell.fg != fg:
                    parts.append(ANSI[cell.fg])
                    fg = cell.fg
                if cell.bg != bg:
                    parts.append(ANSI_BG[cell.bg])
                    bg = cell.bg
                parts.append(cell.ch)
            parts.append(RESET)
            lines.append("".join(parts).rstrip())
        return "\n".join(lines) + "\n"


def pct_color(value: float, warn: float = 80.0, crit: float = 90.0) -> str:
    if value >= crit:
        return "red"
    if value >= warn:
        return "yellow"
    return "green"


def bar(term: Term, x: int, y: int, width: int, pct: float, color: str) -> None:
    pct = max(0.0, min(100.0, pct))
    filled = round(width * pct / 100.0)
    if filled:
        term.put(x, y, "█" * filled, color)
    if filled < width:
        term.put(x + filled, y, "░" * (width - filled), "empty")


def metric(
    term: Term,
    x: int,
    y: int,
    w: int,
    name: str,
    value: str,
    pct: float,
    detail: str = "",
    warn: float = 80.0,
    crit: float = 90.0,
    color: str | None = None,
) -> None:
    label_w = 16
    value_w = 15
    detail_w = 10
    bar_w = max(8, w - label_w - value_w - detail_w - 2)
    c = color or pct_color(pct, warn, crit)
    term.put(x, y, name[: label_w - 1].ljust(label_w), "muted")
    term.put(x + label_w, y, value[: value_w - 1].rjust(value_w), c)
    bar(term, x + label_w + value_w + 1, y, bar_w, pct, c)
    if detail:
        suffix_x = x + w - detail_w
        term.put(suffix_x, y, detail[:detail_w].rjust(detail_w), c if c in ("yellow", "red") else "muted")


def spark(values: Iterable[int]) -> str:
    chars = "._:-=+*#"
    return "".join(chars[min(7, max(0, round(v / 100 * 7)))] for v in values)


def draw_header(term: Term) -> None:
    term.box(0, 0, COLS, 4, "", bg="panel2")
    term.put(2, 1, "tegrastats-tui THOR", "cyan", "panel2")
    term.put(25, 1, "interval 1ms", "yellow", "panel2")
    term.put(40, 1, "sample #12843", "muted", "panel2")
    term.put(57, 1, "parse 0.07ms", "muted", "panel2")
    term.put(72, 1, "draw 0.42ms", "muted", "panel2")
    term.put(87, 1, "drop 0", "green", "panel2")
    term.put(98, 1, "WARN GPU_TEMP GPC1 NVENC0", "red", "panel2")
    summary = "CPU14 avg 41% | RAM 42.6/64.0G | GR3D 91%@3GPC | EMC 64%@3200 | MAX TEMP 86.4C | VIN 38.6/168W"
    term.put(2, 2, summary[: COLS - 4], "text", "panel2")


def draw_memory_cpu(term: Term) -> None:
    x, y, w, h = 0, 4, 67, 25
    term.box(x, y, w, h, "MEMORY + 14 CPU CLUSTERS")
    yy = y + 2
    rows = [
        ("RAM", "42600/64000MB", 66.6, "lfb512x4"),
        ("SWAP", "768/8192MB", 9.4, "cached128"),
        ("IRAM", "0/252kB", 0.0, "lfb252k"),
        ("LFB", "512x4MB", 60.0, "frag ok"),
        ("CPU_AVG", "41%", 41.0, "14 on"),
    ]
    cpu = [
        ("C00", "78%@2201", 78),
        ("C01", "81%@2201", 81),
        ("C02", "55%@1830", 55),
        ("C03", "92%@2201", 92),
        ("C04", "61%@1830", 61),
        ("C05", "44%@1536", 44),
        ("C06", "18%@1152", 18),
        ("C07", "20%@1152", 20),
        ("C08", "33%@1536", 33),
        ("C09", "37%@1536", 37),
        ("C10", "28%@1152", 28),
        ("C11", "25%@1152", 25),
        ("C12", "12%@729", 12),
        ("C13", "16%@729", 16),
    ]
    for name, value, pct, detail in rows:
        metric(term, x + 2, yy, w - 4, name, value, pct, detail)
        yy += 1
    term.put(x + 2, yy, "─" * (w - 4), "line")
    yy += 1
    for name, value, pct in cpu:
        metric(term, x + 2, yy, w - 4, name, value, pct)
        yy += 1


def draw_engines(term: Term) -> None:
    x, y, w, h = 67, 4, 85, 25
    term.box(x, y, w, h, "THOR ENGINES / EACH INSTANCE")
    rows = [
        ("EMC_FREQ", "64%@3200", 64, "memctl"),
        ("GR3D_ACT", "91%", 91, "shared"),
        ("GPC0", "91%@1098", 91, "freq0"),
        ("GPC1", "91%@1098", 91, "freq1"),
        ("GPC2", "91%@1036", 91, "freq2"),
        ("VIC", "32%@1152", 32, "post"),
        ("APE", "150MHz", 50, "audio"),
        ("NVENC0", "88%@1296", 88, "WARN"),
        ("NVENC1", "04%@1296", 4, "idle"),
        ("NVDEC0", "46%@1296", 46, "dec0"),
        ("NVDEC1", "41%@1296", 41, "dec1"),
        ("NVJPG0", "00%@755", 0, "idle"),
        ("NVJPG1", "52%@755", 52, "jpg1"),
        ("OFA", "17%@1296", 17, "flow"),
        ("PVA0_VPU0", "63%@1011", 63, "pva0"),
        ("PVA0_VPU1", "12%@1011", 12, "pva0"),
        ("PVA1_VPU0", "00%@1011", 0, "pva1"),
        ("PVA1_VPU1", "00%@1011", 0, "pva1"),
    ]
    yy = y + 2
    for name, value, pct, detail in rows:
        metric(term, x + 2, yy, w - 4, name, value, pct, detail, warn=75 if name.startswith("NVENC") else 80)
        yy += 1


def draw_power(term: Term) -> None:
    x, y, w, h = 0, 29, 67, 13
    term.box(x, y, w, h, "THERMAL + THOR POWER")
    rows = [
        ("MCPU_TEMP", "81.2C", 81.2, "trip85"),
        ("GPU_TEMP", "86.4C", 86.4, "WARN"),
        ("SOC_TEMP", "72.0C", 72.0, "ok"),
        ("BOARD_TEMP", "54.1C", 54.1, "ok"),
        ("FAN", "4200rpm", 70.0, "70%"),
        ("VDD_GPU", "18.2/30.0W", 60.7, "avg17.1"),
        ("VDD_CPU_SOC_MSS", "12.5/40.0W", 31.2, "avg11.8"),
        ("VIN_SYS_5V0", "7.6/15.0W", 50.7, "avg7.1"),
        ("VIN", "38.6/168W", 23.0, "cap168"),
        ("OC_EVENT", "1/3", 33.0, "OC2"),
    ]
    yy = y + 2
    for name, value, pct, detail in rows:
        warn = 78 if "TEMP" in name else 80
        crit = 85 if "TEMP" in name else 90
        metric(term, x + 2, yy, w - 4, name, value, pct, detail, warn=warn, crit=crit)
        yy += 1


def draw_history(term: Term) -> None:
    x, y, w, h = 67, 29, 85, 13
    term.box(x, y, w, h, "HISTORY + UNKNOWN + RAW TAIL")
    lines = [
        ("GPU  ", "91%", [61, 64, 70, 76, 82, 91, 90, 88, 91, 93, 91, 91], "red"),
        ("EMC  ", "64%", [30, 35, 42, 48, 52, 57, 60, 63, 64, 63, 65, 64], "green"),
        ("CPU  ", "41%", [22, 28, 31, 39, 45, 48, 47, 43, 41, 39, 42, 41], "green"),
        ("RAM  ", "67%", [63, 63, 64, 64, 65, 65, 66, 66, 66, 67, 67, 67], "green"),
        ("VIN  ", "39W", [20, 21, 23, 26, 27, 30, 35, 39, 38, 37, 39, 39], "green"),
    ]
    yy = y + 2
    for label, value, values, color in lines:
        term.put(x + 2, yy, label, "muted")
        term.put(x + 8, yy, value.rjust(4), color)
        term.put(x + 14, yy, spark(values), color)
        bar(term, x + 29, yy, 24, values[-1], color)
        term.put(x + 56, yy, f"last {values[-1]:>3}", "muted")
        yy += 1
    term.put(x + 2, yy, "UNKNOWN", "yellow")
    term.put(x + 11, yy, "board=Jetson_AGX_Thor mode=MAXN drive_os=7.x", "muted")
    yy += 1
    term.put(x + 2, yy, "RAW", "cyan")
    term.put(x + 7, yy, "RAM 42600/64000MB ... NVENC0 88%@1296 NVENC1 04%@1296", "muted")
    yy += 1
    term.put(x + 2, yy, "RAW", "cyan")
    term.put(x + 7, yy, "PVA0_FREQ [63%,12%]@1011 PVA1_FREQ [0%,0%]@1011", "muted")
    yy += 1
    term.put(x + 2, yy, "NOTE", "magenta")
    term.put(x + 7, yy, "Unrecognized tokens stay visible instead of being dropped.", "muted")


def draw_footer(term: Term) -> None:
    term.box(0, 42, COLS, 2, "", bg="panel2")
    term.put(2, 43, "q quit   +/- interval 1-100ms   p pause   r raw   f filter   t thresholds   instances: separated", "muted", "panel2")


def render_png(term: Term, path: Path) -> None:
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as exc:
        raise SystemExit("Pillow is required to render PNG output") from exc

    font_paths = [
        Path("C:/Windows/Fonts/CascadiaMono.ttf"),
        Path("C:/Windows/Fonts/consola.ttf"),
        Path("C:/Windows/Fonts/DejaVuSansMono.ttf"),
    ]
    for font_path in font_paths:
        if font_path.exists():
            font = ImageFont.truetype(str(font_path), 15)
            break
    else:
        font = ImageFont.load_default()

    sample = Image.new("RGB", (32, 32), PALETTE["bg"])
    draw = ImageDraw.Draw(sample)
    bbox = draw.textbbox((0, 0), "M", font=font)
    cell_w = max(8, bbox[2] - bbox[0] + 1)
    cell_h = 19
    margin = 12
    image = Image.new("RGB", (term.cols * cell_w + margin * 2, term.rows * cell_h + margin * 2), PALETTE["bg"])
    draw = ImageDraw.Draw(image)

    for y, row in enumerate(term.cells):
        for x, cell in enumerate(row):
            px = margin + x * cell_w
            py = margin + y * cell_h
            draw.rectangle([px, py, px + cell_w, py + cell_h], fill=PALETTE[cell.bg])
            if cell.ch != " ":
                draw.text((px, py), cell.ch, fill=PALETTE[cell.fg], font=font)

    image.save(path)


def main() -> None:
    term = Term(COLS, ROWS)
    draw_header(term)
    draw_memory_cpu(term)
    draw_engines(term)
    draw_power(term)
    draw_history(term)
    draw_footer(term)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "tui_actual_preview.txt").write_text(term.plain_text() + "\n", encoding="utf-8")
    (OUT_DIR / "tui_actual_preview.ansi").write_text(term.ansi_text(), encoding="utf-8")
    render_png(term, OUT_DIR / "tui_actual_preview.png")
    print(OUT_DIR / "tui_actual_preview.png")


if __name__ == "__main__":
    main()
