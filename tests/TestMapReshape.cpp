// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestMapReshape.h"

#include "../src/global/HideQDebug.h"
#include "../src/global/progresscounter.h"
#include "../src/map/ChangeList.h"
#include "../src/map/ChangeTypes.h"
#include "../src/map/ExitDirection.h"
#include "../src/map/ExitFlags.h"
#include "../src/map/Map.h"
#include "../src/map/MapReshapeApply.h"
#include "../src/map/MapReshapeGraph.h"
#include "../src/map/MapReshapeScaling.h"
#include "../src/map/MapReshapeScorer.h"
#include "../src/map/MapReshapeSolver.h"
#include "../src/map/MapReshapeTypes.h"
#include "../src/map/RawRoom.h"
#include "../src/map/RoomHandle.h"
#include "../src/map/RoomIdSet.h"
#include "../src/map/coordinate.h"
#include "../src/map/mmapper2room.h"
#include "../src/map/roomid.h"
#include "../src/syntax/Sublist.h"
#include "../src/syntax/SyntaxArgs.h"
#include "../src/syntax/TokenMatcher.h"
#include "../src/syntax/syntax-helpers.h"

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

    // Includes the vertical steps: the z terms (leaving a layer, and the
    // scope-wide charge for an extra layer) live outside the per-room cost
    // functions, so they were the easiest ones for the delta to miss.
    const std::vector<Coordinate> steps = {Coordinate{1, 0, 0},
                                           Coordinate{-1, 0, 0},
                                           Coordinate{0, 1, 0},
                                           Coordinate{0, -1, 0},
                                           Coordinate{0, 0, 1},
                                           Coordinate{0, 0, -1}};
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

namespace {

/// Two rows tied together by vertical exits, packed with no slack, plus one
/// extra room on the bottom row that has nowhere to sit.
///
/// Every single-room move out of this is a loss: sliding a room right lands
/// on its neighbour, and sliding the rightmost one right breaks a vertical
/// alignment for more than the move saves. Only shifting a whole column can
/// open the space, and even that pays off just after a room fills it.
///
///     10 11 12 13        (top row, ids 10..13)
///      0  1  2  3        (bottom row, ids 0..3)
///         99             (the homeless room, connected between 1 and 2)
NODISCARD Map buildPackedLadder()
{
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 0});
    b.addRoom(2, Coordinate{2, 0, 0});
    b.addRoom(3, Coordinate{3, 0, 0});
    b.addRoom(10, Coordinate{0, 1, 0});
    b.addRoom(11, Coordinate{1, 1, 0});
    b.addRoom(12, Coordinate{2, 1, 0});
    b.addRoom(13, Coordinate{3, 1, 0});
    b.addRoom(99, Coordinate{1, -1, 0});
    b.linkBoth(0, ExitDirEnum::EAST, 1);
    b.linkBoth(1, ExitDirEnum::EAST, 99);
    b.linkBoth(99, ExitDirEnum::EAST, 2);
    b.linkBoth(2, ExitDirEnum::EAST, 3);
    b.linkBoth(10, ExitDirEnum::EAST, 11);
    b.linkBoth(11, ExitDirEnum::EAST, 12);
    b.linkBoth(12, ExitDirEnum::EAST, 13);
    b.linkBoth(0, ExitDirEnum::NORTH, 10);
    b.linkBoth(1, ExitDirEnum::NORTH, 11);
    b.linkBoth(2, ExitDirEnum::NORTH, 12);
    b.linkBoth(3, ExitDirEnum::NORTH, 13);
    return b.build();
}

NODISCARD MapReshapeGraph packedLadderGraph(const Map &map)
{
    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (const uint32_t id : {0u, 1u, 2u, 3u, 10u, 11u, 12u, 13u, 99u}) {
        core.insert(internalId(map, id));
    }
    return MapReshapeGraph::build(map, core, options);
}

/// The x coordinates of the given rooms, which must all share one row.
NODISCARD std::vector<int> rowColumns(const MapReshapeGraph &graph,
                                      const Map &map,
                                      const LayoutPositions &positions,
                                      const std::vector<uint32_t> &ids)
{
    std::vector<int> xs;
    for (const uint32_t id : ids) {
        xs.push_back(positions[scopeIndex(graph, map, id)].x);
    }
    std::sort(xs.begin(), xs.end());
    return xs;
}

NODISCARD bool allShareRow(const MapReshapeGraph &graph,
                           const Map &map,
                           const LayoutPositions &positions,
                           const std::vector<uint32_t> &ids)
{
    const int y = positions[scopeIndex(graph, map, ids.front())].y;
    for (const uint32_t id : ids) {
        if (positions[scopeIndex(graph, map, id)].y != y) {
            return false;
        }
    }
    return true;
}

} // namespace

void TestMapReshape::structuralShiftEscapesDeadlockTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildPackedLadder();
    const MapReshapeGraph graph = packedLadderGraph(map);

    ReshapeSolverOptions unitOnly;
    unitOnly.allowStructuralMoves = false;

    ReshapeResult unitResult;
    (void) MapReshapeSolver::solvePositions(graph, unitOnly, unitResult);

    // Unit moves alone are genuinely stuck here: every single-room move makes
    // the layout worse, so hill climbing accepts none of them.
    QCOMPARE(unitResult.status, ReshapeStatusEnum::Unchanged);
    QCOMPARE(unitResult.stats.scoreAfter, unitResult.stats.scoreBefore);

    ReshapeResult structuralResult;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, structuralResult);

    QCOMPARE(structuralResult.status, ReshapeStatusEnum::Improved);
    QVERIFY2(structuralResult.stats.scoreAfter < unitResult.stats.scoreAfter,
             "shifting a whole column must reach what unit moves cannot");
    QVERIFY(structuralResult.conflicts.empty());
    QCOMPARE(MapReshapeScorer::score(graph, solved).collision, int64_t(0));
}

void TestMapReshape::insertedColumnCreatesNoFakeRoomTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildPackedLadder();
    const MapReshapeGraph graph = packedLadderGraph(map);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);
    QCOMPARE(result.status, ReshapeStatusEnum::Improved);

    // Assertions are translation-invariant: what matters is the shape, not
    // where the solver happened to park it.
    const std::vector<uint32_t> bottom = {0u, 1u, 99u, 2u, 3u};
    const std::vector<uint32_t> top = {10u, 11u, 12u, 13u};
    QVERIFY(allShareRow(graph, map, solved, bottom));
    QVERIFY(allShareRow(graph, map, solved, top));

    // The bottom row now holds all five rooms side by side.
    const std::vector<int> bottomXs = rowColumns(graph, map, solved, bottom);
    QCOMPARE(bottomXs.back() - bottomXs.front(), 4);
    for (size_t i = 1; i < bottomXs.size(); ++i) {
        QCOMPARE(bottomXs[i] - bottomXs[i - 1], 1);
    }

    // The top row spans the same five columns with only four rooms, so one
    // column is simply empty. That gap is the point: the solver allocated
    // geometric space rather than inventing a room to fill it.
    const std::vector<int> topXs = rowColumns(graph, map, solved, top);
    QCOMPARE(topXs.size(), size_t(4));
    QCOMPARE(topXs.back() - topXs.front(), 4);

    // And the scope still contains exactly the rooms it started with.
    QCOMPARE(graph.getRooms().size(), size_t(9));
}

void TestMapReshape::structuralShiftRespectsImmovableRoomsTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildPackedLadder();

    ReshapeOptions options;
    options.marginRings = 1;
    // Freeze one room in the middle of the block. A whole-column shift must
    // step around it rather than dragging it along.
    options.pinned.insert(internalId(map, 12));
    RoomIdSet core;
    for (const uint32_t id : {0u, 1u, 2u, 3u, 10u, 11u, 12u, 13u, 99u}) {
        core.insert(internalId(map, id));
    }
    const MapReshapeGraph graph = MapReshapeGraph::build(map, core, options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph, {}, result);

    for (size_t i = 0; i < graph.getRooms().size(); ++i) {
        const LayoutRoom &room = graph.getRooms()[i];
        if (!isMovable(room.role)) {
            QCOMPARE(solved[i], room.original);
        }
    }
    QVERIFY(result.conflicts.empty());
}

namespace {

/// Shift every room of a chain one cell east.
NODISCARD std::vector<RoomMove> shiftChainEast(const Map &map, const uint32_t count)
{
    std::vector<RoomMove> moves;
    for (uint32_t id = 0; id < count; ++id) {
        const RoomHandle room = map.getRoomHandle(ExternalRoomId{id});
        moves.push_back(
            RoomMove{room.getId(), room.getPosition(), room.getPosition() + Coordinate{1, 0, 0}});
    }
    return moves;
}

NODISCARD bool isConsistent(const Map &map)
{
    try {
        ProgressCounter pc;
        map.checkConsistency(pc);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

void TestMapReshape::naiveApplyCorruptsTheMapTest()
{
    mmqt::HideQDebug forThisTest;
    // Documents why staging exists. Applying the same movements as plain
    // per-room changes leaves every room at the right coordinate, yet the map
    // is broken: changes are applied one at a time, so the first room lands
    // on the second's cell before the second has vacated it, and the spatial
    // index -- which stores only one room per coordinate -- quietly drops the
    // room that was overwritten.
    const Map map = buildChain(3);
    ProgressCounter pc;

    ChangeList naive;
    for (const RoomMove &move : shiftChainEast(map, 3)) {
        naive.add(room_change_types::MoveRelative{move.room, move.to - move.from});
    }
    const Map after = map.apply(pc, naive).map;

    // Positions look right...
    QCOMPARE(after.getRoomHandle(ExternalRoomId{0}).getPosition(), Coordinate(1, 0, 0));
    QCOMPARE(after.getRoomHandle(ExternalRoomId{2}).getPosition(), Coordinate(3, 0, 0));
    // ...but the rooms can no longer be found by coordinate, and the map does
    // not pass its own consistency check.
    QVERIFY(!after.findRoomHandle(Coordinate{1, 0, 0}));
    QVERIFY(!isConsistent(after));
}

void TestMapReshape::stagedApplyKeepsMapConsistentTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(3);
    ProgressCounter pc;

    const std::vector<RoomMove> moves = shiftChainEast(map, 3);
    const Map after = map.apply(pc, map_reshape::buildChanges(map, moves)).map;

    // Same destinations as the naive version...
    for (const RoomMove &move : moves) {
        QCOMPARE(after.getRoomHandle(after.getExternalRoomId(move.room)).getPosition(), move.to);
    }
    // ...but every room is still findable by coordinate, and the map is sound.
    for (int x = 1; x <= 3; ++x) {
        QVERIFY(after.findRoomHandle(Coordinate{x, 0, 0}));
    }
    QVERIFY(!after.findRoomHandle(Coordinate{0, 0, 0}));
    QVERIFY(isConsistent(after));
    QCOMPARE(after.getRoomsCount(), map.getRoomsCount());
}

void TestMapReshape::stagedApplyIsOneUndoStepTest()
{
    mmqt::HideQDebug forThisTest;
    // Undo works by snapshotting the whole immutable map per applyChanges
    // call, so a reshape is atomic as long as every movement travels in one
    // change list. Staging must not break that by needing a second call.
    const Map map = buildChain(4);
    const ChangeList changes = map_reshape::buildChanges(map, shiftChainEast(map, 4));

    // One staging translation plus one change per room.
    QCOMPARE(changes.getChanges().size(), size_t(5));

    ProgressCounter pc;
    const Map after = map.apply(pc, changes).map;
    QVERIFY(after != map);
    QVERIFY(isConsistent(after));
}

void TestMapReshape::emptyMoveListProducesNoChangesTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildChain(2);
    QVERIFY(map_reshape::buildChanges(map, {}).empty());
}

void TestMapReshape::areaSubcommandsDispatchTest()
{
    mmqt::HideQDebug forThisTest;
    using namespace syntax;

    // Mirrors the tree built in doMapCommand. The risky part is that
    // "reshape" appears twice as a sibling -- once bare, once taking an area
    // name -- so this checks the parser resolves both instead of treating
    // them as ambiguous.
    std::string dispatched;
    auto syn0 = [&dispatched](std::string name, std::string help, std::string tag) {
        auto fn = [&dispatched, tag](User &, const Pair *const) { dispatched = tag; };
        return buildSyntax(stringToken(std::move(name)), Accept(fn, std::move(help)));
    };
    auto named = [&dispatched](User &, const Pair *const args) {
        const auto v = getAnyVectorReversed(args);
        // Pinned deliberately. stringToken("area") matches without
        // contributing a value while abbrevToken("reshape") does, so the name
        // lands at index 1. Getting this wrong throws at runtime, where only
        // someone actually typing the command would find it.
        QCOMPARE(v.size(), size_t(2));
        QCOMPARE(v[0].getString(), std::string("reshape"));
        dispatched = "named:" + v[1].getString();
    };

    auto named3d = [&dispatched](User &, const Pair *const args) {
        const auto v = getAnyVectorReversed(args);
        dispatched = "named3d:" + v[1].getString();
    };
    const auto areaSyntax
        = buildSyntax(stringToken("area"),
                      syn0("reshape", "reshape the area you are standing in", "current"),
                      buildSyntax(abbrevToken("reshape"),
                                  TokenMatcher::alloc<ArgString>(),
                                  Accept(named, "reshape the named area")),
                      syn0("reshape3d", "reshape in three dimensions", "volumetric"),
                      buildSyntax(abbrevToken("reshape3d"),
                                  TokenMatcher::alloc<ArgString>(),
                                  Accept(named3d, "reshape the named area in three dimensions")),
                      syn0("check", "report layout problems", "check"));
    const auto root = buildSyntax(stringToken("_map"), areaSyntax);

    const auto run = [&dispatched, &root](const char *const args) {
        dispatched.clear();
        const std::string owned{args};
        std::ignore = processSyntax(root, "_map", StringView{owned});
        return dispatched;
    };

    QCOMPARE(run("area reshape"), std::string("current"));
    QCOMPARE(run("area check"), std::string("check"));
    QCOMPARE(run("area reshape Bree"), std::string("named:Bree"));
    // Area names contain spaces, so the argument must swallow them.
    QCOMPARE(run("area reshape \"Great East Road\""), std::string("named:Great East Road"));
    // Nonsense must not silently dispatch to anything.
    QCOMPARE(run("area bogus"), std::string());
}

// ---------------------------------------------------------------------------
// Z handling
// ---------------------------------------------------------------------------

namespace {

/// Rooms 0 and 1 side by side on one layer, joined by an up exit -- a
/// staircase a mapper drew flat so it could be read.
NODISCARD Map buildHandFlattenedStair()
{
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 0});
    b.linkBoth(0, ExitDirEnum::UP, 1);
    return b.build();
}

NODISCARD RoomIdSet coreOf(const Map &map, const std::vector<uint32_t> &ids)
{
    return makeCore(map, ids);
}

} // namespace

void TestMapReshape::flattenCollapsesUnnecessaryLayerTest()
{
    mmqt::HideQDebug forThisTest;
    // A north exit that also changes layer, for no reason: there is free
    // space directly north of room 1 on its own layer.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 0});
    b.addRoom(2, Coordinate{1, 1, 1});
    b.linkBoth(0, ExitDirEnum::EAST, 1);
    b.linkBoth(1, ExitDirEnum::NORTH, 2);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, coreOf(map, {0, 1, 2}), options);
    QCOMPARE(graph.countZLevels(), size_t(2));

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Flatten),
                                                                    result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    QCOMPARE(result.stats.zLevelsAfter, size_t(1));
    for (const Coordinate &pos : solved) {
        QCOMPARE(pos.z, 0);
    }
}

void TestMapReshape::flattenLeavesHandFlattenedStackAloneTest()
{
    mmqt::HideQDebug forThisTest;
    // The case that matters most in practice. Mappers routinely draw small
    // interiors flat for readability, and those are deliberate, not mistakes.
    // Flattening must leave them exactly as they are rather than deciding an
    // up exit ought to mean another layer.
    const Map map = buildHandFlattenedStair();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, coreOf(map, {0, 1}), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Flatten),
                                                                    result);

    for (const Coordinate &pos : solved) {
        QCOMPARE(pos.z, 0);
    }
    QCOMPARE(result.stats.zLevelsAfter, size_t(1));
}

void TestMapReshape::volumetricRestoresVerticalityTest()
{
    mmqt::HideQDebug forThisTest;
    // The same flattened staircase, asked for in three dimensions instead.
    const Map map = buildHandFlattenedStair();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, coreOf(map, {0, 1}), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    const Coordinate lower = solved[scopeIndex(graph, map, 0)];
    const Coordinate upper = solved[scopeIndex(graph, map, 1)];
    QVERIFY2(upper.z > lower.z, "an up exit should lead upwards when asked for real height");
}

void TestMapReshape::volumetricKeepsExistingStackTest()
{
    mmqt::HideQDebug forThisTest;
    // Genuine verticality must survive: nothing in this mode should flatten.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{0, 0, 1});
    b.addRoom(2, Coordinate{0, 0, 2});
    b.linkBoth(0, ExitDirEnum::UP, 1);
    b.linkBoth(1, ExitDirEnum::UP, 2);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, coreOf(map, {0, 1, 2}), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    QCOMPARE(solved[scopeIndex(graph, map, 0)].z, 0);
    QCOMPARE(solved[scopeIndex(graph, map, 1)].z, 1);
    QCOMPARE(solved[scopeIndex(graph, map, 2)].z, 2);
}

void TestMapReshape::flattenNeverOpensANewLayerTest()
{
    mmqt::HideQDebug forThisTest;
    // The spec's governing rule, now enforced by the solver rather than only
    // by the score: a crowded layout must be resolved by spreading sideways,
    // never by moving a room onto another layer.
    const Map map = buildPackedLadder();
    const MapReshapeGraph graph = packedLadderGraph(map);
    QCOMPARE(graph.countZLevels(), size_t(1));

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Flatten),
                                                                    result);

    QCOMPARE(result.stats.zLevelsAfter, size_t(1));
    for (const Coordinate &pos : solved) {
        QCOMPARE(pos.z, 0);
    }
}

void TestMapReshape::solveRespectsIterationBudgetTest()
{
    mmqt::HideQDebug forThisTest;
    // Structural look-ahead runs a nested search per candidate. Those nested
    // searches used to start with a fresh allowance each, so the real bound
    // was the budget multiplied by the number of candidates -- which stayed
    // hidden only while frozen z let every refinement converge at once. On a
    // three-layer lattice that was the difference between five and a half
    // seconds and a second and a half.
    const Map map = buildPackedLadder();
    const MapReshapeGraph graph = packedLadderGraph(map);

    ReshapeSolverOptions options = ReshapeSolverOptions::forMode(ReshapeModeEnum::Flatten);
    options.maxSweeps = 3;
    // The floor that normally protects small scopes is lifted here, so the
    // sweep cap is the thing under test rather than the floor.
    options.minIterations = 0;
    const size_t cap = options.maxSweeps * graph.getRooms().size();

    ReshapeResult result;
    std::ignore = MapReshapeSolver::solvePositions(graph, options, result);

    QVERIFY2(result.stats.iterations <= cap,
             "every search, nested ones included, must share one iteration budget");
}

namespace {

/// A two-storey building drawn flat, the way a mapper draws one so it can be
/// read: ground floor 0-1, upper floor 2-3, each floor joined sideways, and
/// a staircase from 1 up to 2. Every room on one layer.
NODISCARD Map buildFlatTwoStorey()
{
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 0});
    b.addRoom(2, Coordinate{2, 0, 0});
    b.addRoom(3, Coordinate{3, 0, 0});
    b.linkBoth(0, ExitDirEnum::EAST, 1);
    b.linkBoth(2, ExitDirEnum::EAST, 3);
    b.linkBoth(1, ExitDirEnum::UP, 2);
    return b.build();
}

NODISCARD MapReshapeGraph twoStoreyGraph(const Map &map)
{
    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (const uint32_t id : {0u, 1u, 2u, 3u}) {
        core.insert(internalId(map, id));
    }
    return MapReshapeGraph::build(map, core, options);
}

} // namespace

void TestMapReshape::volumetricLiftsWholeStoreyTest()
{
    mmqt::HideQDebug forThisTest;
    const Map map = buildFlatTwoStorey();
    const MapReshapeGraph graph = twoStoreyGraph(map);
    QCOMPARE(graph.countZLevels(), size_t(1));

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    // Each floor keeps its own rooms together, and the upper one ends up
    // above the lower. Absolute height does not matter, only the relation.
    const int ground = solved[scopeIndex(graph, map, 0)].z;
    const int upper = solved[scopeIndex(graph, map, 2)].z;
    QCOMPARE(solved[scopeIndex(graph, map, 1)].z, ground);
    QCOMPARE(solved[scopeIndex(graph, map, 3)].z, upper);
    QVERIFY2(upper > ground, "the upper storey should end up above the lower one");
}

void TestMapReshape::liftingOneRoomAloneIsRejectedTest()
{
    mmqt::HideQDebug forThisTest;
    // Why whole-storey moves have to exist. Raising just the room at the top
    // of the stair fixes the staircase but drags its horizontal exit across
    // a layer, which costs more than it saves -- so no sequence of
    // single-room moves can ever unflatten this, whatever the weights are.
    const Map map = buildFlatTwoStorey();
    const MapReshapeGraph graph = twoStoreyGraph(map);
    const ReshapeWeights weights = makeWeights(ReshapeModeEnum::Volumetric);

    const LayoutPositions flat = MapReshapeScorer::currentPositions(graph);
    LayoutPositions oneLifted = flat;
    oneLifted[scopeIndex(graph, map, 2)].z += 1;

    LayoutPositions storeyLifted = flat;
    storeyLifted[scopeIndex(graph, map, 2)].z += 1;
    storeyLifted[scopeIndex(graph, map, 3)].z += 1;

    const int64_t flatScore = MapReshapeScorer::score(graph, flat, weights).total();
    const int64_t oneScore = MapReshapeScorer::score(graph, oneLifted, weights).total();
    const int64_t storeyScore = MapReshapeScorer::score(graph, storeyLifted, weights).total();

    QVERIFY2(oneScore > flatScore, "lifting one room out of its storey must be a loss");
    QVERIFY2(storeyScore < flatScore, "lifting the whole storey must be a gain");
}

void TestMapReshape::flattenCollapsesStoreyOntoParentTest()
{
    mmqt::HideQDebug forThisTest;
    // The same building, this time genuinely stacked, asked to lie flat.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 0});
    b.addRoom(2, Coordinate{1, 0, 1});
    b.addRoom(3, Coordinate{2, 0, 1});
    b.linkBoth(0, ExitDirEnum::EAST, 1);
    b.linkBoth(2, ExitDirEnum::EAST, 3);
    b.linkBoth(1, ExitDirEnum::UP, 2);
    const Map map = b.build();
    const MapReshapeGraph graph = twoStoreyGraph(map);
    QCOMPARE(graph.countZLevels(), size_t(2));

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Flatten),
                                                                    result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    QCOMPARE(result.stats.zLevelsAfter, size_t(1));
    // Collapsed by moving the storey as a unit, not by scattering its rooms.
    QCOMPARE(solved[scopeIndex(graph, map, 2)].z, solved[scopeIndex(graph, map, 3)].z);
    QVERIFY(MapReshapeScorer::score(graph, solved).collision == 0);
}

void TestMapReshape::nearSubcommandsDispatchTest()
{
    mmqt::HideQDebug forThisTest;
    using namespace syntax;

    // The radius-scoped verbs, which is how a map with one enormous area or
    // none at all gets reshaped in pieces. Each takes an integer rather than
    // a name, so the matched values differ from the area forms -- worth
    // pinning, since getting that index wrong throws only when someone
    // actually types the command.
    std::string dispatched;
    auto radius = [&dispatched](const char *const tag) {
        return [&dispatched, tag](User &, const Pair *const args) {
            const auto v = getAnyVectorReversed(args);
            QCOMPARE(v.size(), size_t(2));
            dispatched = std::string{tag} + ":" + std::to_string(v[1].getInt());
        };
    };
    const auto nearSyntax
        = buildSyntax(stringToken("near"),
                      buildSyntax(abbrevToken("reshape"),
                                  TokenMatcher::alloc<ArgInt>(),
                                  Accept(radius("flat"), "flatten within N steps")),
                      buildSyntax(abbrevToken("reshape3d"),
                                  TokenMatcher::alloc<ArgInt>(),
                                  Accept(radius("3d"), "three dimensions within N steps")),
                      buildSyntax(abbrevToken("check"),
                                  TokenMatcher::alloc<ArgInt>(),
                                  Accept(radius("check"), "report within N steps")));
    const auto root = buildSyntax(stringToken("_map"), nearSyntax);

    const auto run = [&dispatched, &root](const char *const args) {
        dispatched.clear();
        const std::string owned{args};
        std::ignore = processSyntax(root, "_map", StringView{owned});
        return dispatched;
    };

    QCOMPARE(run("near reshape 10"), std::string("flat:10"));
    QCOMPARE(run("near reshape3d 4"), std::string("3d:4"));
    QCOMPARE(run("near check 25"), std::string("check:25"));
    // A radius is required: without one there is no sensible default scope.
    QCOMPARE(run("near reshape"), std::string());
}

void TestMapReshape::volumetricRebuildsTowerTest()
{
    mmqt::HideQDebug forThisTest;
    // A five-storey tower drawn as a flat corridor, which is how such a
    // place usually ends up on a map. In three dimensions it should come
    // back as a tower: one storey per up exit.
    SyntheticMap b;
    for (uint32_t i = 0; i < 5; ++i) {
        b.addRoom(i, Coordinate{static_cast<int>(i), 0, 0});
    }
    for (uint32_t i = 0; i + 1 < 5; ++i) {
        b.linkBoth(i, ExitDirEnum::UP, i + 1);
    }
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (uint32_t i = 0; i < 5; ++i) {
        core.insert(internalId(map, i));
    }
    const MapReshapeGraph graph = MapReshapeGraph::build(map, core, options);
    QCOMPARE(graph.countZLevels(), size_t(1));

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    QCOMPARE(result.stats.zLevelsAfter, size_t(5));
    for (uint32_t i = 0; i + 1 < 5; ++i) {
        const Coordinate lower = solved[scopeIndex(graph, map, i)];
        const Coordinate upper = solved[scopeIndex(graph, map, i + 1)];
        QCOMPARE(upper.z - lower.z, 1);
    }
}

void TestMapReshape::volumetricStacksRoomsAboveEachOtherTest()
{
    mmqt::HideQDebug forThisTest;
    // Rooms joined by a stair belong above one another, not merely higher.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{4, 3, 0});
    b.linkBoth(0, ExitDirEnum::UP, 1);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    const Coordinate lower = solved[scopeIndex(graph, map, 0)];
    const Coordinate upper = solved[scopeIndex(graph, map, 1)];
    QCOMPARE(upper.z - lower.z, 1);
    QCOMPARE(upper.x, lower.x);
    QCOMPARE(upper.y, lower.y);
}

void TestMapReshape::volumetricKeepsHorizontalExitsOnOneLayerTest()
{
    mmqt::HideQDebug forThisTest;
    // Height must not be invented where the exits do not ask for it: a
    // corridor with no vertical exits stays on one layer however free
    // layers are in this mode.
    const Map map = buildChain(5);

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map,
                                                         makeCore(map, {0, 1, 2, 3, 4}),
                                                         options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    const int z = solved.front().z;
    for (const Coordinate &pos : solved) {
        QCOMPARE(pos.z, z);
    }
    QCOMPARE(MapReshapeScorer::score(graph, solved, makeWeights(ReshapeModeEnum::Volumetric))
                 .layerMismatch,
             int64_t(0));
}

void TestMapReshape::volumetricIgnoresOriginalHeightTest()
{
    mmqt::HideQDebug forThisTest;
    // Existing coordinates are a visualization, not evidence. Two rooms put
    // on wildly wrong layers by some earlier edit must be free to travel as
    // far as the exits require, so the prior on original height has to be
    // weak enough not to strand them.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 9});
    b.addRoom(2, Coordinate{2, 0, -7});
    b.linkBoth(0, ExitDirEnum::EAST, 1);
    b.linkBoth(1, ExitDirEnum::EAST, 2);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1, 2}), options);
    QCOMPARE(graph.countZLevels(), size_t(3));

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    // The exits are all horizontal, so the honest answer is one layer --
    // even though reaching it means moving a room nine levels.
    QCOMPARE(result.status, ReshapeStatusEnum::Improved);
    const int z = solved[scopeIndex(graph, map, 0)].z;
    QCOMPARE(solved[scopeIndex(graph, map, 1)].z, z);
    QCOMPARE(solved[scopeIndex(graph, map, 2)].z, z);
}

namespace {

/// A dense knot of rooms: a small grid whose every room is joined to its
/// neighbours, but laid out three cells apart because that is the only way
/// the rigid grid could fit it. A city or a keep looks like this.
NODISCARD Map buildCrampedBlock(const int side, const int spacing)
{
    SyntheticMap b;
    const auto id = [side](int x, int y) { return static_cast<uint32_t>(y * side + x); };
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            b.addRoom(id(x, y), Coordinate{x * spacing, y * spacing, 0});
        }
    }
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x + 1 < side; ++x) {
            b.linkBoth(id(x, y), ExitDirEnum::EAST, id(x + 1, y));
        }
    }
    for (int y = 0; y + 1 < side; ++y) {
        for (int x = 0; x < side; ++x) {
            b.linkBoth(id(x, y), ExitDirEnum::NORTH, id(x, y + 1));
        }
    }
    return b.build();
}

NODISCARD MapReshapeGraph wholeMapGraph(const Map &map, const int side)
{
    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (int i = 0; i < side * side; ++i) {
        core.insert(internalId(map, static_cast<uint32_t>(i)));
    }
    return MapReshapeGraph::build(map, core, options);
}

} // namespace

void TestMapReshape::crampedGroupIsFlaggedForScalingTest()
{
    mmqt::HideQDebug forThisTest;
    const int side = 4;
    const Map map = buildCrampedBlock(side, 3);
    const MapReshapeGraph graph = wholeMapGraph(map, side);
    const LayoutPositions positions = MapReshapeScorer::currentPositions(graph);

    const auto candidates = MapReshapeScaling::findCandidates(graph, positions);
    QCOMPARE(candidates.size(), size_t(1));
    QCOMPARE(candidates.front().rooms.size(), size_t(side * side));
    // Every exit inside the block spans three cells, so drawing it at a
    // third of the size would make each read as one again.
    QCOMPARE(candidates.front().averageEdgeLength, 3.0);
    QVERIFY(candidates.front().suggestedScale() < 0.4);
}

void TestMapReshape::naturallySparseGroupIsNotFlaggedTest()
{
    mmqt::HideQDebug forThisTest;
    // Sparseness on its own proves nothing. A road is spread out across the
    // map yet its exits are one cell each, so it is not cramped and must not
    // be flagged -- otherwise every long road would be proposed for
    // shrinking.
    SyntheticMap b;
    for (uint32_t i = 0; i < 20; ++i) {
        b.addRoom(i, Coordinate{static_cast<int>(i), 0, 0});
    }
    for (uint32_t i = 0; i + 1 < 20; ++i) {
        b.linkBoth(i, ExitDirEnum::EAST, i + 1);
    }
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (uint32_t i = 0; i < 20; ++i) {
        core.insert(internalId(map, i));
    }
    const MapReshapeGraph graph = MapReshapeGraph::build(map, core, options);

    QVERIFY(
        MapReshapeScaling::findCandidates(graph, MapReshapeScorer::currentPositions(graph)).empty());
}

void TestMapReshape::suggestedScaleShrinksTheFootprintTest()
{
    mmqt::HideQDebug forThisTest;
    const int side = 5;
    const Map map = buildCrampedBlock(side, 4);
    const MapReshapeGraph graph = wholeMapGraph(map, side);
    const LayoutPositions positions = MapReshapeScorer::currentPositions(graph);

    const auto candidates = MapReshapeScaling::findCandidates(graph, positions);
    QCOMPARE(candidates.size(), size_t(1));
    const ScalingCandidate &candidate = candidates.front();

    // The block currently spans 17x17 cells for 25 rooms. Drawn at the
    // suggested scale it needs a footprint barely larger than the rooms
    // themselves, which is what a local space's portal would be sized to.
    QCOMPARE(candidate.spanWidth, 17);
    QCOMPARE(candidate.spanHeight, 17);
    QVERIFY(candidate.scaledWidth() < candidate.spanWidth);
    QVERIFY(candidate.scaledHeight() < candidate.spanHeight);
    QVERIFY(candidate.scaledWidth() <= side + 1);
}

void TestMapReshape::hangingAreaMovesUnderItsEntranceTest()
{
    mmqt::HideQDebug forThisTest;
    // The shape a cellar or a mine usually has on a map: a surface room,
    // and the place below it drawn well off to the north because that is
    // where there happened to be space. The two are joined only by the way
    // down.
    //
    // Giving the lower place its own layer is not enough on its own -- it
    // also has to come back over to sit beneath the room it hangs from, and
    // no single-room move can do that without tearing its own exits apart.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    for (uint32_t i = 1; i <= 6; ++i) {
        b.addRoom(i, Coordinate{static_cast<int>(i) - 1, 10, 0});
    }
    for (uint32_t i = 1; i + 1 <= 6; ++i) {
        b.linkBoth(i, ExitDirEnum::EAST, i + 1);
    }
    b.linkBoth(0, ExitDirEnum::DOWN, 1);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (uint32_t i = 0; i <= 6; ++i) {
        core.insert(internalId(map, i));
    }
    const MapReshapeGraph graph = MapReshapeGraph::build(map, core, options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Volumetric),
                                                                    result);

    QCOMPARE(result.status, ReshapeStatusEnum::Improved);

    const Coordinate entrance = solved[scopeIndex(graph, map, 0)];
    const Coordinate landing = solved[scopeIndex(graph, map, 1)];

    // Down leads down...
    QVERIFY2(landing.z < entrance.z, "a down exit should lead downwards");
    // ...and lands directly beneath the room it came from, rather than ten
    // cells away across the map.
    QCOMPARE(landing.x, entrance.x);
    QCOMPARE(landing.y, entrance.y);

    // The lower place travelled as one piece: still a straight row.
    for (uint32_t i = 1; i <= 6; ++i) {
        const Coordinate room = solved[scopeIndex(graph, map, i)];
        QCOMPARE(room.y, landing.y);
        QCOMPARE(room.z, landing.z);
        QCOMPARE(room.x, landing.x + static_cast<int>(i) - 1);
    }
}

void TestMapReshape::draggedApartIslandClosesUpTest()
{
    mmqt::HideQDebug forThisTest;
    // A small block whose rooms have been pulled apart by hand, still joined
    // to the rest of the map by one long exit running away to the
    // north-east. There is nothing in the way, so selecting the block and
    // reshaping it should simply close it back up.
    SyntheticMap b;
    // 2x3 block, every room three cells from the next instead of one.
    const auto id = [](int x, int y) { return static_cast<uint32_t>(y * 2 + x); };
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            b.addRoom(id(x, y), Coordinate{x * 3, y * 3, 0});
        }
    }
    for (int y = 0; y < 3; ++y) {
        b.linkBoth(id(0, y), ExitDirEnum::EAST, id(1, y));
    }
    for (int y = 0; y + 1 < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            b.linkBoth(id(x, y), ExitDirEnum::NORTH, id(x, y + 1));
        }
    }
    // The outside world, a long way off and well off-axis.
    b.addRoom(90, Coordinate{40, 28, 0});
    b.linkBoth(id(1, 2), ExitDirEnum::EAST, 90);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    RoomIdSet core;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            core.insert(internalId(map, id(x, y)));
        }
    }
    const MapReshapeGraph graph = MapReshapeGraph::build(map, core, options);

    ReshapeResult result;
    const LayoutPositions solved = MapReshapeSolver::solvePositions(graph,
                                                                    ReshapeSolverOptions::forMode(
                                                                        ReshapeModeEnum::Flatten),
                                                                    result);
    QCOMPARE(result.status, ReshapeStatusEnum::Improved);

    // Every exit inside the block should be back to a single cell.
    for (int y = 0; y < 3; ++y) {
        const Coordinate west = solved[scopeIndex(graph, map, id(0, y))];
        const Coordinate east = solved[scopeIndex(graph, map, id(1, y))];
        QCOMPARE(east.x - west.x, 1);
        QCOMPARE(east.y, west.y);
    }
    for (int y = 0; y + 1 < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            const Coordinate lower = solved[scopeIndex(graph, map, id(x, y))];
            const Coordinate upper = solved[scopeIndex(graph, map, id(x, y + 1))];
            QCOMPARE(upper.y - lower.y, 1);
            QCOMPARE(upper.x, lower.x);
        }
    }
}

void TestMapReshape::distantNeighbourDoesNotDominateTest()
{
    mmqt::HideQDebug forThisTest;
    // The reason the block above could not close up. An exit to a room far
    // away and far off-axis is charged for being off-axis in proportion to
    // the offset, so one distant neighbour used to outweigh every exit
    // inside the area put together, and the solver chased an anchor it
    // could never line up with instead of tidying what was selected.
    SyntheticMap b;
    b.addRoom(0, Coordinate{0, 0, 0});
    b.addRoom(1, Coordinate{1, 0, 0});
    b.linkBoth(0, ExitDirEnum::EAST, 1);
    b.addRoom(90, Coordinate{30, 25, 0});
    b.linkBoth(1, ExitDirEnum::EAST, 90);
    const Map map = b.build();

    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    const LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    const ReshapeWeights weights = makeWeights(ReshapeModeEnum::Flatten);

    // Cost of the tidy interior exit, against the cost of the long one
    // reaching outside.
    int64_t interior = 0;
    int64_t outward = 0;
    for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
        const int64_t cost = MapReshapeScorer::edgeCost(edge,
                                                        positions[edge.fromIndex],
                                                        positions[edge.toIndex],
                                                        weights);
        if (edge.scopeDistance == 0) {
            interior += cost;
        } else {
            outward += cost;
        }
    }
    QCOMPARE(interior, int64_t(0));

    // Undivided, that single exit would be charged over twelve thousand for
    // being twenty-five cells off-axis. It still counts -- an island should
    // not drift loose -- but no longer enough to decide the whole layout.
    QVERIFY2(outward < weights.alignment * 25,
             "a distant neighbour must not be charged at full strength");
    QVERIFY2(outward > 0, "but it must still count for something");
}

void TestMapReshape::scorerAndSolverAgreeOnEdgeCostTest()
{
    mmqt::HideQDebug forThisTest;
    // The scorer's per-component breakdown and the single number the solver
    // prices moves with have to come from the same arithmetic. They did not
    // once: the solver charged alignment by angle while the breakdown still
    // charged it by raw offset, so the search optimized one function and was
    // judged against another, and a selection that had closed itself up
    // perfectly was reported as worse than leaving it spread out.
    const Map map = buildChain(4);
    ReshapeOptions options;
    options.marginRings = 1;
    const MapReshapeGraph graph = MapReshapeGraph::build(map, makeCore(map, {0, 1}), options);

    const std::vector<Coordinate> offsets = {Coordinate{1, 0, 0},
                                             Coordinate{3, 1, 0},
                                             Coordinate{-2, 4, 0},
                                             Coordinate{7, -3, 1},
                                             Coordinate{0, 0, 0}};
    for (const ReshapeModeEnum mode : {ReshapeModeEnum::Flatten, ReshapeModeEnum::Volumetric}) {
        const ReshapeWeights weights = makeWeights(mode);
        for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
            for (const Coordinate &offset : offsets) {
                const Coordinate from{0, 0, 0};
                const Coordinate to = from + offset;
                QCOMPARE(MapReshapeScorer::edgeBreakdown(edge, from, to, weights).total(),
                         MapReshapeScorer::edgeCost(edge, from, to, weights));
            }
        }
    }

    // And the whole-layout score must equal the sum of its parts, so that
    // "improved" means the same thing to both.
    const LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
    const LayoutScore score = MapReshapeScorer::score(graph, positions);
    int64_t edgeTotal = 0;
    for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
        edgeTotal += MapReshapeScorer::edgeCost(edge,
                                                positions[edge.fromIndex],
                                                positions[edge.toIndex],
                                                ReshapeWeights{});
    }
    QCOMPARE(score.direction + score.alignment + score.edgeLength + score.boundary
                 + score.layerMismatch,
             edgeTotal);
}

QTEST_MAIN(TestMapReshape)
