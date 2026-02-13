#include "graph_window.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cmath>
#include <cstdio>

// ── Global singleton ────────────────────────────────────────────
static GraphWindow* g_graph = nullptr;

void         set_graph_window(GraphWindow* gw) { g_graph = gw; }
GraphWindow* get_graph_window()                { return g_graph; }

// ═════════════════════════════════════════════════════════════════
//  GraphCanvas
// ═════════════════════════════════════════════════════════════════
GraphCanvas::GraphCanvas(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h) {}

void GraphCanvas::draw() {
    // Background.
    fl_color(fl_rgb_color(12, 12, 22));
    fl_rectf(x(), y(), w(), h());

    int cx   = x() + w() / 2;
    int cy   = y() + h() / 2;
    double scale = std::max(params.A, params.B) * 1.15;
    if (scale < 0.01) scale = 1.0;
    int half = std::min(w(), h()) / 2 - 10;

    // Grid.
    fl_color(fl_rgb_color(30, 30, 45));
    for (int i = -4; i <= 4; ++i) {
        int gx = cx + i * half / 4;
        int gy = cy + i * half / 4;
        fl_line(gx, y(), gx, y() + h());
        fl_line(x(), gy, x() + w(), gy);
    }

    // Axes.
    fl_color(fl_rgb_color(70, 70, 90));
    fl_line(x(), cy, x() + w(), cy);
    fl_line(cx, y(), cx, y() + h());

    // Curve.
    fl_color(fl_rgb_color(0, 220, 120));
    fl_line_style(FL_SOLID, 2);
    fl_begin_line();
    for (int i = 0; i <= params.num_points; ++i) {
        double t = 2.0 * M_PI * i / params.num_points;
        auto [px, py] = params.eval(t);
        double wx = cx + (px / scale) * half;
        double wy = cy - (py / scale) * half;
        fl_vertex(wx, wy);
    }
    fl_end_line();
    fl_line_style(0);

    // Equation overlay.
    fl_color(fl_rgb_color(170, 170, 190));
    fl_font(FL_COURIER, 12);
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "x(t) = %.2f sin(%.2f t + %.2f)", params.A, params.a, params.delta);
    fl_draw(buf, x() + 8, y() + 16);
    std::snprintf(buf, sizeof(buf),
                  "y(t) = %.2f sin(%.2f t)", params.B, params.b);
    fl_draw(buf, x() + 8, y() + 32);
}

// ═════════════════════════════════════════════════════════════════
//  GraphWindow
// ═════════════════════════════════════════════════════════════════
static constexpr int kSliderH   = 25;
static constexpr int kSliderGap = 5;
static constexpr int kLabelW    = 60;
static constexpr int kPad       = 10;
static constexpr int kNumSliders = 6;
static constexpr int kSliderArea = kNumSliders * (kSliderH + kSliderGap);

GraphWindow::GraphWindow(int w, int h, const char* title)
    : Fl_Double_Window(w, h, title)
{
    begin();

    int canvas_h = h - kSliderArea - 2 * kPad;
    canvas_ = new GraphCanvas(kPad, kPad, w - 2 * kPad, canvas_h);

    int sy = kPad + canvas_h + kSliderGap;
    int sw = w - 2 * kPad - kLabelW;

    auto make_slider = [&](const char* label, double lo, double hi,
                           double val, double step) -> Fl_Value_Slider* {
        auto* sl = new Fl_Value_Slider(kPad + kLabelW, sy, sw, kSliderH, label);
        sl->type(FL_HORIZONTAL);
        sl->bounds(lo, hi);
        sl->step(step);
        sl->value(val);
        sl->align(FL_ALIGN_LEFT);
        sl->callback(slider_cb, this);
        sy += kSliderH + kSliderGap;
        return sl;
    };

    sl_a_     = make_slider("a",      1.0,  10.0,     3.0,          1.0);
    sl_b_     = make_slider("b",      1.0,  10.0,     2.0,          1.0);
    sl_delta_ = make_slider("delta",  0.0,  2*M_PI,   M_PI/2.0,     0.01);
    sl_A_     = make_slider("A",      0.1,  2.0,      1.0,          0.05);
    sl_B_     = make_slider("B",      0.1,  2.0,      1.0,          0.05);
    sl_pts_   = make_slider("points", 100,  5000,     1000,         100);

    end();
    resizable(canvas_);
    size_range(400, 400);
}

void GraphWindow::slider_cb(Fl_Widget*, void* data) {
    auto* self = static_cast<GraphWindow*>(data);
    self->sliders_to_params();
    self->canvas_->redraw();
}

void GraphWindow::sliders_to_params() {
    auto& p = canvas_->params;
    p.a          = sl_a_->value();
    p.b          = sl_b_->value();
    p.delta      = sl_delta_->value();
    p.A          = sl_A_->value();
    p.B          = sl_B_->value();
    p.num_points = static_cast<int>(sl_pts_->value());
}

void GraphWindow::params_to_sliders() {
    const auto& p = canvas_->params;
    sl_a_->value(p.a);
    sl_b_->value(p.b);
    sl_delta_->value(p.delta);
    sl_A_->value(p.A);
    sl_B_->value(p.B);
    sl_pts_->value(p.num_points);
}

void GraphWindow::sync_and_redraw() {
    params_to_sliders();
    canvas_->redraw();
}
