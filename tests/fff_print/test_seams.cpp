#include <catch2/catch_all.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_helpers.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;

TEST_CASE("Center/custom-point seam option is registered and round-trips", "[Seams]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

    // The custom point defaults to the object center.
    REQUIRE(config.opt_enum<SeamPosition>("seam_position") != spCustom);
    REQUIRE(config.opt_float("seam_position_x") == 0.0);
    REQUIRE(config.opt_float("seam_position_y") == 0.0);

    // Serializing back also checks that "custom" is registered in the enum name table.
    config.set_deserialize_strict({
        { "seam_position",   "custom" },
        { "seam_position_x", "40"     },
        { "seam_position_y", "-15"    },
    });
    REQUIRE(config.opt_enum<SeamPosition>("seam_position") == spCustom);
    REQUIRE(config.opt_serialize("seam_position") == "custom");
    REQUIRE(config.opt_float("seam_position_x") == 40.0);
    REQUIRE(config.opt_float("seam_position_y") == -15.0);

    REQUIRE(config.opt_enum<SeamSide>("seam_position_ref") == ssClosest);
    config.set_deserialize_strict({ { "seam_position_ref", "farthest" } });
    REQUIRE(config.opt_enum<SeamSide>("seam_position_ref") == ssFarthest);
    REQUIRE(config.opt_serialize("seam_position_ref") == "farthest");

    REQUIRE(config.opt_bool("seam_position_align") == true);

    // The Process tab can only bind options that are part of the print preset.
    const std::vector<std::string> &opts = Preset::print_options();
    REQUIRE(std::find(opts.begin(), opts.end(), "seam_position")       != opts.end());
    REQUIRE(std::find(opts.begin(), opts.end(), "seam_position_x")     != opts.end());
    REQUIRE(std::find(opts.begin(), opts.end(), "seam_position_y")     != opts.end());
    REQUIRE(std::find(opts.begin(), opts.end(), "seam_position_ref")   != opts.end());
    REQUIRE(std::find(opts.begin(), opts.end(), "seam_position_align") != opts.end());
}

// Slices a cylinder with the seam aimed at (target_x, target_y) and returns the mean XY of the
// outer wall seams across layers. A cylinder has a single nearest point (no corner ties) and no
// overhangs. The custom point is relative to the object center, so the tests compare seams
// between runs rather than against absolute bed coordinates.
static Vec2d outer_wall_seam_mean(const std::string &target_x, const std::string &target_y, size_t &n_seams,
                                  const std::string &reference = "closest")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "seam_position",      "custom"   },
        { "seam_position_x",    target_x   },
        { "seam_position_y",    target_y   },
        { "seam_position_ref",  reference  },
        { "wall_loops",         "2"        },
        { "layer_height",       "0.3"      },
        { "initial_layer_print_height", "0.3" },
        { "gcode_comments",     "1"        },
    });

    const std::string gcode = slice({ make_cylinder(10.0, 20.0) }, config);

    std::vector<Vec2d> seams;
    bool   seam_pending = false;
    double prev_x = 0.0, prev_y = 0.0;

    GCodeReader reader;
    reader.parse_buffer(gcode, [&seams, &seam_pending, &prev_x, &prev_y]
                        (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.raw().find("Outer wall") != std::string::npos) {
            seam_pending = true;
        } else if (seam_pending && line.extruding(self) && line.dist_XY(self) > 0) {
            seams.emplace_back(prev_x, prev_y);
            seam_pending = false;
        }
        prev_x = self.x();
        prev_y = self.y();
    });

    n_seams = seams.size();
    if (seams.empty())
        return Vec2d(0.0, 0.0);
    Vec2d sum(0.0, 0.0);
    for (const Vec2d &p : seams)
        sum += p;
    return sum / double(seams.size());
}

TEST_CASE("Center/custom-point seam follows the configured X/Y point", "[Seams]")
{
    size_t n_xp = 0, n_xn = 0, n_yp = 0, n_yn = 0;
    const Vec2d seam_x_plus  = outer_wall_seam_mean( "40",  "0", n_xp);
    const Vec2d seam_x_minus = outer_wall_seam_mean("-40",  "0", n_xn);
    const Vec2d seam_y_plus  = outer_wall_seam_mean(  "0", "40", n_yp);
    const Vec2d seam_y_minus = outer_wall_seam_mean(  "0","-40", n_yn);

    REQUIRE(n_xp > 5);
    REQUIRE(n_xn > 5);
    REQUIRE(n_yp > 5);
    REQUIRE(n_yn > 5);

    // Aiming at +X vs -X moves the seam to opposite sides of the 20 mm cylinder; Y stays put.
    REQUIRE(seam_x_plus.x() - seam_x_minus.x() > 8.0);
    REQUIRE_THAT(seam_x_plus.y() - seam_x_minus.y(), Catch::Matchers::WithinAbs(0.0, 4.0));

    REQUIRE(seam_y_plus.y() - seam_y_minus.y() > 8.0);
    REQUIRE_THAT(seam_y_plus.x() - seam_y_minus.x(), Catch::Matchers::WithinAbs(0.0, 4.0));

    // "Farthest from point" flips the side.
    size_t n_far = 0;
    const Vec2d seam_x_plus_far = outer_wall_seam_mean("40", "0", n_far, "farthest");
    REQUIRE(n_far > 5);
    REQUIRE(seam_x_plus.x() - seam_x_plus_far.x() > 8.0);
}
