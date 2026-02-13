#include "tcl_console.h"
#include "graph_window.h"
#include "plugin_process.h"

#include <cmath>
#include <cstring>
#include <string>

TclConsole::TclConsole() = default;

TclConsole::~TclConsole() {
    if (interp_) {
        Tcl_DeleteInterp(interp_);
        interp_ = nullptr;
    }
    delete win_;
}

void TclConsole::init_interp() {
    Tcl_FindExecutable(nullptr);
    interp_ = Tcl_CreateInterp();
    if (Tcl_Init(interp_) != TCL_OK) {
        const char* err = Tcl_GetStringResult(interp_);
        if (win_) {
            win_->append_output("Tcl_Init error: ");
            win_->append_output(err);
            win_->append_output("\n");
        }
    }

    Tcl_CreateObjCommand(interp_, "puts", puts_cmd,
                         static_cast<ClientData>(this), nullptr);
    Tcl_CreateObjCommand(interp_, "app_info", app_info_cmd,
                         static_cast<ClientData>(this), nullptr);
    Tcl_CreateObjCommand(interp_, "graph", graph_cmd,
                         static_cast<ClientData>(this), nullptr);
    Tcl_CreateObjCommand(interp_, "launch_tk_plugin", launch_plugin_cmd,
                         reinterpret_cast<ClientData>(0), nullptr);
    Tcl_CreateObjCommand(interp_, "launch_tkinter_plugin", launch_plugin_cmd,
                         reinterpret_cast<ClientData>(1), nullptr);
}

void TclConsole::ensure_init() {
    if (!win_) {
        win_ = new ConsoleWindow(600, 400, "Tcl Console");
        win_->set_prompt("% ");
        win_->set_command_callback([this](const char* cmd) { on_command(cmd); });
    }
    if (!interp_) {
        init_interp();
        win_->append_output("Tcl " TCL_VERSION " ready.\n");
    }
}

void TclConsole::show() {
    ensure_init();
    win_->show();
}

// ── Console command handling ────────────────────────────────────

void TclConsole::on_command(const char* cmd) {
    std::string echo = std::string("% ") + cmd + "\n";
    win_->append_output(echo.c_str());

    int rc = Tcl_Eval(interp_, cmd);
    const char* result = Tcl_GetStringResult(interp_);

    if (result && result[0] != '\0') {
        if (rc == TCL_ERROR) {
            std::string err = std::string("ERROR: ") + result + "\n";
            win_->append_output(err.c_str());
        } else {
            std::string out = std::string(result) + "\n";
            win_->append_output(out.c_str());
        }
    }
}

// ── Custom puts ─────────────────────────────────────────────────
int TclConsole::puts_cmd(ClientData cd, Tcl_Interp* interp,
                         int objc, Tcl_Obj* const objv[])
{
    auto* self = static_cast<TclConsole*>(cd);

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
    if (remaining == 2) {
        strIdx += 1;
    } else if (remaining < 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "wrong # args: should be \"puts ?-nonewline? ?channelId? string\"", -1));
        return TCL_ERROR;
    }

    const char* str = Tcl_GetString(objv[strIdx]);
    if (self->win_) {
        self->win_->append_output(str);
        if (newline) self->win_->append_output("\n");
    }
    return TCL_OK;
}

// ── app_info ────────────────────────────────────────────────────
int TclConsole::app_info_cmd(ClientData, Tcl_Interp* interp,
                             int, Tcl_Obj* const*)
{
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        "FLTK Console App with embedded Tcl & Python", -1));
    return TCL_OK;
}

// ── launch_plugin (tk=0, tkinter=1 via ClientData) ──────────────
int TclConsole::launch_plugin_cmd(ClientData cd, Tcl_Interp*,
                                  int, Tcl_Obj* const*)
{
    if (reinterpret_cast<intptr_t>(cd) == 0)
        launch_tk_graph_plugin();
    else
        launch_tkinter_graph_plugin();
    return TCL_OK;
}

// ── graph command ───────────────────────────────────────────────
int TclConsole::graph_cmd(ClientData, Tcl_Interp* interp,
                          int objc, Tcl_Obj* const objv[])
{
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "usage: graph set|get|params|preset|eval ...", -1));
        return TCL_ERROR;
    }

    auto* gw = get_graph_window();
    if (!gw) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("graph window not available", -1));
        return TCL_ERROR;
    }

    const char* sub = Tcl_GetString(objv[1]);

    if (std::strcmp(sub, "set") == 0) {
        if (objc != 4) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: graph set <param> <value>", -1));
            return TCL_ERROR;
        }
        double value;
        if (Tcl_GetDoubleFromObj(interp, objv[3], &value) != TCL_OK) return TCL_ERROR;
        if (!gw->params().set(Tcl_GetString(objv[2]), value)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown parameter", -1));
            return TCL_ERROR;
        }
        gw->show();
        gw->sync_and_redraw();
        return TCL_OK;
    }

    if (std::strcmp(sub, "get") == 0) {
        if (objc != 3) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: graph get <param>", -1));
            return TCL_ERROR;
        }
        double v = gw->params().get(Tcl_GetString(objv[2]));
        if (std::isnan(v)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown parameter", -1));
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewDoubleObj(v));
        return TCL_OK;
    }

    if (std::strcmp(sub, "params") == 0) {
        auto all = gw->params().all();
        Tcl_Obj* dict = Tcl_NewDictObj();
        for (auto& [k, v] : all)
            Tcl_DictObjPut(interp, dict,
                           Tcl_NewStringObj(k.c_str(), -1), Tcl_NewDoubleObj(v));
        Tcl_SetObjResult(interp, dict);
        return TCL_OK;
    }

    if (std::strcmp(sub, "preset") == 0) {
        if (objc != 3) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                "usage: graph preset <name>  (circle, figure8, lissajous, star, bowtie)", -1));
            return TCL_ERROR;
        }
        if (!gw->params().load_preset(Tcl_GetString(objv[2]))) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown preset", -1));
            return TCL_ERROR;
        }
        gw->show();
        gw->sync_and_redraw();
        return TCL_OK;
    }

    if (std::strcmp(sub, "eval") == 0) {
        if (objc != 3) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: graph eval <t>", -1));
            return TCL_ERROR;
        }
        double t;
        if (Tcl_GetDoubleFromObj(interp, objv[2], &t) != TCL_OK) return TCL_ERROR;
        auto [px, py] = gw->params().eval(t);
        Tcl_Obj* elems[2] = { Tcl_NewDoubleObj(px), Tcl_NewDoubleObj(py) };
        Tcl_SetObjResult(interp, Tcl_NewListObj(2, elems));
        return TCL_OK;
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        "unknown subcommand: use set|get|params|preset|eval", -1));
    return TCL_ERROR;
}
