#pragma once

#include "graph_params.h"

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Value_Slider.H>

// Custom widget that draws the parametric curve.
class GraphCanvas : public Fl_Widget {
public:
    GraphCanvas(int x, int y, int w, int h);
    void draw() override;
    GraphParams params;
};

// Popup window: canvas + parameter sliders.
class GraphWindow : public Fl_Double_Window {
public:
    GraphWindow(int w, int h, const char* title);

    GraphParams&       params()       { return canvas_->params; }
    const GraphParams& params() const { return canvas_->params; }

    // Push current params into sliders and redraw the canvas.
    void sync_and_redraw();

private:
    static void slider_cb(Fl_Widget* w, void* data);
    void sliders_to_params();
    void params_to_sliders();

    GraphCanvas*      canvas_;
    Fl_Value_Slider*  sl_a_;
    Fl_Value_Slider*  sl_b_;
    Fl_Value_Slider*  sl_delta_;
    Fl_Value_Slider*  sl_A_;
    Fl_Value_Slider*  sl_B_;
    Fl_Value_Slider*  sl_pts_;
};

// Global singleton (set by main, used by console commands).
void         set_graph_window(GraphWindow* gw);
GraphWindow* get_graph_window();
