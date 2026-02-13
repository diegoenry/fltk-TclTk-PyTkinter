#pragma once

#include <cstdio>
#include <string>

// Manages a Tk/tkinter plugin running in a child process.
// The child prints "SET param value" and "PRESET name" lines to stdout.
// This class reads them via an FLTK fd-callback and updates the graph.
class PluginProcess {
public:
    ~PluginProcess();

    bool launch(const char* interpreter, const std::string& script,
                const std::string& args);
    bool running() const { return pipe_ != nullptr; }
    void stop();

private:
    static void fd_callback(int fd, void* data);
    void on_data();
    void process_line(const std::string& line);

    FILE*       pipe_ = nullptr;
    int         fd_   = -1;
    std::string line_buf_;
    std::string script_path_;
};

// Launch a Tcl/Tk or Python/tkinter graph-slider plugin subprocess.
// Safe to call repeatedly — does nothing if already running.
void launch_tk_graph_plugin();
void launch_tkinter_graph_plugin();
