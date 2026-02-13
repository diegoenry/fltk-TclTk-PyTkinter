#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>

#include "tcl_console.h"
#include "python_console.h"
#include "graph_window.h"
#include "plugin_process.h"

static TclConsole    g_tcl;
static PythonConsole g_python;

static void tcl_btn_cb(Fl_Widget*, void*)     { g_tcl.show(); }
static void python_btn_cb(Fl_Widget*, void*)  { g_python.show(); }
static void graph_btn_cb(Fl_Widget*, void*)   { if (auto* gw = get_graph_window()) gw->show(); }
static void tk_plugin_cb(Fl_Widget*, void*)   { launch_tk_graph_plugin(); }
static void tkinter_plugin_cb(Fl_Widget*, void*) { launch_tkinter_graph_plugin(); }

int main(int argc, char* argv[]) {
    Fl_Window win(420, 160, "FLTK Console Launcher");
    win.begin();

    Fl_Button tcl_btn(20,  15, 120, 45, "Tcl Console");
    Fl_Button py_btn (150, 15, 120, 45, "Python Console");
    Fl_Button grp_btn(280, 15, 120, 45, "Graph");
    tcl_btn.callback(tcl_btn_cb);
    py_btn.callback(python_btn_cb);
    grp_btn.callback(graph_btn_cb);

    Fl_Button tk_btn    (75,  80, 120, 45, "Tk Plugin");
    Fl_Button tkinter_btn(225, 80, 120, 45, "Tkinter Plugin");
    tk_btn.callback(tk_plugin_cb);
    tkinter_btn.callback(tkinter_plugin_cb);

    win.end();
    win.show(argc, argv);

    GraphWindow graph_win(700, 650, "Parametric Graph");
    set_graph_window(&graph_win);

    int ret = Fl::run();

    set_graph_window(nullptr);
    g_python.release_python_objects();
    PythonConsole::finalize_python();
    return ret;
}
