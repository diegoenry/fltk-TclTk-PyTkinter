#include "python_console.h"
#include "graph_window.h"
#include "plugin_process.h"

#include <Python.h>

#include <cmath>
#include <string>

bool PythonConsole::py_initialized_ = false;

// ── Python C-function wrappers for the graph ────────────────────

static PyObject* py_graph_set(PyObject*, PyObject* args) {
    const char* param; double value;
    if (!PyArg_ParseTuple(args, "sd", &param, &value)) return nullptr;
    auto* gw = get_graph_window();
    if (!gw) { PyErr_SetString(PyExc_RuntimeError, "graph window not available"); return nullptr; }
    if (!gw->params().set(param, value)) {
        PyErr_SetString(PyExc_ValueError, "unknown parameter (a, b, A, B, delta, points)");
        return nullptr;
    }
    gw->show(); gw->sync_and_redraw();
    Py_RETURN_NONE;
}

static PyObject* py_graph_get(PyObject*, PyObject* args) {
    const char* param;
    if (!PyArg_ParseTuple(args, "s", &param)) return nullptr;
    auto* gw = get_graph_window();
    if (!gw) { PyErr_SetString(PyExc_RuntimeError, "graph window not available"); return nullptr; }
    double v = gw->params().get(param);
    if (std::isnan(v)) { PyErr_SetString(PyExc_ValueError, "unknown parameter"); return nullptr; }
    return PyFloat_FromDouble(v);
}

static PyObject* py_graph_params(PyObject*, PyObject*) {
    auto* gw = get_graph_window();
    if (!gw) { PyErr_SetString(PyExc_RuntimeError, "graph window not available"); return nullptr; }
    auto all = gw->params().all();
    PyObject* dict = PyDict_New();
    for (auto& [k, v] : all) {
        PyObject* fval = PyFloat_FromDouble(v);
        PyDict_SetItemString(dict, k.c_str(), fval);
        Py_DECREF(fval);
    }
    return dict;
}

static PyObject* py_graph_preset(PyObject*, PyObject* args) {
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name)) return nullptr;
    auto* gw = get_graph_window();
    if (!gw) { PyErr_SetString(PyExc_RuntimeError, "graph window not available"); return nullptr; }
    if (!gw->params().load_preset(name)) {
        PyErr_SetString(PyExc_ValueError, "unknown preset (circle, figure8, lissajous, star, bowtie)");
        return nullptr;
    }
    gw->show(); gw->sync_and_redraw();
    Py_RETURN_NONE;
}

static PyObject* py_graph_eval(PyObject*, PyObject* args) {
    double t;
    if (!PyArg_ParseTuple(args, "d", &t)) return nullptr;
    auto* gw = get_graph_window();
    if (!gw) { PyErr_SetString(PyExc_RuntimeError, "graph window not available"); return nullptr; }
    auto [px, py] = gw->params().eval(t);
    return Py_BuildValue("(dd)", px, py);
}

static PyObject* py_launch_tk(PyObject*, PyObject*)       { launch_tk_graph_plugin();       Py_RETURN_NONE; }
static PyObject* py_launch_tkinter(PyObject*, PyObject*)  { launch_tkinter_graph_plugin();  Py_RETURN_NONE; }

static PyMethodDef graph_method_defs[] = {
    {"graph_set",              py_graph_set,       METH_VARARGS, "graph_set('param', value)"},
    {"graph_get",              py_graph_get,       METH_VARARGS, "graph_get('param')"},
    {"graph_params",           py_graph_params,    METH_NOARGS,  "graph_params() -> dict"},
    {"graph_preset",           py_graph_preset,    METH_VARARGS, "graph_preset('name')"},
    {"graph_eval",             py_graph_eval,      METH_VARARGS, "graph_eval(t) -> (x,y)"},
    {"launch_tk_plugin",       py_launch_tk,       METH_NOARGS,  "Launch Tcl/Tk graph plugin"},
    {"launch_tkinter_plugin",  py_launch_tkinter,  METH_NOARGS,  "Launch tkinter graph plugin"},
    {nullptr, nullptr, 0, nullptr}
};

// ── PythonConsole ───────────────────────────────────────────────

PythonConsole::PythonConsole() = default;

PythonConsole::~PythonConsole() {
    release_python_objects();
    delete win_;
}

void PythonConsole::release_python_objects() {
    Py_XDECREF(console_obj_);  console_obj_  = nullptr;
    Py_XDECREF(capture_out_);  capture_out_  = nullptr;
    Py_XDECREF(capture_err_);  capture_err_  = nullptr;
    Py_XDECREF(locals_);       locals_       = nullptr;
}

void PythonConsole::finalize_python() {
    if (py_initialized_) {
        Py_FinalizeEx();
        py_initialized_ = false;
    }
}

void PythonConsole::init_python() {
    if (!py_initialized_) {
        Py_Initialize();
        py_initialized_ = true;
    }

    locals_ = PyDict_New();

    PyObject* code_mod = PyImport_ImportModule("code");
    if (!code_mod) { win_->append_output("ERROR: could not import 'code'\n"); PyErr_Print(); return; }
    PyObject* ic_class = PyObject_GetAttrString(code_mod, "InteractiveConsole");
    Py_DECREF(code_mod);
    if (!ic_class) { win_->append_output("ERROR: InteractiveConsole not found\n"); PyErr_Print(); return; }

    PyObject* args = PyTuple_Pack(1, locals_);
    console_obj_ = PyObject_CallObject(ic_class, args);
    Py_DECREF(args); Py_DECREF(ic_class);
    if (!console_obj_) { win_->append_output("ERROR: could not create InteractiveConsole\n"); PyErr_Print(); return; }

    PyObject* io_mod = PyImport_ImportModule("io");
    if (!io_mod) { win_->append_output("ERROR: could not import 'io'\n"); PyErr_Print(); return; }
    PyObject* sio_class = PyObject_GetAttrString(io_mod, "StringIO");
    Py_DECREF(io_mod);
    capture_out_ = PyObject_CallNoArgs(sio_class);
    capture_err_ = PyObject_CallNoArgs(sio_class);
    Py_DECREF(sio_class);
    if (!capture_out_ || !capture_err_) { win_->append_output("ERROR: StringIO failed\n"); PyErr_Print(); return; }

    PyObject* sys_mod = PyImport_ImportModule("sys");
    if (sys_mod) {
        PyObject_SetAttrString(sys_mod, "stdout", capture_out_);
        PyObject_SetAttrString(sys_mod, "stderr", capture_err_);
        Py_DECREF(sys_mod);
    }

    for (PyMethodDef* m = graph_method_defs; m->ml_name; ++m) {
        PyObject* func = PyCFunction_New(m, nullptr);
        PyDict_SetItemString(locals_, m->ml_name, func);
        Py_DECREF(func);
    }
}

void PythonConsole::ensure_init() {
    if (!win_) {
        win_ = new ConsoleWindow(600, 400, "Python Console");
        win_->set_prompt(">>> ");
        win_->set_command_callback([this](const char* cmd) { on_command(cmd); });
    }
    if (!console_obj_) {
        init_python();
        if (py_initialized_) {
            std::string banner = std::string("Python ") + Py_GetVersion() + "\n";
            win_->append_output(banner.c_str());
        }
    }
}

void PythonConsole::show() {
    ensure_init();
    win_->show();
}

void PythonConsole::on_command(const char* cmd) {
    const char* prompt = more_ ? "... " : ">>> ";
    std::string echo = std::string(prompt) + cmd + "\n";
    win_->append_output(echo.c_str());

    if (!console_obj_) return;

    PyObject* result = PyObject_CallMethod(console_obj_, "push", "s", cmd);
    flush_output();

    if (result) {
        more_ = PyObject_IsTrue(result);
        Py_DECREF(result);
    } else {
        PyErr_Print();
        flush_output();
        more_ = false;
    }
    win_->set_prompt(more_ ? "... " : ">>> ");
}

void PythonConsole::flush_output() {
    auto read_and_reset = [&](PyObject* sio) {
        if (!sio) return;
        PyObject* val = PyObject_CallMethod(sio, "getvalue", nullptr);
        if (val) {
            const char* s = PyUnicode_AsUTF8(val);
            if (s && s[0] != '\0') win_->append_output(s);
            Py_DECREF(val);
        }
        PyObject* zero = PyLong_FromLong(0);
        PyObject* r1 = PyObject_CallMethod(sio, "truncate", "O", zero); Py_XDECREF(r1);
        PyObject* r2 = PyObject_CallMethod(sio, "seek",     "O", zero); Py_XDECREF(r2);
        Py_DECREF(zero);
    };
    read_and_reset(capture_out_);
    read_and_reset(capture_err_);
}
