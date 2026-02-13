#pragma once

#include "console_window.h"

struct _object;
typedef _object PyObject;

class PythonConsole {
public:
    PythonConsole();
    ~PythonConsole();

    void show();

    // Release all Python object refs. Safe to call multiple times.
    // Must be called while the Python interpreter is still alive.
    void release_python_objects();

    static void finalize_python();

private:
    void ensure_init();
    void init_python();
    void on_command(const char* cmd);
    void flush_output();

    ConsoleWindow* win_       = nullptr;

    PyObject* console_obj_    = nullptr;
    PyObject* capture_out_    = nullptr;
    PyObject* capture_err_    = nullptr;
    PyObject* locals_         = nullptr;

    bool more_ = false;
    static bool py_initialized_;
};
