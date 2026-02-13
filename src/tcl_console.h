#pragma once

#include "console_window.h"
#include <tcl.h>

class TclConsole {
public:
    TclConsole();
    ~TclConsole();

    void show();

private:
    void ensure_init();
    void init_interp();
    void on_command(const char* cmd);

    static int puts_cmd(ClientData cd, Tcl_Interp* interp,
                        int objc, Tcl_Obj* const objv[]);
    static int app_info_cmd(ClientData cd, Tcl_Interp* interp,
                            int objc, Tcl_Obj* const objv[]);
    static int graph_cmd(ClientData cd, Tcl_Interp* interp,
                         int objc, Tcl_Obj* const objv[]);
    static int launch_plugin_cmd(ClientData cd, Tcl_Interp* interp,
                                 int objc, Tcl_Obj* const objv[]);

    ConsoleWindow* win_    = nullptr;
    Tcl_Interp*    interp_ = nullptr;
};
