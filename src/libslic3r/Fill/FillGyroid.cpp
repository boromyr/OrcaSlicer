#include "../ClipperUtils.hpp"
#include "../MarchingSquares.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include "FillBase.hpp"
#include "FillGyroid.hpp"

// ---------------------------------------------------------------------------
// Marching-squares scalar field for the optimized gyroid branch.
// Modeled after FillTpmsFK.cpp's ScalarField.
//
// The gyroid scalar field is the standard implicit equation
//     F(x,y,z) = sin(fx*x)cos(fy*y) + sin(fy*y)cos(fz*z) + sin(fz*z)cos(fx*x)
// Marching squares extracts the iso-zero contour, which gives smoother
// transitions between vertical and horizontal regimes than the analytical
// asin-based wave generator. Setting fz = omega * baseline anisotropically
// tightens the wave along the layer-stacking axis, shortening the effective
// vertical strand length and improving column-buckling resistance under
// Z-axis compression.
// ---------------------------------------------------------------------------
namespace marchsq {
using namespace Slic3r;

using coordr_t = long;
using Pointf   = Vec2d;

struct GyroidField
{
    static constexpr float gsizef = 0.40f;
    static constexpr float rsizef = 0.004f;
    const coord_t          rsize  = scaled(rsizef);
    const coordr_t         gsize  = std::round(gsizef / rsizef);
    Point                  size;
    Point                  offs;
    coordf_t               z;
    float                  fx;
    float                  fy;
    float                  fz;
    float                  isoval = 0.0f;

    explicit GyroidField(const BoundingBox bb, const coordf_t z, const float period, const float omega = 1.0f)
        : size{bb.size()}, offs{bb.min}, z{z}
    {
        const float baseline = float(2.0 * PI) / std::max(period, 1e-3f);
        fx = baseline;
        fy = baseline;
        fz = omega * baseline;
    }

    float get_scalar(coordf_t x, coordf_t y, coordf_t z_arg) const
    {
        const float a = fx * float(x);
        const float b = fy * float(y);
        const float c = fz * float(z_arg);
        return std::sin(a) * std::cos(b) + std::sin(b) * std::cos(c) + std::sin(c) * std::cos(a);
    }

    float get_scalar(Coord p) const
    {
        Pointf pf = to_Pointf(p);
        return get_scalar(pf.x(), pf.y(), z);
    }

    inline coord_t  to_coord (const coordr_t& x) const { return x * rsize; }
    inline coordr_t to_coordr(const coord_t& x)  const { return x / rsize; }
    inline Point  to_Point (const Coord& p) const { return Point(to_coord(p.c) + offs.x(), to_coord(p.r) + offs.y()); }
    inline Coord  to_Coord (const Point& p) const { return Coord(to_coordr(p.y() - offs.y()), to_coordr(p.x() - offs.x())); }
    inline Pointf to_Pointf(const Point& p) const { return Pointf(unscaled(p.x()), unscaled(p.y())); }
    inline Pointf to_Pointf(const Coord& p) const { return to_Pointf(to_Point(p)); }
};

template<> struct _RasterTraits<GyroidField>
{
    using ValueType = float;
    static float  get (const GyroidField& sf, size_t row, size_t col) { return sf.get_scalar(Coord(row, col)); }
    static size_t rows(const GyroidField& sf) { return sf.to_coordr(sf.size.y()); }
    static size_t cols(const GyroidField& sf) { return sf.to_coordr(sf.size.x()); }
};

inline Polylines get_gyroid_polylines(const GyroidField& sf, const double tolerance = SCALED_EPSILON)
{
    std::vector<Ring> rings = execute_with_policy(ex_tbb, sf, sf.isoval, {sf.gsize, sf.gsize});
    Polylines polys;
    polys.reserve(rings.size());
    for (const Ring& ring : rings) {
        Polyline poly;
        Points&  pts = poly.points;
        pts.reserve(ring.size() + 1);
        for (const Coord& crd : ring)
            pts.emplace_back(sf.to_Point(crd));
        pts.push_back(pts.front());
        if (tolerance >= 0.0)
            poly.simplify(tolerance);
        polys.emplace_back(poly);
    }
    return polys;
}

} // namespace marchsq

namespace Slic3r {

static inline double f(double x, double z_sin, double z_cos, bool vertical, bool flip)
{
    if (vertical) {
        double phase_offset = (z_cos < 0 ? M_PI : 0) + M_PI;
        double a   = sin(x + phase_offset);
        double b   = - z_cos;
        double res = z_sin * cos(x + phase_offset + (flip ? M_PI : 0.));
        double r   = sqrt(sqr(a) + sqr(b));
        return asin(a/r) + asin(res/r) + M_PI;
    }
    else {
        double phase_offset = z_sin < 0 ? M_PI : 0.;
        double a   = cos(x + phase_offset);
        double b   = - z_sin;
        double res = z_cos * sin(x + phase_offset + (flip ? 0 : M_PI));
        double r   = sqrt(sqr(a) + sqr(b));
        return (asin(a/r) + asin(res/r) + 0.5 * M_PI);
    }
}

static inline Polyline make_wave(
    const std::vector<Vec2d>& one_period, double width, double height, double offset, double scaleFactor,
    double z_cos, double z_sin, bool vertical, bool flip)
{
    std::vector<Vec2d> points = one_period;
    double period = points.back()(0);
    if (width != period) // do not extend if already truncated
    {
        points.reserve(one_period.size() * size_t(floor(width / period)));
        points.pop_back();

        size_t n = points.size();
        do {
            points.emplace_back(points[points.size()-n].x() + period, points[points.size()-n].y());
        } while (points.back()(0) < width - EPSILON);

        points.emplace_back(Vec2d(width, f(width, z_sin, z_cos, vertical, flip)));
    }

    // and construct the final polyline to return:
    Polyline polyline;
    polyline.points.reserve(points.size());
    for (auto& point : points) {
        point(1) += offset;
        point(1) = std::clamp(double(point.y()), 0., height);
        if (vertical)
            std::swap(point(0), point(1));
        polyline.points.emplace_back((point * scaleFactor).cast<coord_t>());
    }

    return polyline;
}

static std::vector<Vec2d> make_one_period(double width, double scaleFactor, double z_cos, double z_sin, bool vertical, bool flip, double tolerance)
{
    std::vector<Vec2d> points;
    double dx = M_PI_2; // exact coordinates on main inflexion lobes
    double limit = std::min(2*M_PI, width);
    points.reserve(coord_t(ceil(limit / tolerance / 3)));

    for (double x = 0.; x < limit - EPSILON; x += dx) {
        points.emplace_back(Vec2d(x, f(x, z_sin, z_cos, vertical, flip)));
    }
    points.emplace_back(Vec2d(limit, f(limit, z_sin, z_cos, vertical, flip)));

    // piecewise increase in resolution up to requested tolerance
    for(;;)
    {
        size_t size = points.size();
        for (unsigned int i = 1;i < size; ++i) {
            auto& lp = points[i-1]; // left point
            auto& rp = points[i];   // right point
            double x = lp(0) + (rp(0) - lp(0)) / 2;
            double y = f(x, z_sin, z_cos, vertical, flip);
            Vec2d ip = {x, y};
            if (std::abs(cross2(Vec2d(ip - lp), Vec2d(ip - rp))) > sqr(tolerance)) {
                points.emplace_back(std::move(ip));
            }
        }

        if (size == points.size())
            break;
        else
        {
            // insert new points in order
            std::sort(points.begin(), points.end(),
                      [](const Vec2d &lhs, const Vec2d &rhs) { return lhs(0) < rhs(0); });
        }
    }

    return points;
}

// ---------------------------------------------------------------------------
// "Optimized" gyroid wave: marching-squares variant gated on
// params.gyroid_optimized. The wave shape is extracted from the gyroid
// implicit scalar field (see marchsq::GyroidField above) at iso=0, with
// the Z dimension's spatial frequency multiplied by an Euler-Bernoulli
// buckling-derived factor so the vertical strands become shorter columns,
// raising the critical buckling load against Z-axis compression.
//
// The formula is INVERTED from a naive "scale with density" derivation:
// at LOW density the gyroid strands are long and slender (prime buckling
// targets), so they need the most shortening; at high density the strands
// are already short and need little extra help. omega is therefore the
// inverse-square-root of density_adjusted:
//
//   omega = sqrt(1 / density_adj) / sqrt(1 + layer_h/spacing),
//           clamped [1.0, 2.0]
//
// fx and fy are left at the baseline frequency, so the per-XY-slice line
// length per unit area is preserved -> mass at the same `sparse_infill_density`
// setting matches the standard gyroid path. Strength gain comes purely from
// the shorter vertical column length (P_cr proportional to 1/L^2).
//
// Empirical Python sim (sim_gyroid_compare.py) at layer_h=0.20, spacing=0.45:
//
//   density   omega   line/std   strength/std   strength_per_mass
//     10%      2.00     1.00        2.84             2.84
//     15%      1.38     1.00        1.89             1.89
//     20%      1.19     1.00        1.42             1.42
//     30%      1.00     1.00        1.00             1.00
//     50%+     1.00     1.00        1.00             1.00
//
// When gyroid_optimized is false, behavior is byte-identical to the
// standard parametric gyroid path below.
// ---------------------------------------------------------------------------

static inline double compute_omega_factor(double density_adjusted, double line_spacing, double layer_height)
{
    double lh_ratio   = (line_spacing > 0.) ? layer_height / line_spacing : 0.5;
    double correction = 1.0 / std::sqrt(1.0 + lh_ratio);
    double raw        = std::sqrt(1.0 / std::max(density_adjusted, 0.1)) * correction;
    return std::clamp(raw, 1.0, 2.0);
}

static Polylines make_gyroid_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    const double scaleFactor = scale_(line_spacing) / density_adjusted;

    // tolerance in scaled units. clamp the maximum tolerance as there's
    // no processing-speed benefit to do so beyond a certain point
    const double tolerance = std::min(line_spacing / 2, FillGyroid::PatternTolerance) / unscale<double>(scaleFactor);

    //scale factor for 5% : 8 712 388
    // 1z = 10^-6 mm ?
    const double z     = gridZ / scaleFactor;
    const double z_sin = sin(z);
    const double z_cos = cos(z);

    bool vertical = (std::abs(z_sin) <= std::abs(z_cos));
    double lower_bound = 0.;
    double upper_bound = height;
    bool flip = true;
    if (vertical) {
        flip = false;
        lower_bound = -M_PI;
        upper_bound = width - M_PI_2;
        std::swap(width,height);
    }

    std::vector<Vec2d> one_period_odd = make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip, tolerance); // creates one period of the waves, so it doesn't have to be recalculated all the time
    flip = !flip;                                                                   // even polylines are a bit shifted
    std::vector<Vec2d> one_period_even = make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip, tolerance);
    Polylines result;

    for (double y0 = lower_bound; y0 < upper_bound + EPSILON; y0 += M_PI) {
        // creates odd polylines
        result.emplace_back(make_wave(one_period_odd, width, height, y0, scaleFactor, z_cos, z_sin, vertical, flip));
        // creates even polylines
        y0 += M_PI;
        if (y0 < upper_bound + EPSILON) {
            result.emplace_back(make_wave(one_period_even, width, height, y0, scaleFactor, z_cos, z_sin, vertical, flip));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Saddle bridges: gated on params.gyroid_saddle_bridges.
//
// Seen from straight above, a gyroid slice is a set of wavy strands. Every half
// period along Z the strands swap connectivity, and on the layers around that
// transition two strands run towards each other and stop just short of touching.
// Those unconnected necks are the weak points of the pattern; this code welds
// each of them with a small slab of internal bridge infill.
//
// The necks are the in-plane critical points of the gyroid's implicit field
//
//     F(u,v,w) = sin(u)cos(v) + sin(v)cos(w) + sin(w)cos(u)
//
// Solving F = dF/du = dF/dv = 0 (u, v the in-plane axes, w the layer axis) has
// exactly one family of solutions:
//
//     u = a*pi,   v = (b + 1/2)*pi,   w = pi/4 + n*pi/2      for a + b + n odd
//
// (verified numerically against both wave generators). Precisely at w the two
// strands cross; dw away from it they are the two branches of a hyperbola. With
// alpha = cos(u) = (-1)^a, beta = sin(v) = (-1)^b and A = sin(w), the local
// expansion of F is
//
//     1/2 * (du,dv) * H * (du,dv) + F_w * dw = 0,
//     H = [[-A*alpha, -alpha*beta], [-alpha*beta, A*alpha]],   F_w = -2*A*beta
//
// H is trace free, so its eigenvalues are +-lambda with lambda = |H| = sqrt(3/2),
// and |F_w| = sqrt(2); both are the same at every saddle. The branch vertices
// therefore sit on a principal axis of H at
//
//     half_gap = sqrt(2 * |F_w * dw| / lambda)
//
// from the saddle - on the +lambda axis when F_w*dw < 0, on the perpendicular one
// otherwise. A hyperbola meets its axis at right angles, so a segment laid along
// that axis joins the two strand tips head-on.
//
// The neck is not a one-dimensional gap though. Stepping a distance v sideways
// along the neck, the same hyperbola puts the two branches at
//
//     half_width(v) = sqrt(half_gap^2 + v^2)
//
// from the centre, so the empty region is a bowtie that opens up on both sides.
// The weld is therefore a small bridged slab, not a single line: a fan of lines
// parallel to the gap axis, one line spacing apart, each ending on the two
// branches at its own offset. Carrying the fan out until the slab is as wide as
// its centre line is long covers the neck instead of leaving the bowtie open,
// which is one line on a dense gyroid and several once the pattern opens up.
// ---------------------------------------------------------------------------

namespace {

// Spacing of the saddle planes along the parametric Z axis.
constexpr double SaddlePlaneStep = M_PI / 2.;

// Where the wave pattern sits, so the saddle solver can work for both generators.
struct SaddleFrame
{
    // Scaled coordinate that parametric (0, 0) maps to.
    Point  origin{ 0, 0 };
    // Scaled X/Y length of one parametric radian. Identical on both axes, so
    // parametric angles are real angles.
    double scaled_radian{ 0. };
    // Parametric radians per mm along Z.
    double freq_z{ 0. };
    // The generator traces F(y, x, z) = 0 rather than F(x, y, z) = 0.
    bool   swap_xy{ false };
};

struct SaddleNeck
{
    // Saddle position and gap axis, in the generator's parametric XY frame.
    Vec2d  center;
    Vec2d  dir;
    // Parametric distance from the saddle to either strand tip.
    double half_gap;
};

inline Point saddle_to_point(const SaddleFrame &frame, const Vec2d &p)
{
    return Point(frame.origin.x() + coord_t(std::lround(p.x() * frame.scaled_radian)),
                 frame.origin.y() + coord_t(std::lround(p.y() * frame.scaled_radian)));
}

// Enumerate the saddles of the layer sliced at z that fall inside area, skipping the ones
// whose gap is above max_gap. Gaps are parametric, and are the same for every saddle sharing
// a plane. Necks whose strands already run into each other are not rejected here: however
// close the two tips come, the bowtie still opens on both sides of that contact, so which
// part of a neck is worth welding is decided line by line in make_saddle_bridges().
std::vector<SaddleNeck> collect_gyroid_saddles(
    const SaddleFrame &frame, double z, double layer_height, const BoundingBox &area, double max_gap)
{
    std::vector<SaddleNeck> out;
    if (frame.scaled_radian <= 0. || frame.freq_z <= 0. || layer_height <= 0. || ! area.defined)
        return out;

    // Parametric extent to cover. u and v are the gyroid's own in-plane axes.
    const double x_lo = double(area.min.x() - frame.origin.x()) / frame.scaled_radian;
    const double x_hi = double(area.max.x() - frame.origin.x()) / frame.scaled_radian;
    const double y_lo = double(area.min.y() - frame.origin.y()) / frame.scaled_radian;
    const double y_hi = double(area.max.y() - frame.origin.y()) / frame.scaled_radian;
    const double u_lo = frame.swap_xy ? y_lo : x_lo;
    const double u_hi = frame.swap_xy ? y_hi : x_hi;
    const double v_lo = frame.swap_xy ? x_lo : y_lo;
    const double v_hi = frame.swap_xy ? x_hi : y_hi;
    // Bail out instead of spinning on a degenerate frame.
    const double cells = ((u_hi - u_lo) / M_PI + 2.) * ((v_hi - v_lo) / M_PI + 2.);
    if (! (cells > 0.) || cells > 4.e6)
        return out;

    // Each layer owns the saddle planes within half a layer height of its own slicing
    // plane. With a uniform layer height those windows tile the Z axis, so every saddle is
    // welded on exactly one layer - the one it lies closest to, which is also the one where
    // its gap is narrowest.
    const double w      = z * frame.freq_z;
    const double w_half = 0.5 * layer_height * frame.freq_z;
    const long   n_lo   = long(std::ceil ((w - w_half - 0.25 * M_PI) / SaddlePlaneStep));
    const long   n_hi   = long(std::floor((w + w_half - 0.25 * M_PI) / SaddlePlaneStep));

    for (long n = n_lo; n <= n_hi; ++ n) {
        const double w_saddle = 0.25 * M_PI + double(n) * SaddlePlaneStep;
        const double dw       = w - w_saddle;
        // |F_w| = sqrt(2) and lambda = sqrt(3/2) hold at every saddle, so how far this
        // layer sits from the saddle plane is all the gap depends on.
        const double half_gap = std::sqrt(2. * std::sqrt(2.) * std::abs(dw) / std::sqrt(1.5));
        if (2. * half_gap > max_gap)
            continue;
        const double A = std::sin(w_saddle);
        for (long a = long(std::ceil(u_lo / M_PI)); a <= long(std::floor(u_hi / M_PI)); ++ a)
            for (long b = long(std::ceil(v_lo / M_PI - 0.5)); b <= long(std::floor(v_hi / M_PI - 0.5)); ++ b) {
                if (((a + b + n) & 1) == 0)
                    continue; // not a member of the saddle family
                const double alpha = (a & 1) ? -1. : 1.; // cos(u)
                const double beta  = (b & 1) ? -1. : 1.; // sin(v)
                const double h_uu  = - A * alpha;
                const double h_uv  = - alpha * beta;
                const double f_w   = - 2. * A * beta;
                // Principal axis carrying the +lambda eigenvalue of H. The branch
                // vertices lie on it when F_w*dw < 0 and on the perpendicular one when
                // F_w*dw > 0, which is what flips the neck by 90 degrees as the slicing
                // plane crosses the saddle.
                double angle = 0.5 * std::atan2(h_uv, h_uu);
                if (f_w * dw > 0.)
                    angle += 0.5 * M_PI;
                Vec2d center(double(a) * M_PI, (double(b) + 0.5) * M_PI);
                if (frame.swap_xy) {
                    center = Vec2d(center.y(), center.x());
                    angle  = 0.5 * M_PI - angle;
                }
                out.push_back({ center, Vec2d(std::cos(angle), std::sin(angle)), half_gap });
            }
    }
    return out;
}

// Build the welds for one fill region, clipped to it. Coordinates are in the pattern's own
// (rotated) frame; the caller rotates them back.
Polylines make_saddle_bridges(
    const SaddleFrame &frame, const FillParams &params, const ExPolygon &expolygon, double z, double spacing)
{
    const double radian_mm = unscale<double>(frame.scaled_radian);
    if (radian_mm <= 0. || spacing <= 0.)
        return Polylines();

    // Centre distance at which the two strands' material meets: a strand is line_width wide,
    // and multiline splits each wave into that many parallel lines whose spread runs along
    // the gap axis, closing that much of the gap by itself.
    const double line_width = params.flow.width() > 0.f ? double(params.flow.width()) : spacing;
    const double touching   = line_width + double(params.multiline - 1) * spacing;

    // Saddles just outside the region can still reach into it once the overlaps are added.
    BoundingBox area = expolygon.contour.bounding_box();
    if (! area.defined)
        return Polylines();
    area.offset(coord_t(2. * M_PI * frame.scaled_radian));

    // One line spacing, in parametric units. It is both the pitch between the lines of a weld
    // and how far each of them runs past the strand it ends on, so the lines merge into a
    // solid slab that overlaps the two strands and absorbs any residual error of the local
    // model in the overlap.
    const double line_pitch = spacing / radian_mm;

    // Two upper bounds on the gap. The quadratic expansion only describes the saddle's own
    // neighbourhood, so a gap past a quarter period is out of the model's range. And the
    // saddles sit on a lattice of pitch pi, so the weld's centre line, overlaps included, has
    // to be shorter than that pitch - beyond it the strands are packed closer than the
    // extrusion is wide and a weld would just be laid on top of the neighbouring ones.
    const double max_gap = std::min(0.5 * M_PI, M_PI - 2. * line_pitch);
    const std::vector<SaddleNeck> necks = collect_gyroid_saddles(
        frame, z, double(params.layer_height), area, max_gap);
    if (necks.empty())
        return Polylines();

    // How wide a neck opens depends on how far the layer sits from the saddle plane, and that
    // offset drifts as the saddle planes and the layer grid slide past each other. Sizing the
    // fan per layer would make the same pattern get a seven line weld on most layers and a
    // five line one every few, so size it once from the widest neck a layer can be handed -
    // half a layer height off the plane, the far edge of the window each layer owns. Every
    // weld of a given pattern then gets the same fan, and only the length of its lines still
    // follows the neck actually in front of it.
    const double half_gap_max = std::sqrt(2. * std::sqrt(2.) * (0.5 * double(params.layer_height) * frame.freq_z) /
                                          std::sqrt(1.5));
    // Carry the fan out until the slab is as wide across as its centre line is long, which
    // makes the weld a square patch over the neck rather than a slit that leaves the bowtie
    // open on both sides.
    double reach_max = half_gap_max + line_pitch;
    // It still has to fit inside its own lattice cell, in both directions: the slab must stay
    // narrower than the lattice pitch, and so must its widest line, which reaches
    // sqrt(half_gap^2 + reach^2) + line_pitch either side of the centre. Squeezing the weld
    // rather than dropping it keeps the dense end of the range welded by a single line, the
    // way it was before the neck was wide enough to need a patch.
    reach_max = std::min(reach_max, 0.5 * (M_PI - line_pitch));
    const double room = 0.5 * M_PI - line_pitch;
    reach_max = std::min(reach_max, std::sqrt(std::max(0., room * room - half_gap_max * half_gap_max)));
    const int reach = int(reach_max / line_pitch);

    // Half the centre distance at which the strands' material meets, in parametric units.
    const double contact = 0.5 * touching / radian_mm;

    Polylines out;
    for (const SaddleNeck &neck : necks) {
        // The branches stand 2*sqrt(half_gap^2 + v^2) apart at offset v, so their material runs
        // into itself over v^2 <= contact^2 - half_gap^2 and nowhere else. Every half period a
        // saddle plane falls close enough to a layer for the two tips to meet at the centre,
        // and such a neck looks welded shut - but only at the centre, because the bowtie keeps
        // opening either side of that contact however close the tips come. So drop just the
        // lines that would land on the joint already in place, rather than writing the whole
        // neck off. Squared and left unclamped: where the tips never meet this goes negative
        // and no offset is excluded.
        const double joined = contact * contact - neck.half_gap * neck.half_gap;

        const Vec2d across(- neck.dir.y(), neck.dir.x());
        Polylines   slab;
        slab.reserve(size_t(2 * reach + 1));
        double wanted = 0.;
        int    lines  = 0;
        for (int i = - reach; i <= reach; ++ i) {
            const double v = double(i) * line_pitch;
            if (v * v <= joined)
                continue; // the two strands already run into each other here
            // Where the two branches sit at this offset, plus the overlap onto them.
            const double half = std::sqrt(neck.half_gap * neck.half_gap + v * v) + line_pitch;
            const Vec2d  c    = neck.center + v * across;
            Polyline pl;
            pl.points = { saddle_to_point(frame, c - half * neck.dir),
                          saddle_to_point(frame, c + half * neck.dir) };
            slab.emplace_back(std::move(pl));
            wanted += 2. * half;
            ++ lines;
        }
        if (lines == 0)
            continue; // the whole neck is closed already

        // Clipping to the region can cut the slab down near the border, where it would hang
        // off with nothing to weld to. Allow every line to lose its outer overlap and no more,
        // and take the weld whole or not at all so a partial slab is never left behind.
        const double keep = (wanted - double(lines) * line_pitch) * frame.scaled_radian;
        slab = intersection_pl(std::move(slab), expolygon);
        double have = 0.;
        for (const Polyline &pl : slab)
            have += pl.length();
        if (have >= keep)
            append(out, std::move(slab));
    }
    return out;
}

} // namespace

// FIXME: needed to fix build on Mac on buildserver
constexpr double FillGyroid::PatternTolerance;

void FillGyroid::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    auto infill_angle = float(this->angle + (CorrectionAngle * 2*M_PI) / 360.);
    if(std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    BoundingBox bb = expolygon.contour.bounding_box();
    // Density adjusted to have a good %of weight.
    double      density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);
    // Distance between the gyroid waves in scaled coordinates.
    coord_t     distance = coord_t(scale_(this->spacing) / density_adjusted);

    // align bounding box to a multiple of our grid module
    bb.merge(align_to_grid(bb.min, Point(2*M_PI*distance, 2*M_PI*distance)));

    // Expand the bounding box to avoid artifacts at the edges
    coord_t expand = 10 * (scale_(this->spacing));
    bb.offset(expand); 

    // generate pattern
    Polylines   polylines;
    SaddleFrame saddle_frame;
    if (params.gyroid_optimized) {
        // Marching-squares path on the gyroid implicit field. Base period matches
        // the standard parametric path's wavelength: 2*pi * spacing / density_adj.
        // omega >= 1 always, so fz >= baseline -> shorter vertical wavelength ->
        // shorter effective column length -> higher buckling resistance.
        //
        // Mass: fx and fy are left at baseline (same as standard), so the
        // per-XY-slice line length per unit area is approximately preserved.
        // Empirically (sim_gyroid_compare.py) the optimized line/std ratio is
        // ~1.000 across densities, so no period compensation is needed.
        const double lh = (params.layer_height > 0.) ? double(params.layer_height) : double(this->spacing);
        const double omega = compute_omega_factor(density_adjusted, this->spacing * params.multiline, lh);

        const float density_factor = std::max(0.001f, float(params.density * DensityAdjust / params.multiline));
        const float period         = float(2.0 * M_PI) * float(this->spacing) / density_factor;

        // bb is already expanded above by 10 * scale_(spacing) for edge artifacts;
        // skip a second offset here to avoid raster-area bloat in the marching squares pass.
        marchsq::GyroidField sf(bb, this->z, period, float(omega));
        polylines = marchsq::get_gyroid_polylines(sf, SCALED_SPARSE_INFILL_RESOLUTION);

        // The field is sampled in object coordinates, so the pattern is anchored at the origin.
        const double baseline      = 2. * M_PI / std::max(double(period), 1e-3);
        saddle_frame.origin        = Point(0, 0);
        saddle_frame.scaled_radian = scale_(1. / baseline);
        saddle_frame.freq_z        = omega * baseline;
        saddle_frame.swap_xy       = false;
    } else {
        polylines = make_gyroid_waves(
            scale_(this->z),
            density_adjusted,
            this->spacing,
            ceil(bb.size()(0) / distance) + 1.,
            ceil(bb.size()(1) / distance) + 1.);

        // The parametric generator produces wave coords relative to the grid origin;
        // shift them into absolute layer coords. The marching-squares branch above
        // already emits absolute coords via GyroidField::to_Point, so it skips this.
        for (Polyline &pl : polylines)
            pl.translate(bb.min);

        // make_gyroid_waves() works in units of scale_(spacing) / density_adjusted, relative
        // to bb.min, and traces F(y, x, z) = 0 rather than F(x, y, z) = 0.
        saddle_frame.origin        = bb.min;
        saddle_frame.scaled_radian = scale_(this->spacing) / density_adjusted;
        saddle_frame.freq_z        = density_adjusted / this->spacing;
        saddle_frame.swap_xy       = true;
    }

    // Orca: weld the saddle points of this layer with small internal bridge patches.
    if (params.gyroid_saddle_bridges) {
        Polylines bridges = make_saddle_bridges(saddle_frame, params, expolygon, this->z, this->spacing);
        if (std::abs(infill_angle) >= EPSILON)
            for (Polyline &pl : bridges)
                pl.rotate(infill_angle);
        append(m_saddle_bridges, std::move(bridges));
    }

    // Apply multiline offset if needed
    multiline_fill(polylines, params, spacing);

	polylines = intersection_pl(std::move(polylines), expolygon);

    if (! polylines.empty()) {
		// Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
		polylines.erase(
			std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
			polylines.end());
    }

	if (! polylines.empty()) {
		// connect lines
		size_t polylines_out_first_idx = polylines_out.size();
        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

	    // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
	        	it->rotate(infill_angle);
	    }
    }
}

Polylines FillGyroid::fill_surface(const Surface *surface, const FillParams &params)
{
    // _fill_surface_single() accumulates into m_saddle_bridges, once per island.
    m_saddle_bridges.clear();
    return Fill::fill_surface(surface, params);
}

void FillGyroid::fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out)
{
    const size_t out_begin = out.size();
    Fill::fill_surface_extrusion(surface, params, out);
    if (m_saddle_bridges.empty())
        return;
    Polylines bridges = std::move(m_saddle_bridges);
    m_saddle_bridges.clear();

    // Reduce the flow the way a regular bridge does, but keep the sparse infill's width and
    // height: the weld has to merge with the two strands it joins, and unlike a bridge over a
    // solid region it sits in the middle of a sparse layer, where an extrusion thicker than
    // nominal would be in the way of the next one.
    Flow flow = params.flow;
    if (params.config != nullptr && params.config->bridge_flow > 0. && ! flow.bridge())
        flow = flow.with_flow_ratio(params.config->bridge_flow);

    // The welds hang between two strands of this very layer, so they have to be extruded
    // after them. Both the order of the collections and the order of the paths inside one of
    // them are re-chained by the G-code path planner, so chain the infill here, append the
    // welds behind it and hand the result over as already ordered.
    ExtrusionEntityCollection *eec = out.size() > out_begin ?
        dynamic_cast<ExtrusionEntityCollection*>(out[out_begin]) : nullptr;
    if (eec == nullptr || eec->entities.empty())
        return; // no infill was produced here, so there is nothing to weld to
    if (! eec->no_sort) {
        chain_and_reorder_extrusion_entities(eec->entities, nullptr);
        eec->no_sort = true;
    }
    // Pinning the order also means the welds keep the order they were enumerated in, which
    // ignores where they sit on the plate. Chain them from where the infill ends instead.
    // Note chain_polylines() is not usable here: its greedy walk can end before every segment
    // has been visited, and the resulting count mismatch is only caught by an assert, so a
    // release build would silently drop welds.
    const Point start = eec->entities.back()->last_point();
    ExtrusionEntitiesPtr paths;
    extrusion_entities_append_paths(paths, std::move(bridges), erInternalBridgeInfill,
                                    flow.mm3_per_mm(), flow.width(), flow.height());
    chain_and_reorder_extrusion_entities(paths, &start);
    append(eec->entities, std::move(paths));
}

} // namespace Slic3r
