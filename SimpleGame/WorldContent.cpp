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
    double width, depth, yaw;
    explicit Grid(const GeneratedPlanet &p, const GeneratedRegion &r)
        : seed(WorldIdentity::StreamSeed(p.terrain.Planet(), r.definition.id, "surface_content_v1"))
    {
        bool city = r.definition.kind == RegionKind::Settlement;
        if (city && p.layoutRevision >= GeneratedPlanet::CurrentLayoutRevision)
        {
            const bool major = r.definition.tier >= SettlementTier::Metropolis;
            width = major ? 68 + Fraction(seed) * 14 : 64 + Fraction(seed) * 14;
            depth = major ? 74 + Fraction(seed + 1) * 16 : 70 + Fraction(seed + 1) * 14;
        }
        else
        {
            width = city ? 84 + Fraction(seed) * 24 : 132 + Fraction(seed) * 44;
            depth = city ? 92 + Fraction(seed + 1) * 30 : 140 + Fraction(seed + 1) * 44;
        }
        // Match Halo's world axes: the shared quarter-view projects these axes at 45 degrees.
        // Warped shared nodes keep organic streets without randomly rotating an entire settlement.
        yaw = 0;
    }
    Vector3d Node(std::int64_t x, std::int64_t y) const
    {
        // Shared nodes have smooth bounded warps: every edge joins exactly and no cells fold.
        double a = Fraction(seed + 3) * 6, b = Fraction(seed + 4) * 6;
        return Rotate({double(x) * width + width * .13 * std::sin(double(y) * .61 + a),
                       double(y) * depth + depth * .13 * std::sin(double(x) * .53 + b), 0},
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
    p.terrain = PlanetTerrain(p.terrain.Planet(), std::move(reservations), p.terrain.Homeland());
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
        auto road = [&](Vector3d a, Vector3d b, unsigned lanes, double width = 0) {
            unsigned steps = (std::max)(2u, unsigned(std::ceil(Norm(Sub(b, a)) / 16)) * 2);
            for (unsigned i = 0; i < steps; ++i)
            {
                cancelled();
                auto left = Add(a, Mul(Sub(b, a), double(i) / steps)),
                     right = Add(a, Mul(Sub(b, a), double(i + 1) / steps));
                auto pa = At(p, r, left), pb = At(p, r, right);
                pa.altitudeMeters += .08;
                pb.altitudeMeters += .08;
                cellAt(Mul(Add(left, right), .5))
                    .streets.push_back({{pa, pb}, width > 0 ? width : lanes * 3., 2.4, lanes});
            }
        };
        for (const auto &edge : city.roads)
        {
            road(edge.start, edge.end, edge.lanes);
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
    const bool building = form <= ContentForm::Shelter || form == ContentForm::Building;
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
        else
        {
            structure.sizeMeters = {7, 9, 4};
            switch (r.definition.kind)
            {
            case RegionKind::Outpost:
                structure.use = ContentUse::Expedition;
                structure.form = random % 3 == 0   ? ContentForm::Relay
                                 : random % 3 == 1 ? ContentForm::Solar
                                                   : ContentForm::Shelter;
                break;
            case RegionKind::Mine:
                structure.use = ContentUse::Mining;
                structure.form = ContentForm::Conveyor;
                structure.sizeMeters = {8, 14, 5};
                break;
            case RegionKind::Outlaw:
                structure.use = ContentUse::Salvage;
                structure.form = random % 2 ? ContentForm::Cargo : ContentForm::Shelter;
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
        const double extent = Norm(structure.sizeMeters) * .5;
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
        for (const auto &street : cell.streets)
        {
            SurfaceStreet part{{}, street.widthMeters, street.sidewalkMeters, street.lanes};
            auto flush = [&] {
                if (part.points.size() > 1)
                {
                    result.streets.push_back(std::move(part));
                }
                part = SurfaceStreet{{}, street.widthMeters, street.sidewalkMeters, street.lanes};
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
            Vector3d v{double(gx) * meta.width + meta.width * .13 * std::sin(double(gy) * .61 + meta.warpA),
                       double(gy) * meta.depth + meta.depth * .13 * std::sin(double(gx) * .53 + meta.warpB), 0};
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
