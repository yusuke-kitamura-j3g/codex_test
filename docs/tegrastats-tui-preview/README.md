# Thor Tegrastats TUI Preview

This directory contains the pre-implementation visual preview for a Thor-based
`tegrastats` TUI.

The layout keeps multi-instance engines separate. For example, `NVENC0` and
`NVENC1` are rendered on independent rows so a one-sided encode load is visible.

Files:

- `tui_actual_preview.png`: rendered terminal screenshot.
- `tui_actual_preview.txt`: UTF-8 text frame without ANSI color.
- `tui_actual_preview.ansi`: terminal frame with ANSI color escapes.

Regenerate the preview from the repository root:

```bash
python tools/render_tegrastats_tui_preview.py
```
