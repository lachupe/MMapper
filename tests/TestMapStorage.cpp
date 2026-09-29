// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestMapStorage.h"

#include "../src/global/Version.h"
#include "../src/global/progresscounter.h"
#include "../src/map/ExitDirection.h"
#include "../src/map/ExitFieldVariant.h"
#include "../src/map/ExitFlags.h"
#include "../src/map/Map.h"
#include "../src/map/RawRoom.h"
#include "../src/map/RoomFingerprint.h"
#include "../src/map/RoomHandle.h"
#include "../src/map/coordinate.h"
#include "../src/map/mmapper2room.h"
#include "../src/map/roomid.h"
#include "../src/mapstorage/MapDestination.h"
#include "../src/mapstorage/MapSource.h"
#include "../src/mapstorage/RawMapData.h"
#include "../src/mapstorage/XmlMapStorage.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest/QtTest>

// XmlMapStorage checks the map version with CompareVersion, which lives beside the update checker
// that asks for these; the real ones are generated into the application alone.
const char *getMMapperVersion()
{
    return "v26.09.0";
}

bool isMMapperBeta()
{
    return false;
}

namespace {

// The fixed test vector. The mume3d client computes the same function in GDScript and checks it
// against the same vector. Computed independently of MMapper with:
//
//   printf 'The Prancing Pony\nThe common room of the inn is warm and smoky, and a fire burns in the hearth.\nPipe-smoke hangs under the low beams.\n\nINDOORS\nE,N,S,U\nA Private Parlour\nThe Kitchen\nMain Street\n' | sha256sum
//   0eb3411b99c6bdb668446f32b4a034d3eadf9848b14b268b1f57e2aec33735d4  -
//
// That is the name, the description (which ends in a newline of its own), the terrain, the
// exits the room has, sorted by letter (east to "A Private Parlour", north to "The Kitchen",
// south to "Main Street", and up to no room at all), and the names of the rooms they lead to.
constexpr const char *const PONY_NAME = "The Prancing Pony";
constexpr const char *const PONY_DESCRIPTION
    = "The common room of the inn is warm and smoky, and a fire burns in the hearth.\n"
      "Pipe-smoke hangs under the low beams.\n";
constexpr const char *const PONY_TEXT
    = "The Prancing Pony\n"
      "The common room of the inn is warm and smoky, and a fire burns in the hearth.\n"
      "Pipe-smoke hangs under the low beams.\n"
      "\n"
      "INDOORS\n"
      "E,N,S,U\n"
      "A Private Parlour\n"
      "The Kitchen\n"
      "Main Street\n";
constexpr const char *const PONY_FINGERPRINT = "0eb3411b99c6";

// A second vector, for a room without exits, and for the text being hashed as UTF-8:
//
//   printf 'N\303\251n Hithoel\nMist rises from the lake.\n\nWATER\n\n' | sha256sum
//   cdd8f56ca9455db2a0f13a2e78a1d8523c7d43ed4da48d6ed973c1ed4e5d0839  -
//
// No room on a map can have that name: MMapper keeps names and descriptions in plain ASCII (its
// sanitizer writes "Nen"), so in practice the UTF-8 is ASCII. The vector pins the rule anyway.
constexpr const char *const LAKE_NAME = "N\303\251n Hithoel";
constexpr const char *const LAKE_DESCRIPTION = "Mist rises from the lake.\n";
constexpr const char *const LAKE_FINGERPRINT = "cdd8f56ca945";

NODISCARD ExternalRawRoom makeRoom(const uint32_t id,
                                   const Coordinate position,
                                   const char *const name,
                                   const char *const description,
                                   const RoomTerrainEnum terrain)
{
    ExternalRawRoom room;
    room.setId(ExternalRoomId{id});
    room.setPosition(position);
    room.setName(RoomName{name});
    room.setDescription(RoomDesc{description});
    room.setTerrainType(terrain);
    room.status = RoomStatusEnum::Permanent;
    return room;
}

void addExit(ExternalRawRoom &room, const ExitDirEnum dir, const std::initializer_list<uint32_t> to)
{
    ExternalRawExit &exit = room.exits[dir];
    exit.addExitFlags(ExitFlagEnum::EXIT);
    for (const uint32_t id : to) {
        exit.outgoing.insert(ExternalRoomId{id});
    }
}

/// An inn and the rooms around it, with every kind of exit the fingerprint has to tell apart.
NODISCARD Map makeInnMap()
{
    ExternalRawRoom pony = makeRoom(1,
                                    Coordinate{0, 0, 0},
                                    PONY_NAME,
                                    PONY_DESCRIPTION,
                                    RoomTerrainEnum::INDOORS);
    addExit(pony, ExitDirEnum::NORTH, {2});
    addExit(pony, ExitDirEnum::SOUTH, {3});
    addExit(pony, ExitDirEnum::EAST, {4});
    // A door changes nothing.
    pony.exits[ExitDirEnum::EAST].addExitFlags(ExitFlagEnum::DOOR);
    pony.exits[ExitDirEnum::EAST].setDoorName(DoorName{"oak door"});
    // An exit the room has that nobody mapped: it counts, and leads to no room.
    pony.exits[ExitDirEnum::UP].addExitFlags(ExitFlagEnum::EXIT);
    // Flags without the EXIT flag: the export writes it, marked NO_EXIT, but the room has no
    // exit down, so it does not count.
    pony.exits[ExitDirEnum::DOWN].addExitFlags(ExitFlagEnum::NO_MATCH);

    ExternalRawRoom kitchen = makeRoom(2,
                                       Coordinate{0, 1, 0},
                                       "The Kitchen",
                                       "Pots and pans hang from hooks along the blackened walls.\n",
                                       RoomTerrainEnum::INDOORS);
    addExit(kitchen, ExitDirEnum::SOUTH, {1});

    ExternalRawRoom street = makeRoom(3,
                                      Coordinate{0, -1, 0},
                                      "Main Street",
                                      "The main street of Bree runs between the houses.\n",
                                      RoomTerrainEnum::ROAD);
    addExit(street, ExitDirEnum::NORTH, {1});
    // Two rooms one way: the lower externalId (5) is the one named.
    addExit(street, ExitDirEnum::WEST, {6, 5});

    ExternalRawRoom parlour = makeRoom(4,
                                       Coordinate{1, 0, 0},
                                       "A Private Parlour",
                                       "A small, quiet room with a round table and a few chairs.\n",
                                       RoomTerrainEnum::INDOORS);
    addExit(parlour, ExitDirEnum::WEST, {1});

    ExternalRawRoom alley = makeRoom(5,
                                     Coordinate{-1, -1, 0},
                                     "A Narrow Alley",
                                     "The alley is dark and smells of refuse.\n",
                                     RoomTerrainEnum::CITY);
    addExit(alley, ExitDirEnum::EAST, {3});

    // A name the export has to escape.
    ExternalRawRoom yard = makeRoom(6,
                                    Coordinate{-1, -2, 0},
                                    "A Muddy Yard & Stable",
                                    "Puddles of rainwater gather in the ruts of the yard.\n",
                                    RoomTerrainEnum::CITY);
    addExit(yard, ExitDirEnum::EAST, {3});

    // No terrain, so no <terrain> in the export, and no description.
    ExternalRawRoom nowhere = makeRoom(7,
                                       Coordinate{5, 5, 0},
                                       "Somewhere Unknown",
                                       "",
                                       RoomTerrainEnum::UNDEFINED);

    std::vector<ExternalRawRoom> rooms{pony, kitchen, street, parlour, alley, yard, nowhere};
    ProgressCounter pc;
    return Map::fromRooms(pc, std::move(rooms), {}).modified;
}

/// What a program reading the export sees of one <room>.
struct NODISCARD XmlRoom final
{
    QString name;
    QString fingerprint;
    QString description;
    QString terrain;
    /// The `dir` of each <exit> the room has, and its first <to>, or "" when it has none.
    std::vector<std::pair<QString, QString>> exits;
    /// The `dir` of each <exit> marked NO_EXIT.
    QStringList notExits;
};

/// The rooms of an export by id, read the way another program would read them.
NODISCARD std::map<QString, XmlRoom> readRooms(const QByteArray &xml)
{
    std::map<QString, XmlRoom> rooms;
    QXmlStreamReader reader{xml};
    if (!reader.readNextStartElement() || reader.name() != QStringLiteral("map")) {
        return rooms;
    }
    while (reader.readNextStartElement()) {
        if (reader.name() != QStringLiteral("room")) {
            reader.skipCurrentElement();
            continue;
        }
        XmlRoom room;
        const QXmlStreamAttributes attributes = reader.attributes();
        const QString id = attributes.value("id").toString();
        room.name = attributes.value("name").toString();
        room.fingerprint = attributes.value("fingerprint").toString();
        while (reader.readNextStartElement()) {
            const QString element = reader.name().toString();
            if (element == QStringLiteral("description")) {
                room.description = reader.readElementText();
            } else if (element == QStringLiteral("terrain")) {
                room.terrain = reader.readElementText();
            } else if (element == QStringLiteral("exit")) {
                const QString dir = reader.attributes().value("dir").toString();
                QString firstTo;
                bool isExit = true;
                while (reader.readNextStartElement()) {
                    const QString child = reader.name().toString();
                    const QString text = reader.readElementText();
                    if (child == QStringLiteral("to") && firstTo.isEmpty()) {
                        firstTo = text;
                    } else if (child == QStringLiteral("exitflag")
                               && text == QStringLiteral("NO_EXIT")) {
                        isExit = false;
                    }
                }
                if (isExit) {
                    room.exits.emplace_back(dir, firstTo);
                } else {
                    room.notExits.append(dir);
                }
            } else {
                reader.skipCurrentElement();
            }
        }
        rooms.emplace(id, std::move(room));
    }
    return rooms;
}

/// The fingerprint of an exported room worked out from the export alone, without MMapper's
/// code: what the mume3d client does in GDScript.
NODISCARD QString fingerprintFromExport(const XmlRoom &room, const std::map<QString, XmlRoom> &rooms)
{
    static const std::map<QString, QString> letters{{"north", "N"},
                                                    {"south", "S"},
                                                    {"east", "E"},
                                                    {"west", "W"},
                                                    {"up", "U"},
                                                    {"down", "D"}};
    std::vector<std::pair<QString, QString>> exits;
    for (const auto &[dir, to] : room.exits) {
        const auto letter = letters.find(dir);
        if (letter == letters.end()) {
            continue; // "unknown" and "none" are not directions a room shows.
        }
        const auto target = rooms.find(to);
        exits.emplace_back(letter->second, target == rooms.end() ? QString{} : target->second.name);
    }
    std::sort(exits.begin(), exits.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    QStringList dirs;
    QStringList targets;
    for (const auto &[letter, target] : exits) {
        dirs.append(letter);
        targets.append(target);
    }
    const QString text = room.name + "\n" + room.description + "\n" + room.terrain + "\n"
                         + dirs.join(",") + "\n" + targets.join("\n");
    const QByteArray digest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().left(12));
}

/// Reads an export back in as MMapper loads a map file.
NODISCARD std::optional<RawMapLoadData> importXml(const QByteArray &xml)
{
    const auto source = MapSource::alloc(QStringLiteral("export.xml"), xml);
    XmlMapStorage storage{AbstractMapStorage::Data{source}, nullptr};
    storage.setProgressCounter(std::make_shared<ProgressCounter>());
    return storage.loadData();
}

} // namespace

QByteArray TestMapStorage::exportXml(const Map &map)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        return QByteArray{};
    }
    const QString fileName = dir.filePath(QStringLiteral("export.xml"));
    {
        const auto destination = MapDestination::alloc(fileName, SaveFormatEnum::MM2XML);
        XmlMapStorage storage{AbstractMapStorage::Data{destination}, nullptr};
        storage.setProgressCounter(std::make_shared<ProgressCounter>());
        MapLoadData data;
        data.mapPair.modified = map;
        data.filename = fileName;
        data.readonly = false;
        if (!storage.virt_saveData(data)) {
            return QByteArray{};
        }
        destination->finalize();
    }
    QFile file{fileName};
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray{};
    }
    return file.readAll();
}

void TestMapStorage::fingerprintVectorTest()
{
    room_fingerprint::Fields fields;
    fields.name = PONY_NAME;
    fields.description = PONY_DESCRIPTION;
    fields.terrain = "INDOORS";
    // In the order MMapper keeps them (north, south, east, up), not the order they are hashed in.
    fields.exits = {{'N', "The Kitchen"},
                    {'S', "Main Street"},
                    {'E', "A Private Parlour"},
                    {'U', ""}};

    QCOMPARE(room_fingerprint::canonicalText(fields), QByteArray{PONY_TEXT});
    QCOMPARE(room_fingerprint::compute(fields), QString{PONY_FINGERPRINT});
    QCOMPARE(room_fingerprint::compute(fields).size(),
             static_cast<qsizetype>(room_fingerprint::DIGITS));

    // Every field counts.
    room_fingerprint::Fields other = fields;
    other.exits.back().target = "The Loft";
    QVERIFY(room_fingerprint::compute(other) != QString{PONY_FINGERPRINT});
    other = fields;
    other.exits.pop_back();
    QVERIFY(room_fingerprint::compute(other) != QString{PONY_FINGERPRINT});
    other = fields;
    other.terrain = "CITY";
    QVERIFY(room_fingerprint::compute(other) != QString{PONY_FINGERPRINT});
}

void TestMapStorage::fingerprintUtf8VectorTest()
{
    room_fingerprint::Fields fields;
    fields.name = QString::fromUtf8(LAKE_NAME);
    fields.description = LAKE_DESCRIPTION;
    fields.terrain = "WATER";

    QCOMPARE(room_fingerprint::canonicalText(fields),
             QByteArray{"N\303\251n Hithoel\nMist rises from the lake.\n\nWATER\n\n"});
    QCOMPARE(room_fingerprint::compute(fields), QString{LAKE_FINGERPRINT});
}

void TestMapStorage::fingerprintOfRoomTest()
{
    const Map map = makeInnMap();
    QCOMPARE(map.getRoomsCount(), static_cast<size_t>(7));

    const RoomHandle pony = map.getRoomHandle(ExternalRoomId{1});
    QCOMPARE(pony.getDescription().toQString(), QString{PONY_DESCRIPTION});
    QCOMPARE(room_fingerprint::canonicalText(room_fingerprint::fieldsOf(pony)),
             QByteArray{PONY_TEXT});
    QCOMPARE(room_fingerprint::compute(pony), QString{PONY_FINGERPRINT});

    // An exit to two rooms names the one with the lower externalId: the alley (5), not the yard.
    const room_fingerprint::Fields street = room_fingerprint::fieldsOf(
        map.getRoomHandle(ExternalRoomId{3}));
    QCOMPARE(street.exits.size(), static_cast<size_t>(2));
    QVERIFY(street.exits.at(1).letter == 'W');
    QCOMPARE(street.exits.at(1).target, QStringLiteral("A Narrow Alley"));

    // A room without a terrain has none in the export, and none in its fingerprint.
    const room_fingerprint::Fields nowhere = room_fingerprint::fieldsOf(
        map.getRoomHandle(ExternalRoomId{7}));
    QCOMPARE(nowhere.terrain, QString{});
    QCOMPARE(room_fingerprint::terrainWord(RoomTerrainEnum::UNDEFINED), QString{});
    QCOMPARE(room_fingerprint::terrainWord(RoomTerrainEnum::FOREST), QStringLiteral("FOREST"));
}

void TestMapStorage::xmlExportFingerprintTest()
{
    const Map map = makeInnMap();
    const QByteArray xml = exportXml(map);
    QVERIFY(!xml.isEmpty());

    const std::map<QString, XmlRoom> rooms = readRooms(xml);
    QCOMPARE(rooms.size(), map.getRoomsCount());

    const QRegularExpression hex{QStringLiteral("^[0-9a-f]{12}$")};
    for (const auto &[id, room] : rooms) {
        // Every room has one, and it is the room's own...
        QVERIFY2(hex.match(room.fingerprint).hasMatch(), qPrintable(id));
        const RoomHandle handle = map.getRoomHandle(ExternalRoomId{id.toUInt()});
        QCOMPARE(room.fingerprint, room_fingerprint::compute(handle));
        // ...and a program with nothing but the export works it out the same.
        QCOMPARE(fingerprintFromExport(room, rooms), room.fingerprint);
    }

    QCOMPARE(rooms.at("1").fingerprint, QString{PONY_FINGERPRINT});
    // The export does write the exit down the room does not have, and marks it so.
    QCOMPARE(rooms.at("1").notExits, QStringList{"down"});
    // It writes the two rooms west of the street lowest id first: the first <to> is the one the
    // fingerprint names.
    const auto &streetExits = rooms.at("3").exits;
    const auto west = std::find_if(streetExits.begin(), streetExits.end(), [](const auto &exit) {
        return exit.first == QStringLiteral("west");
    });
    QVERIFY(west != streetExits.end());
    QCOMPARE(west->second, QStringLiteral("5"));
    QVERIFY(xml.contains("A Muddy Yard &amp; Stable"));
}

void TestMapStorage::xmlRoundTripTest()
{
    const Map map = makeInnMap();
    const QByteArray xml = exportXml(map);
    QVERIFY(xml.contains("fingerprint=\"" + QByteArray{PONY_FINGERPRINT} + "\""));

    // MMapper reads its own export back, fingerprints and all, and has the same map.
    std::optional<RawMapLoadData> loaded = importXml(xml);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->rooms.size(), map.getRoomsCount());
    ProgressCounter pc;
    const Map reloaded = Map::fromRooms(pc, std::move(loaded->rooms), {}).modified;
    QCOMPARE(reloaded.getRoomsCount(), map.getRoomsCount());
    map.getRooms().for_each([&map, &reloaded](const RoomId id) {
        const RoomHandle before = map.getRoomHandle(id);
        const RoomHandle after = reloaded.getRoomHandle(before.getIdExternal());
        QCOMPARE(after.getName().toQString(), before.getName().toQString());
        QCOMPARE(after.getDescription().toQString(), before.getDescription().toQString());
        QVERIFY(after.getTerrainType() == before.getTerrainType());
        QVERIFY(after.getPosition() == before.getPosition());
        QCOMPARE(room_fingerprint::compute(after), room_fingerprint::compute(before));
    });

    // The attribute is only ever written: a wrong one is not even looked at.
    QByteArray tampered = xml;
    tampered.replace("fingerprint=\"" + QByteArray{PONY_FINGERPRINT} + "\"",
                     "fingerprint=\"not a fingerprint\"");
    QVERIFY(tampered != xml);
    const std::optional<RawMapLoadData> stillLoaded = importXml(tampered);
    QVERIFY(stillLoaded.has_value());
    QCOMPARE(stillLoaded->rooms.size(), map.getRoomsCount());
}

QTEST_MAIN(TestMapStorage)
