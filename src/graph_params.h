#pragma once

#include <cmath>
#include <map>
#include <string>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Lissajous parametric curve parameters.
// x(t) = A * sin(a*t + delta)
// y(t) = B * sin(b*t)
struct GraphParams {
    double a     = 3.0;          // x frequency
    double b     = 2.0;          // y frequency
    double A     = 1.0;          // x amplitude
    double B     = 1.0;          // y amplitude
    double delta = M_PI / 2.0;   // phase shift
    int num_points = 1000;

    std::pair<double, double> eval(double t) const {
        return {A * std::sin(a * t + delta), B * std::sin(b * t)};
    }

    // Set a parameter by name.  Returns false if name is unknown.
    bool set(const std::string& name, double value);

    // Get a parameter by name.  Returns NAN if unknown.
    double get(const std::string& name) const;

    // Load a named preset.  Returns false if unknown.
    bool load_preset(const std::string& name);

    // All parameters as a name→value map.
    std::map<std::string, double> all() const;
};
