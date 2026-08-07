#include <cmath>
#include <cstdio>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTurnUnits = 256.0;

double project_x(double x, double yaw_units,
                 double center = 160.0, double focal = 120.0) {
    const double angle = yaw_units * (2.0 * kPi / kTurnUnits);
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    const double u = (x - center) / focal;
    const double denominator = cosine - u * sine;
    return center + focal * ((u * cosine + sine) / denominator);
}

bool close(double a, double b, double tolerance = 1.0e-9) {
    return std::abs(a - b) <= tolerance;
}

}  // namespace

int main() {
    int failures = 0;
    const auto expect = [&](bool condition, const char *message) {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    };

    expect(close(project_x(37.25, 0.0), 37.25),
           "zero residual is an identity transform");
    expect(project_x(160.0, -0.5) < 160.0,
           "negative pending yaw moves the world left");
    expect(project_x(160.0, 0.5) > 160.0,
           "positive pending yaw moves the world right");

    /* Immediately before/after the retail byte advances, the guest projection
     * changes by one whole yaw unit and the presentation remainder changes by
     * the opposite unit. Their combined camera angle must be continuous. */
    const double focal = 120.0;
    const double center = 160.0;
    const double base_angle = 0.31;
    const double one_unit = 2.0 * kPi / kTurnUnits;
    const double before_base = center + focal * std::tan(base_angle);
    const double after_base = center + focal * std::tan(base_angle - one_unit);
    const double before = project_x(before_base, -0.99, center, focal);
    const double after = project_x(after_base, 0.01, center, focal);
    expect(close(before, after, 1.0e-8),
           "fractional presentation stays continuous across a yaw-byte step");

    if (failures != 0) return 1;
    std::puts("Presentation yaw math tests passed");
    return 0;
}
