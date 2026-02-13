#include "console_window.h"

#include <FL/Fl.H>
#include <FL/Enumerations.H>

#include <cstring>

static constexpr int kInputH = 30;
static constexpr int kPad    = 4;

ConsoleWindow::ConsoleWindow(int w, int h, const char* title)
    : Fl_Double_Window(w, h, title)
{
    begin();

    buffer_  = new Fl_Text_Buffer();
    display_ = new Fl_Text_Display(kPad, kPad, w - 2 * kPad, h - kInputH - 3 * kPad);
    display_->buffer(buffer_);
    display_->textfont(FL_COURIER);
    display_->textsize(14);
    display_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    input_ = new Fl_Input(kPad, h - kInputH - kPad, w - 2 * kPad, kInputH);
    input_->textfont(FL_COURIER);
    input_->textsize(14);
    input_->when(FL_WHEN_ENTER_KEY_ALWAYS);
    input_->callback(on_input_enter, this);

    end();
    resizable(display_);
    size_range(300, 200);
}

ConsoleWindow::~ConsoleWindow() {
    // Fl_Text_Display does not own the buffer.
    delete buffer_;
}

void ConsoleWindow::append_output(const char* text) {
    buffer_->append(text);
    // Scroll to end.
    int total = buffer_->length();
    display_->insert_position(total);
    display_->show_insert_position();
}

void ConsoleWindow::set_prompt(const char* prompt) {
    input_->label(prompt);
    // Adjust input x to accommodate label width.
    int lw = 0, lh = 0;
    fl_measure(prompt, lw, lh);
    lw += 8; // some padding
    input_->resize(kPad + lw, input_->y(), w() - 2 * kPad - lw, input_->h());
    redraw();
}

int ConsoleWindow::handle(int event) {
    if (event == FL_KEYDOWN) {
        int key = Fl::event_key();
        // If the input has focus, handle up/down for history.
        if (Fl::focus() == input_) {
            if (key == FL_Up)   { history_up();   return 1; }
            if (key == FL_Down) { history_down(); return 1; }
        }
    }
    return Fl_Double_Window::handle(event);
}

void ConsoleWindow::on_input_enter(Fl_Widget* /*w*/, void* data) {
    auto* self = static_cast<ConsoleWindow*>(data);
    const char* text = self->input_->value();
    if (!text) text = "";

    // Save to history.
    std::string cmd(text);
    if (!cmd.empty()) {
        self->history_.push_back(cmd);
    }
    self->hist_pos_ = -1;

    self->input_->value("");

    if (self->cmd_cb_) {
        self->cmd_cb_(cmd.c_str());
    }
}

void ConsoleWindow::history_up() {
    if (history_.empty()) return;
    if (hist_pos_ < 0)
        hist_pos_ = static_cast<int>(history_.size()) - 1;
    else if (hist_pos_ > 0)
        --hist_pos_;
    input_->value(history_[hist_pos_].c_str());
    input_->insert_position(input_->size()); // cursor to end
}

void ConsoleWindow::history_down() {
    if (history_.empty() || hist_pos_ < 0) return;
    ++hist_pos_;
    if (hist_pos_ >= static_cast<int>(history_.size())) {
        hist_pos_ = -1;
        input_->value("");
    } else {
        input_->value(history_[hist_pos_].c_str());
        input_->insert_position(input_->size());
    }
}
