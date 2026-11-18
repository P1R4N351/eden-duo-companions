// SPDX-FileCopyrightText: Copyright 2026 P1R4N351
// SPDX-License-Identifier: GPL-3.0-or-later

// TotkLayer::Calc against the game's rule (totk_layer.h), no game files needed; with TOTK_BECO=<MainField.beco>
// (from the player's own Pack/Bootup.Nin_NX_NVN.pack.zs, WorldMgr/AreaHeight/) also over the real grids.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "totk_layer.h"

namespace {
int failures = 0;
const char* Name(TotkLayer::Layer l) {
    return l == TotkLayer::Sky ? "Sky" : l == TotkLayer::Underground ? "Depths" : "Surface";
}
void Expect(const TotkLayer::Params& p, float x, float y, float z, TotkLayer::Layer want, const char* what) {
    const auto got = TotkLayer::Calc(p, x, y, z);
    if (got != want) {
        std::fprintf(stderr, "FAIL %s: (%g, %g, %g) is %s, want %s\n", what, x, y, z, Name(got), Name(want));
        ++failures;
    }
}
TotkLayer::Grid Flat(int w, int h, std::uint16_t v) {
    TotkLayer::Grid g;
    g.w = w;
    g.h = h;
    g.data.assign(static_cast<size_t>(w) * h, v);
    return g;
}
} // namespace

int main() {
    using TotkLayer::Ground;
    using TotkLayer::Sky;
    using TotkLayer::Underground;
    TotkLayer::Params p;   // MainField: UndergroundHeight -40, margin 50, SkyMargin 150
    p.sky = Flat(20, 16, 1010);      // open sky everywhere ...
    p.terrain = Flat(100, 80, 100);  // ... over flat land at 100 m
    // one sky island in the 500 m cell (10, 8) = x 0..500, z 0..4000+... : origin (-5000,-4000) -> x 0..500, z 0..500
    p.sky.data[8 * 20 + 10] = 400;
    // one mountain: terrain cell (60, 45) = x 1000..1100, z 500..600 at 1000 m
    p.terrain.data[45 * 100 + 60] = 1000;

    // the Depths line: y < UndergroundHeight - margin/2 = -65 (strict)
    Expect(p, 0, -64.9f, 0, Ground, "just above the Depths line");
    Expect(p, 0, -65.0f, 0, Ground, "on the Depths line");
    Expect(p, 0, -65.1f, 0, Underground, "just below the Depths line");
    Expect(p, 0, -900, 0, Underground, "Depths floor");
    Expect(p, 2000, -1500, 2000, Underground, "deep Depths");
    for (float y = 50; y > -100; y -= 5)   // diving into a chasm: Surface, then Depths once below -65
        Expect(p, -254, y, -1102, y < -65 ? Underground : Ground, "chasm dive");
    Expect(p, 0, -30, 0, Ground, "a cave below sea level but above the line");

    // the Sky: s = 1 - (max(A, T+150) - y)/150 >= 0.4  <=>  y >= max(A - 90, T + 60). Exactly on that line float32
    // gives s a hair under 0.4f, as in the game's own arithmetic (which this mirrors), so the cases step past it.
    Expect(p, -3000, 919, -2000, Ground, "open land, just under the sky threshold (1010 - 90)");
    Expect(p, -3000, 920.1f, -2000, Sky, "open land, just over the sky threshold");
    Expect(p, -3000, 300, -2000, Ground, "flying low over flat land");
    Expect(p, 250, 310.1f, 250, Sky, "on the sky island (threshold 400 - 90)");
    Expect(p, 250, 309, 250, Ground, "under the sky island");
    for (float y = 1500; y > 100; y -= 10)   // falling from the island: Sky until y < 310, then Surface
        Expect(p, 250, y, 250, y > 310 ? Sky : Ground, "falling from a sky island");
    // a mountain top is the Surface: Sky needs T + 60 above it (max-biased: the peak dominates its cell)
    Expect(p, 1050, 1000, 550, Ground, "standing on a 1000 m peak");
    Expect(p, 1050, 1059, 550, Ground, "jumping on the peak");
    Expect(p, 1050, 1060.1f, 550, Sky, "just over 60 m above the peak");
    // outside the grid: indices clamp to the edge cells
    Expect(p, -7000, 921, -6000, Sky, "beyond the map's corner, high");
    Expect(p, 7000, 200, 6000, Ground, "beyond the map's corner, low");

    // no area-height object: never Sky (the game's SkyRate returns 0)
    TotkLayer::Params none;
    none.have_area_height = false;
    Expect(none, 0, 5000, 0, Ground, "no area-height grids, very high");
    Expect(none, 0, -100, 0, Underground, "no area-height grids, Depths");
    // a grid missing inside the object: it samples as 1000 (H = 1000 + 150 with the terrain missing)
    TotkLayer::Params missing;
    Expect(missing, 0, 1059, 0, Ground, "both grids missing, under 1000 + 60");
    Expect(missing, 0, 1060.1f, 0, Sky, "both grids missing, just over 1000 + 60");

    // the real main field (owner's own file), when given
    if (const char* path = std::getenv("TOTK_BECO")) {
        std::ifstream f{path, std::ios::binary};
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{f}, {}};
        TotkLayer::Params real;
        if (!TotkLayer::ParseBeco(bytes, real) || real.sky.w != 20 || real.sky.h != 16 || real.terrain.w != 100 ||
            real.terrain.h != 80) {
            std::fprintf(stderr, "FAIL: %s is not a 20x16 + 100x80 area-height file\n", path);
            return 1;
        }
        Expect(real, -254, 120, -1102, Ground, "Lookout Landing");
        Expect(real, -254, -900, -1102, Underground, "under Lookout Landing");
        // every cell's own sky threshold: just above is Sky, well below on the ground is Surface
        for (int cz = 0; cz < 16; ++cz)
            for (int cx = 0; cx < 20; ++cx) {
                const float x = -5000 + cx * 500 + 250, z = -4000 + cz * 500 + 250;
                Expect(real, x, 2500, z, Sky, "high above every cell");
            }
        std::printf("real grids: sky %dx%d, terrain %dx%d\n", real.sky.w, real.sky.h, real.terrain.w, real.terrain.h);
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("totk_layer_test: PASS\n");
    return 0;
}
