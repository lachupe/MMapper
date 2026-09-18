// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestMapReshape.h"

#include "../src/global/HideQDebug.h"
#include "../src/global/progresscounter.h"
#include "../src/map/ExitDirection.h"
#include "../src/map/ExitFlags.h"
#include "../src/map/Map.h"
#include "../src/map/MapReshapeGraph.h"
#include "../src/map/MapReshapeScorer.h"
#include "../src/map/MapReshapeSolver.h"
#include "../src/map/MapReshapeTypes.h"
#include "../src/map/RawRoom.h"
#include "../src/map/RoomHandle.h"
#include "../src/map/RoomIdSet.h"
#include "../src/map/coordinate.h"
#include "../src/map/mmapper2room.h"
#include "../src/map/roomid.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <QtTest/QtTest>

namespace {

/// Builds small synthetic maps. Only `outgoing` needs to be set: WorldBuilder
/// derives the matching `incoming` entries and repairs exit flags.
class NODISCARD SyntheticMap final
{
private:
    std::vector<ExternalRawRoom> m_rooms;

public:
    void addRoom(const uint32_t id, const Coordinate pos, const std::string &area = {})
    {
        auto &room = m_rooms.emplace_back();
        room.id = ExternalRoomId{id};
        room.status = RoomStatusEnum::Permanent;
        room.setPosition(pos);
        room.setName(RoomName{"room " + std::to_string(id)});
        if (!area.empty()) {
            room.setArea(makeRoomArea(area));
        }
    }

    /// One-way exit, optionally carrying extra flags such as RANDOM.
    void link(const uint32_t from,
              const ExitDirEnum dir,
              const uint32_t to,
              const ExitFlags extraFlags = {})
    {
        ExternalRawRoom &room = findRoom(from);
        auto &exit = room.getExit(dir);
        exit.outgoing.insert(ExternalRoomId{to});
        exit.addExitFlags(ExitFlags{ExitFlagEnum::EXIT} | extraFlags);
    }

    /// Exit plus its reverse, which is what an ordinary mapped corridor looks
    /// like.
    void linkBoth(const uint32_t a, const ExitDirEnum dir, const uint32_t b)
    {
        link(a, dir, b);
        link(b, opposite(dir), a);
    }

    NODISCARD Map build()
    {
        ProgressCounter pc;
        return Map::fromRooms(pc, m_rooms, {}).modified;
    }

private:
    NODISCARD ExternalRawRoom &findRoom(const uint32_t id)
    {
        const auto it = std::find_if(m_rooms.begin(), m_rooms.end(), [id](const ExternalRawRoom &r) {
            return r.id == ExternalRoomId{id};
        });
        if (it == m_rooms.end()) {
            throw std::runtime_error("SyntheticMap::findRoom: no such room");
        }
        return *it;
    }
};

/// External ids are remapped when the map is built, so tests translate.
NODISCARD RoomId internalId(const Map &map, const uint32_t externalId)
{
    const RoomHandle room = map.findRoomHandle(ExternalRoomId{externalId});
    if (!room) {
        throw std::runtime_error("internalId: room not found");
    }
    return room.getId();
}

/// A west-to-east chain of `count` rooms at y = z = 0, ids 0..count-1.
NODISCARD Map buildChain(const uint32_t count)
{
    SyntheticMap builder;
    for (uint32_t i = 0; i < count; ++i) {
        builder.addRoom(i, Coordinate{static_cast<int>(i), 0, 0});
    }
    for (uint32_t i = 0; i + 1 < count; ++i) {
        builder.linkBoth(i, ExitDirEnum::EAST, i + 1);
    }
    return builder.build();
}

NODISCARD const LayoutRoom *findRoomInScope(const MapReshapeGraph &graph,
                                            const Map &map,
                                            const uint32_t externalId)
{
    const auto index = graph.findIndex(internalId(map, externalId));
    if (!index) {
        return nullptr;
    }
    return &graph.getRoom(*index);
}

NODISCARD RoomIdSet makeCore(const Map &map, const std::vector<uint32_t> &externalIds)
{
    RoomIdSet set;
    for (const uint32_t id : externalIds) {
        set.insert(internalId(map, id));
    }
    return set;
}

/// Index of a room in the graph, by the external id used when building.
NODISCARD size_t scopeIndex(const MapReshapeGraph &graph, const Map &map, const uint32_t externalId)
{
    const auto index = graph.findIndex(internalId(map, externalId));
    if (!index) {
        throw std::runtime_error("scopeIndex: room not in scope");
    }
    return *index;
}

NODISCARD size_t countIssues(const std::vector<LayoutIssue> &issues, const LayoutIssueEnum kind)
{
    return static_cast<size_t>(
        std::count_if(issues.begin(), issues.end(), [kind](const LayoutIssue &i) {
            return i.kind == kind;
        }));
}

} // namespace

TestMapReshape::TestMapReshape() = default;

TestMapReshape::~TestMapReshape() = default;

void TestMapReshape::rolesByGraphDistanceTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(5);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0}), options);

    const LayoutRoom *const core = findRoomInScope(graph, map, 0);
    QVERIFY(core != nullptr);
    QCOMPARE(core->role, LayoutRoomRoleEnum::Core);
    QCOMPARE(core->marginDistance, 0);

    const LayoutRoom *const margin = findRoomInScope(graph, map, 1);
    QVERIFY(margin != nullptr);
    QCOMPARE(margin->role, LayoutRoomRoleEnum::Margin);
    QCOMPARE(margin->marginDistance, 1);

    // One ring past the margin is kept as an immovable anchor, so
    // cross-boundary exits still have a direction to point at.
    const LayoutRoom *const anchor = findRoomInScope(graph, map, 2);
    QVERIFY(anchor != nullptr);
    QCOMPARE(anchor->role, LayoutRoomRoleEnum::FixedExternal);
    QCOMPARE(anchor->marginDistance, 2);

    // Anything further out is not collected at all.
    QVERIFY(findRoomInScope(graph, map, 3) == nullptr);
    QVERIFY(findRoomInScope(graph, map, 4) == nullptr);

    // Extraction must not move anything.
    for (const LayoutRoom &room : graph.getRooms()) {
        QCOMPARE(room.current, room.original);
    }
}

void TestMapReshape::pinnedOverridesRoleTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(4);

    ReshapeOptions options;
    options.marginRings = 1;
    options.pinned.insert(internalId(map, 1));
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    // Room 1 is a core room by graph distance, but the user froze it.
    const LayoutRoom *const pinned = findRoomInScope(graph, map, 1);
    QVERIFY(pinned != nullptr);
    QCOMPARE(pinned->role, LayoutRoomRoleEnum::Pinned);
    QCOMPARE(pinned->marginDistance, 0);
    QVERIFY(!isMovable(pinned->role));

    QCOMPARE(findRoomInScope(graph, map, 0)->role, LayoutRoomRoleEnum::Core);
}

void TestMapReshape::horizontalEdgesTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    QVERIFY(graph.getVerticalEdges().empty());

    const size_t indexA = *graph.findIndex(internalId(map, 0));
    const size_t indexB = *graph.findIndex(internalId(map, 1));

    // A two-way corridor is two directed edges.
    QCOMPARE(graph.getHorizontalEdges().size(), size_t(2));

    const auto hasEdge = [&graph](const size_t from, const size_t to, const ExitDirEnum dir) {
        return std::any_of(graph.getHorizontalEdges().begin(),
                           graph.getHorizontalEdges().end(),
                           [&](const LayoutEdge &e) {
                               return e.fromIndex == from && e.toIndex == to && e.dir == dir;
                           });
    };
    QVERIFY(hasEdge(indexA, indexB, ExitDirEnum::EAST));
    QVERIFY(hasEdge(indexB, indexA, ExitDirEnum::WEST));
}

void TestMapReshape::boundaryEdgesTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(4);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0}), options);

    const size_t core = *graph.findIndex(internalId(map, 0));
    const size_t margin = *graph.findIndex(internalId(map, 1));
    const size_t fixed = *graph.findIndex(internalId(map, 2));

    const auto findEdge = [&graph](const size_t from, const size_t to) -> std::optional<LayoutEdge> {
        for (const LayoutEdge &e : graph.getHorizontalEdges()) {
            if (e.fromIndex == from && e.toIndex == to) {
                return e;
            }
        }
        return std::nullopt;
    };

    // Core to margin is interior: both ends can move.
    const auto interior = findEdge(core, margin);
    QVERIFY(interior.has_value());
    QVERIFY(!interior->crossesBoundary);

    // Margin to fixed is the elastic boundary edge: the fixed end stays put
    // while the margin end may drift, so its length must be allowed to grow.
    const auto boundary = findEdge(margin, fixed);
    QVERIFY(boundary.has_value());
    QVERIFY(boundary->crossesBoundary);

    // The reverse edge is recorded too: the fixed room's exit still anchors
    // the direction its movable neighbour should sit in.
    const auto reverse = findEdge(fixed, margin);
    QVERIFY(reverse.has_value());
    QVERIFY(reverse->crossesBoundary);

    // Room 3 is out of scope entirely, so no edge reaches it.
    QVERIFY(!graph.findIndex(internalId(map, 3)).has_value());
}

void TestMapReshape::verticalExitsAreSeparatedTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{0, 0, 1});
    builder.linkBoth(0, ExitDirEnum::UP, 1);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    // UP/DOWN must not become horizontal constraints, and must not be treated
    // as a fixed one-level z displacement either: MUME's vertical exits do not
    // reliably mean z +/- 1. They are collected for later Z work only.
    QVERIFY(graph.getHorizontalEdges().empty());
    QCOMPARE(graph.getVerticalEdges().size(), size_t(2));
    QCOMPARE(graph.countZLevels(), size_t(2));
}

void TestMapReshape::unusableExitsAreRecordedTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{1, 0, 0});
    builder.link(0, ExitDirEnum::EAST, 1, ExitFlags{ExitFlagEnum::RANDOM});
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    // A random exit says nothing about where its target sits, so it must not
    // constrain the layout -- but it is reported rather than silently dropped.
    QVERIFY(graph.getHorizontalEdges().empty());
    QCOMPARE(graph.getUnsupportedExits().size(), size_t(1));
    QCOMPARE(graph.getUnsupportedExits().front().dir, ExitDirEnum::EAST);
    QCOMPARE(graph.getUnsupportedExits().front().from, internalId(map, 0));
}

void TestMapReshape::occupancyIncludesUnconnectedRoomsTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{1, 0, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    // No exit connects this room to the others, but it still occupies space
    // the solver must not move a room onto.
    builder.addRoom(2, Coordinate{3, 0, 0});
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    options.obstaclePadding = 4;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    QVERIFY(findRoomInScope(graph, map, 2) == nullptr);
    const auto occupant = graph.findOccupant(Coordinate{3, 0, 0});
    QVERIFY(occupant.has_value());
    QCOMPARE(*occupant, internalId(map, 2));

    // Empty space stays empty: a gap is not a missing room.
    QVERIFY(!graph.findOccupant(Coordinate{2, 0, 0}).has_value());
}

void TestMapReshape::buildForAreaTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0}, "Bree");
    builder.addRoom(1, Coordinate{1, 0, 0}, "Bree");
    builder.addRoom(2, Coordinate{2, 0, 0}, "Great East Road");
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    builder.linkBoth(1, ExitDirEnum::EAST, 2);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const auto graph = MapReshapeGraph::buildForArea(map, makeRoomArea("Bree"), options);
    QVERIFY(graph.has_value());

    QCOMPARE(findRoomInScope(*graph, map, 0)->role, LayoutRoomRoleEnum::Core);
    QCOMPARE(findRoomInScope(*graph, map, 1)->role, LayoutRoomRoleEnum::Core);
    // The neighbouring area is pulled in as margin, not as core: reshaping one
    // area should only minimally disturb what surrounds it.
    QCOMPARE(findRoomInScope(*graph, map, 2)->role, LayoutRoomRoleEnum::Margin);

    const ReshapeStatistics stats = graph->computeStatistics();
    QCOMPARE(stats.coreRooms, size_t(2));
    QCOMPARE(stats.marginRooms, size_t(1));
    QCOMPARE(stats.roomsMoved, size_t(0));

    QVERIFY(!MapReshapeGraph::buildForArea(map, makeRoomArea("Nowhere"), options).has_value());
}

void TestMapReshape::existingCollisionsAreReportedTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(3);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1, 2}), options);

    // A well-formed layout starts clean.
    QVERIFY(graph.findCollisions().empty());
    QCOMPARE(graph.computeStatistics().conflictsBefore, size_t(0));
    QCOMPARE(graph.countZLevels(), size_t(1));
}

void TestMapReshape::staleSelectionIsIgnoredTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    RoomIdSet core = makeCore(map, {0});
    // A selection can outlive the rooms it names; that must not throw.
    core.insert(RoomId{9999});

    ReshapeOptions options;
    options.marginRings = 0;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, core, options);

    QCOMPARE(graph.computeStatistics().coreRooms, size_t(1));
    QVERIFY(findRoomInScope(graph, map, 0) != nullptr);
}

// ---------------------------------------------------------------------------
// Scoring basics
// ---------------------------------------------------------------------------

void TestMapReshape::cleanLayoutScoresNearZeroTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(3);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1, 2}), options);
    const LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    const LayoutScore s = MapReshapeScorer::score(graph, positions);

    // A tidy straight corridor has nothing wrong with it. Only compactness is
    // non-zero, because the rooms legitimately span some space.
    QCOMPARE(s.collision, int64_t(0));
    QCOMPARE(s.direction, int64_t(0));
    QCOMPARE(s.alignment, int64_t(0));
    QCOMPARE(s.edgeLength, int64_t(0));
    QCOMPARE(s.movement, int64_t(0));
    QCOMPARE(s.zLayers, int64_t(0));
    QCOMPARE(s.layerMismatch, int64_t(0));
    QVERIFY(MapReshapeScorer::findIssues(graph, positions).empty());
}

void TestMapReshape::wrongDirectionIsPenalizedTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    // Put the east neighbour to the west of its source. Both directed edges
    // now point the wrong way.
    positions[scopeIndex(graph, map, 1)] = Coordinate{-1, 0, 0};

    const LayoutScore s = MapReshapeScorer::score(graph, positions);
    const ReshapeWeights weights;
    // The penalty is graded by how wrong the direction is, not a flat charge,
    // so that local search has a gradient to descend instead of a plateau.
    // Each of the two directed edges is one cell past the boundary, costing
    // (1 - along) = 2 units of the weight.
    QCOMPARE(s.direction, weights.wrongDirection * 4);
    QCOMPARE(countIssues(MapReshapeScorer::findIssues(graph, positions),
                         LayoutIssueEnum::WrongDirection),
             size_t(2));
}

void TestMapReshape::misalignmentIsProportionalTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);
    const ReshapeWeights weights;

    const auto alignmentAtOffset = [&](const int dy) {
        LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
        positions[scopeIndex(graph, map, 1)] = Coordinate{1, dy, 0};
        return MapReshapeScorer::score(graph, positions).alignment;
    };

    // An east exit wants equal y; drifting off that axis costs per cell, and
    // twice as far costs twice as much.
    QCOMPARE(alignmentAtOffset(0), int64_t(0));
    QCOMPARE(alignmentAtOffset(1), weights.alignment * 2);
    QCOMPARE(alignmentAtOffset(2), weights.alignment * 4);
}

void TestMapReshape::overlongEdgeIsMildTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);
    const ReshapeWeights weights;

    LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    positions[scopeIndex(graph, map, 1)] = Coordinate{3, 0, 0};
    const LayoutScore s = MapReshapeScorer::score(graph, positions);

    // "A . . B" is a legitimate rendering of one exit: stretched, not broken.
    // Two extra cells on each of the two directed edges.
    QCOMPARE(s.edgeLength, weights.edgeLength * 4);
    QCOMPARE(s.direction, int64_t(0));
    QCOMPARE(countIssues(MapReshapeScorer::findIssues(graph, positions), LayoutIssueEnum::Overlong),
             size_t(2));
}

void TestMapReshape::boundaryStretchIsCheaperTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(4);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0}), options);

    // Stretch the margin room away from its fixed neighbour. This is the
    // mechanism that lets an area grow when the world around it cannot move.
    LayoutPositions stretched = MapReshapeScorer::currentPositions(graph);
    stretched[scopeIndex(graph, map, 1)] = Coordinate{-2, 0, 0};
    const LayoutScore s = MapReshapeScorer::score(graph, stretched);

    QVERIFY(s.boundary > 0);
    const ReshapeWeights weights;
    QVERIFY(weights.boundaryEdgeLength < weights.edgeLength);
}

void TestMapReshape::collisionIsDetectedTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(3);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1, 2}), options);

    LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    positions[scopeIndex(graph, map, 1)] = positions[scopeIndex(graph, map, 0)];

    const LayoutScore s = MapReshapeScorer::score(graph, positions);
    QCOMPARE(s.collision, ReshapeWeights{}.collision);
    QCOMPARE(countIssues(MapReshapeScorer::findIssues(graph, positions), LayoutIssueEnum::Collision),
             size_t(1));
}

void TestMapReshape::externalObstacleCollidesTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{1, 0, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    // Unconnected, so it never enters the scope -- but it still owns its cell.
    builder.addRoom(2, Coordinate{4, 0, 0});
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    options.obstaclePadding = 6;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    positions[scopeIndex(graph, map, 1)] = Coordinate{4, 0, 0};

    // The fixed room is authoritative: it is the candidate placement that is
    // wrong, not the room that was already there.
    QCOMPARE(MapReshapeScorer::score(graph, positions).collision, ReshapeWeights{}.collision);
}

void TestMapReshape::movingImmovableRoomIsRejectedTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(4);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0}), options);

    LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    // Room 2 is the fixed anchor beyond the margin; it must never move.
    positions[scopeIndex(graph, map, 2)] = Coordinate{2, 5, 0};

    const LayoutScore s = MapReshapeScorer::score(graph, positions);
    QCOMPARE(s.immovableMoved, ReshapeWeights{}.immovableMoved);
    QCOMPARE(countIssues(MapReshapeScorer::findIssues(graph, positions),
                         LayoutIssueEnum::ImmovableMoved),
             size_t(1));
}

void TestMapReshape::mismatchedPositionsThrowTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    positions.pop_back();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             (void) MapReshapeScorer::score(graph, positions));
}

// ---------------------------------------------------------------------------
// Scoring: the spec's priority ordering
// ---------------------------------------------------------------------------

void TestMapReshape::stretchingBeatsNewLayerTest()
{
    mmqt::HideQDebug forThisTest;
    // The spec's motivating example: A-B-C-D with E hanging south of C. If E
    // will not fit, widening the row must always beat pushing E onto another
    // layer.
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{1, 0, 0});
    builder.addRoom(2, Coordinate{2, 0, 0});
    builder.addRoom(3, Coordinate{3, 0, 0});
    builder.addRoom(4, Coordinate{2, -1, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    builder.linkBoth(1, ExitDirEnum::EAST, 2);
    builder.linkBoth(2, ExitDirEnum::EAST, 3);
    builder.linkBoth(2, ExitDirEnum::SOUTH, 4);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map,
                                                         makeCore(map, {0, 1, 2, 3, 4}),
                                                         options);

    // Candidate A: spread the row out horizontally.
    LayoutPositions stretched = MapReshapeScorer::currentPositions(graph);
    stretched[scopeIndex(graph, map, 2)] = Coordinate{3, 0, 0};
    stretched[scopeIndex(graph, map, 3)] = Coordinate{5, 0, 0};
    stretched[scopeIndex(graph, map, 4)] = Coordinate{3, -1, 0};

    // Candidate B: leave x/y alone and lift one room to another layer.
    LayoutPositions layered = MapReshapeScorer::currentPositions(graph);
    layered[scopeIndex(graph, map, 4)] = Coordinate{2, -1, 1};

    const int64_t stretchedScore = MapReshapeScorer::score(graph, stretched).total();
    const int64_t layeredScore = MapReshapeScorer::score(graph, layered).total();

    QVERIFY2(stretchedScore < layeredScore,
             "horizontal stretching must be preferred over using another z layer");
}

void TestMapReshape::directionBeatsExactSpacingTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    // Correct direction, but two cells away instead of one.
    LayoutPositions farButCorrect = MapReshapeScorer::currentPositions(graph);
    farButCorrect[scopeIndex(graph, map, 1)] = Coordinate{2, 0, 0};

    // Perfect unit spacing, but the east exit now points west.
    LayoutPositions closeButBackwards = MapReshapeScorer::currentPositions(graph);
    closeButBackwards[scopeIndex(graph, map, 1)] = Coordinate{-1, 0, 0};

    QVERIFY2(MapReshapeScorer::score(graph, farButCorrect).total()
                 < MapReshapeScorer::score(graph, closeButBackwards).total(),
             "an east exit of length two must beat one that points west");
}

void TestMapReshape::distantMarginCostsMoreTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(5);

    ReshapeOptions options;
    // Two margin rings, so there is a near margin and a far one.
    options.marginRings = 2;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0}), options);

    QCOMPARE(findRoomInScope(graph, map, 1)->marginDistance, 1);
    QCOMPARE(findRoomInScope(graph, map, 2)->marginDistance, 2);

    const auto movementCostOfShifting = [&](const uint32_t externalId) {
        LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
        const size_t index = scopeIndex(graph, map, externalId);
        positions[index] = positions[index] + Coordinate{0, 1, 0};
        return MapReshapeScorer::score(graph, positions).movement;
    };

    // Resistance grows with distance from the core, so displacement is
    // absorbed as close to the core as possible.
    QVERIFY(movementCostOfShifting(2) > movementCostOfShifting(1));
}

void TestMapReshape::coreMovesCheaperThanMarginTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(4);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0}), options);

    const auto movementCostOfShifting = [&](const uint32_t externalId) {
        LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
        const size_t index = scopeIndex(graph, map, externalId);
        positions[index] = positions[index] + Coordinate{0, 1, 0};
        return MapReshapeScorer::score(graph, positions).movement;
    };

    // Moving core rooms is the point of the exercise; disturbing the
    // surroundings is a cost to be minimised.
    QVERIFY(movementCostOfShifting(0) < movementCostOfShifting(1));
}

void TestMapReshape::collisionOutranksEverythingTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(3);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1, 2}), options);

    // A thoroughly ugly but valid layout: backwards, misaligned and stretched.
    LayoutPositions ugly = MapReshapeScorer::currentPositions(graph);
    ugly[scopeIndex(graph, map, 1)] = Coordinate{-7, 9, 0};
    ugly[scopeIndex(graph, map, 2)] = Coordinate{-14, -9, 0};

    // A layout that is otherwise perfect except two rooms share one cell.
    LayoutPositions colliding = MapReshapeScorer::currentPositions(graph);
    colliding[scopeIndex(graph, map, 1)] = colliding[scopeIndex(graph, map, 0)];

    QVERIFY2(MapReshapeScorer::score(graph, colliding).total()
                 > MapReshapeScorer::score(graph, ugly).total(),
             "a collision must outrank any amount of merely ugly geometry");
}

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

namespace {

/// A 3x2 lattice, already laid out correctly:
///     A-B-C
///     | | |
///     D-E-F
NODISCARD Map buildCleanGrid()
{
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 1, 0});
    builder.addRoom(1, Coordinate{1, 1, 0});
    builder.addRoom(2, Coordinate{2, 1, 0});
    builder.addRoom(3, Coordinate{0, 0, 0});
    builder.addRoom(4, Coordinate{1, 0, 0});
    builder.addRoom(5, Coordinate{2, 0, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    builder.linkBoth(1, ExitDirEnum::EAST, 2);
    builder.linkBoth(3, ExitDirEnum::EAST, 4);
    builder.linkBoth(4, ExitDirEnum::EAST, 5);
    builder.linkBoth(0, ExitDirEnum::SOUTH, 3);
    builder.linkBoth(1, ExitDirEnum::SOUTH, 4);
    builder.linkBoth(2, ExitDirEnum::SOUTH, 5);
    return builder.build();
}

NODISCARD RoomIdSet allRooms(const Map &map, const uint32_t count)
{
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < count; ++i) {
        ids.push_back(i);
    }
    return makeCore(map, ids);
}

} // namespace

void TestMapReshape::cleanGridIsLeftAloneTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildCleanGrid();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 6), options);

    const ReshapeResult result = MapReshapeSolver::solve(graph);

    // A layout with nothing wrong with it must not be churned: "no
    // unnecessary movement" is the baseline the spec asks for first.
    QCOMPARE(result.status, ReshapeStatusEnum::Unchanged);
    QVERIFY(result.moves.empty());
}

void TestMapReshape::misalignedRoomIsStraightenedTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    // Correct side, but well off the east-west axis.
    builder.addRoom(1, Coordinate{1, 3, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 2), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    // An east exit should end up sharing a row with its source.
    const Coordinate a = solved[scopeIndex(graph, map, 0)];
    const Coordinate b = solved[scopeIndex(graph, map, 1)];
    QCOMPARE(a.y, b.y);
    QVERIFY(b.x > a.x);
}

void TestMapReshape::wrongDirectionIsFixedTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    // East exit whose target sits to the west: the layout contradicts the map.
    // Placed off-axis deliberately. Two rooms sharing a row cannot be swapped
    // by unit moves at all, because the one that must pass has to step through
    // the other's cell and pay a collision on the way. Getting round that
    // needs the structural moves of a later milestone, not a better hill
    // climber; here there is a clear path around.
    builder.addRoom(1, Coordinate{-2, 1, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 2), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    QVERIFY2(solved[scopeIndex(graph, map, 1)].x > solved[scopeIndex(graph, map, 0)].x,
             "the east neighbour must end up east of its source");
    QCOMPARE(MapReshapeScorer::score(graph, solved).direction, int64_t(0));
}

void TestMapReshape::unnecessaryGapIsClosedTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    // "A . . . B" with nothing needing the space in between.
    builder.addRoom(1, Coordinate{4, 0, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 2), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    const int gap = solved[scopeIndex(graph, map, 1)].x - solved[scopeIndex(graph, map, 0)].x;
    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    QVERIFY2(gap < 4, "an unnecessary gap should shrink");
    QVERIFY2(gap >= 1, "but the exit must still point east");
}

void TestMapReshape::immovableRoomsNeverMoveTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(5);

    ReshapeOptions options;
    options.marginRings = 1;
    options.pinned.insert(internalId(map, 1));
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    for (size_t i = 0; i < graph.getRooms().size(); ++i) {
        const LayoutRoom &room = graph.getRooms()[i];
        if (!isMovable(room.role)) {
            QCOMPARE(solved[i], room.original);
        }
    }
    for (const RoomMove &move : result.moves) {
        const LayoutRoom &room = graph.getRoom(*graph.findIndex(move.room));
        QVERIFY(isMovable(room.role));
    }
}

void TestMapReshape::solverIntroducesNoCollisionsTest()
{
    mmqt::HideQDebug forThisTest;
    // A hub with four neighbours, all initially scattered, so the solver has
    // to pull them in without stacking them on one another.
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{3, 1, 0});
    builder.addRoom(2, Coordinate{-3, 1, 0});
    builder.addRoom(3, Coordinate{1, 4, 0});
    builder.addRoom(4, Coordinate{1, -4, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    builder.linkBoth(0, ExitDirEnum::WEST, 2);
    builder.linkBoth(0, ExitDirEnum::NORTH, 3);
    builder.linkBoth(0, ExitDirEnum::SOUTH, 4);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 5), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    QVERIFY(result.conflicts.empty());
    QCOMPARE(MapReshapeScorer::score(graph, solved).collision, int64_t(0));
}

void TestMapReshape::solvedLayoutIsLocalMinimumTest()
{
    mmqt::HideQDebug forThisTest;
    // The solver prices candidate moves with an incremental delta instead of
    // rescoring the whole layout. If that arithmetic is wrong anywhere -- edge
    // costs, collision pairs, the bounding box behind compactness -- the
    // search stops somewhere that is not actually a local minimum. Checking
    // the finished layout against full scoring catches that without reaching
    // into the solver's internals.
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{4, 2, 0});
    builder.addRoom(2, Coordinate{-3, 5, 0});
    builder.addRoom(3, Coordinate{7, -4, 0});
    builder.addRoom(4, Coordinate{2, 2, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    builder.linkBoth(1, ExitDirEnum::NORTH, 2);
    builder.linkBoth(1, ExitDirEnum::SOUTH, 3);
    builder.linkBoth(0, ExitDirEnum::NORTH, 4);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 5), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);
    const int64_t settled = MapReshapeScorer::score(graph, solved).total();

    const std::vector<Coordinate> steps = {Coordinate{1, 0, 0},
                                           Coordinate{-1, 0, 0},
                                           Coordinate{0, 1, 0},
                                           Coordinate{0, -1, 0}};
    for (size_t i = 0; i < graph.getRooms().size(); ++i) {
        if (!isMovable(graph.getRooms()[i].role)) {
            continue;
        }
        for (const Coordinate &step : steps) {
            LayoutPositions trial = solved;
            trial[i] = trial[i] + step;
            QVERIFY2(MapReshapeScorer::score(graph, trial).total() >= settled,
                     "solver stopped at a layout a single unit move could improve, so the "
                     "incremental score delta disagrees with full scoring");
        }
    }
}

void TestMapReshape::solverNeverWorsensScoreTest()
{
    mmqt::HideQDebug forThisTest;
    SyntheticMap builder;
    builder.addRoom(0, Coordinate{0, 0, 0});
    builder.addRoom(1, Coordinate{5, 5, 0});
    builder.addRoom(2, Coordinate{-6, 3, 0});
    builder.linkBoth(0, ExitDirEnum::EAST, 1);
    builder.linkBoth(0, ExitDirEnum::SOUTH, 2);
    const Map map = builder.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, allRooms(map, 3), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    // Reported figures must match what an independent full scoring says, and
    // a reshape must never hand back something worse than it started with.
    QCOMPARE(MapReshapeScorer::score(graph, solved).total(), result.stats.scoreAfter);
    QVERIFY(result.stats.scoreAfter <= result.stats.scoreBefore);
    if (result.status == ReshapeStatusEnum::Improved) {
        QVERIFY(result.stats.scoreAfter < result.stats.scoreBefore);
        QVERIFY(!result.moves.empty());
    }
}

void TestMapReshape::emptyScopeIsUnchangedTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);

    ReshapeOptions options;
    options.marginRings = 0;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, RoomIdSet{}, options);

    QVERIFY(graph.empty());
    const ReshapeResult result = MapReshapeSolver::solve(graph);
    QCOMPARE(result.status, ReshapeStatusEnum::Unchanged);
    QVERIFY(result.moves.empty());
}

QTEST_MAIN(TestMapReshape)
