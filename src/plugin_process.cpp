#include "plugin_process.h"
#include "graph_window.h"

#include <FL/Fl.H>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

// ═════════════════════════════════════════════════════════════════
//  PluginProcess
// ═════════════════════════════════════════════════════════════════

PluginProcess::~PluginProcess() { stop(); }

bool PluginProcess::launch(const char* interpreter,
                           const std::string& script,
                           const std::string& args)
{
    if (pipe_) return true;  // already running

    // Determine temp file extension.
    bool is_python = (std::strstr(interpreter, "python") != nullptr);
    script_path_ = std::string("/tmp/fltk_plugin") + (is_python ? ".py" : ".tcl");

    FILE* f = std::fopen(script_path_.c_str(), "w");
    if (!f) return false;
    std::fputs(script.c_str(), f);
    std::fclose(f);

    std::string cmd = std::string(interpreter) + " "
                    + script_path_ + " " + args + " 2>&1";
    pipe_ = popen(cmd.c_str(), "r");
    if (!pipe_) { std::remove(script_path_.c_str()); return false; }

    fd_ = fileno(pipe_);
    int flags = fcntl(fd_, F_GETFL);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    Fl::add_fd(fd_, FL_READ, fd_callback, this);
    return true;
}

void PluginProcess::stop() {
    if (pipe_) {
        Fl::remove_fd(fd_);
        pclose(pipe_);
        pipe_ = nullptr;
        fd_   = -1;
        line_buf_.clear();
    }
    if (!script_path_.empty()) {
        std::remove(script_path_.c_str());
        script_path_.clear();
    }
}

void PluginProcess::fd_callback(int, void* data) {
    static_cast<PluginProcess*>(data)->on_data();
}

void PluginProcess::on_data() {
    char buf[1024];
    ssize_t n = read(fd_, buf, sizeof(buf) - 1);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (n <= 0) { stop(); return; }
    buf[n] = '\0';
    line_buf_ += buf;

    std::string::size_type pos;
    while ((pos = line_buf_.find('\n')) != std::string::npos) {
        process_line(line_buf_.substr(0, pos));
        line_buf_.erase(0, pos + 1);
    }
}

void PluginProcess::process_line(const std::string& line) {
    auto* gw = get_graph_window();
    if (!gw) return;

    char arg[64];
    double value;

    if (std::sscanf(line.c_str(), "SET %63s %lf", arg, &value) == 2) {
        gw->params().set(arg, value);
        gw->show();
        gw->sync_and_redraw();
    } else if (std::sscanf(line.c_str(), "PRESET %63s", arg) == 1) {
        gw->params().load_preset(arg);
        gw->show();
        gw->sync_and_redraw();
    }
}

// ═════════════════════════════════════════════════════════════════
//  Embedded scripts
// ═════════════════════════════════════════════════════════════════

static const char* tcl_plugin_script = R"tcl(
package require Tk

lassign $argv init_a init_b init_delta init_A init_B

wm title . "Tk Graph Plugin"

foreach {name label from to res init} [list \
    a      "Freq a"  1.0  10.0   1.0   $init_a \
    b      "Freq b"  1.0  10.0   1.0   $init_b \
    delta  "Phase"   0.0  6.2832 0.01  $init_delta \
    A      "Amp A"   0.1  2.0    0.05  $init_A \
    B      "Amp B"   0.1  2.0    0.05  $init_B \
] {
    set f [ttk::frame .f_$name]
    ttk::label $f.l -text $label -width 8
    scale $f.s -from $from -to $to -resolution $res \
        -orient horizontal -length 280 \
        -command [list on_slider $name]
    $f.s set $init
    pack $f.l $f.s -side left -padx 5
    pack $f -fill x -padx 10 -pady 3
}

set bf [ttk::frame .presets]
foreach preset {circle figure8 lissajous star bowtie} {
    ttk::button $bf.$preset -text $preset \
        -command [list on_preset $preset]
    pack $bf.$preset -side left -padx 3
}
pack $bf -pady 10

proc on_slider {name value} {
    puts "SET $name $value"
    flush stdout
}

array set preset_data {
    circle    {a 1 b 1 delta 1.5708 A 1 B 1}
    figure8   {a 1 b 2 delta 0      A 1 B 1}
    lissajous {a 3 b 2 delta 1.5708 A 1 B 1}
    star      {a 5 b 6 delta 1.5708 A 1 B 1}
    bowtie    {a 2 b 3 delta 0.7854 A 1 B 1}
}

proc on_preset {name} {
    global preset_data
    puts "PRESET $name"
    flush stdout
    foreach {param val} $preset_data($name) {
        .f_$param.s set $val
    }
}
)tcl";

static const char* python_plugin_script = R"py(
import sys, tkinter as tk
from tkinter import ttk

init_a, init_b, init_delta, init_A, init_B = (float(x) for x in sys.argv[1:6])

root = tk.Tk()
root.title("Tkinter Graph Plugin")

sliders = {}
for name, label, lo, hi, res, init in [
    ('a',     'Freq a', 1,   10,   1,    init_a),
    ('b',     'Freq b', 1,   10,   1,    init_b),
    ('delta', 'Phase',  0.0, 6.28, 0.01, init_delta),
    ('A',     'Amp A',  0.1, 2.0,  0.05, init_A),
    ('B',     'Amp B',  0.1, 2.0,  0.05, init_B),
]:
    f = ttk.Frame(root)
    ttk.Label(f, text=label, width=8).pack(side='left', padx=5)
    s = tk.Scale(f, from_=lo, to=hi, resolution=res,
                 orient='horizontal', length=280,
                 command=lambda v, n=name: on_slider(n, v))
    s.set(init)
    s.pack(side='left', padx=5)
    f.pack(fill='x', padx=10, pady=3)
    sliders[name] = s

presets = {
    'circle':    {'a': 1, 'b': 1, 'delta': 1.5708, 'A': 1, 'B': 1},
    'figure8':   {'a': 1, 'b': 2, 'delta': 0,      'A': 1, 'B': 1},
    'lissajous': {'a': 3, 'b': 2, 'delta': 1.5708, 'A': 1, 'B': 1},
    'star':      {'a': 5, 'b': 6, 'delta': 1.5708, 'A': 1, 'B': 1},
    'bowtie':    {'a': 2, 'b': 3, 'delta': 0.7854, 'A': 1, 'B': 1},
}

def on_slider(name, value):
    print(f"SET {name} {value}", flush=True)

def on_preset(name):
    print(f"PRESET {name}", flush=True)
    for param, val in presets[name].items():
        sliders[param].set(val)

bf = ttk.Frame(root)
for p in ['circle', 'figure8', 'lissajous', 'star', 'bowtie']:
    ttk.Button(bf, text=p, command=lambda x=p: on_preset(x)).pack(side='left', padx=3)
bf.pack(pady=10)

root.mainloop()
)py";

// ═════════════════════════════════════════════════════════════════
//  Global plugin launchers
// ═════════════════════════════════════════════════════════════════

static PluginProcess g_tk_plugin;
static PluginProcess g_tkinter_plugin;

static std::string current_param_args() {
    auto* gw = get_graph_window();
    if (!gw) return "3 2 1.5708 1 1";
    auto& p = gw->params();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f %.6f %.6f",
                  p.a, p.b, p.delta, p.A, p.B);
    return buf;
}

static const char* find_tclsh() {
    static const char* paths[] = {
        "/opt/homebrew/opt/tcl-tk/bin/tclsh9.0",
        "/opt/homebrew/opt/tcl-tk/bin/tclsh",
        "/opt/homebrew/bin/tclsh",
        "/usr/local/bin/tclsh",
        nullptr
    };
    for (auto** p = paths; *p; ++p)
        if (access(*p, X_OK) == 0) return *p;
    return "tclsh";
}

static const char* find_python3() {
    static const char* paths[] = {
        "/opt/homebrew/opt/python@3.14/bin/python3",
        "/opt/homebrew/opt/python@3.13/bin/python3",
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python3",
        nullptr
    };
    for (auto** p = paths; *p; ++p)
        if (access(*p, X_OK) == 0) return *p;
    return "python3";
}

void launch_tk_graph_plugin() {
    if (g_tk_plugin.running()) return;
    g_tk_plugin.launch(find_tclsh(), tcl_plugin_script, current_param_args());
}

void launch_tkinter_graph_plugin() {
    if (g_tkinter_plugin.running()) return;
    g_tkinter_plugin.launch(find_python3(), python_plugin_script, current_param_args());
}
