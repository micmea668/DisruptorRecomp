#include <cmath>
#include <cstdio>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTurnUnits = 256.0;

struct ProjectedPoint {
    double x;
    double y;
};

ProjectedPoint project_xy(double x, double y, double yaw_units,
                          bool reproject_y,
                          double center_x = 160.0,
                          double center_y = 120.0,
                          double focal_x = 120.0) {
    const double angle = yaw_units * (2.0 * kPi / kTurnUnits);
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    const double u = (x - center_x) / focal_x;
    const double denominator = cosine - u * sine;
    return {
        center_x + focal_x * ((u * cosine + sine) / denominator),
        reproject_y ? center_y + (y - center_y) / denominator : y,
    };
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

    const ProjectedPoint identity = project_xy(37.25, 81.5, 0.0, true);
    expect(close(identity.x, 37.25) && close(identity.y, 81.5),
           "zero residual is an X/Y identity transform");
    expect(project_xy(160.0, 120.0, -0.5, true).x < 160.0,
           "negative pending yaw moves the world left");
    expect(project_xy(160.0, 120.0, 0.5, true).x > 160.0,
           "positive pending yaw moves the world right");

    const ProjectedPoint horizon = project_xy(300.0, 120.0, 0.75, true);
    expect(close(horizon.y, 120.0),
           "yaw reprojection preserves the projection horizon");

    const ProjectedPoint shifted_horizon = project_xy(
        300.0, 156.0, 0.75, true, 160.0, 156.0, 120.0);
    expect(close(shifted_horizon.y, 156.0),
           "yaw reprojection preserves a shifted projection horizon");
    const ProjectedPoint above_shifted_horizon = project_xy(
        300.0, 120.0, 0.75, true, 160.0, 156.0, 120.0);
    expect(!close(above_shifted_horizon.y, 120.0),
           "shifted horizon is consumed by the full-X/Y yaw transform");

    const ProjectedPoint edge = project_xy(300.0, 16.0, 0.75, true);
    expect(!close(edge.y, 16.0),
           "full yaw reprojection adjusts off-horizon Y at the screen edge");
    const ProjectedPoint legacy_x_only = project_xy(300.0, 16.0, 0.75, false);
    expect(close(legacy_x_only.y, 16.0),
           "the independently selectable X-only control preserves Y");

    const ProjectedPoint rotated = project_xy(287.5, 53.25, 0.8, true);
    const ProjectedPoint restored = project_xy(rotated.x, rotated.y, -0.8, true);
    expect(close(restored.x, 287.5, 1.0e-8) &&
               close(restored.y, 53.25, 1.0e-8),
           "opposite full-X/Y yaw transforms are inverses");

    const ProjectedPoint first_band = project_xy(
        287.5, 53.25, 0.8, true, 160.0, 120.0, 120.0);
    const ProjectedPoint second_band = project_xy(
        287.5, 53.25 + 256.0, 0.8, true,
        160.0, 120.0 + 256.0, 120.0);
    expect(close(first_band.x, second_band.x, 1.0e-8) &&
               close(first_band.y + 256.0, second_band.y, 1.0e-8),
           "framebuffer bands use the same display-relative yaw horizon");

    /* Immediately before/after the retail byte advances, the guest projection
     * changes by one whole yaw unit and the presentation remainder changes by
     * the opposite unit. Their combined camera angle must be continuous. */
    const double focal = 120.0;
    const double center = 160.0;
    const double center_y = 120.0;
    const double base_angle = 0.31;
    const double before_base = center + focal * std::tan(base_angle);
    const double before_base_y = 47.0;
    const ProjectedPoint after_guest = project_xy(
        before_base, before_base_y, -1.0, true, center, center_y, focal);
    const ProjectedPoint before = project_xy(
        before_base, before_base_y, -0.99, true, center, center_y, focal);
    const ProjectedPoint after = project_xy(
        after_guest.x, after_guest.y, 0.01, true, center, center_y, focal);
    expect(close(before.x, after.x, 1.0e-8) &&
               close(before.y, after.y, 1.0e-8),
           "fractional X/Y presentation stays continuous across a yaw-byte step");

    if (failures != 0) return 1;
    std::puts("Presentation yaw math tests passed");
    return 0;
}
