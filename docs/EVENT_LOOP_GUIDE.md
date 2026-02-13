# Event Loop Guide: FLTK + Tcl + Python

This guide explains the core design challenge and solution in this application:
**how to use multiple GUI toolkits without event-loop conflicts**.

---

## The Problem: One Thread, Multiple Toolkits

Every GUI toolkit has an event loop — a `while(true)` that waits for OS events
(mouse clicks, key presses, window resizes) and dispatches them to widgets.

```
   FLTK                    Tk                      Tkinter
   ─────                   ──                      ───────
   Fl::run() {             Tk_MainLoop() {         root.mainloop() {
     while (has_windows) {   while (has_windows) {   while (has_windows) {
       wait_for_event();       wait_for_event();       wait_for_event();
       dispatch();             dispatch();             dispatch();
     }                       }                       }
   }                       }                       }
```

If you call `Fl::run()`, it blocks until all FLTK windows close. While it's
blocking, no other event loop can run. You can't interleave them:

```
 Thread execution ──▶ Fl::run() ──────────────────▶ (blocking)
                          │
                          ╳ Tk_MainLoop() — NEVER REACHED
                          ╳ root.mainloop() — NEVER REACHED
```

This means Tk and Tkinter windows would freeze — they'd draw once but never
process events.

---

## The Solution: Embed Interpreters, Isolate GUIs

```
 ┌──────────────────────────────────────────────────────────────┐
 │                                                              │
 │  SAFE: Embed interpreters (synchronous function calls)       │
 │  ─────                                                       │
 │  Tcl_Eval(interp, "expr 2+2")      → returns "4"           │
 │  console.push("print('hello')")     → returns, prints text  │
 │                                                              │
 │  These are NOT event loops. They're function calls that      │
 │  execute and return. FLTK's event loop stays in control.     │
 │                                                              │
 ├──────────────────────────────────────────────────────────────┤
 │                                                              │
 │  SAFE: Launch GUI toolkits in subprocesses                   │
 │  ─────                                                       │
 │  popen("tclsh script.tcl")          → child runs Tk_MainLoop│
 │  popen("python3 script.py")         → child runs mainloop() │
 │                                                              │
 │  Each process has its own event loop. They don't conflict.   │
 │  Communication happens via pipes (stdout → Fl::add_fd).      │
 │                                                              │
 ├──────────────────────────────────────────────────────────────┤
 │                                                              │
 │  DANGEROUS: Running Tk/Tkinter in the same process           │
 │  ──────────                                                  │
 │  Tcl_Eval(interp, "package require Tk")  ← starts Tk event  │
 │  import tkinter; root = tk.Tk()          ← starts Tk event  │
 │                                                              │
 │  Now you have TWO toolkits competing for the event loop.     │
 │  Widgets freeze, events get lost, timers break.              │
 │                                                              │
 └──────────────────────────────────────────────────────────────┘
```

---

## How FLTK Stays in Control

Here is exactly what happens during one iteration of FLTK's event loop when
the user interacts with different parts of the application:

### Case 1: User types a Tcl command

```
 Fl::run()
    │
    ├──▶ wait for event
    │
    ├──▶ [keyboard: Enter pressed in Tcl console input]
    │       │
    │       ├──▶ on_input_enter() callback fires
    │       │       │
    │       │       ├──▶ save to history
    │       │       ├──▶ cmd_cb_("expr 2+2")
    │       │       │       │
    │       │       │       ├──▶ Tcl_Eval(interp_, "expr 2+2")
    │       │       │       │       │
    │       │       │       │       └──▶ Tcl executes, returns TCL_OK
    │       │       │       │
    │       │       │       ├──▶ Tcl_GetStringResult → "4"
    │       │       │       ├──▶ win_->append_output("4\n")
    │       │       │       └──▶ return
    │       │       │
    │       │       └──▶ return
    │       │
    │       └──▶ return (callback done)
    │
    ├──▶ wait for next event ...
```

Total time spent in Tcl: microseconds. FLTK never lost control.

### Case 2: Plugin subprocess sends data

```
 Fl::run()
    │
    ├──▶ wait for event
    │
    ├──▶ [pipe fd is readable — Fl::add_fd handler fires]
    │       │
    │       ├──▶ fd_callback()
    │       │       │
    │       │       ├──▶ read(fd, buf, ...) → "SET a 5.000000\n"
    │       │       ├──▶ line_buf_ += buf
    │       │       ├──▶ found '\n' → process_line("SET a 5.000000")
    │       │       │       │
    │       │       │       ├──▶ sscanf → param="a", value=5.0
    │       │       │       ├──▶ gw->params().set("a", 5.0)
    │       │       │       ├──▶ gw->sync_and_redraw()
    │       │       │       └──▶ return
    │       │       │
    │       │       └──▶ return
    │       │
    │       └──▶ return (handler done)
    │
    ├──▶ [FLTK redraws damaged widgets — graph canvas repaints]
    │
    ├──▶ wait for next event ...
```

The pipe is read in tiny chunks, never blocking. FLTK stays responsive.

---

## Fl::add_fd — The Key Integration Point

```cpp
// In PluginProcess::launch():
Fl::add_fd(fd_, FL_READ, fd_callback, this);
```

This tells FLTK: "When `fd_` has data to read, call `fd_callback`."

Internally, FLTK adds this fd to its `select()` / `poll()` call. The event
loop already waits for OS events — now it also waits for pipe data. No extra
threads, no busy-waiting.

```
 Fl::run() internally does something like:

    while (windows_open) {
        select(max_fd+1, &read_fds, ..., timeout);
        //       ▲
        //       └── includes our pipe fd!

        if (FD_ISSET(pipe_fd, &read_fds))
            call fd_callback();      // our handler

        dispatch_fltk_events();      // mouse, keyboard, redraws
    }
```

When the child process exits, `read()` returns 0 (EOF), and our handler
calls `stop()` which does `Fl::remove_fd(fd_)` — clean teardown.

---

## What About Cooperative Event Loop Interleaving?

Some applications try to run Tk inside the FLTK process by manually
interleaving the two event loops:

```cpp
// DON'T DO THIS (fragile, platform-dependent, hard to get right)
void idle_callback(void*) {
    while (Tcl_DoOneEvent(TCL_DONT_WAIT)) { }  // pump Tk events
}
Fl::add_idle(idle_callback);
```

This *can* work but has serious drawbacks:

| Issue | Description |
|-------|-------------|
| **CPU usage** | `TCL_DONT_WAIT` busy-loops when idle |
| **Timing** | Tk timers and animations don't fire at the right rate |
| **Platform** | macOS requires the event loop on the main thread — two toolkits fight for it |
| **Complexity** | Error handling, shutdown ordering, and edge cases multiply |
| **Debugging** | When something freezes, which event loop is stuck? |

The subprocess approach avoids all of this. Each process runs one toolkit
correctly. Communication is explicit and simple.

---

## Summary Rules

```
 ╔═══════════════════════════════════════════════════════════════╗
 ║  RULE 1:  One GUI event loop per thread (ideally per process)║
 ║                                                               ║
 ║  RULE 2:  Embedding an interpreter ≠ running its event loop  ║
 ║           Tcl_Eval() is a function call, not an event loop   ║
 ║                                                               ║
 ║  RULE 3:  Use Fl::add_fd() to integrate pipe I/O into FLTK  ║
 ║           No threads needed for subprocess communication      ║
 ║                                                               ║
 ║  RULE 4:  If you need a second GUI toolkit's widgets,        ║
 ║           launch it in a subprocess                           ║
 ║                                                               ║
 ║  RULE 5:  Design a simple text protocol for IPC              ║
 ║           (e.g., "SET param value" lines on stdout)          ║
 ╚═══════════════════════════════════════════════════════════════╝
```
