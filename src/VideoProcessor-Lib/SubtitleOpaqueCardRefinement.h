#pragma once
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdlib>

// A confirmed text seed nominates this bounded proof only. Neither display
// padding nor a search limit may manufacture an opaque-card boundary.
struct SubtitleOpaqueCardRefinement
{
    SubtitleBoxRect interior;
    // Corroborated outer side measurements for cleanup only; not glyph ownership.
    SubtitleBoxRect measuredBounds;
    std::array<SubtitleBoxRect, 3> glyphRows{};
    int glyphRowCount = 0;
    unsigned sampledPixels = 0;
    bool workLimit = false;
};

inline bool RefineSubtitleOpaqueCard(const AnalysisLumaSource& source,
    const SubtitleBoxRect& seed, int glyphHeight, int pictureTop,
    int pictureBottom, int darkLimit, int inkFloor, int inkU, int inkV,
    SubtitleOpaqueCardRefinement& result, int coreInkFloor = -1)
{
    result = {};
    if (!source.IsValid() || !seed.Valid() || glyphHeight < 4 ||
        pictureTop < 0 || pictureBottom > source.height || pictureTop >= pictureBottom)
        return false;
    const int first = (std::max)(pictureTop, seed.top);
    const int last = (std::min)(pictureBottom, seed.bottom);
    if (first >= last) return false;
    const int stride = (std::max)(1, (std::min)(8, glyphHeight / 12));
    const int center = (seed.left + seed.right) / 2;
    const int maximumWidth = (std::min)(source.width - 2,
        (std::max)((seed.right - seed.left) * 8, glyphHeight * 32));
    // A partial seed need not be centered in its card. Search every possible
    // bounded card containing the seed, then enforce the measured width below.
    // One outside witness pixel distinguishes a real edge from the limit.
    const int minX = (std::max)(0, seed.right - maximumWidth - 1);
    const int maxX = (std::min)(source.width - 1, seed.left + maximumWidth);
    const int maxDepth = (std::min)(source.height / 3, glyphHeight * 6);
    const int minY = (std::max)(pictureTop, first - maxDepth);
    const int maxY = (std::min)(pictureBottom, last + maxDepth);
    constexpr unsigned sampleBudget = 131072;
    auto sample = [&](int x, int y, AnalysisLumaSample& p) {
        if (x < 0 || x >= source.width || y < pictureTop || y >= pictureBottom)
            return false;
        if (++result.sampledPixels > sampleBudget) {
            result.workLimit = true; return false;
        }
        return source.Sample(x, y, p);
    };
    auto dark = [&](int x, int y) {
        AnalysisLumaSample p;
        return sample(x, y, p) && p.luma <= darkLimit;
    };
    auto ink = [&](const AnalysisLumaSample& p) {
        // Loose antialias coverage below proves backing; only the established
        // caption core may extend glyph bounds or introduce another line.
        return p.luma >= (std::max)(inkFloor, coreInkFloor) &&
            std::abs(int(p.chromaU) - inkU) <= 32 &&
            std::abs(int(p.chromaV) - inkV) <= 32;
    };
    struct Run { int left, right, y; };
    std::vector<Run> candidates;
    // Whitespace immediately beyond a partial word exposes the complete card
    // width. Starting inside the physical bar would instead expose the frame.
    for (int distance = stride; distance <= glyphHeight; distance += stride) {
        for (int y : { first - distance, last + distance - 1 }) {
            if (y < pictureTop || y >= pictureBottom || !dark(center, y)) continue;
            int left = center, right = center;
            while (left > minX) {
                const int next = (std::max)(minX, left - stride);
                if (!dark(next, y)) break;
                left = next;
            }
            while (right < maxX) {
                const int next = (std::min)(maxX, right + stride);
                if (!dark(next, y)) break;
                right = next;
            }
            if (left == minX || right == maxX) continue;
            // Refine only these two narrow edges at native resolution.
            while (left > minX && dark(left - 1, y)) --left;
            while (right < maxX && dark(right + 1, y)) ++right;
            if (left <= minX || right >= maxX || left > seed.left || right < seed.right - 1)
                continue;
            if (right - left + 1 < glyphHeight * 2 || right - left + 1 > maximumWidth) continue;
            candidates.push_back({ left, right + 1, y });
        }
    }
    if (result.workLimit || candidates.size() < 2) return false;
    int selected = -1, bestWidth = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        int support = 0;
        for (size_t j = 0; j < candidates.size(); ++j)
            if (std::abs(candidates[i].left - candidates[j].left) <= stride &&
                std::abs(candidates[i].right - candidates[j].right) <= stride &&
                std::abs(candidates[i].y - candidates[j].y) >= stride)
                ++support;
        const int width = candidates[i].right - candidates[i].left;
        if (support && width > bestWidth) { selected = int(i); bestWidth = width; }
    }
    if (selected < 0) return false;
    const Run chosen = candidates[selected];
    // Use the inner intersection of corroborated edges. This is copy authority,
    // not a cleanup rectangle; outside antialias/fringe never belongs here.
    int left = chosen.left, right = chosen.right;
    int outerLeft = left, outerRight = right;
    for (const auto& c : candidates)
        if (std::abs(c.left - chosen.left) <= stride &&
            std::abs(c.right - chosen.right) <= stride) {
            left = (std::max)(left, c.left); right = (std::min)(right, c.right);
            outerLeft = (std::min)(outerLeft, c.left); outerRight = (std::max)(outerRight, c.right);
        }
    struct Row { int y, inkLeft, inkRight; bool opaque, centerOpaque; };
    auto inspect = [&](int y) {
        Row row{ y, right, left, false, false };
        unsigned tested = 0, bad = 0, black = 0, centerTested = 0, centerBad = 0, centerBlack = 0;
        bool sideDark = true;
        const int innerLeft = left + stride, innerRight = right - stride;
        const int centerLeft = left + (right - left) / 4;
        const int centerRight = right - (right - left) / 4;
        for (int x = innerLeft; x < innerRight; x += stride) {
            AnalysisLumaSample p;
            if (!sample(x, y, p)) { ++bad; ++tested; continue; }
            const bool isDark = p.luma <= darkLimit;
            const bool isInk = ink(p);
            const bool coverage = isDark || (std::abs(int(p.chromaU) - inkU) <= 80 && std::abs(int(p.chromaV) - inkV) <= 80);
            if (x == innerLeft || x + stride >= innerRight) sideDark = sideDark && isDark;
            ++tested; bad += !coverage; black += isDark;
            if (x >= centerLeft && x < centerRight) {
                ++centerTested; centerBad += !coverage; centerBlack += isDark;
            }
            if (isInk) {
                row.inkLeft = (std::min)(row.inkLeft, x);
                row.inkRight = (std::max)(row.inkRight, x + stride);
            }
        }
        row.opaque = sideDark && tested >= 3 && bad * 100 <= tested * 3 && black * 100 >= tested * 25;
        row.centerOpaque = centerTested >= 3 && centerBad * 100 <= centerTested * 3 && centerBlack * 100 >= centerTested * 65;
        return row;
    };
    std::vector<Row> rows;
    const Row origin = inspect(chosen.y);
    if (!origin.opaque || result.workLimit) return false;
    rows.push_back(origin);
    int cardTop = chosen.y, cardBottom = chosen.y + 1;
    bool topBounded = false, bottomBounded = false;
    for (int direction : { -1, 1 }) {
        int lastAccepted = chosen.y;
        bool bounded = false;
        for (int y = chosen.y + direction * stride; y >= minY && y < maxY; y += direction * stride) {
            Row row = inspect(y);
            if (result.workLimit) return false;
            if (!row.opaque) {
                // A narrower black continuation suggests a stepped/ambiguous
                // card. Leave it to per-line detection instead of claiming its
                // rectangular union as a single interior.
                if (row.centerOpaque) {
                    const int farther = y + direction * (std::max)(stride * 2, glyphHeight / 4);
                    if (farther >= minY && farther < maxY && inspect(farther).centerOpaque) return false;
                }
                bounded = true;
                // Resolve the transition in only the final stride. Corners may
                // remain outside this conservative rectangular interior.
                for (int yy = lastAccepted + direction; yy != y; yy += direction) {
                    Row edge = inspect(yy);
                    if (!edge.opaque) break;
                    rows.push_back(edge); lastAccepted = yy;
                }
                break;
            }
            rows.push_back(row); lastAccepted = y;
        }
        // The last coarse row may be short of the physical edge. Inspect that
        // final stride before using the bar as the closing boundary, so a thin
        // bright picture/card edge cannot enter glyph ownership unsampled.
        if (!bounded && ((direction < 0 && minY == pictureTop) ||
            (direction > 0 && maxY == pictureBottom))) {
            for (int y = lastAccepted + direction; y >= minY && y < maxY; y += direction) {
                Row edge = inspect(y);
                if (!edge.opaque) { bounded = true; break; }
                rows.push_back(edge); lastAccepted = y;
            }
        }
        if (direction < 0) {
            topBounded = bounded || minY == pictureTop;
            cardTop = bounded ? lastAccepted : minY;
        } else {
            bottomBounded = bounded || maxY == pictureBottom;
            cardBottom = bounded ? lastAccepted + 1 : maxY;
        }
    }
    // A physical-bar edge can close one side; the opposite side must be finite.
    if (!topBounded || !bottomBounded || result.workLimit ||
        cardTop > first || cardBottom < last || cardBottom - cardTop > maxDepth)
        return false;
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.y < b.y; });
    const int joinGap = (std::max)(stride * 2, glyphHeight / 3);
    for (const Row& row : rows) {
        if (row.inkRight <= row.inkLeft || row.y <= cardTop || row.y >= cardBottom - 1) continue;
        const SubtitleBoxRect bright{ row.inkLeft, row.y,
            (std::min)(right, row.inkRight), (std::min)(cardBottom - 1, row.y + stride) };
        if (result.glyphRowCount && bright.top - result.glyphRows[result.glyphRowCount - 1].bottom <= joinGap) {
            auto& current = result.glyphRows[result.glyphRowCount - 1];
            current.left = (std::min)(current.left, bright.left);
            current.right = (std::max)(current.right, bright.right);
            current.bottom = (std::max)(current.bottom, bright.bottom);
        } else {
            if (result.glyphRowCount == 3) return false;
            result.glyphRows[result.glyphRowCount++] = bright;
        }
    }
    if (!result.glyphRowCount) return false;
    for (int i = 0; i < result.glyphRowCount; ++i)
        if (result.glyphRows[i].bottom - result.glyphRows[i].top > glyphHeight * 2)
            return false;
    result.interior = { left, cardTop, right, cardBottom };
    result.measuredBounds = { outerLeft, cardTop, outerRight, cardBottom };
    return true;
}
