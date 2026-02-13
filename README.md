# fltk-TclTk-PyTkinter

A C++17 educational application demonstrating how to embed **Tcl** and **Python** interpreters inside an **FLTK** GUI, and safely launch **Tcl/Tk** and **Python/Tkinter** plugin windows as subprocesses — all without event-loop conflicts.

## The Problem

> How do you combine FLTK, Tcl, Python, Tk, and Tkinter in a single application without their event loops fighting each other?

**The answer: embed the interpreters, isolate the GUIs.**

Tcl and Python interpreters are embedded directly into the FLTK process — `Tcl_Eval()` and `InteractiveConsole.push()` are synchronous function calls, not event loops. When Tk or Tkinter GUIs are needed, they run in **subprocesses** with their own event loops, communicating back to the parent via pipes.

```
 ┌──────────────── Main Process (Fl::run) ─────────────────┐
 │  Tcl Console (embedded)    Python Console (embedded)     │
 │  Graph Window (FLTK)       Fl::add_fd() pipe reader      │
 └────────────┬──────────────────────────┬──────────────────┘
              │ stdout pipe              │ stdout pipe
       ┌──────┴──────┐           ┌───────┴──────┐
       │  tclsh + Tk │           │ python3      │
       │  (own loop) │           │ + tkinter    │
       └─────────────┘           └──────────────┘
```

## Features

- **Tcl Console** — Interactive Tcl interpreter with command history and custom graph commands
- **Python Console** — Interactive Python interpreter with `code.InteractiveConsole` and graph integration
- **Parametric Graph Window** — Lissajous curve visualization with real-time slider controls
- **Tk Plugin** — Subprocess running a Tcl/Tk slider GUI that controls the graph
- **Tkinter Plugin** — Subprocess running a Python/Tkinter slider GUI that controls the graph
- **Pipe-based IPC** — Simple text protocol (`SET a 5.0`, `PRESET circle`) over stdout pipes, integrated into FLTK's event loop via `Fl::add_fd()`

## Prerequisites (macOS with Homebrew)

```bash
brew install fltk tcl-tk python@3
```

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Run

```bash
./build/fltk_console
```

This opens a launcher window with buttons for the Tcl Console, Python Console, Graph, Tk Plugin, and Tkinter Plugin.

## Test

```bash
cd build
ctest --output-on-failure
```

## Project Structure

```
src/
├── main.cpp              Entry point, launcher window
├── console_window.h/cpp  Reusable FLTK console widget (display + input + history)
├── tcl_console.h/cpp     Embedded Tcl interpreter + custom commands
├── python_console.h/cpp  Embedded Python interpreter + C-extension functions
├── graph_params.h/cpp    Pure C++ parametric curve math (no GUI dependency)
├── graph_window.h/cpp    FLTK graph canvas + slider panel
└── plugin_process.h/cpp  Subprocess launcher + pipe reader + embedded scripts

tests/
└── test_interpreters.cpp  Headless tests for Tcl, Python, and GraphParams

docs/
├── ARCHITECTURE.md        Full architecture and design documentation
├── C_API_PATTERNS.md      Tcl and Python C API usage patterns
└── EVENT_LOOP_GUIDE.md    Deep dive into the event loop integration strategy
```

## Documentation

See the `docs/` directory for detailed documentation:

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** — Full system architecture with Mermaid diagrams, component map, data flow, plugin protocol, and memory management
- **[C_API_PATTERNS.md](docs/C_API_PATTERNS.md)** — Patterns for working with Tcl and Python C APIs
- **[EVENT_LOOP_GUIDE.md](docs/EVENT_LOOP_GUIDE.md)** — Guide to the event loop integration strategy

## AI Disclosure

This repository was created with assistance from [Claude Code](https://claude.ai/claude-code) using Anthropic's **Claude Opus 4.6** model. Claude assisted with code generation, architecture design, documentation, debugging, and repository setup.
