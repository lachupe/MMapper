// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestMapReshape.h"

#include "../src/global/HideQDebug.h"
#include "../src/global/progresscounter.h"
#include "../src/map/ExitDirection.h"
#include "../src/map/ExitFlags.h"
#include "../src/map/Map.h"
#include "../src/map/MapReshapeGraph.h"
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

QTEST_MAIN(TestMapReshape)
