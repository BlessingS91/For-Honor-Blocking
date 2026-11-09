#!/usr/bin/env python3
"""Tkinter desktop frontend for the FHB HKX early-indicator patcher.

Uses the CLI in this folder as a subprocess. No third-party Python packages.
Requires Windows + HKXC Anno CLI to actually process Skyrim SE HKX files.
"""

from __future__ import annotations

import json
import math
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, scrolledtext, ttk

HERE = Path(__file__).resolve().parent
PATCHER = HERE / "fhb_indicator_patcher.py"
SETTINGS = Path.home() / ".fhb_indicator_patcher_gui.json"
PROGRESS_RE = re.compile(r"^PROGRESS (\d+)/(\d+) \| (.+)$")


class IndicatorGUI(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("FHB | Early Indicator Patcher")
        self.geometry("950x845")
        self.minsize(800, 740)

        self.input_dir = tk.StringVar()
        self.output_dir = tk.StringVar()
        self.tool_path = tk.StringVar(value="hkxc-anno-cli.exe")
        self.right_lead = tk.StringVar(value="150")
        self.left_lead = tk.StringVar(value="150")
        self.align_swings = tk.BooleanVar(value=False)  # Leave original workflow unchanged unless enabled.
        self.swing_before_hit = tk.StringVar(value="200")
        self.hit_trigger = tk.StringVar(value="HitFrame")
        self.set_prehitframe = tk.BooleanVar(value=False)
        self.prehitframe_ms = tk.StringVar(value="16.667")
        self.right_trigger = tk.StringVar(value="weaponSwing")
        self.left_trigger = tk.StringVar(value="weaponLeftSwing")
        self.right_event = tk.StringVar(value="indicatorStart")
        self.left_event = tk.StringVar(value="LeftIndicatorStart")
        self.replace_existing = tk.BooleanVar(value=False)
        self.overwrite = tk.BooleanVar(value=False)
        self.verify = tk.BooleanVar(value=True)
        self.show_advanced = tk.BooleanVar(value=False)
        self.status_text = tk.StringVar(value="Ready. Preview first to inspect proposed changes.")
        self.progress_text = tk.StringVar(value="0 / 0 files")
        self.progress_value = tk.DoubleVar(value=0)

        self.worker: threading.Thread | None = None
        self.process: subprocess.Popen[str] | None = None
        self.events: queue.Queue[tuple] = queue.Queue()
        self.cancel_file: Path | None = None
        self.work_tempdir: tempfile.TemporaryDirectory[str] | None = None
        self.busy = False
        self.stopping = False

        self._style()
        self._load_settings()
        self._make_ui()
        self.after(90, self._poll_events)
        self.protocol("WM_DELETE_WINDOW", self._close)

    def _style(self) -> None:
        self.configure(bg="#F5F7FA")
        style = ttk.Style(self)
        for name in ("vista", "xpnative", "clam"):
            if name in style.theme_names():
                style.theme_use(name)
                break
        style.configure("Root.TFrame", background="#F5F7FA")
        style.configure("Head.TLabel", background="#F5F7FA", font=("Segoe UI", 20, "bold"), foreground="#182434")
        style.configure("Sub.TLabel", background="#F5F7FA", font=("Segoe UI", 10), foreground="#526171")
        style.configure("Hint.TLabel", background="#F5F7FA", font=("Segoe UI", 9), foreground="#596575")
        style.configure("Box.TLabelframe", background="#F5F7FA")
        style.configure("Box.TLabelframe.Label", font=("Segoe UI", 10, "bold"), foreground="#27384A")
        style.configure("Action.TButton", font=("Segoe UI", 10, "bold"), padding=(14, 9))
        style.configure("Plain.TButton", padding=(9, 6))

    def _make_ui(self) -> None:
        content = ttk.Frame(self, style="Root.TFrame", padding=(22, 16))
        content.pack(fill="both", expand=True)
        content.columnconfigure(0, weight=1)
        content.rowconfigure(5, weight=1)

        heading = ttk.Frame(content, style="Root.TFrame")
        heading.grid(row=0, column=0, sticky="ew", pady=(0, 15))
        ttk.Label(heading, text="FHB Early Indicator Patcher", style="Head.TLabel").pack(anchor="w")
        ttk.Label(heading, text="Add UI telegraph events ahead of existing weapon swings in Skyrim SE HKX animations.",
                  style="Sub.TLabel").pack(anchor="w", pady=(3, 0))

        paths = ttk.LabelFrame(content, text="1  Animation folders and converter", style="Box.TLabelframe", padding=12)
        paths.grid(row=1, column=0, sticky="ew", pady=(0, 11))
        paths.columnconfigure(1, weight=1)
        self._path_row(paths, 0, "Input HKX folder", self.input_dir, self._choose_input, folder=True)
        self._path_row(paths, 1, "Output folder", self.output_dir, self._choose_output, folder=True)
        self._path_row(paths, 2, "HKXC Anno CLI", self.tool_path, self._choose_converter, folder=False)
        ttk.Label(paths, text="Source files are never overwritten. The output folder must be separate from the input folder.",
                  foreground="#647180").grid(row=3, column=1, sticky="w", pady=(6, 0))

        timing = ttk.LabelFrame(content, text="2  Timing: hit -> swing -> indicator", style="Box.TLabelframe", padding=12)
        timing.grid(row=2, column=0, sticky="ew", pady=(0, 11))
        timing.columnconfigure(1, weight=1)
        timing.columnconfigure(3, weight=1)
        ttk.Label(timing, text="Right-hand lead (ms)").grid(row=0, column=0, sticky="w", padx=(0, 10))
        ttk.Entry(timing, textvariable=self.right_lead, width=12).grid(row=0, column=1, sticky="w")
        ttk.Label(timing, text="Left-hand lead (ms)").grid(row=0, column=2, sticky="w", padx=(15, 10))
        ttk.Entry(timing, textvariable=self.left_lead, width=12).grid(row=0, column=3, sticky="w")
        ttk.Label(timing, text="Indicator lead is measured before each weaponSwing / weaponLeftSwing.",
                  foreground="#647180").grid(row=1, column=0, columnspan=4, sticky="w", pady=(7, 0))
        ttk.Checkbutton(timing, text="Standardize swings before HitFrame", variable=self.align_swings).grid(
            row=2, column=0, columnspan=2, sticky="w", pady=(10, 0))
        ttk.Label(timing, text="Swing lead (ms)").grid(row=2, column=2, sticky="e", padx=(0, 10), pady=(10, 0))
        ttk.Entry(timing, textvariable=self.swing_before_hit, width=12).grid(row=2, column=3, sticky="w", pady=(10, 0))
        ttk.Label(timing, text="Optional. Pair all left/right swings with HitFrame annotations in time order. "
                  "Files with unequal counts are skipped with an error.",
                  foreground="#647180", wraplength=855, justify="left").grid(
            row=3, column=0, columnspan=4, sticky="w", pady=(7, 0))

        ttk.Checkbutton(timing, text="Add/replace preHitFrame in every animation",
                        variable=self.set_prehitframe).grid(row=4, column=0, columnspan=2,
                                                            sticky="w", pady=(10, 0))
        ttk.Label(timing, text="Time from start (ms)").grid(row=4, column=2, sticky="e",
                                                             padx=(0, 10), pady=(10, 0))
        ttk.Entry(timing, textvariable=self.prehitframe_ms, width=12).grid(
            row=4, column=3, sticky="w", pady=(10, 0))
        ttk.Label(timing, text="Optional. Removes previous preHitFrame annotations (including duplicates), then inserts one at this time.",
                  foreground="#647180", wraplength=855, justify="left").grid(
            row=5, column=0, columnspan=4, sticky="w", pady=(7, 0))

        opts = ttk.LabelFrame(content, text="3  Options", style="Box.TLabelframe", padding=12)
        opts.grid(row=3, column=0, sticky="ew", pady=(0, 11))
        for c in range(3):
            opts.columnconfigure(c, weight=1)
        ttk.Checkbutton(opts, text="Verify generated HKX files", variable=self.verify).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(opts, text="Replace existing indicator timings", variable=self.replace_existing).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(opts, text="Overwrite files in output folder", variable=self.overwrite).grid(row=0, column=2, sticky="w")
        ttk.Checkbutton(opts, text="Advanced event names", variable=self.show_advanced,
                        command=self._toggle_advanced).grid(row=1, column=0, sticky="w", pady=(9, 0))

        self.advanced = ttk.Frame(opts)
        self.advanced.columnconfigure(1, weight=1)
        self.advanced.columnconfigure(3, weight=1)
        mappings = (
            ("Right trigger", self.right_trigger, "Right indicator", self.right_event),
            ("Left trigger", self.left_trigger, "Left indicator", self.left_event),
        )
        for r, (label_a, var_a, label_b, var_b) in enumerate(mappings):
            ttk.Label(self.advanced, text=label_a).grid(row=r, column=0, sticky="w", padx=(0, 8), pady=3)
            ttk.Entry(self.advanced, textvariable=var_a).grid(row=r, column=1, sticky="ew", padx=(0, 22), pady=3)
            ttk.Label(self.advanced, text=label_b).grid(row=r, column=2, sticky="w", padx=(0, 8), pady=3)
            ttk.Entry(self.advanced, textvariable=var_b).grid(row=r, column=3, sticky="ew", pady=3)
        ttk.Label(self.advanced, text="Hit annotation").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=3)
        ttk.Entry(self.advanced, textvariable=self.hit_trigger).grid(row=2, column=1, sticky="ew", padx=(0, 22), pady=3)
        ttk.Label(self.advanced, text="e.g. HitFrame (case-insensitive)", foreground="#647180").grid(
            row=2, column=2, columnspan=2, sticky="w", pady=3)

        actions = ttk.Frame(content, style="Root.TFrame")
        actions.grid(row=4, column=0, sticky="ew", pady=(1, 11))
        actions.columnconfigure(3, weight=1)
        self.preview_button = ttk.Button(actions, text="Preview changes", style="Action.TButton",
                                         command=lambda: self._launch(write=False))
        self.preview_button.grid(row=0, column=0, padx=(0, 9))
        self.write_button = ttk.Button(actions, text="Patch HKX files", style="Action.TButton",
                                       command=lambda: self._launch(write=True))
        self.write_button.grid(row=0, column=1, padx=(0, 9))
        self.stop_button = ttk.Button(actions, text="Stop after current file", style="Plain.TButton",
                                      command=self._request_stop, state="disabled")
        self.stop_button.grid(row=0, column=2, padx=(0, 9))
        ttk.Button(actions, text="Open output", style="Plain.TButton", command=self._open_output).grid(
            row=0, column=4, sticky="e")

        results = ttk.LabelFrame(content, text="Progress and results", style="Box.TLabelframe", padding=10)
        results.grid(row=5, column=0, sticky="nsew")
        results.columnconfigure(0, weight=1)
        results.rowconfigure(2, weight=1)
        ttk.Label(results, textvariable=self.status_text).grid(row=0, column=0, sticky="w")
        ttk.Label(results, textvariable=self.progress_text).grid(row=0, column=1, sticky="e")
        ttk.Progressbar(results, variable=self.progress_value, maximum=100, mode="determinate").grid(
            row=1, column=0, columnspan=2, sticky="ew", pady=(7, 9))
        self.log = scrolledtext.ScrolledText(results, height=13, wrap="word", state="disabled",
                                             font=("Consolas", 9), relief="flat", borderwidth=1,
                                             foreground="#222D39", background="#FFFFFF")
        self.log.grid(row=2, column=0, columnspan=2, sticky="nsew")
        footer = ttk.Frame(results)
        footer.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Label(footer, text="Preview does not modify HKX files. Patch writes only to the output folder.",
                  foreground="#647180").pack(side="left")
        ttk.Button(footer, text="Clear log", command=self._clear_log).pack(side="right")

    def _path_row(self, parent, row, label, variable, action, *, folder):
        ttk.Label(parent, text=label, width=16).grid(row=row, column=0, sticky="w", padx=(0, 10), pady=4)
        ttk.Entry(parent, textvariable=variable).grid(row=row, column=1, sticky="ew", pady=4)
        ttk.Button(parent, text="Browse...", command=action, style="Plain.TButton").grid(
            row=row, column=2, sticky="e", padx=(8, 0), pady=4)

    def _choose_input(self):
        selected = filedialog.askdirectory(title="Select source HKX animation folder", initialdir=self.input_dir.get() or None)
        if selected:
            self.input_dir.set(selected)
            if not self.output_dir.get().strip():
                self.output_dir.set(str(Path(selected).parent / (Path(selected).name + "_FHB_Patched")))

    def _choose_output(self):
        selected = filedialog.askdirectory(title="Select separate output folder", initialdir=self.output_dir.get() or None,
                                           mustexist=False)
        if selected:
            self.output_dir.set(selected)

    def _choose_converter(self):
        selected = filedialog.askopenfilename(title="Select hkxc-anno-cli.exe",
                                              filetypes=[("Executable files", "*.exe"), ("All files", "*")])
        if selected:
            self.tool_path.set(selected)

    def _toggle_advanced(self):
        if self.show_advanced.get():
            self.advanced.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        else:
            self.advanced.grid_remove()

    def _validate(self) -> list[str] | None:
        if not PATCHER.is_file():
            messagebox.showerror("Missing patcher", f"Cannot locate {PATCHER}")
            return None
        try:
            input_path = Path(self.input_dir.get().strip().strip('"')).expanduser().resolve()
            output_path = Path(self.output_dir.get().strip().strip('"')).expanduser().resolve()
        except (ValueError, OSError) as error:
            messagebox.showerror("Invalid folder", str(error))
            return None
        if not self.input_dir.get().strip() or not input_path.is_dir():
            messagebox.showerror("Invalid input", "Select an existing folder containing .hkx animations.")
            return None
        if not self.output_dir.get().strip() or output_path == input_path or input_path in output_path.parents or output_path in input_path.parents:
            messagebox.showerror("Invalid output", "Output must be a separate folder, not inside the input folder (or vice versa).")
            return None
        tool = self.tool_path.get().strip().strip('"')
        if not tool or not (Path(tool).expanduser().is_file() or shutil.which(tool)):
            messagebox.showerror("Converter not found", "Select hkxc-anno-cli.exe, or add it to PATH.")
            return None
        try:
            right = float(self.right_lead.get().strip())
            left = float(self.left_lead.get().strip())
            swing = float(self.swing_before_hit.get().strip())
            if not all(math.isfinite(x) and x >= 0 for x in (right, left, swing)):
                raise ValueError("Lead times must be nonnegative finite numbers.")
        except ValueError:
            messagebox.showerror("Invalid timing", "Enter nonnegative numeric lead times (milliseconds) for both indicators and the swing.")
            return None
        if self.set_prehitframe.get():
            try:
                prehit = float(self.prehitframe_ms.get().strip())
                if not math.isfinite(prehit) or prehit < 0:
                    raise ValueError("Invalid preHitFrame time")
            except ValueError:
                messagebox.showerror("Invalid preHitFrame time", "Enter a nonnegative number of milliseconds.")
                return None
        names = [self.right_trigger.get().strip(), self.left_trigger.get().strip(),
                 self.right_event.get().strip(), self.left_event.get().strip(), self.hit_trigger.get().strip()]
        if any(not n or '\n' in n or '\r' in n for n in names):
            messagebox.showerror("Invalid event name", "Event names must be non-empty single-line strings.")
            return None
        if self.align_swings.get() and len({names[0].casefold(), names[1].casefold(), names[4].casefold()}) != 3:
            messagebox.showerror("Invalid event name", "Right swing, left swing, and HitFrame names must be distinct.")
            return None
        args = ["--input", str(input_path), "--output", str(output_path), "--tool", tool,
                "--right-lead-ms", str(right), "--left-lead-ms", str(left),
                "--right-trigger", names[0], "--left-trigger", names[1],
                "--right-event", names[2], "--left-event", names[3], "--progress"]
        if self.align_swings.get():
            args.extend(["--align-swings", "--swing-before-hit-ms", str(swing), "--hit-trigger", names[4]])
        if self.set_prehitframe.get():
            args.extend(["--set-prehitframe", "--prehitframe-ms", str(prehit)])
        if self.replace_existing.get():
            args.append("--replace-existing")
        if self.overwrite.get():
            args.append("--overwrite")
        if not self.verify.get():
            args.append("--no-verify")
        return args

    def _launch(self, *, write: bool):
        if self.busy:
            return
        args = self._validate()
        if args is None:
            return
        if write:
            flags = []
            if self.overwrite.get():
                flags.append("Existing output HKX files may be overwritten.")
            if self.replace_existing.get():
                flags.append("Existing indicator annotation timings may be changed.")
            if self.align_swings.get():
                flags.append("Existing weaponSwing/weaponLeftSwing annotation timestamps will be retimed relative to HitFrame.")
            if self.set_prehitframe.get():
                flags.append("Existing preHitFrame annotations will be replaced with one fixed-time annotation per animation.")
            detail = ("\n" + "\n".join(flags)) if flags else ""
            if not messagebox.askyesno("Confirm HKX patch", "Write patched animations to the separate output folder?"
                                       "\n\nInput: " + self.input_dir.get() + "\nOutput: " + self.output_dir.get()
                                       + detail + "\n\nOriginal input files will not be modified."):
                return
            args.append("--write")
        self._save_settings()
        self.work_tempdir = tempfile.TemporaryDirectory(prefix="fhb_gui_")
        self.cancel_file = Path(self.work_tempdir.name) / "stop.flag"
        args.extend(["--cancel-file", str(self.cancel_file)])
        self.progress_value.set(0)
        self.progress_text.set("0 / 0 files")
        self.status_text.set("Patching..." if write else "Previewing...")
        self._append("\n" + "=" * 64 + "\n" + ("PATCH" if write else "PREVIEW") + " started\n")
        self.busy = True
        self.stopping = False
        self.preview_button.configure(state="disabled")
        self.write_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.worker = threading.Thread(target=self._run_worker, args=(args,), daemon=True)
        self.worker.start()

    def _run_worker(self, args: list[str]):
        try:
            creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            command = [sys.executable, "-u", str(PATCHER), *args]
            self.process = subprocess.Popen(command, cwd=str(HERE), stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                            errors="replace", bufsize=1, creationflags=creationflags)
            assert self.process.stdout is not None
            for line in self.process.stdout:
                self.events.put(("line", line.rstrip("\r\n")))
            code = self.process.wait()
            self.events.put(("done", code))
        except Exception as error:
            self.events.put(("error", str(error)))

    def _poll_events(self):
        try:
            for _ in range(250):
                kind, *payload = self.events.get_nowait()
                if kind == "line":
                    line = payload[0]
                    found = PROGRESS_RE.match(line)
                    if found:
                        count, total = int(found.group(1)), int(found.group(2))
                        self.progress_value.set(100.0 * count / total if total else 0)
                        self.progress_text.set(f"{count:,} / {total:,} files")
                        self.status_text.set("Processing: " + found.group(3)[-95:])
                    else:
                        self._append(line + "\n")
                elif kind == "done":
                    code = payload[0]
                    if code == 0:
                        self.status_text.set("Completed successfully")
                    elif code == 130:
                        self.status_text.set("Stopped safely after the last completed file")
                    else:
                        self.status_text.set(f"Completed with errors (exit code {code})")
                    self._finish_run()
                elif kind == "error":
                    self._append("GUI ERROR: " + payload[0] + "\n")
                    self.status_text.set("Failed to start patcher")
                    self._finish_run()
        except queue.Empty:
            pass
        self.after(90, self._poll_events)

    def _request_stop(self):
        if not self.busy or self.cancel_file is None or self.stopping:
            return
        try:
            self.cancel_file.write_text("stop\n", encoding="utf-8")
            self.stopping = True
            self.stop_button.configure(state="disabled")
            self.status_text.set("Stop requested. Finishing the current HKX safely...")
            self._append("Stop requested; the current file will finish before processing ends.\n")
        except OSError as error:
            messagebox.showerror("Unable to stop", str(error))

    def _finish_run(self):
        self.busy = False
        self.stopping = False
        self.preview_button.configure(state="normal")
        self.write_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
        self.process = None
        self.cancel_file = None
        if self.work_tempdir is not None:
            self.work_tempdir.cleanup()
            self.work_tempdir = None
        self._append("=" * 64 + "\n")

    def _append(self, text: str):
        self.log.configure(state="normal")
        self.log.insert("end", text)
        # Avoid unbounded memory growth when a folder contains thousands of animations.
        if int(self.log.index("end-1c").split(".")[0]) > 5500:
            self.log.delete("1.0", "1300.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _open_output(self):
        if not self.output_dir.get().strip():
            messagebox.showinfo("Output folder", "Select an output folder first.")
            return
        path = Path(self.output_dir.get().strip().strip('"')).expanduser()
        if not path.is_dir():
            messagebox.showinfo("Output folder", "The output folder does not exist yet. Run a patch first.")
            return
        if os.name == "nt":
            os.startfile(str(path))
        elif sys.platform == "darwin":
            subprocess.Popen(["open", str(path)])
        else:
            subprocess.Popen(["xdg-open", str(path)])

    def _load_settings(self):
        try:
            data = json.loads(SETTINGS.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return
        for name in ("input_dir", "output_dir", "tool_path", "right_lead", "left_lead", "align_swings",
                     "swing_before_hit", "hit_trigger", "set_prehitframe", "prehitframe_ms",
                     "right_trigger", "left_trigger", "right_event",
                     "left_event", "replace_existing", "overwrite", "verify"):
            if name in data and isinstance(data[name], (str, bool)):
                getattr(self, name).set(data[name])

    def _save_settings(self):
        data = {name: getattr(self, name).get() for name in
                ("input_dir", "output_dir", "tool_path", "right_lead", "left_lead", "align_swings",
                 "swing_before_hit", "hit_trigger", "set_prehitframe", "prehitframe_ms",
                 "right_trigger", "left_trigger", "right_event",
                 "left_event", "replace_existing", "overwrite", "verify")}
        try:
            SETTINGS.write_text(json.dumps(data, indent=2), encoding="utf-8")
        except OSError:
            pass

    def _close(self):
        if self.busy:
            messagebox.showinfo("Patcher running", "The patcher is still processing. Use 'Stop after current file' before closing.")
            return
        self._save_settings()
        self.destroy()


if __name__ == "__main__":
    IndicatorGUI().mainloop()
