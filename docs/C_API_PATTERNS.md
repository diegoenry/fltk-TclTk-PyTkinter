# C API Patterns: Embedding Tcl & Python

This document covers the specific C API patterns used in this project for
embedding Tcl and Python interpreters in a C++ application.

---

## Tcl Embedding Patterns

### Interpreter Lifecycle

```
 Tcl_FindExecutable(nullptr)     Initialize Tcl library (call once)
         │
         ▼
 Tcl_CreateInterp()              Create an interpreter instance
         │
         ▼
 Tcl_Init(interp)                Load Tcl standard library (init.tcl)
         │
         ▼
 Tcl_CreateObjCommand(...)       Register custom C commands
         │
         ▼
 Tcl_Eval(interp, "...")         Execute commands (repeatable)
         │
         ▼
 Tcl_GetStringResult(interp)     Read the result string
         │
         ▼
 Tcl_DeleteInterp(interp)        Destroy the interpreter (cleanup)
```

### Custom Command Signature

Every custom Tcl command implemented in C has this signature:

```cpp
int MyCommand(ClientData cd,        // your context pointer
              Tcl_Interp* interp,   // the interpreter
              int objc,             // argument count (including command name)
              Tcl_Obj* const objv[] // argument objects
);
// Returns TCL_OK or TCL_ERROR
```

**ClientData** is a `void*` that FLTK passes through unchanged. Use it to
reach your C++ object:

```cpp
// Registration:
Tcl_CreateObjCommand(interp, "mycommand", my_cmd,
                     static_cast<ClientData>(this), nullptr);

// Inside the command:
auto* self = static_cast<MyClass*>(cd);
```

### Result Handling

```
 ┌─────────────────────────────────────────────────────────────┐
 │  Tcl_SetObjResult(interp, Tcl_NewStringObj("...", -1))     │
 │  → Sets the command's return value                          │
 │  → -1 means "strlen for me"                                │
 │                                                             │
 │  Tcl_GetStringResult(interp)                                │
 │  → Returns const char* — valid until next Tcl_Eval          │
 │                                                             │
 │  Return TCL_ERROR + Tcl_SetObjResult for error messages     │
 └─────────────────────────────────────────────────────────────┘
```

---

## Python Embedding Patterns

### Interpreter Lifecycle

```
 Py_Initialize()                  Start the Python interpreter
         │
         ▼
 PyDict_New()                     Create a locals dict for the console
         │
         ▼
 code.InteractiveConsole(locals)  Create a REPL console object
         │
         ▼
 io.StringIO() × 2               Create stdout/stderr captures
         │
         ▼
 sys.stdout = capture_out         Redirect output to our captures
 sys.stderr = capture_err
         │
         ▼
 PyObject_CallMethod(console,     Execute commands (repeatable)
     "push", "s", line)                returns True if more input needed
         │
         ▼
 sio.getvalue()                   Read captured output
 sio.truncate(0); sio.seek(0)    Reset capture for next command
         │
         ▼
 Py_XDECREF(...)                  Release all PyObject* refs
         │
         ▼
 Py_FinalizeEx()                  Shut down Python (call once, at end)
```

### Reference Counting Rules

This is the most error-prone part of the Python C API.

```
 ┌─────────────────────────────────────────────────────────────────┐
 │  EVERY PyObject* IS REFERENCE-COUNTED                           │
 │                                                                 │
 │  When you CREATE a new reference (refcount starts at 1):        │
 │    PyDict_New()          → you own it, must DECREF              │
 │    PyFloat_FromDouble()  → you own it, must DECREF              │
 │    PyImport_ImportModule() → you own it, must DECREF            │
 │    PyObject_CallMethod() → you own it, must DECREF              │
 │    PyTuple_Pack()        → you own it, must DECREF              │
 │                                                                 │
 │  When you BORROW a reference (refcount NOT incremented):        │
 │    PyDict_GetItemString() → borrowed, do NOT DECREF             │
 │    PyTuple_GetItem()      → borrowed, do NOT DECREF             │
 │                                                                 │
 │  CRITICAL: Functions that STORE refs:                           │
 │    PyDict_SetItemString(dict, key, val)                         │
 │      → INCREMENTS val's refcount (does NOT steal)               │
 │      → You must still DECREF val after                          │
 │                                                                 │
 │    PyTuple_SetItem(tuple, idx, val)                             │
 │      → STEALS val's reference (does NOT increment)              │
 │      → Do NOT DECREF val after                                  │
 │                                                                 │
 │  When in doubt: check the Python C API docs for "New reference" │
 │  vs "Borrowed reference" vs "Steals a reference".               │
 └─────────────────────────────────────────────────────────────────┘
```

### Example: A Real Bug and Its Fix

This project originally had a refcount leak in `py_graph_params`. The
pattern below shows why it leaked and how it was fixed:

```cpp
// WRONG — leaked one PyFloat per iteration (this was a real bug):
PyObject* dict = PyDict_New();
for (auto& [k, v] : items)
    PyDict_SetItemString(dict, k.c_str(), PyFloat_FromDouble(v));
    //                                     ^^^^^^^^^^^^^^^^^^^^^^^^
    //                                     new ref (refcount=1)
    //                     SetItemString increments to 2
    //                     Nobody decrements the original → LEAK

// CORRECT — the fix applied in python_console.cpp:
PyObject* dict = PyDict_New();
for (auto& [k, v] : items) {
    PyObject* fval = PyFloat_FromDouble(v);  // refcount = 1
    PyDict_SetItemString(dict, k.c_str(), fval);  // refcount = 2
    Py_DECREF(fval);                         // refcount = 1 (dict owns it)
}
```

### Exposing C Functions to Python

```cpp
// 1. Define the function:
static PyObject* my_func(PyObject* self, PyObject* args) {
    const char* name;
    double value;
    if (!PyArg_ParseTuple(args, "sd", &name, &value))  // "s"=string, "d"=double
        return nullptr;  // PyArg_ParseTuple already set the exception

    // ... do work ...

    Py_RETURN_NONE;  // equivalent to: Py_INCREF(Py_None); return Py_None;
}

// 2. Register in a method table:
static PyMethodDef methods[] = {
    {"my_func", my_func, METH_VARARGS, "Description"},
    {nullptr, nullptr, 0, nullptr}  // sentinel
};

// 3. Inject into the console's locals dict:
for (PyMethodDef* m = methods; m->ml_name; ++m) {
    PyObject* func = PyCFunction_New(m, nullptr);
    PyDict_SetItemString(locals, m->ml_name, func);
    Py_DECREF(func);
}
```

---

## FLTK Patterns Used

### Widget Ownership

```
 window->begin();
     new Fl_Button(...)    ← window now owns this widget
     new Fl_Input(...)     ← window now owns this widget
 window->end();

 // When window is destroyed, all children are destroyed automatically.
 // Do NOT delete children manually.

 // EXCEPTION:
 Fl_Text_Buffer* buf = new Fl_Text_Buffer();
 display->buffer(buf);
 // Fl_Text_Display does NOT own the buffer.
 // You MUST delete buf in your destructor.
```

### Callback Pattern

```cpp
// Static function + void* data → cast back to your class
void MyWindow::some_callback(Fl_Widget* w, void* data) {
    auto* self = static_cast<MyWindow*>(data);
    self->do_something();
}

// Registration:
widget->callback(some_callback, this);
```

### Integrating External I/O (Fl::add_fd)

```cpp
// Register:
Fl::add_fd(fd, FL_READ, my_callback, my_data);

// FLTK adds fd to its select()/poll() set.
// When fd is readable, my_callback fires during the normal event loop.
// No threads needed.

// Cleanup:
Fl::remove_fd(fd);
```

---

## Shutdown Ordering

Getting cleanup right requires understanding the destruction order:

```
 ┌─────────────────────────────────────────────────────────────────┐
 │  CORRECT ORDER (as implemented)                                 │
 │                                                                 │
 │  1. Fl::run() returns (all windows closed)                     │
 │  2. Null out global pointers (set_graph_window(nullptr))       │
 │  3. g_python.release_python_objects()  ← XDECREF + null ptrs  │
 │  4. PythonConsole::finalize_python()   ← Py_FinalizeEx()      │
 │  5. main() returns                                              │
 │  6. Stack objects destroyed (FLTK windows)                     │
 │  7. Static destructors:                                         │
 │     - g_python: XDECREF no-ops (already null), delete window  │
 │     - g_tcl: Tcl_DeleteInterp, delete window                  │
 │     - plugin processes: pclose pipes, remove temp files        │
 │                                                                 │
 │  KEY: Steps 3-4 must happen in order. Never XDECREF after      │
 │  FinalizeEx. The release_python_objects() method is idempotent  │
 │  — safe to call from both main() and the destructor.            │
 └─────────────────────────────────────────────────────────────────┘
```
