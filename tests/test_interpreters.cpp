// Test suite for the embedded Tcl and Python interpreters.
// Exercises the same C API calls the app uses, but captures output into
// strings instead of an FLTK widget.  No display server required.

#include <Python.h>   // Must come first on some platforms.

#include <tcl.h>

#include "graph_params.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// ── Tiny test harness ───────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

struct TestFailure {};

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            std::cout << "FAIL\n    " __FILE__ ":"                   \
                      << __LINE__ << ": " #cond "\n";               \
            ++g_fail;                                                \
            throw TestFailure{};                                     \
        }                                                            \
    } while (0)

#define CHECK_STR(a, b)                                              \
    do {                                                             \
        std::string _a(a), _b(b);                                    \
        if (_a != _b) {                                              \
            std::cout << "FAIL\n    " __FILE__ ":"                   \
                      << __LINE__ << ": \"" << _a                    \
                      << "\" != \"" << _b << "\"\n";                 \
            ++g_fail;                                                \
            throw TestFailure{};                                     \
        }                                                            \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                             \
    do {                                                             \
        std::string _h(haystack), _n(needle);                        \
        if (_h.find(_n) == std::string::npos) {                      \
            std::cout << "FAIL\n    " __FILE__ ":"                   \
                      << __LINE__ << ": \"" << _h                    \
                      << "\" does not contain \""                    \
                      << _n << "\"\n";                               \
            ++g_fail;                                                \
            throw TestFailure{};                                     \
        }                                                            \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                        \
    do {                                                             \
        double _a = (a), _b = (b);                                   \
        if (std::fabs(_a - _b) > (tol)) {                           \
            std::cout << "FAIL\n    " __FILE__ ":"                   \
                      << __LINE__ << ": " << _a                      \
                      << " not near " << _b << "\n";                 \
            ++g_fail;                                                \
            throw TestFailure{};                                     \
        }                                                            \
    } while (0)

static void run_test(const char* name, std::function<void()> fn) {
    std::cout << "  " << name << " ... " << std::flush;
    try {
        fn();
        std::cout << "ok\n";
        ++g_pass;
    } catch (const TestFailure&) {
        // already counted in CHECK macros
    }
}

// ── Tcl output capture (mirrors the app's custom puts) ──────────
static std::string g_tcl_output;

static int test_puts_cmd(ClientData /*cd*/, Tcl_Interp* interp,
                         int objc, Tcl_Obj* const objv[])
{
    bool newline = true;
    int strIdx = 1;

    if (objc >= 2) {
        const char* first = Tcl_GetString(objv[1]);
        if (std::strcmp(first, "-nonewline") == 0) {
            newline = false;
            strIdx = 2;
        }
    }
    int remaining = objc - strIdx;
    if (remaining == 2) strIdx += 1;   // skip channel
    else if (remaining < 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # args", -1));
        return TCL_ERROR;
    }

    g_tcl_output += Tcl_GetString(objv[strIdx]);
    if (newline) g_tcl_output += "\n";
    return TCL_OK;
}

// ── Python helpers ──────────────────────────────────────────────
static std::string py_drain_stringio(PyObject* sio) {
    std::string result;
    PyObject* val = PyObject_CallMethod(sio, "getvalue", nullptr);
    if (val) {
        const char* s = PyUnicode_AsUTF8(val);
        if (s) result = s;
        Py_DECREF(val);
    }
    PyObject* zero = PyLong_FromLong(0);
    PyObject* r1 = PyObject_CallMethod(sio, "truncate", "O", zero);
    Py_XDECREF(r1);
    PyObject* r2 = PyObject_CallMethod(sio, "seek", "O", zero);
    Py_XDECREF(r2);
    Py_DECREF(zero);
    return result;
}

struct PushResult {
    bool        more;
    std::string out;
    std::string err;
};

static PushResult py_push(PyObject* console,
                          PyObject* cap_out, PyObject* cap_err,
                          const char* line)
{
    PushResult pr{};
    PyObject* r = PyObject_CallMethod(console, "push", "s", line);
    pr.out = py_drain_stringio(cap_out);
    pr.err = py_drain_stringio(cap_err);
    if (r) {
        pr.more = PyObject_IsTrue(r);
        Py_DECREF(r);
    } else {
        PyErr_Print();
        pr.out += py_drain_stringio(cap_out);
        pr.err += py_drain_stringio(cap_err);
        pr.more = false;
    }
    return pr;
}

// ═════════════════════════════════════════════════════════════════
//  Tcl tests
// ═════════════════════════════════════════════════════════════════
static void run_tcl_tests() {
    std::cout << "\n=== Tcl interpreter tests ===\n";

    Tcl_FindExecutable(nullptr);
    Tcl_Interp* interp = Tcl_CreateInterp();
    Tcl_Init(interp);
    Tcl_CreateObjCommand(interp, "puts", test_puts_cmd, nullptr, nullptr);

    run_test("tcl_expr_arithmetic", [&]() {
        int rc = Tcl_Eval(interp, "expr {2 + 2}");
        CHECK(rc == TCL_OK);
        CHECK_STR(Tcl_GetStringResult(interp), "4");
    });

    run_test("tcl_string_length", [&]() {
        int rc = Tcl_Eval(interp, "string length \"hello world\"");
        CHECK(rc == TCL_OK);
        CHECK_STR(Tcl_GetStringResult(interp), "11");
    });

    run_test("tcl_variable_set_get", [&]() {
        Tcl_Eval(interp, "set x 42");
        int rc = Tcl_Eval(interp, "expr {$x * 2}");
        CHECK(rc == TCL_OK);
        CHECK_STR(Tcl_GetStringResult(interp), "84");
    });

    run_test("tcl_puts_capture", [&]() {
        g_tcl_output.clear();
        int rc = Tcl_Eval(interp, "puts \"hello from tcl\"");
        CHECK(rc == TCL_OK);
        CHECK_STR(g_tcl_output, "hello from tcl\n");
    });

    run_test("tcl_puts_nonewline", [&]() {
        g_tcl_output.clear();
        int rc = Tcl_Eval(interp, "puts -nonewline \"no newline\"");
        CHECK(rc == TCL_OK);
        CHECK_STR(g_tcl_output, "no newline");
    });

    run_test("tcl_puts_with_channel", [&]() {
        g_tcl_output.clear();
        int rc = Tcl_Eval(interp, "puts stdout \"via stdout\"");
        CHECK(rc == TCL_OK);
        CHECK_STR(g_tcl_output, "via stdout\n");
    });

    run_test("tcl_syntax_error", [&]() {
        int rc = Tcl_Eval(interp, "expr {2 +}");
        CHECK(rc == TCL_ERROR);
        const char* err = Tcl_GetStringResult(interp);
        CHECK(err != nullptr);
        CHECK(std::strlen(err) > 0);
    });

    run_test("tcl_unknown_command", [&]() {
        int rc = Tcl_Eval(interp, "nonexistent_cmd");
        CHECK(rc == TCL_ERROR);
        CHECK_CONTAINS(Tcl_GetStringResult(interp), "nonexistent_cmd");
    });

    run_test("tcl_proc_define_and_call", [&]() {
        Tcl_Eval(interp, "proc double {n} { expr {$n * 2} }");
        int rc = Tcl_Eval(interp, "double 21");
        CHECK(rc == TCL_OK);
        CHECK_STR(Tcl_GetStringResult(interp), "42");
    });

    run_test("tcl_list_operations", [&]() {
        int rc = Tcl_Eval(interp, "llength {a b c d}");
        CHECK(rc == TCL_OK);
        CHECK_STR(Tcl_GetStringResult(interp), "4");
    });

    Tcl_DeleteInterp(interp);
}

// ═════════════════════════════════════════════════════════════════
//  Python tests
// ═════════════════════════════════════════════════════════════════
static void run_python_tests() {
    std::cout << "\n=== Python interpreter tests ===\n";

    Py_Initialize();

    // Set up InteractiveConsole + StringIO capture (same as the app).
    PyObject* locals = PyDict_New();
    PyObject* code_mod = PyImport_ImportModule("code");
    PyObject* ic_class = PyObject_GetAttrString(code_mod, "InteractiveConsole");
    Py_DECREF(code_mod);

    PyObject* args = PyTuple_Pack(1, locals);
    PyObject* console = PyObject_CallObject(ic_class, args);
    Py_DECREF(args);
    Py_DECREF(ic_class);

    PyObject* io_mod = PyImport_ImportModule("io");
    PyObject* sio_class = PyObject_GetAttrString(io_mod, "StringIO");
    Py_DECREF(io_mod);

    PyObject* cap_out = PyObject_CallNoArgs(sio_class);
    PyObject* cap_err = PyObject_CallNoArgs(sio_class);
    Py_DECREF(sio_class);

    PyObject* sys_mod = PyImport_ImportModule("sys");
    PyObject_SetAttrString(sys_mod, "stdout", cap_out);
    PyObject_SetAttrString(sys_mod, "stderr", cap_err);
    Py_DECREF(sys_mod);

    run_test("py_simple_expression", [&]() {
        auto r = py_push(console, cap_out, cap_err, "2 + 2");
        CHECK(!r.more);
        CHECK_CONTAINS(r.out, "4");
    });

    run_test("py_print", [&]() {
        auto r = py_push(console, cap_out, cap_err, "print('hello python')");
        CHECK(!r.more);
        CHECK_CONTAINS(r.out, "hello python");
    });

    run_test("py_variable_assignment", [&]() {
        auto r = py_push(console, cap_out, cap_err, "x = 42");
        CHECK(!r.more);
        CHECK(r.out.empty());
        CHECK(r.err.empty());
    });

    run_test("py_use_variable", [&]() {
        auto r = py_push(console, cap_out, cap_err, "x * 2");
        CHECK(!r.more);
        CHECK_CONTAINS(r.out, "84");
    });

    run_test("py_multiline_for_loop", [&]() {
        auto r1 = py_push(console, cap_out, cap_err, "for i in range(3):");
        CHECK(r1.more);

        auto r2 = py_push(console, cap_out, cap_err, "    print(i)");
        CHECK(r2.more);

        auto r3 = py_push(console, cap_out, cap_err, "");  // blank line ends block
        CHECK(!r3.more);
        CHECK_CONTAINS(r3.out, "0");
        CHECK_CONTAINS(r3.out, "1");
        CHECK_CONTAINS(r3.out, "2");
    });

    run_test("py_multiline_function_def", [&]() {
        auto r1 = py_push(console, cap_out, cap_err, "def greet(name):");
        CHECK(r1.more);

        auto r2 = py_push(console, cap_out, cap_err, "    return f'hello {name}'");
        CHECK(r2.more);

        auto r3 = py_push(console, cap_out, cap_err, "");
        CHECK(!r3.more);

        auto r4 = py_push(console, cap_out, cap_err, "greet('world')");
        CHECK(!r4.more);
        CHECK_CONTAINS(r4.out, "hello world");
    });

    run_test("py_syntax_error", [&]() {
        auto r = py_push(console, cap_out, cap_err, "def (");
        CHECK(!r.more);
        CHECK_CONTAINS(r.err, "SyntaxError");
    });

    run_test("py_runtime_error", [&]() {
        auto r = py_push(console, cap_out, cap_err, "undefined_variable");
        CHECK(!r.more);
        CHECK_CONTAINS(r.err, "NameError");
    });

    run_test("py_import_module", [&]() {
        auto r = py_push(console, cap_out, cap_err, "import math");
        CHECK(!r.more);
        CHECK(r.err.empty());

        auto r2 = py_push(console, cap_out, cap_err, "print(math.pi)");
        CHECK(!r2.more);
        CHECK_CONTAINS(r2.out, "3.14159");
    });

    run_test("py_list_comprehension", [&]() {
        auto r = py_push(console, cap_out, cap_err,
                         "[i**2 for i in range(5)]");
        CHECK(!r.more);
        CHECK_CONTAINS(r.out, "0, 1, 4, 9, 16");
    });

    run_test("py_recovery_after_error", [&]() {
        py_push(console, cap_out, cap_err, "1/0");  // ZeroDivisionError
        auto r = py_push(console, cap_out, cap_err, "print('recovered')");
        CHECK(!r.more);
        CHECK_CONTAINS(r.out, "recovered");
    });

    Py_DECREF(console);
    Py_DECREF(cap_out);
    Py_DECREF(cap_err);
    Py_DECREF(locals);
    Py_FinalizeEx();
}

// ═════════════════════════════════════════════════════════════════
//  GraphParams tests (pure C++, no FLTK)
// ═════════════════════════════════════════════════════════════════
static void run_graph_tests() {
    std::cout << "\n=== Graph parameter tests ===\n";

    run_test("graph_defaults", []() {
        GraphParams p;
        CHECK_NEAR(p.a, 3.0, 1e-9);
        CHECK_NEAR(p.b, 2.0, 1e-9);
        CHECK_NEAR(p.A, 1.0, 1e-9);
        CHECK_NEAR(p.B, 1.0, 1e-9);
        CHECK_NEAR(p.delta, M_PI / 2.0, 1e-9);
        CHECK(p.num_points == 1000);
    });

    run_test("graph_set_get", []() {
        GraphParams p;
        CHECK(p.set("a", 5.0));
        CHECK_NEAR(p.get("a"), 5.0, 1e-9);
        CHECK(p.set("b", 7.0));
        CHECK_NEAR(p.get("b"), 7.0, 1e-9);
        CHECK(p.set("A", 1.5));
        CHECK_NEAR(p.get("A"), 1.5, 1e-9);
        CHECK(p.set("B", 0.8));
        CHECK_NEAR(p.get("B"), 0.8, 1e-9);
        CHECK(p.set("delta", 1.23));
        CHECK_NEAR(p.get("delta"), 1.23, 1e-9);
        CHECK(p.set("points", 500));
        CHECK_NEAR(p.get("points"), 500.0, 1e-9);
    });

    run_test("graph_set_unknown", []() {
        GraphParams p;
        CHECK(!p.set("bogus", 1.0));
        CHECK(std::isnan(p.get("bogus")));
    });

    run_test("graph_preset_circle", []() {
        GraphParams p;
        CHECK(p.load_preset("circle"));
        CHECK_NEAR(p.a, 1.0, 1e-9);
        CHECK_NEAR(p.b, 1.0, 1e-9);
        CHECK_NEAR(p.delta, M_PI / 2.0, 1e-9);
        // Circle: x(t)=cos(t), y(t)=sin(t)
        auto [x0, y0] = p.eval(0.0);
        CHECK_NEAR(x0, 1.0, 1e-9);   // cos(0)=1
        CHECK_NEAR(y0, 0.0, 1e-9);   // sin(0)=0
        auto [x1, y1] = p.eval(M_PI / 2.0);
        CHECK_NEAR(x1, 0.0, 1e-6);   // cos(pi/2)=0
        CHECK_NEAR(y1, 1.0, 1e-6);   // sin(pi/2)=1
    });

    run_test("graph_preset_figure8", []() {
        GraphParams p;
        CHECK(p.load_preset("figure8"));
        CHECK_NEAR(p.a, 1.0, 1e-9);
        CHECK_NEAR(p.b, 2.0, 1e-9);
        CHECK_NEAR(p.delta, 0.0, 1e-9);
        // At t=0: x=sin(0)=0, y=sin(0)=0
        auto [x0, y0] = p.eval(0.0);
        CHECK_NEAR(x0, 0.0, 1e-9);
        CHECK_NEAR(y0, 0.0, 1e-9);
    });

    run_test("graph_preset_unknown", []() {
        GraphParams p;
        CHECK(!p.load_preset("nonexistent"));
    });

    run_test("graph_all_params", []() {
        GraphParams p;
        p.set("a", 4.0);
        p.set("b", 5.0);
        auto m = p.all();
        CHECK(m.size() == 6);
        CHECK_NEAR(m["a"], 4.0, 1e-9);
        CHECK_NEAR(m["b"], 5.0, 1e-9);
        CHECK(m.count("A") == 1);
        CHECK(m.count("B") == 1);
        CHECK(m.count("delta") == 1);
        CHECK(m.count("points") == 1);
    });

    run_test("graph_eval_lissajous", []() {
        GraphParams p;  // defaults: a=3, b=2, A=1, B=1, delta=pi/2
        // At t=0: x=sin(delta)=sin(pi/2)=1, y=sin(0)=0
        auto [x0, y0] = p.eval(0.0);
        CHECK_NEAR(x0, 1.0, 1e-9);
        CHECK_NEAR(y0, 0.0, 1e-9);
        // At t=pi: x=sin(3*pi + pi/2)=sin(7pi/2)=-1, y=sin(2*pi)=0
        auto [xp, yp] = p.eval(M_PI);
        CHECK_NEAR(xp, -1.0, 1e-6);
        CHECK_NEAR(yp, 0.0, 1e-6);
    });

    run_test("graph_eval_amplitude_scaling", []() {
        GraphParams p;
        p.load_preset("circle");
        p.A = 2.0;
        p.B = 3.0;
        auto [x0, y0] = p.eval(0.0);
        CHECK_NEAR(x0, 2.0, 1e-9);   // A * cos(0)
        CHECK_NEAR(y0, 0.0, 1e-9);
        auto [x1, y1] = p.eval(M_PI / 2.0);
        CHECK_NEAR(x1, 0.0, 1e-6);
        CHECK_NEAR(y1, 3.0, 1e-6);   // B * sin(pi/2)
    });
}

// ═════════════════════════════════════════════════════════════════
int main() {
    run_tcl_tests();
    run_python_tests();
    run_graph_tests();

    std::cout << "\n=== Results: " << g_pass << " passed, "
              << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
