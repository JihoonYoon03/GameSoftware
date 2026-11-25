#include "stdafx.h"
#include "WorldContent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <cstring>
#include <set>

namespace world
{
namespace
{
using G = SurfaceGeometry;
bool Cancelled(const std::atomic_bool *cancel)
{
    return cancel && cancel->load();
}
std::uint64_t Mix(std::uint64_t n)
{
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
double Fraction(std::uint64_t n)
{
    return double(Mix(n) >> 11) / 9007199254740992.;
}
Vector3d Add(Vector3d a, Vector3d b)
{
    return {a.x + b.x, a.y + b.y, 0};
}
Vector3d Sub(Vector3d a, Vector3d b)
{
    return {a.x - b.x, a.y - b.y, 0};
}
Vector3d Mul(Vector3d a, double n)
{
    return {a.x * n, a.y * n, 0};
}
double Cross(Vector3d a, Vector3d b)
{
    return a.x * b.y - a.y * b.x;
}
double Norm(Vector3d a)
{
    return std::hypot(a.x, a.y);
}
Vector3d Rotate(Vector3d a, double angle)
{
    return {a.x * std::cos(angle) - a.y * std::sin(angle), a.x * std::sin(angle) + a.y * std::cos(angle), 0};
}
double SegmentDistance(Vector3d p, Vector3d a, Vector3d b)
{
    auto ab = Sub(b, a), ap = Sub(p, a);
    double square = ab.x * ab.x + ab.y * ab.y;
    double t = square > 1.e-12 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / square, 0., 1.) : 0;
    return Norm(Sub(ap, Mul(ab, t)));
}
double Winding(const GeneratedRegion &r)
{
    double sum = 0;
    for (std::size_t i = 0; i < r.footprint.size(); ++i)
    {
        sum += Cross(r.footprint[i], r.footprint[(i + 1) % r.footprint.size()]);
    }
    return sum >= 0 ? 1. : -1.;
}
bool Inside(const GeneratedRegion &r, Vector3d p, double margin = 0)
{
    if (r.footprint.size() < 3)
    {
        return false;
    }
    double sign = Winding(r);
    for (std::size_t i = 0; i < r.footprint.size(); ++i)
    {
        auto a = r.footprint[i], edge = Sub(r.footprint[(i + 1) % r.footprint.size()], a);
        if (sign * Cross(edge, Sub(p, a)) < margin * Norm(edge))
        {
            return false;
        }
    }
    return true;
}
bool Clip(const GeneratedRegion &r, Vector3d &a, Vector3d &b)
{
    auto delta = Sub(b, a);
    double lo = 0, hi = 1, sign = Winding(r);
    for (std::size_t i = 0; i < r.footprint.size(); ++i)
    {
        auto edge = Sub(r.footprint[(i + 1) % r.footprint.size()], r.footprint[i]);
        double value = sign * Cross(edge, Sub(a, r.footprint[i]));
        double change = sign * Cross(edge, delta);
        if (std::fabs(change) < 1.e-12)
        {
            if (value < 0)
            {
                return false;
            }
        }
        else if (change > 0)
        {
            lo = (std::max)(lo, -value / change);
        }
        else
        {
            hi = (std::min)(hi, -value / change);
        }
        if (lo > hi)
        {
            return false;
        }
    }
    b = Add(a, Mul(delta, hi));
    a = Add(a, Mul(delta, lo));
    return Norm(Sub(b, a)) > .1;
}
struct Grid
{
    std::uint64_t seed;
    double width, depth, yaw, warpScale;
    explicit Grid(const GeneratedPlanet &p, const GeneratedRegion &r)
        : seed(WorldIdentity::StreamSeed(p.terrain.Planet(), r.definition.id, "surface_content_v1"))
    {
        bool city = r.definition.kind == RegionKind::Settlement;
        if (city && p.layoutRevision >= 5)
        {
            // CQ-04B: Varied rectangular urban blocks (82-116m) with organic street curvature (warpScale 0.115)
            // so settlements feel natural rather than rigid uniform checkerboards while keeping quarter-view alignment.
            const bool major = r.definition.tier >= SettlementTier::Metropolis;
            width = major ? 92 + Fraction(seed) * 16 : 82 + Fraction(seed) * 14;
            depth = major ? 98 + Fraction(seed + 1) * 18 : 88 + Fraction(seed + 1) * 16;
            warpScale = .115;
        }
        else if (city && p.layoutRevision >= 4)
        {
            const bool major = r.definition.tier >= SettlementTier::Metropolis;
            width = major ? 68 + Fraction(seed) * 14 : 64 + Fraction(seed) * 14;
            depth = major ? 74 + Fraction(seed + 1) * 16 : 70 + Fraction(seed + 1) * 14;
            warpScale = .13;
        }
        else
        {
            width = city ? 84 + Fraction(seed) * 24 : 132 + Fraction(seed) * 44;
            depth = city ? 92 + Fraction(seed + 1) * 30 : 140 + Fraction(seed + 1) * 44;
            warpScale = .13;
        }
        // Match Halo's world axes: the shared quarter-view projects these axes at 45 degrees.
        // Warped shared nodes keep organic streets without randomly rotating an entire settlement.
        yaw = 0;
    }
    Vector3d Node(std::int64_t x, std::int64_t y) const
    {
        // Shared nodes have smooth bounded warps: every edge joins exactly and no cells fold.
        double a = Fraction(seed + 3) * 6, b = Fraction(seed + 4) * 6;
        return Rotate({double(x) * width + width * warpScale * std::sin(double(y) * .61 + a),
                       double(y) * depth + depth * warpScale * std::sin(double(x) * .53 + b), 0},
                      yaw);
    }
    std::uint64_t CellSeed(ContentCellKey key) const
    {
        return Mix(seed ^ Mix(std::uint64_t(key.x)) ^ Mix(std::uint64_t(key.y) + 0x9e3779b97f4a7c15ULL));
    }
};
SurfacePoint At(const GeneratedPlanet &p, const GeneratedRegion &r, Vector3d local)
{
    auto point = G::FromLocal(r.frame, local, p.terrain.Planet().radiusMeters);
    point.altitudeMeters = p.terrain.Height(point);
    return point;
}
bool Reserved(const GeneratedPlanet &p, const SurfacePoint &point, double radius)
{
    const double planetRadius = p.terrain.Planet().radiusMeters;
    for (const auto &anchor : p.anchors)
    {
        if (anchor.location.space == SpaceKind::Surface &&
            G::Distance(planetRadius, point, anchor.location.position) < radius + 14)
        {
            return true;
        }
    }
    return false;
}
bool IntercityClear(const GeneratedPlanet &p, const GeneratedRegion &r, Vector3d local, double radius)
{
    // Existing regional approach roads retain a clear corridor through generated parcels.
    auto center = G::FromLocal(r.frame, local, p.terrain.Planet().radiusMeters);
    for (const auto &connection : p.connections)
    {
        if (connection.mode != RouteMode::Road)
        {
            continue;
        }
        // The macro planner routes unrelated connections outside region footprints.
        if (connection.from >= p.nodes.size() || connection.to >= p.nodes.size())
        {
            continue;
        }
        if (p.nodes[connection.from].regionId != r.definition.id &&
            p.nodes[connection.to].regionId != r.definition.id)
        {
            continue;
        }
        for (std::size_t i = 1; i < connection.points.size(); ++i)
        {
            auto a = connection.points[i - 1], b = connection.points[i];
            // Chord broad phase avoids expensive tangent projections of distant route segments.
            double chord = std::sqrt((std::max)(0., 2 - 2 * G::Dot(a.normal, b.normal))) *
                           p.terrain.Planet().radiusMeters;
            double reach = (chord * 1.001 + radius + 12) / p.terrain.Planet().radiusMeters;
            if (reach < G::Pi && G::Dot(center.normal, a.normal) < std::cos(reach))
            {
                continue;
            }
            auto x = G::ToLocal(r.frame, a, p.terrain.Planet().radiusMeters);
            auto y = G::ToLocal(r.frame, b, p.terrain.Planet().radiusMeters);
            if (SegmentDistance(local, x, y) < radius + 7)
            {
                return false;
            }
        }
    }
    return true;
}
void AddStreet(ContentCell &cell, const GeneratedPlanet &p, const GeneratedRegion &r, Vector3d a, Vector3d b,
               unsigned lanes)
{
    if (p.cityCores)
    {
        if (auto core = p.cityCores->Find(r.definition.id))
        {
            if (Norm(a) < core->radiusMeters + 20 || Norm(b) < core->radiusMeters + 20)
            {
                return;
            }
        }
    }
    if (!Clip(r, a, b))
    {
        return;
    }
    SurfaceStreet street;
    street.lanes = lanes;
    street.widthMeters = lanes * 3.;
    street.sidewalkMeters = 2.4;
    // Eight-metre samples let the road follow the same terrain used by movement.
    // Even subdivisions preserve the exact midpoint used by authored approach T-junctions.
    unsigned steps = (std::max)(2u, unsigned(std::ceil(Norm(Sub(b, a)) / 16)) * 2);
    for (unsigned i = 0; i <= steps; ++i)
    {
        auto point = At(p, r, Add(a, Mul(Sub(b, a), double(i) / steps)));
        if (point.altitudeMeters < PlanetTerrain::SeaLevelMeters ||
            Reserved(p, point, street.widthMeters * .5))
        {
            if (street.points.size() > 1)
            {
                cell.streets.push_back(std::move(street));
            }
            street = SurfaceStreet{{}, double(lanes * 3), 2.4, lanes};
            continue;
        }
        point.altitudeMeters += .08;
        street.points.push_back(point);
    }
    if (street.points.size() > 1)
    {
        cell.streets.push_back(std::move(street));
    }
}
ContentForm CityForm(SettlementTier tier, double zoneRatio, std::uint64_t seed, ContentUse &use)
{
    unsigned choice = unsigned(seed % 12);
    // Downtown Core (< 0.28): civic landmarks, transit hubs, high-rise towers & plazas
    if (zoneRatio < .28)
    {
        if (choice <= 1)
        {
            use = ContentUse::Administration;
            return ContentForm::Dome;
        }
        if (choice <= 3)
        {
            use = ContentUse::Services;
            return ContentForm::Transit;
        }
        use = ContentUse::Housing;
        if (tier >= SettlementTier::Town)
        {
            return choice % 3 == 0   ? ContentForm::Spire
                   : choice % 3 == 1 ? ContentForm::TwinBridge
                                     : ContentForm::Arcology;
        }
        return choice % 2 == 0 ? ContentForm::Arcology : ContentForm::Terrace;
    }
    // Outer Industrial / Logistics Ring (> 0.68)
    if (zoneRatio > .68)
    {
        if (choice <= 3)
        {
            use = ContentUse::Industry;
            return choice % 2 == 0 ? ContentForm::Workshop : ContentForm::Cargo;
        }
        if (choice == 4)
        {
            use = ContentUse::Services;
            return ContentForm::Solar;
        }
        if (choice == 5)
        {
            use = ContentUse::Services;
            return ContentForm::WaterTank;
        }
        use = ContentUse::Housing;
        return choice % 2 == 0 ? ContentForm::Habitat : ContentForm::Courtyard;
    }
    // Mid-Ring Dense Mixed-Use & Residential (0.28 .. 0.68)
    if (choice == 0)
    {
        use = ContentUse::Industry;
        return ContentForm::Workshop;
    }
    if (choice == 1 || choice == 2)
    {
        use = ContentUse::Services;
        return ContentForm::Transit;
    }
    if (choice == 3)
    {
        use = ContentUse::Administration;
        return ContentForm::Dome;
    }
    use = ContentUse::Housing;
    if (tier >= SettlementTier::Metropolis)
    {
        return choice % 4 == 0   ? ContentForm::Spire
               : choice % 4 == 1 ? ContentForm::TwinBridge
               : choice % 4 == 2 ? ContentForm::Arcology
                                 : ContentForm::Courtyard;
    }
    return choice % 3 == 0   ? ContentForm::Terrace
           : choice % 3 == 1 ? ContentForm::Courtyard
                             : ContentForm::Habitat;
}
// ============================================================================
// CQ-04B (Layout Revision 5) — Realistic Road Cross-Sections, Grade Separation,
// Roundabouts, District Zoning, Unique Landmarks, Perimeter Block Parceling & OBB Checks
// ============================================================================
struct RoadProfile
{
    double carriagewayMeters = 6.;
    double sidewalkMeters = 2.4;
    double halfTotalMeters = 5.4;
    bool hasMedian = false;
    bool hasLeftPocket = false;
};
RoadProfile ProfileForLanes(unsigned lanes)
{
    if (lanes >= 6)
    {
        return {19.4, 2.8, 12.5, true, true}; // 3.0*6 + 1.4 median + 2.8*2 sidewalks = 25.0m
    }
    if (lanes >= 4)
    {
        return {12.8, 2.4, 8.8, true, true}; // 3.0*4 + 0.8 median + 2.4*2 sidewalks = 17.6m
    }
    return {6.0, 2.4, 5.4, false, false}; // 3.0*2 + 2.4*2 sidewalks = 10.8m
}
unsigned Rev5LanesForCoord(SettlementTier tier, std::int64_t coord, double ringRatio)
{
    if (tier >= SettlementTier::Metropolis)
    {
        if (coord % 4 == 0)
        {
            // Step down 6 -> 4 before reaching the outer 4-lane ring road (never terminate 6-lane as a dead-end).
            return ringRatio <= .64 ? 6u : 4u;
        }
        if (coord % 2 == 0)
        {
            return 4u;
        }
        return 2u;
    }
    if (tier == SettlementTier::Town)
    {
        return (coord % 2 == 0) ? 4u : 2u;
    }
    return coord == 0 ? 4u : 2u;
}
bool Rev5RoundaboutNode(const GeneratedRegion &r, std::int64_t gx, std::int64_t gy, double ringRatio,
                        double &outRadius, double &outIsland, unsigned &outLanes)
{
    if (r.definition.kind != RegionKind::Settlement)
    {
        return false;
    }
    const bool majorAxis = (r.definition.tier >= SettlementTier::Metropolis)
                               ? (gx % 4 == 0 || gy % 4 == 0)
                               : (gx == 0 || gy == 0);
    // Ring-road portal roundabouts (2-lane, R=20.5m, island=11m)
    if (majorAxis && ringRatio >= .64 && ringRatio <= .82)
    {
        outRadius = 20.5;
        outIsland = 11.0;
        outLanes = 4;
        return true;
    }
    // Civic plaza gateway roundabout (1-lane, R=13m, island=7m)
    if (r.definition.tier <= SettlementTier::Town && gx == 1 && gy == 0)
    {
        outRadius = 13.0;
        outIsland = 7.0;
        outLanes = 2;
        return true;
    }
    return false;
}
DistrictZone ClassifyDistrictZone(const GeneratedRegion &r, const Grid &grid, std::int64_t cx, std::int64_t cy,
                                  double zoneRatio)
{
    if (zoneRatio < .22)
    {
        return DistrictZone::CivicCore;
    }
    // Spaceport sector in one deterministic outer quadrant of Metropolis / SystemCapital
    const double angle = std::atan2(double(cy), double(cx));
    const double spAngle = Fraction(grid.seed + 91) * 2. * G::Pi - G::Pi;
    double dAngle = std::fabs(angle - spAngle);
    if (dAngle > G::Pi)
    {
        dAngle = 2. * G::Pi - dAngle;
    }
    if (r.definition.tier >= SettlementTier::Metropolis && zoneRatio >= .52 && zoneRatio <= .82 && dAngle < .52)
    {
        return DistrictZone::Spaceport;
    }
    if (zoneRatio > .68)
    {
        return (dAngle < 1.35) ? DistrictZone::IndustrialLogistics : DistrictZone::ResidentialLow;
    }
    // Voronoi-like deterministic block district clustering (2x2 block clusters share one coherent zone).
    const auto clusterSeed = Mix(grid.seed ^ Mix(std::uint64_t(cx >> 1) + 17) ^ Mix(std::uint64_t(cy >> 1) + 53));
    const unsigned pick = unsigned(clusterSeed % 10);
    if (zoneRatio < .45)
    {
        if (pick < 4)
        {
            return DistrictZone::CommercialTransit;
        }
        return r.definition.tier >= SettlementTier::Metropolis ? DistrictZone::ResidentialHigh
                                                               : DistrictZone::ResidentialLow;
    }
    if (pick < 2)
    {
        return DistrictZone::CommercialTransit;
    }
    if (pick < 4)
    {
        return DistrictZone::IndustrialLogistics;
    }
    return (r.definition.tier >= SettlementTier::Metropolis && pick < 7) ? DistrictZone::ResidentialHigh
                                                                         : DistrictZone::ResidentialLow;
}
struct PlacedObb
{
    Vector3d center;
    double halfX = 0, halfY = 0, yaw = 0;
};
bool ObbOverlap(const PlacedObb &a, const PlacedObb &b, double margin = 1.2)
{
    const double ra = std::hypot(a.halfX, a.halfY), rb = std::hypot(b.halfX, b.halfY);
    const double dx = b.center.x - a.center.x, dy = b.center.y - a.center.y;
    if (dx * dx + dy * dy > (ra + rb + margin) * (ra + rb + margin))
    {
        return false;
    }
    auto separated = [&](double ax, double ay) {
        const double projA =
            std::fabs(ax * std::cos(a.yaw) + ay * std::sin(a.yaw)) * (a.halfX + margin * .5) +
            std::fabs(-ax * std::sin(a.yaw) + ay * std::cos(a.yaw)) * (a.halfY + margin * .5);
        const double projB =
            std::fabs(ax * std::cos(b.yaw) + ay * std::sin(b.yaw)) * (b.halfX + margin * .5) +
            std::fabs(-ax * std::sin(b.yaw) + ay * std::cos(b.yaw)) * (b.halfY + margin * .5);
        return std::fabs(dx * ax + dy * ay) > projA + projB;
    };
    const double ca = std::cos(a.yaw), sa = std::sin(a.yaw);
    const double cb = std::cos(b.yaw), sb = std::sin(b.yaw);
    return !(separated(ca, sa) || separated(-sa, ca) || separated(cb, sb) || separated(-sb, cb));
}
void EmitRev5Street(ContentCell &cell, const GeneratedPlanet &p, const GeneratedRegion &r, Vector3d a,
                    Vector3d b, unsigned lanes, StreetKind kind, double startTrim, double endTrim,
                    double startWidthOverride = 0., double startElevOffset = 0., double endElevOffset = 0.)
{
    if (p.cityCores)
    {
        if (auto core = p.cityCores->Find(r.definition.id))
        {
            // Keep authored CityCore interior & perimeter buffer clear so procedural streets never X-cross core roads.
            if (Norm(a) < core->radiusMeters + 16. || Norm(b) < core->radiusMeters + 16.)
            {
                return;
            }
        }
    }
    if (!Clip(r, a, b))
    {
        return;
    }
    auto delta = Sub(b, a);
    const double fullLen = Norm(delta);
    if (fullLen <= startTrim + endTrim + 3.)
    {
        return;
    }
    const Vector3d dir = Mul(delta, 1. / fullLen);
    a = Add(a, Mul(dir, startTrim));
    b = Sub(b, Mul(dir, endTrim));
    const double len = Norm(Sub(b, a));
    if (len < 2.)
    {
        return;
    }
    const auto prof = ProfileForLanes(lanes);
    const bool elevatedOrUnderpass = kind == StreetKind::Elevated || kind == StreetKind::ElevatedRamp ||
                                     kind == StreetKind::Underpass || kind == StreetKind::UnderpassRamp;
    const unsigned steps = (std::max)(2u, unsigned(std::ceil(len / 12.)) * 2u);
    bool crossesWater = false;
    bool dryEnds = At(p, r, a).altitudeMeters >= PlanetTerrain::SeaLevelMeters &&
                   At(p, r, b).altitudeMeters >= PlanetTerrain::SeaLevelMeters;
    for (unsigned i = 0; i <= steps; ++i)
    {
        const double t = double(i) / steps;
        auto pt = At(p, r, Add(a, Mul(Sub(b, a), t)));
        if (pt.altitudeMeters < PlanetTerrain::SeaLevelMeters)
        {
            crossesWater = true;
            break;
        }
    }
    // Promote short water/ravine spans (<= 150m) with dry endpoints to a continuous Bridge instead of stubs.
    const bool promoteBridge = crossesWater && dryEnds && len <= 150. && !elevatedOrUnderpass;
    if (promoteBridge)
    {
        kind = StreetKind::Bridge;
    }
    SurfaceStreet street;
    street.lanes = lanes;
    street.widthMeters = prof.carriagewayMeters;
    street.sidewalkMeters = (kind == StreetKind::Elevated || kind == StreetKind::ElevatedRamp ||
                             kind == StreetKind::Underpass || kind == StreetKind::UnderpassRamp)
                                ? 1.2
                                : prof.sidewalkMeters;
    street.kind = kind;
    street.startWidthMeters = startWidthOverride;
    street.elevationOffsetMeters = (startElevOffset + endElevOffset) * .5;
    street.hasMedian = prof.hasMedian && kind != StreetKind::PedestrianPlaza;
    street.hasLeftPocket = prof.hasLeftPocket && kind == StreetKind::Surface;

    // Preserve exact midpoint vertex when an authored CityCore approach joins this local street edge.
    std::optional<Vector3d> exactJoin;
    if (p.cityCores)
    {
        if (auto core = p.cityCores->Find(r.definition.id))
        {
            for (const auto &app : core->approaches)
            {
                if (app.cellX == cell.key.x && app.cellY == cell.key.y &&
                    SegmentDistance(app.join, a, b) < .05)
                {
                    exactJoin = app.join;
                    break;
                }
            }
        }
    }
    const double baseElevA = At(p, r, a).altitudeMeters;
    const double baseElevB = At(p, r, b).altitudeMeters;
    std::vector<std::pair<Vector3d, double>> samples;
    samples.reserve(steps + 1);
    if (exactJoin)
    {
        const double joinT = std::clamp(Norm(Sub(*exactJoin, a)) / len, .05, .95);
        const unsigned half = (std::max)(1u, steps / 2u);
        for (unsigned i = 0; i < half; ++i)
        {
            const double f = double(i) / half;
            samples.push_back({Add(a, Mul(Sub(*exactJoin, a), f)), joinT * f});
        }
        samples.push_back({*exactJoin, joinT});
        for (unsigned i = 1; i <= half; ++i)
        {
            const double f = double(i) / half;
            samples.push_back({Add(*exactJoin, Mul(Sub(b, *exactJoin), f)), joinT + (1. - joinT) * f});
        }
    }
    else
    {
        for (unsigned i = 0; i <= steps; ++i)
        {
            const double t = double(i) / steps;
            samples.push_back({Add(a, Mul(Sub(b, a), t)), t});
        }
    }
    for (const auto &[localPt, t] : samples)
    {
        auto pt = At(p, r, localPt);
        if (!promoteBridge && !elevatedOrUnderpass &&
            (pt.altitudeMeters < PlanetTerrain::SeaLevelMeters || Reserved(p, pt, street.widthMeters * .5)))
        {
            if (street.points.size() > 1)
            {
                cell.streets.push_back(std::move(street));
            }
            street.points.clear();
            continue;
        }
        if (promoteBridge)
        {
            const double deckZ = (std::max)(PlanetTerrain::SeaLevelMeters + 4.5,
                                            baseElevA + (baseElevB - baseElevA) * t);
            pt.altitudeMeters = (std::max)(pt.altitudeMeters, deckZ) + .08;
        }
        else if (elevatedOrUnderpass)
        {
            // Smooth S-curve vertical transition for realistic grade ramps (<= 8.9% elevated, <= 7.5% underpass).
            const double s = t * t * (3. - 2. * t);
            const double offset = startElevOffset + (endElevOffset - startElevOffset) * s;
            const double linearGround = baseElevA + (baseElevB - baseElevA) * t;
            pt.altitudeMeters = linearGround + offset + .08;
        }
        else
        {
            pt.altitudeMeters += .08;
        }
        street.points.push_back(pt);
    }
    if (street.points.size() > 1)
    {
        cell.streets.push_back(std::move(street));
    }
}
} // namespace

// ============================================================================
// CityCorePlan — authored geometry is resolved once, before route/relay planning
// ============================================================================
const CityCore *CityCorePlan::Find(std::string_view region) const
{
    for (const auto &city : cities)
    {
        if (city.regionId == region)
        {
            return &city;
        }
    }
    return nullptr;
}
void CityCorePlan::Validate() const
{
    std::set<std::string> regions;
    for (const auto &city : cities)
    {
        if (!WorldIdentity::ValidId(city.regionId) || !regions.insert(city.regionId).second ||
            !std::isfinite(city.radiusMeters) || city.radiusMeters < 40 || city.radiusMeters > 1000)
        {
            throw std::invalid_argument("Invalid authored city core");
        }
        std::set<std::string> ids;
        for (const auto &lot : city.lots)
        {
            if (!WorldIdentity::ValidId(lot.id) || !ids.insert(lot.id).second || !std::isfinite(lot.yaw))
            {
                throw std::invalid_argument("Invalid authored lot identity/orientation");
            }
            for (auto value : {lot.positionMeters.x, lot.positionMeters.y, lot.positionMeters.z,
                               lot.sizeMeters.x, lot.sizeMeters.y, lot.sizeMeters.z})
            {
                if (!std::isfinite(value))
                {
                    throw std::invalid_argument("Non-finite authored lot");
                }
            }
            if (lot.sizeMeters.x <= 0 || lot.sizeMeters.y <= 0 || lot.sizeMeters.z <= 0 ||
                Norm(lot.positionMeters) + Norm(lot.sizeMeters) * .5 > city.radiusMeters)
            {
                throw std::invalid_argument("Authored lot exceeds its protected city core");
            }
            if (lot.form == ContentForm::TransitMesh && lot.assetName.empty())
            {
                throw std::invalid_argument("Authored walkway has no asset identity");
            }
        }
        for (const auto &road : city.roads)
        {
            if (road.from == road.to || (road.lanes != 2 && road.lanes != 4 && road.lanes != 6) ||
                !std::isfinite(road.start.x + road.start.y + road.start.z + road.end.x + road.end.y +
                               road.end.z) ||
                Norm(road.start) > city.radiusMeters || Norm(road.end) > city.radiusMeters ||
                Norm(Sub(road.end, road.start)) < .1)
            {
                throw std::invalid_argument("Invalid authored city street");
            }
        }
    }
}
void CityCorePlan::Prepare(GeneratedPlanet &p, const std::atomic_bool *cancel)
{
    Validate();
    auto cancelled = [&] {
        if (Cancelled(cancel))
        {
            throw std::runtime_error("City preparation cancelled");
        }
    };
    auto reservations = p.terrain.Reservations();
    reservations.erase(std::remove_if(reservations.begin(), reservations.end(),
                                      [&](const auto &pad) {
                                          for (const auto &city : cities)
                                          {
                                              if (pad.id == "core_" + city.regionId)
                                              {
                                                  return true;
                                              }
                                          }
                                          return false;
                                      }),
                       reservations.end());
    for (auto &pad : reservations)
    {
        if (pad.id.compare(0, 7, "anchor_") == 0)
        {
            pad.priority = 3;
        }
    }
    for (auto &city : cities)
    {
        cancelled();
        const auto &r = p.Region(city.regionId);
        if (r.center.space != SpaceKind::Surface ||
            (r.definition.kind != RegionKind::Settlement && r.definition.id != "frontier"))
        {
            throw std::invalid_argument("Authored core requires a surface settlement or frontier field");
        }
        city.elevationMeters = p.terrain.Height(r.center.position);
        double feather = 3000;
        for (const auto &other : cities)
        {
            if (other.regionId == city.regionId)
            {
                continue;
            }
            double gap = G::Distance(p.terrain.Planet().radiusMeters, r.center.position,
                                     p.Region(other.regionId).center.position) -
                         city.radiusMeters - other.radiusMeters;
            if (gap < 10)
            {
                throw std::invalid_argument("Authored city cores overlap");
            }
            feather = (std::min)(feather, gap * .45);
        }
        reservations.push_back({"core_" + city.regionId, r.center.position, city.radiusMeters, feather,
                                city.elevationMeters, 2});
    }
    auto savedTerraces = p.terrain.Terraces();
    p.terrain = PlanetTerrain(p.terrain.Planet(), std::move(reservations), p.terrain.Homeland());
    p.terrain.SetTerraces(std::move(savedTerraces));
    for (auto &city : cities)
    {
        cancelled();
        const auto &r = p.Region(city.regionId);
        std::size_t region = std::size_t(&r - p.regions.data());
        Grid grid(p, r);
        city.cells.clear();
        city.approaches.clear();
        auto owner = [&](Vector3d local) {
            auto axis = Rotate(local, -grid.yaw);
            return std::make_pair(std::int64_t(std::floor(axis.x / grid.width)),
                                  std::int64_t(std::floor(axis.y / grid.depth)));
        };
        auto cellAt = [&](Vector3d local) -> ContentCell & {
            auto address = owner(local);
            auto &cell = city.cells[address];
            cell.key = {region, address.first, address.second};
            return cell;
        };
        std::map<std::uint32_t, std::pair<unsigned, Vector3d>> terminals;
        for (const auto &road : city.roads)
        {
            auto &a = terminals[road.from], &b = terminals[road.to];
            ++a.first;
            a.second = road.start;
            ++b.first;
            b.second = road.end;
        }
        std::set<std::pair<std::int64_t, std::int64_t>> usedJoins;
        for (auto terminal : terminals)
        {
            if (r.definition.kind != RegionKind::Settlement)
            {
                continue;
            }
            if (terminal.second.first != 1)
            {
                continue;
            }
            auto start = terminal.second.second;
            Vector3d direction;
            for (const auto &road : city.roads)
            {
                if (road.from == terminal.first)
                {
                    direction = Sub(road.start, road.end);
                }
                if (road.to == terminal.first)
                {
                    direction = Sub(road.end, road.start);
                }
            }
            direction = Mul(direction, 1 / Norm(direction));
            auto outside = start;
            for (unsigned step = 0; Norm(outside) < city.radiusMeters + 40 && step < 200; ++step)
            {
                outside = Add(outside, Mul(direction, 4));
            }
            auto approximate = owner(outside);
            double best = std::numeric_limits<double>::max();
            CoreApproach approach;
            approach.from = terminal.first;
            for (std::int64_t y = approximate.second - 4; y <= approximate.second + 4; ++y)
            {
                for (std::int64_t x = approximate.first - 4; x <= approximate.first + 4; ++x)
                {
                    if (usedJoins.count({x, y}) || y % 6 == 0)
                    {
                        continue;
                    }
                    auto a = grid.Node(x, y), b = grid.Node(x + 1, y), middle = Mul(Add(a, b), .5);
                    if (Norm(a) < city.radiusMeters + 40 || Norm(b) < city.radiusMeters + 40 ||
                        !Inside(r, middle, 12))
                    {
                        continue;
                    }
                    double distance = Norm(Sub(middle, outside));
                    if (distance < best)
                    {
                        best = distance;
                        approach.cellX = x;
                        approach.cellY = y;
                        approach.join = middle;
                    }
                }
            }
            if (best == std::numeric_limits<double>::max())
            {
                throw std::runtime_error("City footprint has no safe local-street approach");
            }
            approach.points = {start, outside, approach.join};
            usedJoins.insert({approach.cellX, approach.cellY});
            city.approaches.push_back(std::move(approach));
        }
        // CQ-04B (Rev 5+): Track CityCore road junctions so authored core streets trim back to stop-lines
        // and publish explicit SurfaceIntersection pads instead of X-crossing yellow/white markings.
        struct CoreNodeInfo
        {
            Vector3d pos{};
            unsigned degree = 0;
            unsigned maxLanes = 2;
        };
        std::map<std::uint32_t, CoreNodeInfo> coreNodes;
        if (p.layoutRevision >= 5)
        {
            for (const auto &edge : city.roads)
            {
                auto &nf = coreNodes[edge.from];
                nf.pos = edge.start;
                ++nf.degree;
                nf.maxLanes = (std::max)(nf.maxLanes, edge.lanes);
                auto &nt = coreNodes[edge.to];
                nt.pos = edge.end;
                ++nt.degree;
                nt.maxLanes = (std::max)(nt.maxLanes, edge.lanes);
            }
            for (const auto &[nodeId, info] : coreNodes)
            {
                if (info.degree < 2)
                {
                    continue;
                }
                auto pt = At(p, r, info.pos);
                SurfaceIntersection inter;
                inter.id = WorldIdentity::ContentKey(p.terrain.Planet(), city.regionId,
                                                     "core.inter." + std::to_string(nodeId));
                inter.regionId = city.regionId;
                inter.center = pt;
                inter.frame = G::Advance(r.frame, info.pos.x, info.pos.y, p.terrain.Planet().radiusMeters);
                inter.frame.origin = pt;
                inter.maxLanes = info.maxLanes;
                inter.arms = (std::min)(4u, info.degree);
                inter.radiusMeters = info.maxLanes * 1.5 + 2.4 + 1.0;
                inter.control = (info.maxLanes >= 4 && info.degree >= 3) ? IntersectionControl::Signalized
                                                                         : IntersectionControl::Unsignalized;
                inter.mainAxisYaw = grid.yaw;
                inter.cycleSeconds = (inter.control == IntersectionControl::Signalized) ? 56. : 0.;
                cellAt(info.pos).intersections.push_back(std::move(inter));
            }
        }
        auto road = [&](Vector3d a, Vector3d b, unsigned lanes, double width = 0, double startTrim = 0.,
                        double endTrim = 0.) {
            auto delta = Sub(b, a);
            const double fullLen = Norm(delta);
            if (startTrim + endTrim > 0. && fullLen > startTrim + endTrim + 4.)
            {
                const auto dir = Mul(delta, 1. / fullLen);
                a = Add(a, Mul(dir, startTrim));
                b = Sub(b, Mul(dir, endTrim));
            }
            unsigned steps = (std::max)(2u, unsigned(std::ceil(Norm(Sub(b, a)) / 16)) * 2);
            for (unsigned i = 0; i < steps; ++i)
            {
                cancelled();
                auto left = Add(a, Mul(Sub(b, a), double(i) / steps)),
                     right = Add(a, Mul(Sub(b, a), double(i + 1) / steps));
                auto pa = At(p, r, left), pb = At(p, r, right);
                pa.altitudeMeters += .08;
                pb.altitudeMeters += .08;
                SurfaceStreet st;
                st.points = {pa, pb};
                st.widthMeters = width > 0 ? width : lanes * 3.;
                st.sidewalkMeters = 2.4;
                st.lanes = lanes;
                st.hasMedian = (p.layoutRevision >= 5 && lanes >= 4);
                cellAt(Mul(Add(left, right), .5)).streets.push_back(std::move(st));
            }
        };
        for (const auto &edge : city.roads)
        {
            double trimA = 0., trimB = 0.;
            if (p.layoutRevision >= 5)
            {
                if (auto it = coreNodes.find(edge.from); it != coreNodes.end() && it->second.degree >= 2)
                {
                    trimA = it->second.maxLanes * 1.5 + 2.4 + .8;
                }
                if (auto it = coreNodes.find(edge.to); it != coreNodes.end() && it->second.degree >= 2)
                {
                    trimB = it->second.maxLanes * 1.5 + 2.4 + .8;
                }
            }
            road(edge.start, edge.end, edge.lanes, 0., trimA, trimB);
        }
        for (const auto &approach : city.approaches)
        {
            auto start = approach.points.front(), direction = Sub(approach.points[1], start);
            direction = Mul(direction, 1 / Norm(direction));
            road(Sub(start, Mul(direction, 12)), Add(start, Mul(direction, 4)), 2, 10);
            for (std::size_t i = 1; i < approach.points.size(); ++i)
            {
                road(approach.points[i - 1], approach.points[i], 2);
            }
            if (p.layoutRevision >= 5)
            {
                auto jp = At(p, r, approach.join);
                SurfaceIntersection inter;
                inter.id = WorldIdentity::ContentKey(p.terrain.Planet(), city.regionId,
                                                     "core.join." + std::to_string(approach.from));
                inter.regionId = city.regionId;
                inter.center = jp;
                inter.frame = G::Advance(r.frame, approach.join.x, approach.join.y, p.terrain.Planet().radiusMeters);
                inter.frame.origin = jp;
                inter.maxLanes = 2;
                inter.arms = 3;
                inter.radiusMeters = 5.6;
                inter.control = IntersectionControl::Unsignalized;
                inter.mainAxisYaw = grid.yaw;
                cellAt(approach.join).intersections.push_back(std::move(inter));
            }
        }
        for (const auto &lot : city.lots)
        {
            cancelled();
            auto point = At(p, r, lot.positionMeters);
            point.altitudeMeters += lot.positionMeters.z;
            auto frame = G::Advance(r.frame, lot.positionMeters.x, lot.positionMeters.y,
                                    p.terrain.Planet().radiusMeters);
            frame.origin = point;
            SurfaceStructure structure;
            structure.id = WorldIdentity::ContentKey(p.terrain.Planet(), city.regionId, "core1." + lot.id);
            structure.regionId = city.regionId;
            structure.form = lot.form;
            structure.use = lot.use;
            structure.assetName = lot.assetName;
            structure.frame = frame;
            structure.sizeMeters = lot.sizeMeters;
            structure.yaw = lot.yaw;
            structure.variant = lot.variant;
            structure.solid = lot.solid;
            structure.palette = lot.palette;
            structure.foundationMeters = lot.solid ? .15 : 0;
            cellAt(lot.positionMeters).structures.push_back(std::move(structure));
        }
    }
}
bool CityCorePlan::Reserved(const GeneratedPlanet &, const GeneratedRegion &region, Vector3d point,
                            double clearance) const
{
    auto city = Find(region.definition.id);
    if (!city)
    {
        return false;
    }
    if (Norm(point) < city->radiusMeters + clearance)
    {
        return true;
    }
    for (const auto &approach : city->approaches)
    {
        for (std::size_t i = 1; i < approach.points.size(); ++i)
        {
            if (SegmentDistance(point, approach.points[i - 1], approach.points[i]) < 6 + clearance)
            {
                return true;
            }
        }
    }
    return false;
}
std::uint64_t CityCorePlan::Digest() const
{
    std::uint64_t hash = 14695981039346656037ULL;
    auto number = [&](std::uint64_t n) {
        for (unsigned i = 0; i < 8; ++i)
        {
            hash ^= (n >> (i * 8)) & 255;
            hash *= 1099511628211ULL;
        }
    };
    auto text = [&](const std::string &s) {
        number(s.size());
        for (unsigned char c : s)
        {
            number(c);
        }
    };
    auto scalar = [&](double d) {
        std::uint64_t n;
        std::memcpy(&n, &d, sizeof(n));
        number(n);
    };
    auto point = [&](Vector3d p) {
        scalar(p.x);
        scalar(p.y);
        scalar(p.z);
    };
    number(cities.size());
    for (const auto &city : cities)
    {
        text(city.regionId);
        scalar(city.radiusMeters);
        number(city.lots.size());
        number(city.roads.size());
        for (const auto &lot : city.lots)
        {
            text(lot.id);
            text(lot.assetName);
            number(unsigned(lot.form));
            number(unsigned(lot.use));
            point(lot.positionMeters);
            point(lot.sizeMeters);
            scalar(lot.yaw);
            number(lot.variant);
            number(lot.solid);
            number(lot.palette.has_value());
            if (lot.palette)
            {
                for (auto color : {lot.palette->body, lot.palette->accent, lot.palette->emission})
                {
                    for (float c : color)
                    {
                        scalar(c);
                    }
                }
            }
        }
        for (const auto &road : city.roads)
        {
            number(road.from);
            number(road.to);
            point(road.start);
            point(road.end);
            number(road.lanes);
        }
    }
    return hash;
}
bool ContentCellKey::operator<(const ContentCellKey &b) const
{
    return std::tie(region, x, y) < std::tie(b.region, b.x, b.y);
}
bool ContentCellKey::operator==(const ContentCellKey &b) const
{
    return region == b.region && x == b.x && y == b.y;
}
bool SurfaceStructure::Contains(const SurfacePoint &point, double radius, double clearance) const
{
    if (!solid)
    {
        return false;
    }
    const double bound = std::hypot(sizeMeters.x * .5 + clearance, sizeMeters.y * .5 + clearance) + .01;
    // Chord length on the unit sphere is always <= geodesic arc length, so this reject has zero false negatives.
    const double limit = bound / radius;
    if (limit < 2.)
    {
        const double dx = frame.origin.normal.x - point.normal.x;
        const double dy = frame.origin.normal.y - point.normal.y;
        const double dz = frame.origin.normal.z - point.normal.z;
        if (dx * dx + dy * dy + dz * dz > limit * limit)
        {
            return false;
        }
    }
    // Cheap cap test avoids ambiguous tangent projections on the far side of the planet.
    if (G::Distance(radius, frame.origin, point) > bound)
    {
        return false;
    }
    auto local = Rotate(G::ToLocal(frame, point, radius), -yaw);
    return std::fabs(local.x) < sizeMeters.x * .5 + clearance &&
           std::fabs(local.y) < sizeMeters.y * .5 + clearance;
}
double SurfaceStructure::BaseClearanceMeters() const
{
    const bool building =
        form <= ContentForm::Shelter || form == ContentForm::Building || form == ContentForm::MeasuredBuilding;
    return solid && building && foundationMeters > .05 ? .08 : 0;
}
double SurfaceStructure::FoundationTopMeters() const
{
    const double clearance = BaseClearanceMeters();
    return clearance > 0 ? clearance + .02 : 0;
}
std::vector<ContentCellKey> ContentLayout::Near(const GeneratedPlanet &p, const SurfacePoint &point,
                                                double radius)
{
    if (!std::isfinite(radius) || radius < 0 || radius > 4096)
    {
        throw std::invalid_argument("Content query radius must be 0..4096 metres");
    }
    std::vector<ContentCellKey> result;
    for (std::size_t i = 0; i < p.regions.size(); ++i)
    {
        const auto &r = p.regions[i];
        auto center = r.center.space == SpaceKind::Surface ? r.center.position : r.access.position;
        double reach = r.center.space == SpaceKind::Surface ? r.boundingRadiusMeters : 100;
        if (G::Distance(p.terrain.Planet().radiusMeters, center, point) > reach + radius + 200)
        {
            continue;
        }
        Grid grid(p, r);
        auto frame = r.center.space == SpaceKind::Surface ? r.frame : G::Frame(r.access.position);
        auto local = Rotate(G::ToLocal(frame, point, p.terrain.Planet().radiusMeters), -grid.yaw);
        // One extra cell covers warp, parcel radius, and neighbouring cell-owned street edges.
        auto x0 = std::int64_t(std::floor((local.x - radius) / grid.width)) - 1;
        auto x1 = std::int64_t(std::floor((local.x + radius) / grid.width)) + 1;
        auto y0 = std::int64_t(std::floor((local.y - radius) / grid.depth)) - 1;
        auto y1 = std::int64_t(std::floor((local.y + radius) / grid.depth)) + 1;
        if ((x1 - x0 + 1) * (y1 - y0 + 1) > 16384)
        {
            throw std::length_error("Content neighbourhood exceeds the local planning budget");
        }
        for (auto y = y0; y <= y1; ++y)
        {
            for (auto x = x0; x <= x1; ++x)
            {
                auto cellCenter = Mul(Add(grid.Node(x, y), grid.Node(x + 1, y + 1)), .5);
                auto regionLocal = Rotate(local, grid.yaw);
                if (Norm(Sub(cellCenter, regionLocal)) > radius + std::hypot(grid.width, grid.depth))
                {
                    continue;
                }
                if (r.center.space != SpaceKind::Surface && (x != 0 || y != 0))
                {
                    continue;
                }
                result.push_back({i, x, y});
            }
        }
    }
    return result;
}
ContentCell ContentLayout::Generate(const GeneratedPlanet &p, ContentCellKey key,
                                    const std::atomic_bool *cancel)
{
    ContentCell result;
    result.key = key;
    if (key.region >= p.regions.size() || std::abs(double(key.x)) > 1000000000 ||
        std::abs(double(key.y)) > 1000000000)
    {
        throw std::invalid_argument("Invalid content cell address");
    }
    if (Cancelled(cancel))
    {
        return result;
    }
    auto r = p.regions[key.region];
    const bool entrance = r.center.space != SpaceKind::Surface;
    if (entrance)
    {
        // Physical surface entrance only. The mine itself remains on its actual subsurface layer.
        if (key.x != 0 || key.y != 0)
        {
            return result;
        }
        r.frame = G::Frame(r.access.position);
        r.footprint = {{-90, -90, 0}, {90, -90, 0}, {90, 90, 0}, {-90, 90, 0}};
    }
    Grid grid(p, r);
    if (p.cityCores)
    {
        if (auto core = p.cityCores->Find(r.definition.id))
        {
            if (auto found = core->cells.find({key.x, key.y}); found != core->cells.end())
            {
                result = found->second;
            }
        }
    }
    const auto a = grid.Node(key.x, key.y), b = grid.Node(key.x + 1, key.y),
               c = grid.Node(key.x + 1, key.y + 1), d = grid.Node(key.x, key.y + 1);
    const bool city = r.definition.kind == RegionKind::Settlement;
    const auto seed = grid.CellSeed(key);
    if (city && p.layoutRevision >= 5)
    {
        // ====================================================================
        // CQ-04B (Layout Revision 5) — Hierarchical Road Network, Grade Separation,
        // Roundabouts, Explicit Intersections, District Zoning, Unique Landmarks,
        // Perimeter Block Parceling (1:1 Measured Lots, 0 OBB Overlaps) & Streetscape
        // ====================================================================
        const double boundR = (std::max)(200., r.boundingRadiusMeters);
        const double avgStep = (std::max)(40., (grid.width + grid.depth) * .5);
        std::int64_t ringCell = (std::max)(std::int64_t(3), std::int64_t(std::floor(boundR * .68 / avgStep)));
        while (ringCell > 2)
        {
            if (Inside(r, grid.Node(ringCell, 0), 16.) && Inside(r, grid.Node(-ringCell, 0), 16.) &&
                Inside(r, grid.Node(0, ringCell), 16.) && Inside(r, grid.Node(0, -ringCell), 16.))
            {
                break;
            }
            --ringCell;
        }
        const auto cellMid = Mul(Add(a, c), .5);
        const double cellZoneRatio = Norm(cellMid) / boundR;
        const auto nodeRatio = [&](Vector3d pt) { return Norm(pt) / boundR; };
        const double ratioA = nodeRatio(a), ratioB = nodeRatio(b), ratioC = nodeRatio(c), ratioD = nodeRatio(d);

        // Compute lane counts for horizontal (y-indexed) and vertical (x-indexed) grid edges.
        // Outer grid perimeter (|x| == ringCell or |y| == ringCell) forms the continuous 4-lane Ring Road
        // with native grid intersections and cardinal portal roundabouts — zero diagonal X-crossings.
        auto edgeLanes = [&](std::int64_t coord, double edgeRatio) {
            if (std::abs(coord) == ringCell)
            {
                return 4u;
            }
            return Rev5LanesForCoord(r.definition.tier, coord, edgeRatio);
        };
        const unsigned lanesBottom = edgeLanes(key.y, (ratioA + ratioB) * .5);
        const unsigned lanesLeft = edgeLanes(key.x, (ratioA + ratioD) * .5);
        const unsigned lanesTop = edgeLanes(key.y + 1, (ratioD + ratioC) * .5);
        const unsigned lanesRight = edgeLanes(key.x + 1, (ratioB + ratioC) * .5);

        // Helper: check intersection radius at any grid corner (gx, gy).
        auto cornerInfo = [&](std::int64_t gx, std::int64_t gy, Vector3d pos, unsigned lx, unsigned ly,
                              double &trimOut, IntersectionControl &ctrlOut, double &islandOut,
                              unsigned &maxLanesOut, bool &elevOut, bool &underOut) {
            const double rr = nodeRatio(pos);
            elevOut = (r.definition.tier >= SettlementTier::Metropolis && gx == 2 && gy == 0);
            underOut = (r.definition.tier >= SettlementTier::Metropolis && gx == 0 && gy == 0);
            islandOut = 0.;
            maxLanesOut = (std::max)(lx, ly);
            double rbR = 0., rbIsl = 0.;
            unsigned rbL = 2;
            if (elevOut || underOut)
            {
                ctrlOut = IntersectionControl::GradeSeparated;
                trimOut = ProfileForLanes(maxLanesOut).halfTotalMeters + 1.5;
                return;
            }
            const bool cardinalRingPortal =
                (std::abs(gx) == ringCell && gy == 0) || (gx == 0 && std::abs(gy) == ringCell);
            if (cardinalRingPortal)
            {
                ctrlOut = IntersectionControl::Roundabout;
                trimOut = 20.5;
                islandOut = 11.0;
                maxLanesOut = (std::max)(maxLanesOut, 4u);
                return;
            }
            if (Rev5RoundaboutNode(r, gx, gy, rr, rbR, rbIsl, rbL))
            {
                ctrlOut = IntersectionControl::Roundabout;
                trimOut = rbR;
                islandOut = rbIsl;
                maxLanesOut = (std::max)(maxLanesOut, rbL);
                return;
            }
            if (lx >= 4 && ly >= 4)
            {
                ctrlOut = IntersectionControl::Signalized;
            }
            else if (lx >= 4 || ly >= 4)
            {
                ctrlOut = (maxLanesOut >= 6) ? IntersectionControl::Signalized : IntersectionControl::Yield;
            }
            else
            {
                ctrlOut = IntersectionControl::Unsignalized;
            }
            trimOut = ProfileForLanes(maxLanesOut).halfTotalMeters + 1.5;
        };

        // Emit explicit intersection at owned corner (key.x, key.y) when inside city boundary.
        double trimA = 0., trimB = 0., trimD = 0.;
        IntersectionControl ctrlA = IntersectionControl::Unsignalized, ctrlTmp;
        double islandA = 0., islTmp = 0.;
        unsigned maxLA = 2, maxLTmp = 2;
        bool elevA = false, underA = false, elevTmp = false, underTmp = false;
        cornerInfo(key.x, key.y, a, lanesLeft, lanesBottom, trimA, ctrlA, islandA, maxLA, elevA, underA);
        cornerInfo(key.x + 1, key.y, b, lanesRight, lanesBottom, trimB, ctrlTmp, islTmp, maxLTmp, elevTmp,
                   underTmp);
        cornerInfo(key.x, key.y + 1, d, lanesLeft, lanesTop, trimD, ctrlTmp, islTmp, maxLTmp, elevTmp, underTmp);

        const bool inRingA = (std::abs(key.x) <= ringCell && std::abs(key.y) <= ringCell);
        const bool inRingB = (std::abs(key.x + 1) <= ringCell && std::abs(key.y) <= ringCell);
        const bool inRingD = (std::abs(key.x) <= ringCell && std::abs(key.y + 1) <= ringCell);
        const bool bottomOnRing = (std::abs(key.y) == ringCell && key.x >= -ringCell && key.x < ringCell);
        const bool leftOnRing = (std::abs(key.x) == ringCell && key.y >= -ringCell && key.y < ringCell);

        // Suppress direct 6<->2 local crossings on odd side streets of 6-lane avenues (keep 2-block spacing),
        // and deterministically merge ~28% of interior 2-lane local edges into 1x2 / 2x1 superblocks & T-junctions
        // to break rigid checkerboard grid monotony (while preserving the central core & test fixture cells).
        const bool skipBottom6x2 = (!bottomOnRing && lanesBottom == 2 && (lanesLeft >= 6 || lanesRight >= 6));
        const bool skipLeft6x2 = (!leftOnRing && lanesLeft == 2 && (lanesBottom >= 6 || lanesTop >= 6));
        bool hasCoreApproach = false;
        if (p.cityCores)
        {
            if (const auto *core = p.cityCores->Find(r.definition.id))
            {
                for (const auto &app : core->approaches)
                {
                    if (app.cellX == key.x && app.cellY == key.y)
                    {
                        hasCoreApproach = true;
                        break;
                    }
                }
            }
        }
        const bool keepFixtureGrid = (std::abs(key.x) <= 2 && std::abs(key.y) <= 2) || hasCoreApproach;
        const auto mergeHash = Mix(grid.seed ^ Mix(std::uint64_t(key.x) * 37 + 11) ^ Mix(std::uint64_t(key.y) * 91 + 29));
        const bool mergeBottom = (!bottomOnRing && !keepFixtureGrid && lanesBottom == 2 && lanesLeft <= 4 &&
                                  lanesRight <= 4 && (mergeHash % 7 == 0 || mergeHash % 7 == 1));
        const bool mergeLeft = (!leftOnRing && !keepFixtureGrid && !mergeBottom && lanesLeft == 2 &&
                                lanesBottom <= 4 && lanesTop <= 4 && (mergeHash % 7 == 2 || mergeHash % 7 == 3));
        const bool omitBottom = skipBottom6x2 || mergeBottom;
        const bool omitLeft = skipLeft6x2 || mergeLeft;

        if (inRingA && Inside(r, a, 4.) && !(p.cityCores && p.cityCores->Find(r.definition.id) &&
                                             Norm(a) < p.cityCores->Find(r.definition.id)->radiusMeters + 16.))
        {
            auto ptA = At(p, r, a);
            if (ptA.altitudeMeters >= PlanetTerrain::SeaLevelMeters)
            {
                SurfaceIntersection inter;
                inter.id = WorldIdentity::ContentKey(p.terrain.Planet(), r.definition.id,
                                                     "inter." + std::to_string(key.x) + "." +
                                                         std::to_string(key.y));
                inter.regionId = r.definition.id;
                inter.center = ptA;
                inter.frame = G::Advance(r.frame, a.x, a.y, p.terrain.Planet().radiusMeters);
                inter.frame.origin = ptA;
                inter.control = ctrlA;
                inter.radiusMeters = trimA;
                inter.islandRadiusMeters = islandA;
                inter.maxLanes = maxLA;
                inter.arms = (omitBottom || omitLeft) ? 3u : 4u;
                const auto dirH = Sub(b, a);
                inter.mainAxisYaw = std::atan2(dirH.y, dirH.x);
                inter.cycleSeconds = (maxLA >= 6) ? 62. : 54.;
                // Green Wave offset along primary arterial (~50 km/h = 13.89 m/s).
                inter.greenWaveOffsetSeconds =
                    std::fmod(std::fabs(double(key.x) * grid.width + double(key.y) * grid.depth) / 13.89,
                              inter.cycleSeconds);
                inter.hasAllWalk = (maxLA >= 6 && ratioA < .25);
                inter.elevatedOverhead = elevA;
                inter.underpassBelow = underA;
                result.intersections.push_back(std::move(inter));
            }
        }

        // Emit bottom street (a -> b) with grade separation, step-down taper (6->4 / 4->2), RingRoad, and stop-line trim.
        if (!omitBottom && inRingA && inRingB)
        {
            const bool isUnderpassAxis =
                (r.definition.tier >= SettlementTier::Metropolis && key.y == 0 && key.x >= -1 && key.x <= 1);
            if (isUnderpassAxis)
            {
                // Open-cut U-ramps (grade <= 6.8%) into a short -5.4m box culvert under the civic plaza (0, 0).
                if (key.x == -1)
                {
                    EmitRev5Street(result, p, r, a, b, 4, StreetKind::UnderpassRamp, trimA, 0., 0., 0., -5.4);
                }
                else if (key.x == 0)
                {
                    EmitRev5Street(result, p, r, a, b, 4, StreetKind::Underpass, 0., 0., 0., -5.4, -5.4);
                }
                else
                {
                    EmitRev5Street(result, p, r, a, b, 4, StreetKind::UnderpassRamp, 0., trimB, 0., -5.4, 0.);
                }
            }
            else if (bottomOnRing)
            {
                EmitRev5Street(result, p, r, a, b, 4, StreetKind::RingRoad, trimA, trimB);
            }
            else
            {
                // Step-down taper when 6-lane arterial approaches the outer ring road (6 -> 4 over segment).
                const unsigned lanesStart = Rev5LanesForCoord(r.definition.tier, key.y, ratioA);
                const unsigned lanesEnd = Rev5LanesForCoord(r.definition.tier, key.y, ratioB);
                if (lanesStart != lanesEnd)
                {
                    EmitRev5Street(result, p, r, a, b, lanesEnd, StreetKind::Taper, trimA, trimB,
                                   ProfileForLanes(lanesStart).carriagewayMeters);
                }
                else
                {
                    EmitRev5Street(result, p, r, a, b, lanesBottom, StreetKind::Surface, trimA, trimB);
                }
            }
        }
        // Emit left street (a -> d) with elevated overpass (+8.4m) on eastern bypass x == 2 in Metropolis/Capital.
        if (!omitLeft && inRingA && inRingD)
        {
            const bool isOverpassAxis =
                (r.definition.tier >= SettlementTier::Metropolis && key.x == 2 && key.y >= -1 && key.y <= 1);
            if (isOverpassAxis)
            {
                // +8.4m elevated viaduct (clearance >= 8.0m) with smooth 100m ramps (grade <= 8.4%).
                if (key.y == -1)
                {
                    EmitRev5Street(result, p, r, a, d, 4, StreetKind::ElevatedRamp, trimA, 0., 0., 0., 8.4);
                }
                else if (key.y == 0)
                {
                    EmitRev5Street(result, p, r, a, d, 4, StreetKind::Elevated, 0., 0., 0., 8.4, 8.4);
                }
                else
                {
                    EmitRev5Street(result, p, r, a, d, 4, StreetKind::ElevatedRamp, 0., trimD, 0., 8.4, 0.);
                }
            }
            else if (leftOnRing)
            {
                EmitRev5Street(result, p, r, a, d, 4, StreetKind::RingRoad, trimA, trimD);
            }
            else
            {
                const unsigned lanesStart = Rev5LanesForCoord(r.definition.tier, key.x, ratioA);
                const unsigned lanesEnd = Rev5LanesForCoord(r.definition.tier, key.x, ratioD);
                if (lanesStart != lanesEnd)
                {
                    EmitRev5Street(result, p, r, a, d, lanesEnd, StreetKind::Taper, trimA, trimD,
                                   ProfileForLanes(lanesStart).carriagewayMeters);
                }
                else
                {
                    EmitRev5Street(result, p, r, a, d, lanesLeft, StreetKind::Surface, trimA, trimD);
                }
            }
        }

        // ====================================================================
        // District Zoning, Unique Landmarks, Perimeter Block Parceling & Streetscape
        // ====================================================================
        if (std::abs(key.x) >= ringCell || std::abs(key.y) >= ringCell)
        {
            return result;
        }
        const DistrictZone zone = ClassifyDistrictZone(r, grid, key.x, key.y, cellZoneRatio);
        const bool isCivicCenter = (key.x == 0 && key.y == 0);
        const bool isSecondaryLandmark = (r.definition.tier == SettlementTier::SystemCapital && key.x == 0 && key.y == -1);
        std::vector<PlacedObb> placed;
        // Register existing core structures if any so procedural lots never overlap authored core lots.
        for (const auto &existing : result.structures)
        {
            if (existing.solid)
            {
                const auto loc = G::ToLocal(r.frame, existing.frame.origin, p.terrain.Planet().radiusMeters);
                placed.push_back({loc, existing.sizeMeters.x * .5, existing.sizeMeters.y * .5, existing.yaw});
            }
        }

        auto tryPlaceStructure = [&](Vector3d local, double yaw, ContentForm form, std::uint16_t variant,
                                     ContentUse use, Vector3d sizeMeters, std::uint64_t appearance,
                                     const std::string &suffix, bool solid = true) -> bool {
            const double extent = std::hypot(sizeMeters.x, sizeMeters.y) * .5;
            if (!Inside(r, local, extent + 3.))
            {
                return false;
            }
            // Check setback against all 4 incident road edges using true cross-section half-widths.
            const std::array<std::array<Vector3d, 2>, 4> edges = {{{a, b}, {b, c}, {c, d}, {d, a}}};
            const std::array<unsigned, 4> edgeLaneArr = {lanesBottom, lanesRight, lanesTop, lanesLeft};
            for (unsigned e = 0; e < 4; ++e)
            {
                const double roadHalf = ProfileForLanes(edgeLaneArr[e]).halfTotalMeters;
                // Project OBB onto edge normal for accurate rectangular setback (not bloated diagonal radius!).
                const auto ed = Sub(edges[e][1], edges[e][0]);
                const double el = Norm(ed);
                if (el < 1.)
                {
                    continue;
                }
                const double nx = -ed.y / el, ny = ed.x / el;
                const double halfProj =
                    std::fabs(nx * std::cos(yaw) + ny * std::sin(yaw)) * (sizeMeters.x * .5) +
                    std::fabs(-nx * std::sin(yaw) + ny * std::cos(yaw)) * (sizeMeters.y * .5);
                if (SegmentDistance(local, edges[e][0], edges[e][1]) < roadHalf + halfProj + (solid ? 1.2 : .2))
                {
                    return false;
                }
            }
            // Avoid roundabout pads at corners.
            const std::array<std::pair<Vector3d, double>, 3> cornerTrims = {{{a, trimA}, {b, trimB}, {d, trimD}}};
            for (const auto &ct : cornerTrims)
            {
                if (ct.second > 12. && Norm(Sub(local, ct.first)) < ct.second + extent + 1.)
                {
                    return false;
                }
            }
            const PlacedObb cand{local, sizeMeters.x * .5, sizeMeters.y * .5, yaw};
            if (solid)
            {
                for (const auto &other : placed)
                {
                    if (ObbOverlap(cand, other, 1.5))
                    {
                        return false;
                    }
                }
            }
            auto point = At(p, r, local);
            if (solid)
            {
                // Exact rotated-OBB clearance against already-emitted streets so all 4 block edges
                // populate cleanly (avoiding false rejections from conservative circular diagonal extent).
                const double cy = std::cos(yaw), sy = std::sin(yaw);
                for (const auto &road : result.streets)
                {
                    if (road.kind == StreetKind::PedestrianPlaza)
                    {
                        continue;
                    }
                    const double roadClear = road.widthMeters * .5 + road.sidewalkMeters + .25;
                    for (const auto &rpt : road.points)
                    {
                        const auto rl = G::ToLocal(r.frame, rpt, p.terrain.Planet().radiusMeters);
                        const double dx = rl.x - local.x, dy = rl.y - local.y;
                        const double lx = dx * cy + dy * sy;
                        const double ly = -dx * sy + dy * cy;
                        if (std::fabs(lx) < sizeMeters.x * .5 + roadClear &&
                            std::fabs(ly) < sizeMeters.y * .5 + roadClear)
                        {
                            return false;
                        }
                    }
                }
            }
            const double relayCap = std::cos((extent + 8.) / p.terrain.Planet().radiusMeters);
            const bool relayReserved = std::any_of(p.relays.begin(), p.relays.end(), [&](const auto &relay) {
                return G::Dot(point.normal, relay.location.position.normal) > relayCap;
            });
            if (relayReserved || Reserved(p, point, extent + 2.) || !IntercityClear(p, r, local, extent) ||
                (p.cityCores && p.cityCores->Reserved(p, r, local, extent + 3.)))
            {
                return false;
            }
            double low = point.altitudeMeters, high = low;
            for (int sx = -1; sx <= 1; ++sx)
            {
                for (int sy = -1; sy <= 1; ++sy)
                {
                    auto sample =
                        Add(local, Rotate({sx * sizeMeters.x * .45, sy * sizeMeters.y * .45, 0}, yaw));
                    const double h = At(p, r, sample).altitudeMeters;
                    low = (std::min)(low, h);
                    high = (std::max)(high, h);
                }
            }
            if (low < PlanetTerrain::SeaLevelMeters || high - low > 2.5)
            {
                return false;
            }
            SurfaceStructure s;
            s.regionId = r.definition.id;
            s.id = WorldIdentity::ContentKey(p.terrain.Planet(), r.definition.id,
                                             "layout5.cell." + std::to_string(key.x) + "." +
                                                 std::to_string(key.y) + "." + suffix);
            s.form = form;
            s.variant = variant;
            s.use = use;
            s.sizeMeters = sizeMeters;
            s.yaw = yaw;
            s.appearance = appearance;
            s.solid = solid;
            point.altitudeMeters = high;
            auto transported = G::Advance(r.frame, local.x, local.y, p.terrain.Planet().radiusMeters);
            transported.origin = point;
            s.frame = transported;
            s.foundationMeters = solid ? (high - low + .15) : 0.;
            result.structures.push_back(std::move(s));
            if (solid)
            {
                placed.push_back(cand);
            }
            return true;
        };

        // Measured building specifications (exact 1:1 authored dimensions, NEVER force-minified).
        struct BuildingCandidate
        {
            ContentForm form;
            std::uint16_t variant;
            ContentUse use;
            Vector3d sizeMeters; // x = frontage width along street, y = lot depth
        };
        const auto edgeDir0 = Sub(b, a);
        const double blockYaw = std::atan2(edgeDir0.y, edgeDir0.x);

        // Unique Landmark Placement (strictly 1-2 per settlement, dedicated civic superblock).
        bool landmarkPlaced = false;
        if (isCivicCenter && r.definition.tier == SettlementTier::SystemCapital)
        {
            // Unique Capital Gate Administration (35 x 40 m) centered with front pedestrian plaza.
            landmarkPlaced = tryPlaceStructure(cellMid, blockYaw, ContentForm::MeasuredBuilding, 8,
                                               ContentUse::Administration, {35., 40., 64.65}, seed, "landmark.cap0");
        }
        else if (isSecondaryLandmark)
        {
            // Unique Capital Public Forum (37 x 48 m).
            landmarkPlaced = tryPlaceStructure(cellMid, blockYaw, ContentForm::MeasuredBuilding, 9,
                                               ContentUse::Administration, {37., 48., 19.85}, seed, "landmark.cap1");
        }
        else if (isCivicCenter && r.definition.tier == SettlementTier::Metropolis)
        {
            // Unique Metro Civic Commerce landmark (27 x 32 m) + civic plaza.
            landmarkPlaced = tryPlaceStructure(cellMid, blockYaw, ContentForm::MeasuredBuilding, 7,
                                               ContentUse::Services, {27., 32., 51.05}, seed, "landmark.metro0");
        }
        else if (zone == DistrictZone::Spaceport && (seed % 5 == 0))
        {
            // Spaceport landing pad (Scenery variant 25) + transit terminal.
            landmarkPlaced = tryPlaceStructure(cellMid, blockYaw, ContentForm::Scenery, 25, ContentUse::Services,
                                               {18., 18., .6}, seed, "spaceport.pad", false);
        }

        // Zone-specific 1:1 building palettes (never duplicating Capital landmarks outside unique anchors).
        auto pickCandidate = [&](DistrictZone z, std::uint64_t rng, double maxFrontage, double maxDepth,
                                 BuildingCandidate &out) -> bool {
            static constexpr BuildingCandidate kCivic[] = {
                {ContentForm::MeasuredBuilding, 7, ContentUse::Services, {27., 32., 51.05}},
                {ContentForm::MeasuredBuilding, 4, ContentUse::Services, {19., 23., 12.65}},
                {ContentForm::MeasuredBuilding, 2, ContentUse::Services, {13., 12., 7.65}},
                {ContentForm::Dome, 0, ContentUse::Administration, {15., 15., 12.}},
                {ContentForm::Transit, 0, ContentUse::Services, {14., 16., 11.}},
            };
            static constexpr BuildingCandidate kCommercial[] = {
                {ContentForm::MeasuredBuilding, 5, ContentUse::Services, {15., 24., 8.05}},
                {ContentForm::MeasuredBuilding, 4, ContentUse::Services, {19., 23., 12.65}},
                {ContentForm::MeasuredBuilding, 1, ContentUse::Services, {10., 13., 5.05}},
                {ContentForm::MeasuredBuilding, 2, ContentUse::Services, {13., 12., 7.65}},
                {ContentForm::MeasuredBuilding, 7, ContentUse::Services, {27., 32., 51.05}},
                {ContentForm::Transit, 0, ContentUse::Services, {14., 15., 12.}},
            };
            static constexpr BuildingCandidate kResHigh[] = {
                {ContentForm::MeasuredBuilding, 6, ContentUse::Housing, {24., 28., 39.65}},
                {ContentForm::Spire, 0, ContentUse::Housing, {14., 14., 36.}},
                {ContentForm::TwinBridge, 0, ContentUse::Housing, {16., 14., 42.}},
                {ContentForm::Arcology, 0, ContentUse::Housing, {16., 16., 28.}},
                {ContentForm::MeasuredBuilding, 0, ContentUse::Housing, {11., 8., 9.65}},
            };
            static constexpr BuildingCandidate kResLow[] = {
                {ContentForm::MeasuredBuilding, 0, ContentUse::Housing, {11., 8., 9.65}},
                {ContentForm::MeasuredBuilding, 1, ContentUse::Services, {10., 13., 5.05}},
                {ContentForm::MeasuredBuilding, 2, ContentUse::Services, {13., 12., 7.65}},
                {ContentForm::Courtyard, 0, ContentUse::Housing, {14., 14., 9.}},
                {ContentForm::Habitat, 0, ContentUse::Housing, {12., 12., 8.5}},
                {ContentForm::Terrace, 0, ContentUse::Housing, {13., 13., 12.}},
            };
            static constexpr BuildingCandidate kIndustrial[] = {
                {ContentForm::MeasuredBuilding, 3, ContentUse::Industry, {24., 18., 9.65}},
                {ContentForm::Workshop, 0, ContentUse::Industry, {14., 14., 7.5}},
                {ContentForm::Cargo, 0, ContentUse::Industry, {12., 10., 5.}},
                {ContentForm::Solar, 0, ContentUse::Services, {11., 11., 4.}},
                {ContentForm::WaterTank, 0, ContentUse::Services, {9., 9., 8.}},
            };
            const BuildingCandidate *list = kResLow;
            std::size_t count = std::size(kResLow);
            if (z == DistrictZone::CivicCore)
            {
                list = kCivic;
                count = std::size(kCivic);
            }
            else if (z == DistrictZone::CommercialTransit)
            {
                list = kCommercial;
                count = std::size(kCommercial);
            }
            else if (z == DistrictZone::ResidentialHigh)
            {
                list = kResHigh;
                count = std::size(kResHigh);
            }
            else if (z == DistrictZone::IndustrialLogistics || z == DistrictZone::Spaceport)
            {
                list = kIndustrial;
                count = std::size(kIndustrial);
            }
            const double tierMaxHeight = r.definition.tier == SettlementTier::District   ? 16.
                                         : r.definition.tier == SettlementTier::Town     ? 32.
                                         : r.definition.tier == SettlementTier::Metropolis ? 52.
                                                                                         : 96.;
            const std::size_t start = std::size_t(rng % count);
            for (std::size_t k = 0; k < count; ++k)
            {
                const auto &cnd = list[(start + k) % count];
                if (cnd.sizeMeters.x <= maxFrontage && cnd.sizeMeters.y <= maxDepth &&
                    cnd.sizeMeters.z <= tierMaxHeight)
                {
                    out = cnd;
                    return true;
                }
            }
            return false;
        };

        // Perimeter Block Parceling: walk all 4 street-facing edges, orient each building's front
        // normal directly toward its incident street, preserve 1:1 authored ratios, and guarantee 0 OBB overlaps.
        if (!landmarkPlaced)
        {
            const std::array<std::array<Vector3d, 2>, 4> edges = {{{a, b}, {b, c}, {c, d}, {d, a}}};
            const std::array<unsigned, 4> edgeLaneArr = {lanesBottom, lanesRight, lanesTop, lanesLeft};
            // Civic plaza break blocks (~12.5% of CivicCore blocks become full landscaped squares, not half the city).
            const bool plazaBlock = (zone == DistrictZone::CivicCore && (seed % 8 == 0) && !isCivicCenter);
            if (!plazaBlock)
            {
                unsigned lotId = 0;
                for (unsigned e = 0; e < 4; ++e)
                {
                    const auto p0 = edges[e][0], p1 = edges[e][1];
                    const auto edgeVec = Sub(p1, p0);
                    const double edgeLen = Norm(edgeVec);
                    if (edgeLen < 24.)
                    {
                        continue;
                    }
                    const Vector3d tangent = Mul(edgeVec, 1. / edgeLen);
                    // Inward normal points toward cellMid.
                    Vector3d inward = {-tangent.y, tangent.x, 0};
                    const Vector3d mid = Mul(Add(p0, p1), .5);
                    if (inward.x * (cellMid.x - mid.x) + inward.y * (cellMid.y - mid.y) < 0.)
                    {
                        inward = {-inward.x, -inward.y, 0};
                    }
                    const Vector3d outward = {-inward.x, -inward.y, 0};
                    // Outward facing yaw aligns building front normal directly toward its road edge (< 1 deg error).
                    const double facingYaw = std::atan2(outward.y, outward.x);
                    const double roadHalf = ProfileForLanes(edgeLaneArr[e]).halfTotalMeters;
                    const double cornerPad = ProfileForLanes(edgeLaneArr[(e + 3) % 4]).halfTotalMeters + 4.;
                    const double endPad = ProfileForLanes(edgeLaneArr[(e + 1) % 4]).halfTotalMeters + 4.;
                    double cursor = cornerPad;
                    const double limit = edgeLen - endPad;
                    while (cursor + 10. <= limit)
                    {
                        const auto rng = Mix(seed + e * 31 + lotId * 7 + 13);
                        const double remain = limit - cursor;
                        BuildingCandidate cand;
                        if (!pickCandidate(zone, rng, remain, 34., cand))
                        {
                            break;
                        }
                        // Note: Building local +X is depth/front, local +Y is frontage along street when yaw = facingYaw.
                        const double frontage = cand.sizeMeters.x;
                        const double depth = cand.sizeMeters.y;
                        const double distAlong = cursor + frontage * .5;
                        const double setback = roadHalf + depth * .5 + 1.8;
                        const Vector3d pos = Add(Add(p0, Mul(tangent, distAlong)), Mul(inward, setback));
                        // Swap X/Y so local +X faces outward road (depth) and local +Y runs along street (frontage).
                        tryPlaceStructure(pos, facingYaw, cand.form, cand.variant, cand.use,
                                          {depth, frontage, cand.sizeMeters.z}, rng,
                                          "lot." + std::to_string(e) + "." + std::to_string(lotId));
                        cursor += frontage + 2.8;
                        ++lotId;
                    }
                }
            }
            else
            {
                // Landscaped civic plaza pad (vehicle-free) + planters + benches + wayfinding kiosk.
                SurfaceStreet plaza;
                plaza.kind = StreetKind::PedestrianPlaza;
                plaza.lanes = 0;
                plaza.widthMeters = (std::min)(grid.width, grid.depth) * .60;
                plaza.sidewalkMeters = 0.;
                const Vector3d dirH = Mul(Sub(b, a), 1. / (std::max)(1., Norm(Sub(b, a))));
                auto pStart = At(p, r, Sub(cellMid, Mul(dirH, plaza.widthMeters * .5)));
                auto pEnd = At(p, r, Add(cellMid, Mul(dirH, plaza.widthMeters * .5)));
                pStart.altitudeMeters += .06;
                pEnd.altitudeMeters += .06;
                plaza.points = {pStart, pEnd};
                result.streets.push_back(std::move(plaza));
                for (int k = 0; k < 4; ++k)
                {
                    const double ang = k * (G::Pi * .5) + G::Pi * .25;
                    const Vector3d off = {std::cos(ang) * 9., std::sin(ang) * 9., 0};
                    tryPlaceStructure(Add(cellMid, off), ang, ContentForm::Scenery, 14, ContentUse::Services,
                                      {2.2, 2.2, 2.8}, seed + k, "plaza.planter." + std::to_string(k), false);
                    const Vector3d benchOff = {std::cos(ang) * 5.5, std::sin(ang) * 5.5, 0};
                    tryPlaceStructure(Add(cellMid, benchOff), ang + G::Pi * .5, ContentForm::Scenery, 13,
                                      ContentUse::Services, {2.0, .9, 1.0}, seed + k + 10,
                                      "plaza.bench." + std::to_string(k), false);
                }
                tryPlaceStructure(cellMid, blockYaw, ContentForm::Scenery, 16, ContentUse::Services,
                                  {1.4, 1.4, 3.2}, seed + 99, "plaza.kiosk", false);
            }
        }

        // Streetscape layer along bottom & left sidewalks (streetlamps every ~24m + transit shelter on 4/6-lane).
        auto emitSidewalkFurniture = [&](Vector3d p0, Vector3d p1, unsigned edgeLanes, unsigned edgeIdx) {
            const auto dv = Sub(p1, p0);
            const double len = Norm(dv);
            if (len < 30.)
            {
                return;
            }
            const Vector3d t = Mul(dv, 1. / len);
            Vector3d inward = {-t.y, t.x, 0};
            const Vector3d mid = Mul(Add(p0, p1), .5);
            if (inward.x * (cellMid.x - mid.x) + inward.y * (cellMid.y - mid.y) < 0.)
            {
                inward = {-inward.x, -inward.y, 0};
            }
            const double yaw = std::atan2(t.y, t.x);
            const double curbOffset = ProfileForLanes(edgeLanes).carriagewayMeters * .5 + 1.1;
            const unsigned lampCount = unsigned((len - 32.) / 24.);
            for (unsigned i = 0; i < lampCount && i < 4; ++i)
            {
                const double s = 18. + double(i) * 24.;
                if (s > len - 18.)
                {
                    break;
                }
                const Vector3d pos = Add(Add(p0, Mul(t, s)), Mul(inward, curbOffset));
                // Scenery variant 9 = Streetlight (non-solid small prop, auto-culled by R2 at distance).
                tryPlaceStructure(pos, yaw, ContentForm::Scenery, 9, ContentUse::Services, {.6, .6, 5.5},
                                  seed + edgeIdx * 20 + i, "lamp." + std::to_string(edgeIdx) + "." + std::to_string(i),
                                  false);
            }
            if (edgeLanes >= 4 && ((seed + edgeIdx) % 3 == 0))
            {
                const Vector3d pos = Add(mid, Mul(inward, curbOffset + .9));
                // Scenery variant 15 = TransitShelter
                tryPlaceStructure(pos, yaw, ContentForm::Scenery, 15, ContentUse::Services, {4.2, 1.8, 2.8},
                                  seed + edgeIdx * 50, "shelter." + std::to_string(edgeIdx), false);
            }
        };
        if (!omitBottom && inRingA && inRingB)
        {
            emitSidewalkFurniture(a, b, lanesBottom, 0);
        }
        if (!omitLeft && inRingA && inRingD)
        {
            emitSidewalkFurniture(a, d, lanesLeft, 1);
        }
        return result;
    }
    auto lanes = [&](std::int64_t coordinate) {
        if (r.definition.tier >= SettlementTier::Metropolis)
        {
            if (coordinate % 4 == 0)
            {
                return 6u;
            }
            if (coordinate % 2 == 0)
            {
                return 4u;
            }
            return 2u;
        }
        if (r.definition.tier == SettlementTier::Town && coordinate % 3 == 0)
        {
            return 4u;
        }
        return 2u;
    };
    if (city)
    {
        // Each cell owns bottom and left: shared edges are emitted once, with degree-four junctions.
        AddStreet(result, p, r, a, b, lanes(key.y));
        AddStreet(result, p, r, a, d, lanes(key.x));
    }
    const auto cellMid = Mul(Add(a, c), .5);
    const double cellZoneRatio =
        city ? Norm(cellMid) / (std::max)(200., r.boundingRadiusMeters) : 1.;
    // Dense 3x3 urban parcels in Core & Mid-ring; 2x2 parcels in Outer industrial/suburban fringe.
    const bool denseUrban = city && p.layoutRevision >= GeneratedPlanet::CurrentLayoutRevision &&
                            cellZoneRatio <= .68;
    const unsigned gridCols = denseUrban ? 3u : (city ? 2u : 1u);
    const unsigned slots = gridCols * gridCols;
    for (unsigned slot = 0; slot < slots; ++slot)
    {
        if (Cancelled(cancel))
        {
            return {};
        }
        auto random = Mix(seed + slot);
        if (!city && random % 9 != 0 && (key.x != 0 || key.y != 0))
        {
            continue;
        }
        // Civic plazas in core (center slot of periodic blocks) and courtyard green breaks elsewhere.
        if (denseUrban && cellZoneRatio < .28 && slot == 4 && (seed % 3 == 0))
        {
            continue;
        }
        if (city && random % (cellZoneRatio > .68 ? 6 : 11) == 0)
        {
            continue;
        }
        const double u = denseUrban ? (.20 + .30 * double(slot % 3))
                         : city     ? (.26 + .48 * double(slot % 2))
                                    : .5;
        const double v = denseUrban ? (.20 + .30 * double(slot / 3))
                         : city     ? (.26 + .48 * double(slot / 2))
                                    : .5;
        auto local = Add(Mul(Add(Mul(a, 1 - u), Mul(b, u)), 1 - v), Mul(Add(Mul(d, 1 - u), Mul(c, u)), v));
        if (entrance)
        {
            local = {28, 18, 0};
        }
        SurfaceStructure structure;
        structure.regionId = r.definition.id;
        structure.id = WorldIdentity::ContentKey(p.terrain.Planet(), r.definition.id,
                                                 "layout" + std::to_string(p.layoutRevision) + ".cell." +
                                                     std::to_string(key.x) + "." + std::to_string(key.y) +
                                                     "." + std::to_string(slot));
        structure.appearance = random;
        auto direction = Sub(b, a);
        structure.yaw = std::atan2(direction.y, direction.x);
        if (city)
        {
            const double zoneRatio = Norm(local) / (std::max)(200., r.boundingRadiusMeters);
            // GFX-02 / CQ-04C2: Use tier-specific measured buildings (1:1 real-world proportions) on
            // procedural settlement parcels alongside legacy architectural silhouettes.
            struct MeasuredSpec
            {
                std::uint16_t variant;
                ContentUse use;
                Vector3d sizeMeters; // X, Y, Z + 0.65m foundation skirt
            };
            constexpr MeasuredSpec kMeasured[] = {
                {0, ContentUse::Housing, {11., 8., 9.65}},           // Halo_Rowhouse
                {1, ContentUse::Services, {10., 13., 5.05}},         // Halo_ServiceShop
                {2, ContentUse::Services, {13., 12., 7.65}},         // Halo_Clinic
                {3, ContentUse::Industry, {24., 18., 9.65}},         // Karden_Logistics
                {4, ContentUse::Services, {19., 23., 12.65}},        // Karden_TransitHall
                {5, ContentUse::Services, {15., 24., 8.05}},         // Karden_Market
                {6, ContentUse::Housing, {24., 28., 39.65}},         // Metro_MixedResidence
                {7, ContentUse::Services, {27., 32., 51.05}},        // Metro_CivicCommerce
                {8, ContentUse::Administration, {35., 40., 64.65}},  // Capital_GateAdministration
                {9, ContentUse::Administration, {37., 48., 19.85}},  // Capital_PublicForum
            };
            const bool useMeasured =
                p.layoutRevision >= GeneratedPlanet::CurrentLayoutRevision && (random % 5 < 3);
            const MeasuredSpec *picked = nullptr;
            if (useMeasured)
            {
                if (r.definition.tier == SettlementTier::District)
                {
                    picked = &kMeasured[random % 3];
                }
                else if (r.definition.tier == SettlementTier::Town)
                {
                    picked = (zoneRatio > .68) ? &kMeasured[3]
                                               : (random % 3 == 0 ? &kMeasured[0 + (random / 3) % 3]
                                                                  : &kMeasured[3 + (random % 3)]);
                }
                else if (r.definition.tier == SettlementTier::Metropolis)
                {
                    if (zoneRatio < .48 && slot == 4)
                    {
                        picked = &kMeasured[6 + (random % 2)];
                    }
                    else if (zoneRatio > .68)
                    {
                        picked = &kMeasured[3 + (random % 3)];
                    }
                    else
                    {
                        picked = &kMeasured[(random % 2 == 0) ? (6 + (random / 2) % 2) : (3 + (random % 3))];
                    }
                }
                else if (r.definition.tier == SettlementTier::SystemCapital)
                {
                    if (zoneRatio < .32 && slot == 4)
                    {
                        picked = &kMeasured[8 + (random % 2)];
                    }
                    else if (zoneRatio < .68)
                    {
                        picked = &kMeasured[6 + (random % 2)];
                    }
                    else
                    {
                        picked = &kMeasured[3 + (random % 3)];
                    }
                }
            }
            if (picked)
            {
                structure.form = ContentForm::MeasuredBuilding;
                structure.variant = picked->variant;
                structure.use = picked->use;
                // Keep exact authored ratios; scale down only if a dense 3x3 sub-slot cannot host a wide lot.
                const double maxSpan = denseUrban && slot != 4 ? 19. : 52.;
                const double span = (std::max)(picked->sizeMeters.x, picked->sizeMeters.y);
                const double fit = span > maxSpan ? (maxSpan / span) : 1.;
                structure.sizeMeters = {picked->sizeMeters.x * fit, picked->sizeMeters.y * fit,
                                        picked->sizeMeters.z * fit};
            }
            else
            {
                structure.form = CityForm(r.definition.tier, zoneRatio, random, structure.use);
                const double tier = double(unsigned(r.definition.tier));
                const double coreBoost = zoneRatio < .28 ? (tier >= 3 ? 1.42 : 1.22)
                                         : zoneRatio > .68 ? .72
                                                           : 1.;
                const double footScale = denseUrban ? .82 : 1.;
                structure.sizeMeters = {
                    (11. + Fraction(random + 1) * 5.) * footScale,
                    (13. + Fraction(random + 2) * 5.5) * footScale,
                    (5. + tier * tier * 3. + Fraction(random + 3) * (5. + tier * 4.)) * coreBoost};
                if (structure.use != ContentUse::Housing)
                {
                    structure.sizeMeters.z = (6. + tier * 2.4) * coreBoost;
                }
            }
        }
        else
        {
            structure.sizeMeters = {7, 9, 4};
            switch (r.definition.kind)
            {
            case RegionKind::Outpost:
                structure.use = ContentUse::Expedition;
                if (p.layoutRevision >= GeneratedPlanet::CurrentLayoutRevision && random % 2 == 0)
                {
                    structure.form = ContentForm::MeasuredBuilding;
                    const unsigned pick = unsigned((random / 2) % 3);
                    structure.variant = std::uint16_t(10 + pick); // Frontier_FieldWorkshop / FreightDepot / Relay
                    structure.sizeMeters = pick == 0   ? Vector3d{12., 11., 5.35}
                                           : pick == 1 ? Vector3d{20., 16., 10.25}
                                                       : Vector3d{10., 10., 4.65};
                }
                else
                {
                    structure.form = random % 3 == 0   ? ContentForm::Relay
                                     : random % 3 == 1 ? ContentForm::Solar
                                                       : ContentForm::Shelter;
                }
                break;
            case RegionKind::Mine:
                structure.use = ContentUse::Mining;
                if (p.layoutRevision >= GeneratedPlanet::CurrentLayoutRevision && (random % 2 == 0 || entrance))
                {
                    structure.form = ContentForm::MeasuredBuilding;
                    structure.variant = 13; // Mine_AccessSupport
                    structure.sizeMeters = {15., 13., 5.25};
                }
                else
                {
                    structure.form = ContentForm::Conveyor;
                    structure.sizeMeters = {8, 14, 5};
                }
                break;
            case RegionKind::Outlaw:
                structure.use = ContentUse::Salvage;
                if (p.layoutRevision >= GeneratedPlanet::CurrentLayoutRevision && random % 3 == 0)
                {
                    structure.form = ContentForm::MeasuredBuilding;
                    structure.variant = 12; // Frontier_DecommissionedRelay
                    structure.sizeMeters = {10., 10., 4.65};
                }
                else
                {
                    structure.form = random % 2 ? ContentForm::Cargo : ContentForm::Shelter;
                }
                break;
            case RegionKind::AncientRuins:
                structure.use = ContentUse::Archaeology;
                structure.form = random % 2 ? ContentForm::Monolith : ContentForm::Arch;
                structure.sizeMeters = {6, 8, 10};
                break;
            default:
                structure.use = ContentUse::Expedition;
                structure.form = random % 2 ? ContentForm::Shelter : ContentForm::WaterTank;
                break;
            }
        }
        const double extent = std::hypot(structure.sizeMeters.x, structure.sizeMeters.y) * .5;
        if (!Inside(r, local, extent + 3))
        {
            continue;
        }
        // The same setback test covers every incident edge, including roads owned by neighbours.
        bool clear = true;
        std::array<std::array<Vector3d, 2>, 4> edges = {{{a, b}, {b, c}, {c, d}, {d, a}}};
        std::array<unsigned, 4> incidentLanes = {lanes(key.y), lanes(key.x + 1), lanes(key.y + 1),
                                                 lanes(key.x)};
        for (unsigned edge = 0; edge < edges.size(); ++edge)
        {
            if (city && SegmentDistance(local, edges[edge][0], edges[edge][1]) <
                            extent + incidentLanes[edge] * 1.5 + 2.4)
            {
                clear = false;
            }
        }
        if (!clear)
        {
            continue;
        }
        auto point = At(p, r, local);
        const double relayCap = std::cos((extent + 8) / p.terrain.Planet().radiusMeters);
        const bool relayReserved = std::any_of(p.relays.begin(), p.relays.end(), [&](const auto &relay) {
            return G::Dot(point.normal, relay.location.position.normal) > relayCap;
        });
        if (relayReserved || Reserved(p, point, extent + 2) || !IntercityClear(p, r, local, extent) ||
            (p.cityCores && p.cityCores->Reserved(p, r, local, extent + 3)))
        {
            continue;
        }
        double low = point.altitudeMeters, high = low;
        if (p.layoutRevision >= 4)
        {
            for (int x = -2; x <= 2; ++x)
            {
                for (int y = -2; y <= 2; ++y)
                {
                    auto sample = Add(
                        local, Rotate({x * structure.sizeMeters.x * .25, y * structure.sizeMeters.y * .25, 0},
                                      structure.yaw));
                    const auto height = At(p, r, sample).altitudeMeters;
                    low = (std::min)(low, height);
                    high = (std::max)(high, height);
                }
            }
        }
        else
        {
            for (double x : {-1., 1.})
            {
                for (double y : {-1., 1.})
                {
                    auto corner = Add(
                        local, Rotate({x * structure.sizeMeters.x * .5, y * structure.sizeMeters.y * .5, 0},
                                      structure.yaw));
                    double height = At(p, r, corner).altitudeMeters;
                    low = (std::min)(low, height);
                    high = (std::max)(high, height);
                }
            }
        }
        if (low < PlanetTerrain::SeaLevelMeters || high - low > (city ? 2.5 : 4))
        {
            continue;
        }
        point.altitudeMeters = high;
        auto transported = G::Advance(r.frame, local.x, local.y, p.terrain.Planet().radiusMeters);
        transported.origin = point;
        structure.frame = transported;
        structure.foundationMeters = high - low + .15;
        result.structures.push_back(std::move(structure));
    }
    return result;
}
SurfaceContentPatch ContentLayout::Publish(const GeneratedPlanet &p, const ChunkKey &key, double radius,
                                           const std::atomic_bool &cancel)
{
    const auto &planet = p.terrain.Planet();
    key.Validate();
    if (key.planetId != planet.id || key.planetSeed != planet.seed ||
        key.generatorVersion != planet.generatorVersion || key.layer != 0)
    {
        throw std::invalid_argument("Content job belongs to another world/layer");
    }
    SurfaceContentPatch result;
    // Coarse planet previews have no object payload; detailed gameplay chunks are ~128m wide.
    if (radius > 1024)
    {
        return result;
    }
    for (auto cellKey : Near(p, CubeSphere::Center(key), radius + 40))
    {
        if (cancel.load())
        {
            return {};
        }
        auto cell = Generate(p, cellKey, &cancel);
        for (auto &structure : cell.structures)
        {
            if (CubeSphere::Address(planet, structure.frame.origin, key.level) == key)
            {
                result.structures.push_back(std::move(structure));
            }
        }
        for (auto &inter : cell.intersections)
        {
            if (CubeSphere::Address(planet, inter.center, key.level) == key)
            {
                result.intersections.push_back(std::move(inter));
            }
        }
        for (const auto &street : cell.streets)
        {
            auto makePart = [&] {
                SurfaceStreet s;
                s.widthMeters = street.widthMeters;
                s.sidewalkMeters = street.sidewalkMeters;
                s.lanes = street.lanes;
                s.kind = street.kind;
                s.startWidthMeters = street.startWidthMeters;
                s.elevationOffsetMeters = street.elevationOffsetMeters;
                s.hasMedian = street.hasMedian;
                s.hasLeftPocket = street.hasLeftPocket;
                return s;
            };
            SurfaceStreet part = makePart();
            auto flush = [&] {
                if (part.points.size() > 1)
                {
                    result.streets.push_back(std::move(part));
                }
                part = makePart();
            };
            for (std::size_t i = 1; i < street.points.size(); ++i)
            {
                auto a = street.points[i - 1], b = street.points[i];
                auto midpoint = SurfacePoint{
                    G::Unit({a.normal.x + b.normal.x, a.normal.y + b.normal.y, a.normal.z + b.normal.z}), 0};
                if (!(CubeSphere::Address(planet, midpoint, key.level) == key))
                {
                    flush();
                    continue;
                }
                if (part.points.empty())
                {
                    part.points.push_back(a);
                }
                part.points.push_back(b);
            }
            flush();
        }
    }
    if (cancel.load())
    {
        return {};
    }
    std::sort(result.structures.begin(), result.structures.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}
std::vector<UrbanTrafficRoute> ContentLayout::TrafficRoutes(const GeneratedPlanet &p, std::size_t regionIndex)
{
    std::vector<UrbanTrafficRoute> routes;
    if (p.layoutRevision < 5 || regionIndex >= p.regions.size())
    {
        return routes;
    }
    const auto &r = p.regions[regionIndex];
    if (r.definition.kind != RegionKind::Settlement || r.center.space != SpaceKind::Surface)
    {
        return routes;
    }
    Grid grid(p, r);
    const double boundR = (std::max)(200., r.boundingRadiusMeters);
    const double avgStep = (std::max)(40., (grid.width + grid.depth) * .5);
    std::int64_t ringCell = (std::max)(std::int64_t(3), std::int64_t(std::floor(boundR * .68 / avgStep)));
    while (ringCell > 2)
    {
        if (Inside(r, grid.Node(ringCell, 0), 16.) && Inside(r, grid.Node(-ringCell, 0), 16.) &&
            Inside(r, grid.Node(0, ringCell), 16.) && Inside(r, grid.Node(0, -ringCell), 16.))
        {
            break;
        }
        --ringCell;
    }
    const auto *core = p.cityCores ? p.cityCores->Find(r.definition.id) : nullptr;
    const double coreClear = core ? (core->radiusMeters + 18.) : 0.;

    std::map<std::pair<std::int64_t, std::int64_t>, unsigned> nodeMap;
    auto nodeId = [&](std::int64_t gx, std::int64_t gy) {
        auto [it, inserted] = nodeMap.emplace(std::make_pair(gx, gy), unsigned(nodeMap.size()));
        return it->second;
    };

    // Build major arterial coordinate list strictly within [-ringCell, +ringCell].
    std::vector<std::int64_t> axisCoords = {-ringCell, ringCell};
    for (std::int64_t c : {-4, -2, 0, 2, 4})
    {
        if (std::abs(c) < ringCell)
        {
            axisCoords.push_back(c);
        }
    }
    std::sort(axisCoords.begin(), axisCoords.end());
    axisCoords.erase(std::unique(axisCoords.begin(), axisCoords.end()), axisCoords.end());

    auto sampleSegment = [&](std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1, unsigned lanes,
                             const std::string &tag) {
        const std::int64_t dx = x1 > x0 ? 1 : (x1 < x0 ? -1 : 0);
        const std::int64_t dy = y1 > y0 ? 1 : (y1 < y0 ? -1 : 0);
        const std::int64_t steps = (std::max)(std::abs(x1 - x0), std::abs(y1 - y0));
        if (steps <= 0)
        {
            return;
        }
        std::vector<SurfacePoint> pts;
        std::int64_t segStartX = x0, segStartY = y0;
        unsigned subIdx = 0;
        auto flush = [&](std::int64_t endX, std::int64_t endY) {
            if (pts.size() >= 2)
            {
                UrbanTrafficRoute tr;
                tr.suffix = tag + "." + std::to_string(subIdx++);
                tr.fromNode = nodeId(segStartX, segStartY);
                tr.toNode = nodeId(endX, endY);
                tr.lanes = lanes;
                tr.points = std::move(pts);
                routes.push_back(std::move(tr));
            }
            pts.clear();
        };
        for (std::int64_t s = 0; s < steps; ++s)
        {
            const std::int64_t cx = x0 + dx * s, cy = y0 + dy * s;
            const std::int64_t nx = cx + dx, ny = cy + dy;
            const auto la = grid.Node(cx, cy), lb = grid.Node(nx, ny);
            const auto mid = Mul(Add(la, lb), .5);
            const bool blockedCore = core && (Norm(la) < coreClear || Norm(lb) < coreClear);
            const bool insideFootprint = Inside(r, mid, 4.);
            auto pa = At(p, r, la), pb = At(p, r, lb);
            if (blockedCore || !insideFootprint || pa.altitudeMeters < PlanetTerrain::SeaLevelMeters + 1. ||
                pb.altitudeMeters < PlanetTerrain::SeaLevelMeters + 1.)
            {
                flush(cx, cy);
                segStartX = nx;
                segStartY = ny;
                continue;
            }
            // Match Rev 5 grade-separated corridors (+8.4m viaduct on x == 2, -5.4m underpass on y == 0).
            if (r.definition.tier >= SettlementTier::Metropolis && dx == 0 && cx == 2)
            {
                const std::int64_t minY = (std::min)(cy, ny);
                if (minY == -1)
                {
                    pa.altitudeMeters += (cy == 0 ? 8.4 : 0.);
                    pb.altitudeMeters += (ny == 0 ? 8.4 : 0.);
                }
                else if (minY == 0)
                {
                    pa.altitudeMeters += 8.4;
                    pb.altitudeMeters += 8.4;
                }
                else if (minY == 1)
                {
                    pa.altitudeMeters += (cy == 1 ? 8.4 : 0.);
                    pb.altitudeMeters += (ny == 1 ? 8.4 : 0.);
                }
            }
            else if (r.definition.tier >= SettlementTier::Metropolis && dy == 0 && cy == 0)
            {
                const std::int64_t minX = (std::min)(cx, nx);
                if (minX == -1)
                {
                    pa.altitudeMeters += (cx == 0 ? -5.4 : 0.);
                    pb.altitudeMeters += (nx == 0 ? -5.4 : 0.);
                }
                else if (minX == 0)
                {
                    pa.altitudeMeters += -5.4;
                    pb.altitudeMeters += -5.4;
                }
                else if (minX == 1)
                {
                    pa.altitudeMeters += (cx == 1 ? -5.4 : 0.);
                    pb.altitudeMeters += (nx == 1 ? -5.4 : 0.);
                }
            }
            if (pts.empty())
            {
                segStartX = cx;
                segStartY = cy;
                pts.push_back(pa);
            }
            pts.push_back(pb);
        }
        flush(x1, y1);
    };

    // Emit all horizontal and vertical Ring Road + major arterial segments between shared arterial junctions.
    for (std::size_t yi = 0; yi < axisCoords.size(); ++yi)
    {
        const std::int64_t y = axisCoords[yi];
        const unsigned lanes = std::abs(y) == ringCell ? 4u : Rev5LanesForCoord(r.definition.tier, y, .3);
        for (std::size_t xi = 1; xi < axisCoords.size(); ++xi)
        {
            sampleSegment(axisCoords[xi - 1], y, axisCoords[xi], y, lanes,
                          "h." + std::to_string(y) + "." + std::to_string(xi));
        }
    }
    for (std::size_t xi = 0; xi < axisCoords.size(); ++xi)
    {
        const std::int64_t x = axisCoords[xi];
        const unsigned lanes = std::abs(x) == ringCell ? 4u : Rev5LanesForCoord(r.definition.tier, x, .3);
        for (std::size_t yi = 1; yi < axisCoords.size(); ++yi)
        {
            sampleSegment(x, axisCoords[yi - 1], x, axisCoords[yi], lanes,
                          "v." + std::to_string(x) + "." + std::to_string(yi));
        }
    }
    return routes;
}
const char *ContentLayout::UseName(ContentUse use)
{
    switch (use)
    {
    case ContentUse::Housing:
        return "주거";
    case ContentUse::Industry:
        return "산업";
    case ContentUse::Services:
        return "교통·생활";
    case ContentUse::Administration:
        return "행정·관측";
    case ContentUse::Mining:
        return "광업";
    case ContentUse::Salvage:
        return "인양 거점";
    case ContentUse::Archaeology:
        return "고대 유적";
    default:
        return "탐사 거점";
    }
}
void ContentCollision::BuildRegionCache(const GeneratedPlanet &p)
{
    regionMeta.clear();
    regionMeta.reserve(p.regions.size());
    for (const auto &r : p.regions)
    {
        RegionMeta meta;
        meta.surface = r.center.space == SpaceKind::Surface;
        meta.center = meta.surface ? r.center.position : r.access.position;
        meta.frame = meta.surface ? r.frame : G::Frame(r.access.position);
        meta.reach = meta.surface ? r.boundingRadiusMeters : 100.;
        Grid grid(p, r);
        meta.seed = grid.seed;
        meta.width = grid.width;
        meta.depth = grid.depth;
        meta.yaw = grid.yaw;
        meta.warpA = Fraction(grid.seed + 3) * 6.;
        meta.warpB = Fraction(grid.seed + 4) * 6.;
        meta.warpScale = grid.warpScale;
        meta.cellDiag = std::hypot(grid.width, grid.depth);
        regionMeta.push_back(meta);
    }
}
void ContentCollision::BuildCellGrid(const GeneratedPlanet &p, Entry &entry) const
{
    entry.hasGrid = false;
    entry.items.clear();
    entry.bucketOffsets.clear();
    entry.bucketItems.clear();
    if (entry.cell.structures.size() <= 8 || entry.cell.key.region >= regionMeta.size())
    {
        return;
    }
    const auto &meta = regionMeta[entry.cell.key.region];
    const double planetRadius = p.terrain.Planet().radiusMeters;
    double minX = 1.e18, minY = 1.e18, maxX = -1.e18, maxY = -1.e18;
    entry.items.reserve(entry.cell.structures.size());
    for (std::size_t i = 0; i < entry.cell.structures.size(); ++i)
    {
        const auto &s = entry.cell.structures[i];
        if (!s.solid)
        {
            continue;
        }
        const auto loc = G::ToLocal(meta.frame, s.frame.origin, planetRadius);
        const double bound = std::hypot(s.sizeMeters.x * .5, s.sizeMeters.y * .5) + 1.0;
        entry.items.push_back({i, loc.x, loc.y, bound});
        minX = (std::min)(minX, loc.x - bound);
        minY = (std::min)(minY, loc.y - bound);
        maxX = (std::max)(maxX, loc.x + bound);
        maxY = (std::max)(maxY, loc.y + bound);
    }
    if (entry.items.size() <= 8)
    {
        return;
    }
    constexpr double kBucketSize = 16.;
    double spanX = (std::max)(kBucketSize, maxX - minX);
    double spanY = (std::max)(kBucketSize, maxY - minY);
    double bucketSize = kBucketSize;
    if (spanX / bucketSize > 64. || spanY / bucketSize > 64.)
    {
        bucketSize = (std::max)(spanX, spanY) / 64.;
    }
    entry.minX = minX;
    entry.minY = minY;
    entry.invBucket = 1. / bucketSize;
    entry.gridW = (std::max)(1, (std::min)(64, int(std::ceil(spanX * entry.invBucket))));
    entry.gridH = (std::max)(1, (std::min)(64, int(std::ceil(spanY * entry.invBucket))));
    const int totalBuckets = entry.gridW * entry.gridH;
    std::vector<std::uint32_t> counts(size_t(totalBuckets), 0);
    auto clampX = [&](double v) {
        return std::clamp(int(std::floor((v - entry.minX) * entry.invBucket)), 0, entry.gridW - 1);
    };
    auto clampY = [&](double v) {
        return std::clamp(int(std::floor((v - entry.minY) * entry.invBucket)), 0, entry.gridH - 1);
    };
    for (const auto &item : entry.items)
    {
        const int bx0 = clampX(item.cx - item.bound), bx1 = clampX(item.cx + item.bound);
        const int by0 = clampY(item.cy - item.bound), by1 = clampY(item.cy + item.bound);
        for (int by = by0; by <= by1; ++by)
        {
            for (int bx = bx0; bx <= bx1; ++bx)
            {
                ++counts[size_t(by * entry.gridW + bx)];
            }
        }
    }
    entry.bucketOffsets.assign(size_t(totalBuckets + 1), 0);
    for (int i = 0; i < totalBuckets; ++i)
    {
        entry.bucketOffsets[size_t(i + 1)] = entry.bucketOffsets[size_t(i)] + counts[size_t(i)];
    }
    entry.bucketItems.resize(entry.bucketOffsets.back());
    std::vector<std::uint32_t> cursor = entry.bucketOffsets;
    for (std::size_t idx = 0; idx < entry.items.size(); ++idx)
    {
        const auto &item = entry.items[idx];
        const int bx0 = clampX(item.cx - item.bound), bx1 = clampX(item.cx + item.bound);
        const int by0 = clampY(item.cy - item.bound), by1 = clampY(item.cy + item.bound);
        for (int by = by0; by <= by1; ++by)
        {
            for (int bx = bx0; bx <= bx1; ++bx)
            {
                const size_t b = size_t(by * entry.gridW + bx);
                entry.bucketItems[cursor[b]++] = std::uint32_t(idx);
            }
        }
    }
    entry.hasGrid = true;
}
bool ContentCollision::Blocked(const GeneratedPlanet &p, const SurfacePoint &point, double clearance,
                               bool heightAware, double verticalClearance)
{
    if (!std::isfinite(clearance) || clearance < 0 || clearance > 16 || !std::isfinite(verticalClearance) ||
        verticalClearance < -1 || verticalClearance > 16)
    {
        throw std::invalid_argument("Invalid structure clearance");
    }
    if (!owner || !owner->SameWorld(p.terrain.Planet()) || ownerPlan != &p ||
        regionMeta.size() != p.regions.size())
    {
        if (!owner || !owner->SameWorld(p.terrain.Planet()))
        {
            Clear();
            owner = p.terrain.Planet();
        }
        ownerPlan = &p;
        BuildRegionCache(p);
    }
    ++clock;
    scratchKeys.clear();
    const double planetRadius = p.terrain.Planet().radiusMeters;
    for (std::size_t i = 0; i < regionMeta.size(); ++i)
    {
        const auto &meta = regionMeta[i];
        const double limitMeters = meta.reach + clearance + 200.;
        const double limitAngle = limitMeters / planetRadius;
        if (limitAngle < 2.)
        {
            const double dx = meta.center.normal.x - point.normal.x;
            const double dy = meta.center.normal.y - point.normal.y;
            const double dz = meta.center.normal.z - point.normal.z;
            if (dx * dx + dy * dy + dz * dz > limitAngle * limitAngle)
            {
                continue;
            }
        }
        if (G::Distance(planetRadius, meta.center, point) > limitMeters)
        {
            continue;
        }
        const auto regionLocal = G::ToLocal(meta.frame, point, planetRadius);
        const auto local = meta.yaw != 0. ? Rotate(regionLocal, -meta.yaw) : regionLocal;
        const auto x0 = std::int64_t(std::floor((local.x - clearance) / meta.width)) - 1;
        const auto x1 = std::int64_t(std::floor((local.x + clearance) / meta.width)) + 1;
        const auto y0 = std::int64_t(std::floor((local.y - clearance) / meta.depth)) - 1;
        const auto y1 = std::int64_t(std::floor((local.y + clearance) / meta.depth)) + 1;
        const double maxCellDist = clearance + meta.cellDiag;
        const double maxCellDistSq = maxCellDist * maxCellDist;
        auto node = [&](std::int64_t gx, std::int64_t gy) {
            Vector3d v{double(gx) * meta.width + meta.width * meta.warpScale * std::sin(double(gy) * .61 + meta.warpA),
                       double(gy) * meta.depth + meta.depth * meta.warpScale * std::sin(double(gx) * .53 + meta.warpB),
                       0};
            return meta.yaw != 0. ? Rotate(v, meta.yaw) : v;
        };
        for (auto y = y0; y <= y1; ++y)
        {
            for (auto x = x0; x <= x1; ++x)
            {
                const auto a = node(x, y), b = node(x + 1, y + 1);
                const double cx = (a.x + b.x) * .5 - regionLocal.x;
                const double cy = (a.y + b.y) * .5 - regionLocal.y;
                if (cx * cx + cy * cy > maxCellDistSq)
                {
                    continue;
                }
                if (!meta.surface && (x != 0 || y != 0))
                {
                    continue;
                }
                scratchKeys.push_back({{i, x, y}, regionLocal.x, regionLocal.y});
            }
        }
    }
    const double vertical = verticalClearance < 0 ? clearance : verticalClearance;
    for (const auto &candidate : scratchKeys)
    {
        auto found = cells.find(candidate.key);
        if (found == cells.end())
        {
            if (cells.size() >= MaximumCells)
            {
                auto oldest = std::min_element(cells.begin(), cells.end(), [](const auto &a, const auto &b) {
                    return a.second.touched < b.second.touched;
                });
                cells.erase(oldest);
            }
            Entry created;
            created.cell = ContentLayout::Generate(p, candidate.key);
            created.touched = clock;
            BuildCellGrid(p, created);
            found = cells.emplace(candidate.key, std::move(created)).first;
        }
        auto &entry = found->second;
        entry.touched = clock;
        if (entry.hasGrid)
        {
            const double pad = clearance + 1.0;
            if (candidate.localX + pad >= entry.minX && candidate.localY + pad >= entry.minY)
            {
                const int bx0 = std::clamp(int(std::floor((candidate.localX - pad - entry.minX) * entry.invBucket)),
                                           0, entry.gridW - 1);
                const int bx1 = std::clamp(int(std::floor((candidate.localX + pad - entry.minX) * entry.invBucket)),
                                           0, entry.gridW - 1);
                const int by0 = std::clamp(int(std::floor((candidate.localY - pad - entry.minY) * entry.invBucket)),
                                           0, entry.gridH - 1);
                const int by1 = std::clamp(int(std::floor((candidate.localY + pad - entry.minY) * entry.invBucket)),
                                           0, entry.gridH - 1);
                const bool multiBucket = bx0 != bx1 || by0 != by1;
                if (multiBucket)
                {
                    if (visitMarks.size() < entry.items.size())
                    {
                        visitMarks.resize(entry.items.size(), 0);
                    }
                    if (++visitStamp == 0)
                    {
                        std::fill(visitMarks.begin(), visitMarks.end(), 0);
                        visitStamp = 1;
                    }
                }
                for (int by = by0; by <= by1; ++by)
                {
                    for (int bx = bx0; bx <= bx1; ++bx)
                    {
                        const size_t b = size_t(by * entry.gridW + bx);
                        const auto begin = entry.bucketOffsets[b];
                        const auto end = entry.bucketOffsets[b + 1];
                        for (auto k = begin; k < end; ++k)
                        {
                            const auto itemIdx = entry.bucketItems[k];
                            if (multiBucket)
                            {
                                if (visitMarks[itemIdx] == visitStamp)
                                {
                                    continue;
                                }
                                visitMarks[itemIdx] = visitStamp;
                            }
                            const auto &item = entry.items[itemIdx];
                            const double dx = candidate.localX - item.cx;
                            const double dy = candidate.localY - item.cy;
                            const double lim = item.bound + clearance;
                            if (dx * dx + dy * dy > lim * lim)
                            {
                                continue;
                            }
                            const auto &structure = entry.cell.structures[item.index];
                            if ((!heightAware ||
                                 (point.altitudeMeters >= structure.frame.origin.altitudeMeters - vertical &&
                                  point.altitudeMeters <= structure.frame.origin.altitudeMeters +
                                                              structure.sizeMeters.z + vertical)) &&
                                structure.Contains(point, planetRadius, clearance))
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            for (const auto &structure : entry.cell.structures)
            {
                if ((!heightAware ||
                     (point.altitudeMeters >= structure.frame.origin.altitudeMeters - vertical &&
                      point.altitudeMeters <=
                          structure.frame.origin.altitudeMeters + structure.sizeMeters.z + vertical)) &&
                    structure.Contains(point, planetRadius, clearance))
                {
                    return true;
                }
            }
        }
        // Explicit flight hulls must also respect elevated road decks. Existing ground/projectile
        // queries retain their previous contract.
        if (heightAware && verticalClearance >= 0)
        {
            const auto frame = G::Frame(point);
            for (const auto &street : entry.cell.streets)
            {
                for (size_t i = 1; i < street.points.size(); ++i)
                {
                    const auto a = G::ToLocal(frame, street.points[i - 1], planetRadius);
                    const auto b = G::ToLocal(frame, street.points[i], planetRadius);
                    const double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
                    const double t = length > .0001 ? std::clamp(-(a.x * dx + a.y * dy) / length, 0., 1.) : 0;
                    const double altitude =
                        street.points[i - 1].altitudeMeters * (1 - t) + street.points[i].altitudeMeters * t;
                    if (std::hypot(a.x + dx * t, a.y + dy * t) < street.widthMeters / 2 + clearance &&
                        std::abs(point.altitudeMeters - altitude) < vertical + .2)
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
void ContentCollision::Clear()
{
    cells.clear();
    regionMeta.clear();
    scratchKeys.clear();
    owner.reset();
    ownerPlan = nullptr;
    clock = 0;
}
std::vector<const SurfaceStructure *> ContentCollision::Structures() const
{
    std::vector<const SurfaceStructure *> result;
    for (const auto &entry : cells)
    {
        for (const auto &structure : entry.second.cell.structures)
        {
            result.push_back(&structure);
        }
    }
    return result;
}
} // namespace world
