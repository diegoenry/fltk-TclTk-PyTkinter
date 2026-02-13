# FLTK Console — Architecture & Design

An educational C++17 application demonstrating how to embed **Tcl** and **Python**
interpreters inside an **FLTK** GUI, and how to safely launch **Tcl/Tk** and
**Python/Tkinter** plugin windows as subprocesses — all without event-loop conflicts.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [High-Level Architecture](#high-level-architecture)
3. [The Event Loop Problem](#the-event-loop-problem)
4. [Component Map](#component-map)
5. [Data Flow](#data-flow)
6. [Plugin Communication Protocol](#plugin-communication-protocol)
7. [Memory & Lifetime Management](#memory--lifetime-management)
8. [Build & Run](#build--run)

---

## Project Overview

This project answers a common question in desktop application development:

> *How do I combine FLTK, Tcl, Python, Tk, and Tkinter in a single application
> without their event loops fighting each other?*

The answer: **embed the interpreters, isolate the GUIs**.

```
 ┌─────────────────────────────────────────────────────────┐
 │                    FLTK Application                     │
 │                                                         │
 │  ┌──────────────┐  ┌───────────────┐  ┌─────────────┐  │
 │  │ Tcl Console  │  │Python Console │  │ Graph Window│  │
 │  │ (embedded    │  │(embedded      │  │ (FLTK       │  │
 │  │  interpreter)│  │ interpreter)  │  │  drawing)   │  │
 │  └──────────────┘  └───────────────┘  └──────┬──────┘  │
 │                                              │         │
 │         ┌────────────────────────────────────┘         │
 │         │  parameters (a, b, A, B, delta)              │
 │         ▼                                              │
 │  ┌──────────────────────────────────────────────────┐  │
 │  │              Fl::add_fd() pipe reader             │  │
 │  └──────────┬──────────────────────┬────────────────┘  │
 │             │ stdout               │ stdout            │
 └─────────────┼──────────────────────┼───────────────────┘
               │                      │
        ┌──────┴──────┐        ┌──────┴──────┐
        │  Subprocess │        │  Subprocess │
        │  tclsh +Tk  │        │ python3     │
        │  (own event │        │ +tkinter    │
        │   loop)     │        │ (own event  │
        └─────────────┘        │  loop)      │
                               └─────────────┘
```

### What each piece does

| Component | In-process? | Has event loop? | Purpose |
|-----------|:-----------:|:---------------:|---------|
| FLTK GUI | Yes | **Yes** — `Fl::run()` | Main window, console windows, graph canvas |
| Tcl interpreter | Yes (embedded) | No | Execute Tcl commands synchronously |
| Python interpreter | Yes (embedded) | No | Execute Python commands synchronously |
| Tk plugin | **No** (subprocess) | **Yes** — Tk `MainLoop` | Slider GUI that sends params to parent |
| Tkinter plugin | **No** (subprocess) | **Yes** — `mainloop()` | Same, in Python |

---

## High-Level Architecture

```mermaid
graph TB
    subgraph "Main Process (single event loop: Fl::run)"
        MAIN["main.cpp<br/>Launcher Window"]
        TCL["TclConsole<br/>Embedded Tcl interp"]
        PY["PythonConsole<br/>Embedded Python interp"]
        GW["GraphWindow<br/>Lissajous curve + sliders"]
        GP["GraphParams<br/>a, b, A, B, delta, points"]
        CW1["ConsoleWindow<br/>(Tcl output + input)"]
        CW2["ConsoleWindow<br/>(Python output + input)"]
        PP1["PluginProcess<br/>(Tk child)"]
        PP2["PluginProcess<br/>(Tkinter child)"]

        MAIN --> TCL
        MAIN --> PY
        MAIN --> GW
        TCL --> CW1
        PY --> CW2
        GW --> GP
        TCL -->|"graph set/get/preset"| GP
        PY  -->|"graph_set/get/preset"| GP
    end

    subgraph "Child Process 1"
        TK["tclsh<br/>Tk sliders + buttons"]
    end

    subgraph "Child Process 2"
        TKINTER["python3<br/>Tkinter sliders + buttons"]
    end

    PP1 -->|"popen() + pipe"| TK
    PP2 -->|"popen() + pipe"| TKINTER
    TK  -->|"stdout: SET a 5.0"| PP1
    TKINTER -->|"stdout: PRESET circle"| PP2
    PP1 -->|"updates"| GP
    PP2 -->|"updates"| GP
```

---

## The Event Loop Problem

This is the central design challenge. Every GUI toolkit has an event loop that
**blocks** waiting for input events and dispatches them to widgets:

```mermaid
graph LR
    subgraph "FLTK"
        A1["Fl::run()"] -->|"polls"| A2["OS events"]
        A2 -->|"dispatch"| A3["FLTK widgets"]
    end

    subgraph "Tk"
        B1["Tk_MainLoop()"] -->|"polls"| B2["OS events"]
        B2 -->|"dispatch"| B3["Tk widgets"]
    end

    subgraph "Tkinter"
        C1["root.mainloop()"] -->|"polls"| C2["OS events"]
        C2 -->|"dispatch"| C3["Tkinter widgets"]
    end
```

**If you run two event loops in the same thread**, one blocks the other:

```
 Thread ──▶ Fl::run() blocks forever
                │
                ╳  Tk_MainLoop() never gets to run
                ╳  tkinter.mainloop() never gets to run
```

### The Solution

```
 ┌──────────────────────────────────────────────────────────────┐
 │  RULE: One event loop per process, one process per event     │
 │        loop. If you need a second GUI toolkit, use a          │
 │        subprocess.                                            │
 └──────────────────────────────────────────────────────────────┘
```

```mermaid
graph TB
    subgraph "Main Process"
        FL["Fl::run()<br/>THE event loop"]
        FL -->|"Enter pressed"| SYNC_TCL["Tcl_Eval() — synchronous call, returns"]
        FL -->|"Enter pressed"| SYNC_PY["console.push() — synchronous call, returns"]
        FL -->|"fd readable"| PIPE["read pipe from child"]
        SYNC_TCL -->|"returns"| FL
        SYNC_PY  -->|"returns"| FL
        PIPE     -->|"returns"| FL
    end

    subgraph "Child Process (separate)"
        TK_LOOP["Tk_MainLoop()<br/>Its own event loop"]
        TK_LOOP -->|"slider moved"| STDOUT["print to stdout"]
    end

    STDOUT -.->|"pipe"| PIPE
```

Key points:
- **Embedding an interpreter ≠ running its event loop.** `Tcl_Eval()` and
  `InteractiveConsole.push()` are plain function calls. They execute, return a
  result, and FLTK resumes.
- **Fl::add_fd()** integrates pipe I/O into FLTK's event loop. When the child
  process writes to stdout, FLTK wakes up and calls our handler — no threads,
  no polling loops.

---

## Component Map

```mermaid
classDiagram
    class ConsoleWindow {
        -Fl_Text_Display* display_
        -Fl_Text_Buffer* buffer_
        -Fl_Input* input_
        -vector~string~ history_
        -CommandCallback cmd_cb_
        +append_output(text)
        +set_prompt(prompt)
        +handle(event) int
    }

    class TclConsole {
        -ConsoleWindow* win_
        -Tcl_Interp* interp_
        +show()
        -ensure_init()
        -on_command(cmd)
        -puts_cmd()$
        -graph_cmd()$
    }

    class PythonConsole {
        -ConsoleWindow* win_
        -PyObject* console_obj_
        -PyObject* capture_out_
        -PyObject* capture_err_
        -PyObject* locals_
        -bool more_
        +show()
        +finalize_python()$
        -on_command(cmd)
        -flush_output()
    }

    class GraphParams {
        +double a, b, A, B, delta
        +int num_points
        +eval(t) pair~double,double~
        +set(name, value) bool
        +get(name) double
        +load_preset(name) bool
        +all() map
    }

    class GraphCanvas {
        +GraphParams params
        +draw()
    }

    class GraphWindow {
        -GraphCanvas* canvas_
        -Fl_Value_Slider* sl_a_ .. sl_pts_
        +params() GraphParams&
        +sync_and_redraw()
    }

    class PluginProcess {
        -FILE* pipe_
        -int fd_
        -string line_buf_
        +launch(interpreter, script, args) bool
        +running() bool
        +stop()
        -on_data()
        -process_line(line)
    }

    ConsoleWindow <|-- Fl_Double_Window
    TclConsole *-- ConsoleWindow
    PythonConsole *-- ConsoleWindow
    GraphWindow *-- GraphCanvas
    GraphCanvas *-- GraphParams
    GraphWindow <|-- Fl_Double_Window
    TclConsole ..> GraphWindow : graph commands
    PythonConsole ..> GraphWindow : graph_set/get/preset
    PluginProcess ..> GraphWindow : SET/PRESET updates
```

### Source File Map

```
src/
├── main.cpp              Entry point, launcher window, button callbacks
├── console_window.h/cpp  Reusable FLTK console widget (display + input + history)
├── tcl_console.h/cpp     Embedded Tcl interpreter + custom commands
├── python_console.h/cpp  Embedded Python interpreter + C-extension functions
├── graph_params.h/cpp    Pure C++ parametric curve math (no GUI dependency)
├── graph_window.h/cpp    FLTK graph canvas + slider panel
└── plugin_process.h/cpp  Subprocess launcher + pipe reader + embedded scripts

tests/
└── test_interpreters.cpp Headless tests for Tcl, Python, and GraphParams
```

---

## Data Flow

### Console Command Flow

```mermaid
sequenceDiagram
    participant User
    participant FLTK as FLTK Event Loop
    participant CW as ConsoleWindow
    participant Interp as Tcl/Python Interp
    participant GW as GraphWindow

    User->>FLTK: types "graph set a 5" + Enter
    FLTK->>CW: on_input_enter callback
    CW->>CW: save to history
    CW->>Interp: cmd_cb_("graph set a 5")
    Interp->>GW: params().set("a", 5.0)
    Interp->>GW: sync_and_redraw()
    GW->>GW: update sliders + redraw canvas
    Interp-->>CW: return result
    CW->>CW: append_output(result)
    CW-->>FLTK: returns
    FLTK->>FLTK: continue dispatching events
```

### Plugin Subprocess Flow

```mermaid
sequenceDiagram
    participant User
    participant FLTK as FLTK Event Loop
    participant PP as PluginProcess
    participant Child as Subprocess (tclsh/python3)
    participant GW as GraphWindow

    User->>FLTK: clicks "Tk Plugin" button
    FLTK->>PP: launch("tclsh", script, "3 2 1.5708 1 1")
    PP->>PP: write script to /tmp/fltk_plugin.tcl
    PP->>Child: popen("tclsh /tmp/fltk_plugin.tcl 3 2 ...")
    PP->>PP: set fd to O_NONBLOCK
    PP->>FLTK: Fl::add_fd(fd, FL_READ, callback)
    PP-->>FLTK: returns

    Note over Child: Child runs its own Tk event loop

    User->>Child: moves slider "a" to 5
    Child->>Child: on_slider callback
    Child-->>PP: stdout: "SET a 5.000000\n"
    FLTK->>PP: fd_callback (fd is readable)
    PP->>PP: read() + buffer + parse line
    PP->>GW: params().set("a", 5.0)
    PP->>GW: sync_and_redraw()
    PP-->>FLTK: returns

    User->>Child: clicks "circle" preset
    Child-->>PP: stdout: "PRESET circle\n"
    FLTK->>PP: fd_callback
    PP->>GW: params().load_preset("circle")
    PP->>GW: sync_and_redraw()

    Note over Child: User closes Tk window
    Child-->>PP: EOF on pipe
    FLTK->>PP: fd_callback → read()=0
    PP->>PP: stop() → pclose, remove_fd, cleanup
```

---

## Plugin Communication Protocol

The plugins communicate with the parent process through a simple text protocol
over stdout:

```
 ┌──────────────────────────────────────────────────────┐
 │  PROTOCOL: one command per line, flushed immediately │
 │                                                       │
 │  SET <param> <value>    Set a single parameter        │
 │  PRESET <name>          Load a named preset           │
 │                                                       │
 │  Examples:                                            │
 │    SET a 5.000000                                     │
 │    SET delta 1.570796                                 │
 │    PRESET circle                                      │
 │    PRESET figure8                                     │
 └──────────────────────────────────────────────────────┘

 Valid parameters:  a, b, A, B, delta
 Valid presets:     circle, figure8, lissajous, star, bowtie
```

The parent reads lines via `Fl::add_fd()` and parses them with `sscanf`:

```
Child stdout ──▶ pipe ──▶ Fl::add_fd(fd, FL_READ) ──▶ on_data()
                                                          │
                                    ┌─────────────────────┘
                                    ▼
                              line_buf_ accumulates bytes
                                    │
                                    ▼ (on '\n')
                              process_line()
                                    │
                          ┌─────────┴─────────┐
                          ▼                   ▼
                    "SET a 5.0"         "PRESET circle"
                          │                   │
                          ▼                   ▼
                  gw->params().set()   gw->params().load_preset()
                          │                   │
                          └─────────┬─────────┘
                                    ▼
                          gw->sync_and_redraw()
```

---

## Memory & Lifetime Management

### Object Ownership

```mermaid
graph TB
    subgraph "main() stack"
        WIN["Fl_Window win<br/>(stack)"]
        GRAPH["GraphWindow graph_win<br/>(stack)"]
    end

    subgraph "File-scope statics"
        G_TCL["TclConsole g_tcl"]
        G_PY["PythonConsole g_python"]
        G_TK_PLUGIN["PluginProcess g_tk_plugin"]
        G_TKINTER_PLUGIN["PluginProcess g_tkinter_plugin"]
    end

    G_TCL -->|"owns (new/delete)"| CW1["ConsoleWindow*"]
    G_TCL -->|"owns (Create/Delete)"| INTERP["Tcl_Interp*"]
    G_PY  -->|"owns (new/delete)"| CW2["ConsoleWindow*"]
    G_PY  -->|"owns (Py_XDECREF)"| PYOBJ["PyObject* x4"]

    CW1 -->|"owns (new/delete)"| BUF1["Fl_Text_Buffer*"]
    CW2 -->|"owns (new/delete)"| BUF2["Fl_Text_Buffer*"]

    CW1 -->|"FLTK parent owns"| DISP1["Fl_Text_Display*"]
    CW1 -->|"FLTK parent owns"| INP1["Fl_Input*"]

    GRAPH -->|"FLTK parent owns"| CANVAS["GraphCanvas*"]
    GRAPH -->|"FLTK parent owns"| SLIDERS["Fl_Value_Slider* x6"]
```

### Key Rules

```
 ┌──────────────────────────────────────────────────────────────┐
 │  FLTK OWNERSHIP RULE                                         │
 │  Widgets created between begin()/end() are owned by the      │
 │  parent window. The parent deletes them. Do NOT delete them   │
 │  yourself.                                                    │
 │                                                               │
 │  EXCEPTION: Fl_Text_Buffer is NOT owned by Fl_Text_Display.  │
 │  You must delete it yourself.                                 │
 └──────────────────────────────────────────────────────────────┘

 ┌──────────────────────────────────────────────────────────────┐
 │  PYTHON REFCOUNT RULE                                        │
 │  PyDict_SetItemString() does NOT steal references.           │
 │  PyTuple_SetItem() DOES steal references.                    │
 │  Always check the docs for each API call.                    │
 │                                                               │
 │  Use Py_DECREF for every PyObject* you create.               │
 │  Use Py_XDECREF when the pointer might be NULL.              │
 └──────────────────────────────────────────────────────────────┘

 ┌──────────────────────────────────────────────────────────────┐
 │  TCL INTERP RULE                                             │
 │  Tcl_CreateInterp() → Tcl_DeleteInterp() in destructor.     │
 │  String results from Tcl_GetStringResult() are valid only    │
 │  until the next Tcl_Eval() call.                             │
 └──────────────────────────────────────────────────────────────┘
```

### Destruction Order

```
main() returns
    │
    ▼ (stack unwinds)
    graph_win destroyed  ← FLTK window + children freed
    win destroyed        ← launcher window freed
    │
    ▼ (static destructors, reverse order of construction)
    g_python destroyed   ← XDECREF no-ops (already null), delete ConsoleWindow
    g_tcl destroyed      ← Tcl_DeleteInterp, delete ConsoleWindow
    g_tkinter_plugin destroyed ← pclose pipe, remove temp file
    g_tk_plugin destroyed      ← pclose pipe, remove temp file
```

> **Note:** `PythonConsole::finalize_python()` is called before `main()`
> returns, while `g_python`'s destructor runs after. To handle this safely,
> `main()` calls `g_python.release_python_objects()` *before*
> `finalize_python()`, releasing all Python refs while the interpreter is
> still alive. The destructor's subsequent `Py_XDECREF` calls see null
> pointers and become no-ops. See
> [Lessons Learned](#lessons-learned-bugs-that-were-found-and-fixed) below.

---

## Infographic: How Everything Connects

```
╔══════════════════════════════════════════════════════════════════════════╗
║                    FLTK + Tcl + Python Application                     ║
║                         Architecture Overview                          ║
╠══════════════════════════════════════════════════════════════════════════╣
║                                                                        ║
║   ┌─────────────────── MAIN PROCESS ──────────────────────────────┐    ║
║   │                                                               │    ║
║   │   EVENT LOOP: Fl::run()                                       │    ║
║   │   ════════════════════                                        │    ║
║   │   Handles ALL events: mouse, keyboard, timers, pipe I/O      │    ║
║   │                                                               │    ║
║   │   ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐    │    ║
║   │   │  Launcher   │  │ Tcl Console  │  │ Python Console   │    │    ║
║   │   │  Window     │  │              │  │                  │    │    ║
║   │   │ ┌─────────┐ │  │ ┌──────────┐ │  │ ┌──────────┐     │    │    ║
║   │   │ │Tcl Cons.│─┼──▶│Tcl_Eval()│ │  │ │.push()   │     │    │    ║
║   │   │ │Py Cons. │─┼──────────────────▶│ │(sync)    │     │    │    ║
║   │   │ │Graph    │─┼──┐│ (sync)    │ │  │ └────┬─────┘     │    │    ║
║   │   │ │Tk Plug. │─┼─┐││ └────┬─────┘ │  │      │           │    │    ║
║   │   │ │Tkinter  │─┼┐│││      │       │  │      │           │    │    ║
║   │   │ └─────────┘ ││││└──────┼───────┘  └──────┼──────────┘    │    ║
║   │   └─────────────┘││││      │                  │               │    ║
║   │                  ││││      │    ┌─────────────┘               │    ║
║   │              ┌───┘│││      │    │                             │    ║
║   │              │    │││      ▼    ▼                             │    ║
║   │              │    │││  ┌────────────────────────────────┐     │    ║
║   │              │    │││  │  GraphWindow                   │     │    ║
║   │              ▼    │││  │  ┌─────────────┐ ┌──────────┐ │     │    ║
║   │   ┌──────────────┐│││  │  │ GraphCanvas │ │ Sliders  │ │     │    ║
║   │   │ GraphParams  │◀┼┼──│  │ (draw curve)│ │ a,b,δ,   │ │     │    ║
║   │   │ a=3 b=2 δ=π/2│││  │  │             │ │ A,B,pts  │ │     │    ║
║   │   │ A=1 B=1 pts= ││││  │  └─────────────┘ └──────────┘ │     │    ║
║   │   │         1000 ││││  └────────────────────────────────┘     │    ║
║   │   └──────────────┘│││                                        │    ║
║   │         ▲         │││                                        │    ║
║   │         │         │││                                        │    ║
║   │    ┌────┴────┐    │││                                        │    ║
║   │    │ Fl::    │    │││                                        │    ║
║   │    │ add_fd()│◀───┼┼┼────── pipe stdout ──────────────┐     │    ║
║   │    └─────────┘    │││                                 │     │    ║
║   └───────────────────┼┼┼─────────────────────────────────┼─────┘    ║
║                       │││                                 │          ║
║          ┌────────────┘│└────────────┐                    │          ║
║          │             │             │                    │          ║
║          ▼             ▼             ▼                    │          ║
║   ┌─────────────┐  ┌──────────┐  ┌──────────────────┐    │          ║
║   │ SUBPROCESS  │  │SUBPROCESS│  │ SUBPROCESS        │    │          ║
║   │             │  │          │  │                    │    │          ║
║   │ tclsh       │  │ python3  │  │ "SET a 5.0"  ─────┼────┘          ║
║   │ + Tk GUI    │  │+tkinter  │  │ "PRESET circle"───┘              ║
║   │             │  │  GUI     │  │                    │              ║
║   │ Tk_MainLoop │  │mainloop()│  │ (each has its own  │              ║
║   │ (own loop)  │  │(own loop)│  │  event loop)       │              ║
║   └─────────────┘  └──────────┘  └──────────────────┘               ║
║                                                                      ║
╠══════════════════════════════════════════════════════════════════════╣
║  KEY INSIGHT: Tcl_Eval() and console.push() are FUNCTION CALLS,     ║
║  not event loops. They execute and return. Only Tk and Tkinter       ║
║  need their own event loop — so they run in subprocesses.            ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## Lessons Learned: Bugs That Were Found and Fixed

During code review, three bugs were identified and fixed. They are documented
here because each one teaches an important C/C++ API concept.

### 1. PyFloat refcount leak in `py_graph_params`

**What went wrong:** `PyDict_SetItemString` does **not** steal the reference.
Passing `PyFloat_FromDouble(v)` directly as an argument creates a new ref
(refcount=1), then `SetItemString` increments it (refcount=2). When the dict is
eventually freed, refcount drops to 1 — the float is never deallocated.

```cpp
// BEFORE (leaked one PyFloat per parameter per call):
for (auto& [k, v] : all)
    PyDict_SetItemString(dict, k.c_str(), PyFloat_FromDouble(v));

// AFTER (correctly releases the temporary ref):
for (auto& [k, v] : all) {
    PyObject* fval = PyFloat_FromDouble(v);
    PyDict_SetItemString(dict, k.c_str(), fval);
    Py_DECREF(fval);
}
```

**Takeaway:** Always check whether a Python C API function *steals* or
*borrows* a reference. `PyDict_SetItemString` borrows (increments refcount);
`PyTuple_SetItem` steals (does not increment). When in doubt, consult the
[Python C API docs](https://docs.python.org/3/c-api/).

### 2. Py_XDECREF after Py_FinalizeEx

**What went wrong:** `g_python` is a file-scope static. Its destructor runs
*after* `main()` returns, but `Py_FinalizeEx()` was called inside `main()`.
The destructor's `Py_XDECREF` calls operated on a dead interpreter — undefined
behavior.

**Fix:** A new `release_python_objects()` method DECREFs all four `PyObject*`
pointers and nulls them. `main()` now calls it *before* `Py_FinalizeEx()`.
The destructor calls the same method, but since the pointers are already null,
the `Py_XDECREF` calls become harmless no-ops.

```cpp
// main.cpp — correct shutdown order:
g_python.release_python_objects();   // refs released while Python is alive
PythonConsole::finalize_python();    // now safe to tear down the interpreter
```

**Takeaway:** With embedded interpreters, destruction order matters. Release
all interpreter-managed resources *before* finalizing the interpreter. Design
your cleanup methods to be idempotent (safe to call twice).

### 3. Missing EAGAIN handling in non-blocking read

**What went wrong:** `PluginProcess::on_data()` sets the pipe fd to
`O_NONBLOCK`, but treated all negative `read()` returns as EOF. With
non-blocking I/O, `read()` returns -1 with `errno == EAGAIN` when no data is
available — this is not an error, just a spurious wakeup.

```cpp
// BEFORE (spurious wakeup would kill the plugin):
ssize_t n = read(fd_, buf, sizeof(buf) - 1);
if (n <= 0) { stop(); return; }

// AFTER (EAGAIN is handled correctly):
ssize_t n = read(fd_, buf, sizeof(buf) - 1);
if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
if (n <= 0) { stop(); return; }
```

**Takeaway:** Non-blocking I/O requires checking `errno` when `read()` returns
-1. `EAGAIN`/`EWOULDBLOCK` mean "try again later", not "the pipe is broken".
This is easy to miss because `Fl::add_fd` usually only fires when data is
actually available, making the bug rare but real.

---

## Build & Run

### Prerequisites (macOS with Homebrew)

```bash
brew install fltk tcl-tk python@3
```

### Build

```bash
mkdir build && cd build
cmake ..
make
```

### Run

```bash
./fltk_console     # launches the main window
```

### Test

```bash
cd build
ctest --output-on-failure
# or directly:
./test_interpreters
```
