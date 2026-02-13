#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Input.H>

#include <functional>
#include <string>
#include <vector>

// Reusable popup console: scrolling output display + single-line command input.
class ConsoleWindow : public Fl_Double_Window {
public:
    // callback signature: void(const char* command)
    using CommandCallback = std::function<void(const char*)>;

    ConsoleWindow(int w, int h, const char* title);
    ~ConsoleWindow() override;

    void set_command_callback(CommandCallback cb) { cmd_cb_ = std::move(cb); }

    // Append text to the output area and scroll to bottom.
    void append_output(const char* text);

    // Set the prompt prefix shown in the input field label.
    void set_prompt(const char* prompt);

    int handle(int event) override;

private:
    static void on_input_enter(Fl_Widget* w, void* data);
    void history_up();
    void history_down();

    Fl_Text_Display* display_;
    Fl_Text_Buffer*  buffer_;
    Fl_Input*        input_;

    CommandCallback             cmd_cb_;
    std::vector<std::string>    history_;
    int                         hist_pos_ = -1;
};
