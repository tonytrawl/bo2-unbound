"""Guided Windows GUI for publishing Black Ops 2 Unbound packages."""

from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from packager_core import (
    BuildOptions,
    BuildResult,
    PackageError,
    PackagePreview,
    build_package,
    create_snapshot_template,
    preview_package,
    validate_options,
)


APP_TITLE = "Black Ops 2 Unbound Package Builder"
PROJECT_FILE = "unbound-packager-project.json"


class PackageBuilderApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1380x900")
        self.minsize(1140, 760)
        self.configure(bg="#0d1116")
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.worker: threading.Thread | None = None
        self.operation = ""
        self.last_preview: PackagePreview | None = None
        self.last_result: BuildResult | None = None

        self.kind = tk.StringVar(value="full")
        self.version = tk.StringVar(value="")
        self.previous_version = tk.StringVar(value="")
        self.current_snapshot = tk.StringVar(value="")
        self.previous_snapshot = tk.StringVar(value="")
        self.output_directory = tk.StringVar(value="")
        self.release_feed = tk.StringVar(value="")
        self.github_owner = tk.StringVar(value="tonytrawl")
        self.github_repository = tk.StringVar(value="bo2-unbound")
        self.release_tag = tk.StringVar(value="")
        self.status = tk.StringVar(value="Ready — complete Steps 1 and 2, then preview.")
        self._last_suggested_tag = ""

        self._configure_style()
        self._build_ui()
        self.kind.trace_add("write", lambda *_: self._update_mode())
        self.version.trace_add("write", lambda *_: self._suggest_tag())
        self._update_mode()
        self.after(75, self._drain_events)

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background="#0d1116")
        style.configure("Panel.TFrame", background="#171e25")
        style.configure("TLabel", background="#0d1116", foreground="#edf3f7", font=("Segoe UI", 10))
        style.configure("Panel.TLabel", background="#171e25", foreground="#edf3f7", font=("Segoe UI", 10))
        style.configure("Help.TLabel", background="#171e25", foreground="#93a7b3", font=("Segoe UI", 9))
        style.configure("Title.TLabel", background="#0d1116", foreground="#ffffff", font=("Segoe UI Semibold", 22))
        style.configure("Sub.TLabel", background="#0d1116", foreground="#91a6b2", font=("Segoe UI", 10))
        style.configure("Step.TLabel", background="#171e25", foreground="#ff6700", font=("Segoe UI Semibold", 13))
        style.configure("TButton", font=("Segoe UI Semibold", 9), padding=(11, 7))
        style.configure("Accent.TButton", background="#ef5b00", foreground="white", font=("Segoe UI Semibold", 10))
        style.map("Accent.TButton", background=[("active", "#ff7624"), ("disabled", "#4f5b63")])
        style.configure("TRadiobutton", background="#171e25", foreground="#edf3f7", font=("Segoe UI Semibold", 10))
        style.configure("Horizontal.TProgressbar", troughcolor="#28333b", background="#ff6700")

    def _build_ui(self) -> None:
        outer = ttk.Frame(self, padding=(22, 18))
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 14))
        title_group = ttk.Frame(header)
        title_group.pack(side="left", fill="x", expand=True)
        ttk.Label(title_group, text="UNBOUND PACKAGE BUILDER", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            title_group,
            text="Build a universal update-only ZIP and update the shared HQ release feed in one guided pass.",
            style="Sub.TLabel",
        ).pack(anchor="w", pady=(2, 0))
        ttk.Button(header, text="Open project", command=self._open_project).pack(side="right", padx=(8, 0))
        ttk.Button(header, text="Save project", command=self._save_project).pack(side="right")

        main = ttk.Frame(outer)
        main.pack(fill="both", expand=True)
        form_host = ttk.Frame(main, style="Panel.TFrame")
        review_host = ttk.Frame(main, style="Panel.TFrame", padding=18)
        main.columnconfigure(0, weight=3, minsize=620)
        main.columnconfigure(1, weight=2, minsize=400)
        main.rowconfigure(0, weight=1)
        form_host.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
        review_host.grid(row=0, column=1, sticky="nsew")

        canvas = tk.Canvas(form_host, bg="#171e25", highlightthickness=0, borderwidth=0)
        scrollbar = ttk.Scrollbar(form_host, orient="vertical", command=canvas.yview)
        form = ttk.Frame(canvas, style="Panel.TFrame", padding=18)
        form_window = canvas.create_window((0, 0), window=form, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        form.bind("<Configure>", lambda _event: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda event: canvas.itemconfigure(form_window, width=event.width))
        self.bind_all("<MouseWheel>", lambda event: self._scroll_form(canvas, event))
        form.columnconfigure(1, weight=1)

        ttk.Label(form, text="STEP 1  •  CHOOSE THE RELEASE", style="Step.TLabel").grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 8)
        )
        mode = ttk.Frame(form, style="Panel.TFrame")
        mode.grid(row=1, column=0, columnspan=3, sticky="ew")
        ttk.Radiobutton(mode, text="Full release", variable=self.kind, value="full").pack(side="left", padx=(0, 24))
        ttk.Radiobutton(mode, text="Patch / cumulative update", variable=self.kind, value="patch").pack(side="left")
        self.mode_help = ttk.Label(form, style="Help.TLabel", wraplength=430, justify="left")
        self.mode_help.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(4, 8))
        self._entry_row(form, 3, "New version", self.version, "Examples: 0.0.05 or 0.0.05P1.0")
        self.previous_version_widgets = self._entry_row(
            form, 5, "Oldest supported version", self.previous_version,
            "Patch only: the oldest installed version this cumulative ZIP may update.",
        )
        self._entry_row(form, 7, "GitHub release tag", self.release_tag, "Defaults to v<new version>.")
        self._entry_row(form, 9, "GitHub owner", self.github_owner)
        self._entry_row(form, 10, "Repository", self.github_repository)

        ttk.Separator(form).grid(row=11, column=0, columnspan=3, sticky="ew", pady=14)
        ttk.Label(form, text="STEP 2  •  CHOOSE FILES AND PUBLISHING LOCATIONS", style="Step.TLabel").grid(
            row=12, column=0, columnspan=3, sticky="w", pady=(0, 8)
        )
        self._folder_row(
            form, 13, "Current complete snapshot", self.current_snapshot,
            "Select the folder containing update/code and update/content.",
        )
        self.previous_snapshot_widgets = self._folder_row(
            form, 14, "Oldest-version snapshot", self.previous_snapshot,
            "Patch only: the complete snapshot matching the oldest supported version.",
        )
        template_row = ttk.Frame(form, style="Panel.TFrame")
        template_row.grid(row=15, column=1, columnspan=2, sticky="w", pady=(2, 8))
        ttk.Button(template_row, text="Create empty snapshot layout", command=self._create_template).pack(
            anchor="w"
        )
        ttk.Button(template_row, text="Validate current snapshot", command=self._preview).pack(
            anchor="w", pady=(6, 0)
        )
        self._folder_row(
            form, 16, "Package output folder", self.output_directory,
            "ZIPs and release-catalog.json are kept here. Reuse it so cumulative history is retained.",
        )
        self._file_row(
            form, 17, "Shared release feed", self.release_feed,
            "Choose your repository's releases-chain-v2.txt. #HQ, comments, and unrelated records are preserved.",
        )
        ttk.Label(
            form,
            text="The catalog is internal patch-history data. You normally do not edit it; keep it beside your packages.",
            style="Help.TLabel", wraplength=430, justify="left",
        ).grid(row=18, column=0, columnspan=3, sticky="ew", pady=(4, 0))

        ttk.Separator(form).grid(row=19, column=0, columnspan=3, sticky="ew", pady=14)
        ttk.Label(form, text="STEP 3  •  REVIEW AND BUILD", style="Step.TLabel").grid(
            row=20, column=0, columnspan=3, sticky="w", pady=(0, 8)
        )
        ttk.Label(
            form,
            text="Preview hashes the snapshots and shows every ZIP path before anything is written. "
                 "Build creates the ZIP and manifest, updates patch history, then atomically edits the selected feed.",
            style="Help.TLabel", wraplength=430, justify="left",
        ).grid(row=21, column=0, columnspan=3, sticky="ew", pady=(0, 10))
        actions = ttk.Frame(form, style="Panel.TFrame")
        actions.grid(row=22, column=0, columnspan=3, sticky="ew")
        self.preview_button = ttk.Button(actions, text="1  VALIDATE & PREVIEW", command=self._preview)
        self.preview_button.pack(side="left")
        self.build_button = ttk.Button(actions, text="2  BUILD & UPDATE FEED", style="Accent.TButton", command=self._build)
        self.build_button.pack(side="right")

        ttk.Label(review_host, text="RELEASE REVIEW", style="Step.TLabel").pack(anchor="w")
        self.summary = ttk.Label(
            review_host,
            text="Nothing has been previewed yet.\n\nComplete Steps 1 and 2, then choose Validate & Preview.",
            style="Panel.TLabel", wraplength=430, justify="left",
        )
        self.summary.pack(anchor="w", fill="x", pady=(8, 10))
        self.review = tk.Text(
            review_host, bg="#0e1419", fg="#dce8ee", insertbackground="white", relief="flat",
            font=("Cascadia Mono", 9), padx=11, pady=11, wrap="word", state="disabled",
        )
        self.review.pack(fill="both", expand=True)
        review_actions = ttk.Frame(review_host, style="Panel.TFrame")
        review_actions.pack(fill="x", pady=(9, 0))
        self.copy_button = ttk.Button(review_actions, text="Copy feed record", command=self._copy_record, state="disabled")
        self.copy_button.pack(side="left")
        self.open_output_button = ttk.Button(
            review_actions, text="Open output folder", command=self._open_output, state="disabled"
        )
        self.open_output_button.pack(side="left", padx=7)

        footer = ttk.Frame(outer)
        footer.pack(fill="x", pady=(12, 0))
        self.progress = ttk.Progressbar(footer, mode="determinate", maximum=100)
        self.progress.pack(side="left", fill="x", expand=True, padx=(0, 12))
        ttk.Label(footer, textvariable=self.status, style="Sub.TLabel").pack(side="right")

    def _entry_row(
        self, parent: ttk.Frame, row: int, label: str, variable: tk.StringVar, help_text: str = ""
    ) -> tuple[ttk.Entry, ttk.Label | None]:
        ttk.Label(parent, text=label, style="Panel.TLabel").grid(row=row, column=0, sticky="nw", padx=(0, 12), pady=5)
        entry = ttk.Entry(parent, textvariable=variable)
        entry.grid(row=row, column=1, columnspan=2, sticky="ew", pady=5)
        helper: ttk.Label | None = None
        if help_text:
            helper = ttk.Label(parent, text=help_text, style="Help.TLabel", wraplength=330, justify="left")
            helper.grid(row=row + 1, column=1, columnspan=2, sticky="w")
        return entry, helper

    def _scroll_form(self, canvas: tk.Canvas, event: tk.Event) -> None:
        pointer_x = canvas.winfo_pointerx()
        pointer_y = canvas.winfo_pointery()
        left = canvas.winfo_rootx()
        top = canvas.winfo_rooty()
        if left <= pointer_x < left + canvas.winfo_width() and top <= pointer_y < top + canvas.winfo_height():
            canvas.yview_scroll(int(-event.delta / 120), "units")

    def _folder_row(
        self, parent: ttk.Frame, row: int, label: str, variable: tk.StringVar, help_text: str
    ) -> tuple[ttk.Entry, ttk.Button, ttk.Label]:
        ttk.Label(parent, text=label, style="Panel.TLabel").grid(row=row, column=0, sticky="nw", padx=(0, 12), pady=5)
        body = ttk.Frame(parent, style="Panel.TFrame")
        body.grid(row=row, column=1, columnspan=2, sticky="ew", pady=5)
        body.columnconfigure(0, weight=1)
        entry = ttk.Entry(body, textvariable=variable)
        entry.grid(row=0, column=0, sticky="ew")
        button = ttk.Button(body, text="Browse…", command=lambda: self._browse_folder(variable))
        button.grid(row=0, column=1, padx=(7, 0))
        helper = ttk.Label(body, text=help_text, style="Help.TLabel", wraplength=330, justify="left")
        helper.grid(row=1, column=0, columnspan=2, sticky="w", pady=(2, 0))
        return entry, button, helper

    def _file_row(
        self, parent: ttk.Frame, row: int, label: str, variable: tk.StringVar, help_text: str
    ) -> None:
        ttk.Label(parent, text=label, style="Panel.TLabel").grid(row=row, column=0, sticky="nw", padx=(0, 12), pady=5)
        body = ttk.Frame(parent, style="Panel.TFrame")
        body.grid(row=row, column=1, columnspan=2, sticky="ew", pady=5)
        body.columnconfigure(0, weight=1)
        ttk.Entry(body, textvariable=variable).grid(row=0, column=0, sticky="ew")
        ttk.Button(body, text="Choose…", command=self._browse_feed).grid(row=0, column=1, padx=(7, 0))
        ttk.Label(body, text=help_text, style="Help.TLabel", wraplength=330, justify="left").grid(
            row=1, column=0, columnspan=2, sticky="w", pady=(2, 0)
        )

    def _browse_folder(self, variable: tk.StringVar) -> None:
        initial = variable.get() or self.output_directory.get() or str(Path.home())
        chosen = filedialog.askdirectory(initialdir=initial)
        if chosen:
            variable.set(chosen)

    def _browse_feed(self) -> None:
        current = Path(self.release_feed.get()) if self.release_feed.get() else None
        initial_dir = str(current.parent) if current else (self.output_directory.get() or str(Path.home()))
        chosen = filedialog.asksaveasfilename(
            title="Choose the shared HQ release feed",
            initialdir=initial_dir,
            initialfile="releases-chain-v2.txt",
            defaultextension=".txt",
            filetypes=[("Unbound release feed", "releases-chain-v2.txt"), ("Text files", "*.txt")],
        )
        if chosen:
            self.release_feed.set(chosen)

    def _suggest_tag(self) -> None:
        next_tag = f"v{self.version.get().strip()}"
        if not self.release_tag.get().strip() or self.release_tag.get().strip() == self._last_suggested_tag:
            self.release_tag.set(next_tag)
        self._last_suggested_tag = next_tag

    def _update_mode(self) -> None:
        patch = self.kind.get() == "patch"
        state = "normal" if patch else "disabled"
        for widget in self.previous_version_widgets:
            if widget is not None:
                widget.configure(state=state)
        for widget in self.previous_snapshot_widgets:
            widget.configure(state=state)
        self.mode_help.configure(
            text=(
                "PATCH: compare two complete snapshots. The ZIP contains only new/changed files plus "
                "any files required to make a cumulative shortcut safe."
                if patch else
                "FULL: package every file in one complete snapshot. Use this for a new base release or "
                "when files were removed."
            )
        )

    def _build_options(self) -> BuildOptions:
        if not self.release_feed.get().strip():
            raise PackageError("Choose the shared releases-chain-v2.txt feed in Step 2")
        return BuildOptions(
            kind=self.kind.get(), region="UNIVERSAL", version=self.version.get().strip(),
            current_snapshot=self.current_snapshot.get().strip(),
            previous_snapshot=self.previous_snapshot.get().strip(),
            previous_version=self.previous_version.get().strip(),
            output_directory=self.output_directory.get().strip(),
            github_owner=self.github_owner.get().strip(),
            github_repository=self.github_repository.get().strip(),
            release_tag=self.release_tag.get().strip(), release_feed=self.release_feed.get().strip(),
        )

    def _project_data(self) -> dict[str, str]:
        return {
            "kind": self.kind.get(), "version": self.version.get(),
            "previousVersion": self.previous_version.get(), "currentSnapshot": self.current_snapshot.get(),
            "previousSnapshot": self.previous_snapshot.get(), "outputDirectory": self.output_directory.get(),
            "releaseFeed": self.release_feed.get(), "githubOwner": self.github_owner.get(),
            "githubRepository": self.github_repository.get(), "releaseTag": self.release_tag.get(),
        }

    def _load_project_data(self, data: dict[str, str]) -> None:
        mapping = {
            "kind": self.kind, "version": self.version, "previousVersion": self.previous_version,
            "currentSnapshot": self.current_snapshot, "previousSnapshot": self.previous_snapshot,
            "outputDirectory": self.output_directory, "releaseFeed": self.release_feed,
            "githubOwner": self.github_owner, "githubRepository": self.github_repository,
            "releaseTag": self.release_tag,
        }
        for key, variable in mapping.items():
            if key in data:
                variable.set(str(data[key]))

    def _save_project(self) -> None:
        path = filedialog.asksaveasfilename(
            title="Save package project", defaultextension=".json", initialfile=PROJECT_FILE,
            filetypes=[("Unbound project", "*.json")],
        )
        if not path:
            return
        try:
            Path(path).write_text(json.dumps(self._project_data(), indent=2) + "\n", encoding="utf-8")
            self.status.set(f"Saved project: {Path(path).name}")
        except OSError as exc:
            messagebox.showerror(APP_TITLE, f"Could not save the project:\n\n{exc}")

    def _open_project(self) -> None:
        path = filedialog.askopenfilename(title="Open package project", filetypes=[("Unbound project", "*.json")])
        if not path:
            return
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                raise ValueError("Project file must contain an object")
            self._load_project_data(data)
            self.status.set(f"Opened project: {Path(path).name}")
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            messagebox.showerror(APP_TITLE, f"Could not open the project:\n\n{exc}")

    def _create_template(self) -> None:
        destination = filedialog.askdirectory(title="Choose or create a snapshot directory")
        if not destination:
            return
        try:
            path = create_snapshot_template(destination)
            self.current_snapshot.set(str(path))
            self.status.set(f"Created snapshot layout: {path}")
            messagebox.showinfo(
                APP_TITLE,
                "Snapshot layout created.\n\nPut code files in update/code, content files in "
                "update/content, and localized files below update/content/@language.",
            )
        except OSError as exc:
            messagebox.showerror(APP_TITLE, f"Could not create the snapshot layout:\n\n{exc}")

    def _set_busy(self, busy: bool, message: str = "") -> None:
        state = "disabled" if busy else "normal"
        self.preview_button.configure(state=state)
        self.build_button.configure(state=state)
        if busy:
            self.progress.configure(value=0)
        if message:
            self.status.set(message)

    def _preview(self) -> None:
        if self.worker and self.worker.is_alive():
            return
        try:
            options = self._build_options()
            validate_options(options)
        except PackageError as exc:
            messagebox.showerror(APP_TITLE, str(exc))
            return
        self.operation = "preview"
        self._set_busy(True, "Validating snapshots…")

        def run() -> None:
            try:
                result = preview_package(
                    options, lambda message, percent: self.events.put(("progress", (message, percent)))
                )
                self.events.put(("preview-complete", result))
            except Exception as exc:
                self.events.put(("error", exc))

        self.worker = threading.Thread(target=run, daemon=True)
        self.worker.start()

    def _build(self) -> None:
        if self.worker and self.worker.is_alive():
            return
        try:
            options = self._build_options()
            validate_options(options)
        except PackageError as exc:
            messagebox.showerror(APP_TITLE, str(exc))
            return
        self.operation = "build"
        self._set_busy(True, "Building package…")

        def run() -> None:
            try:
                result = build_package(
                    options, lambda message, percent: self.events.put(("progress", (message, percent)))
                )
                self.events.put(("build-complete", result))
            except Exception as exc:
                self.events.put(("error", exc))

        self.worker = threading.Thread(target=run, daemon=True)
        self.worker.start()

    def _show_preview(self, result: PackagePreview) -> None:
        self.last_preview = result
        self.last_result = None
        self.copy_button.configure(state="disabled")
        self.open_output_button.configure(state="disabled")
        self.summary.configure(
            text=f"READY TO BUILD\n{result.archive_name}\n"
                 f"{len(result.included_paths)} file(s) • {format_bytes(result.uncompressed_bytes)} before compression"
        )
        lines = [
            "ARCHIVE", result.archive_name, "", "MANIFEST", result.manifest_name, "",
            "SHARED FEED", result.release_feed, "", "PLANNED FEED RECORD", result.release_record,
            "(SHA-256 and compressed bytes are filled after build.)", "", "ZIP CONTENTS",
        ]
        lines.extend(f"+ {path}" for path in result.included_paths)
        self._set_review("\n".join(lines))

    def _show_result(self, result: BuildResult) -> None:
        self.last_result = result
        self.copy_button.configure(state="normal")
        self.open_output_button.configure(state="normal")
        feed_status = "UPDATED" if result.release_feed_updated else "NOT UPDATED — manual recovery required"
        self.summary.configure(
            text=f"PACKAGE COMPLETE\n{Path(result.archive).name}\n"
                 f"{result.included_files} file(s) • {format_bytes(result.compressed_bytes)} • feed {feed_status}"
        )
        lines = [
            "CREATED", result.archive, result.package_manifest, "", "SHA-256", result.sha256, "",
            "EXACT FEED RECORD", result.release_record, "", "RELEASE FEED", result.chain_releases,
            f"Status: {feed_status}",
        ]
        if result.release_feed_error:
            lines.extend(["", "FEED ERROR", result.release_feed_error])
        tag = self.release_tag.get().strip() or f"v{self.version.get().strip()}"
        lines.extend([
            "", "NEXT STEPS",
            f"1. Create/open GitHub Release tag {tag}.",
            f"2. Upload {Path(result.archive).name} and {Path(result.package_manifest).name}.",
            "3. Verify the asset URL works, then review your local releases-chain-v2.txt.",
            "4. Commit and push the feed yourself when ready; this tool never uses Git.",
            f"5. Keep {Path(result.catalog).name} in this output folder for future cumulative patches; do not upload it.",
        ])
        self._set_review("\n".join(lines))

    def _drain_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "progress":
                    message, percent = payload
                    self.progress.configure(value=percent)
                    self.status.set(str(message))
                elif kind == "preview-complete":
                    self._set_busy(False, "Preview complete — review the exact paths, then build.")
                    self.progress.configure(value=100)
                    self._show_preview(payload)
                elif kind == "build-complete":
                    result: BuildResult = payload
                    self._set_busy(
                        False,
                        "Package complete" if result.release_feed_updated else "Package complete — feed needs attention",
                    )
                    self.progress.configure(value=100)
                    self._show_result(result)
                    if result.release_feed_updated:
                        messagebox.showinfo(
                            APP_TITLE,
                            f"Package created and release feed updated.\n\n{result.archive}\n\n"
                            "Review the publishing steps shown on the right. Nothing was committed or uploaded.",
                        )
                    else:
                        messagebox.showwarning(
                            APP_TITLE,
                            "The ZIP and manifest were created, but releases-chain-v2.txt was NOT updated.\n\n"
                            f"{result.release_feed_error}\n\nCopy the exact feed record from the review panel after fixing the file path or permissions.",
                        )
                elif kind == "error":
                    self._set_busy(False, f"{self.operation.title()} failed")
                    messagebox.showerror(APP_TITLE, str(payload))
        except queue.Empty:
            pass
        self.after(75, self._drain_events)

    def _set_review(self, value: str) -> None:
        self.review.configure(state="normal")
        self.review.delete("1.0", "end")
        self.review.insert("1.0", value)
        self.review.configure(state="disabled")

    def _copy_record(self) -> None:
        if not self.last_result:
            return
        self.clipboard_clear()
        self.clipboard_append(self.last_result.release_record)
        self.status.set("Exact feed record copied to the clipboard")

    def _open_output(self) -> None:
        target = Path(self.output_directory.get()).expanduser()
        if not target.is_dir():
            messagebox.showerror(APP_TITLE, "The output folder does not exist.")
            return
        try:
            os.startfile(str(target))  # type: ignore[attr-defined]
        except OSError as exc:
            messagebox.showerror(APP_TITLE, f"Could not open the output folder:\n\n{exc}")


def format_bytes(value: int) -> str:
    amount = float(value)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if amount < 1024 or unit == "GiB":
            return f"{amount:.1f} {unit}"
        amount /= 1024
    return f"{amount:.1f} GiB"


if __name__ == "__main__":
    app = PackageBuilderApp()
    app.mainloop()
