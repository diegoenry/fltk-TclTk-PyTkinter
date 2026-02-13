#include "graph_params.h"

bool GraphParams::set(const std::string& name, double value) {
    if      (name == "a")      a = value;
    else if (name == "b")      b = value;
    else if (name == "A")      A = value;
    else if (name == "B")      B = value;
    else if (name == "delta")  delta = value;
    else if (name == "points") num_points = static_cast<int>(value);
    else return false;
    return true;
}

double GraphParams::get(const std::string& name) const {
    if (name == "a")      return a;
    if (name == "b")      return b;
    if (name == "A")      return A;
    if (name == "B")      return B;
    if (name == "delta")  return delta;
    if (name == "points") return num_points;
    return NAN;
}

bool GraphParams::load_preset(const std::string& name) {
    if (name == "circle") {
        a = 1; b = 1; A = 1; B = 1; delta = M_PI / 2; num_points = 1000;
    } else if (name == "figure8") {
        a = 1; b = 2; A = 1; B = 1; delta = 0; num_points = 1000;
    } else if (name == "lissajous") {
        a = 3; b = 2; A = 1; B = 1; delta = M_PI / 2; num_points = 1000;
    } else if (name == "star") {
        a = 5; b = 6; A = 1; B = 1; delta = M_PI / 2; num_points = 1000;
    } else if (name == "bowtie") {
        a = 2; b = 3; A = 1; B = 1; delta = M_PI / 4; num_points = 1000;
    } else {
        return false;
    }
    return true;
}

std::map<std::string, double> GraphParams::all() const {
    return {
        {"a", a}, {"b", b}, {"A", A}, {"B", B},
        {"delta", delta}, {"points", static_cast<double>(num_points)}
    };
}
