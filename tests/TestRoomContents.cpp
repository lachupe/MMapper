// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestRoomContents.h"

#include "../src/parser/ContainerLines.h"
#include "../src/parser/RoomContents.h"
#include "../src/parser/XmlElement.h"
#include "../src/proxy/GmcpMessage.h"

#include <QtTest/QtTest>

// The room lines and replies here are MUME's own, from the powwow logs (2005-2009) and from
// MMapper's logs of 2026. Those are one player's settings with a client's additions mixed in,
// so what is tested is the stable core of each message.

namespace {

constexpr int64_t NOW = 1790000000;

NODISCARD XmlElement element(const XmlTagEnum tag, const QString &text = QString{})
{
    XmlElement result;
    result.tag = tag;
    result.name = std::string{to_string_view(tag)};
    result.text = text;
    return result;
}

/// A room display: its name, its description, and the element MMapper hands the tracker.
NODISCARD XmlElement room(const QString &name, const std::vector<XmlElement> &extra = {})
{
    XmlElement result = element(XmlTagEnum::ROOM);
    result.children.push_back(element(XmlTagEnum::NAME, name));
    result.children.push_back(
        element(XmlTagEnum::DESCRIPTION,
                QStringLiteral("This small room is cluttered with the belongings of the guards.")));
    for (const XmlElement &child : extra) {
        result.children.push_back(child);
    }
    return result;
}

/// The objects of a room display with `lines` as its dynamic lines, as at its prompt.
NODISCARD RoomContentsSnapshot shown(RoomContentsTracker &tracker,
                                     const XmlElement &display,
                                     const QString &lines,
                                     const QString &key = QStringLiteral("1234"))
{
    std::ignore = tracker.receive(display, lines, key);
    const auto result = tracker.receive(element(XmlTagEnum::PROMPT), QString{}, QString{});
    if (!result.has_value()) {
        QTest::qFail("no contents at the prompt", __FILE__, __LINE__);
        return RoomContentsSnapshot{};
    }
    return *result;
}

/// Sends `command`, feeds MUME's `replies` one line at a time and then a prompt, and returns
/// every event that came of it.
NODISCARD std::vector<ContainerEvent> exchange(ContainerTracker &tracker,
                                               const QString &command,
                                               const QStringList &replies)
{
    std::vector<ContainerEvent> events;
    if (!command.isEmpty()) {
        tracker.receiveCommand(command);
    }
    for (const QString &line : replies) {
        for (ContainerEvent &event : tracker.receiveLine(line, NOW)) {
            events.push_back(std::move(event));
        }
    }
    for (ContainerEvent &event : tracker.receivePrompt(NOW)) {
        events.push_back(std::move(event));
    }
    return events;
}

/// A tracker whose room holds a wooden chest, a torch, a stone chest and a corpse.
NODISCARD ContainerTracker trackerInRoom()
{
    RoomContentsTracker contents;
    RoomContentsSnapshot snapshot = shown(contents,
                                          room(QStringLiteral("The Guard Room")),
                                          QStringLiteral(
                                              "A wooden chest stands in the corner.\n"
                                              "A large torch lies here among the dust.\n"
                                              "A large stone chest is here.\n"
                                              "The corpse of a rooster is lying here.\n"));
    ContainerTracker tracker;
    tracker.decorate(snapshot);
    return tracker;
}

NODISCARD ContainerEvent only(const std::vector<ContainerEvent> &events)
{
    if (events.size() != 1) {
        QTest::qFail(qPrintable(QStringLiteral("expected one event, got %1").arg(events.size())),
                     __FILE__,
                     __LINE__);
        return ContainerEvent{};
    }
    return events.front();
}

} // namespace

void TestRoomContents::keywordTest()
{
    QCOMPARE(containerKeyword(QStringLiteral("A wooden chest stands in the corner.")),
             QStringLiteral("chest"));
    QCOMPARE(containerKeyword(QStringLiteral("The corpse of a rooster is lying here.")),
             QStringLiteral("corpse"));
    QCOMPARE(containerKeyword(QStringLiteral("A sturdy leather backpack has been left here.")),
             QStringLiteral("backpack"));
    QVERIFY(containerKeyword(QStringLiteral("A large torch lies here among the dust.")).isEmpty());
    QVERIFY(
        containerKeyword(
            QStringLiteral(
                "A large bulletin board, entitled \"Board of the Free Peoples\", is mounted here."))
            .isEmpty());
    // Whole words only, and a tree's trunk is no chest.
    QVERIFY(containerKeyword(QStringLiteral("A chestnut tree grows here.")).isEmpty());
    QVERIFY(containerKeyword(QStringLiteral("The trunk of an ancient oak is covered in moss."))
                .isEmpty());
}

void TestRoomContents::namesContainerTest()
{
    QVERIFY(namesContainer(QStringLiteral("chest")));
    QVERIFY(namesContainer(QStringLiteral("che")));
    // MUME's own first keyword for "A large stone chest is here." is "stonechest".
    QVERIFY(namesContainer(QStringLiteral("stonechest"), QStringLiteral("chest")));
    QVERIFY(!namesContainer(QStringLiteral("corpse"), QStringLiteral("chest")));
    QVERIFY(!namesContainer(QStringLiteral("door")));
    QVERIFY(!namesContainer(QStringLiteral("exit")));
}

void TestRoomContents::objectsTest()
{
    RoomContentsTracker tracker;
    const RoomContentsSnapshot snapshot = shown(tracker,
                                                room(QStringLiteral("The Guard Room")),
                                                QStringLiteral(
                                                    "A wooden chest stands in the corner.\n"
                                                    "A large torch lies here among the dust.\n"
                                                    "A wooden chest stands in the corner.\n"
                                                    "The corpse of a rooster is lying here.\n"));
    QCOMPARE(snapshot.roomKey, QStringLiteral("1234"));
    QVERIFY(snapshot.seen);
    QVERIFY(snapshot.entered);
    QCOMPARE(snapshot.objects.size(), size_t{4});

    const RoomObject &first = snapshot.objects.at(0);
    QCOMPARE(first.index, 0);
    QCOMPARE(first.line, QStringLiteral("A wooden chest stands in the corner."));
    QVERIFY(first.container);
    QCOMPARE(first.keyword, QStringLiteral("chest"));
    QCOMPARE(first.target, QStringLiteral("chest"));
    QVERIFY(!first.state.open.has_value());
    QCOMPARE(first.state.known, int64_t{0});

    const RoomObject &torch = snapshot.objects.at(1);
    QVERIFY(!torch.container);
    QVERIFY(torch.keyword.isEmpty());
    QVERIFY(torch.target.isEmpty());

    // Two identical chests are two objects, and MUME counts the second as 2.chest.
    const RoomObject &second = snapshot.objects.at(2);
    QCOMPARE(second.index, 2);
    QCOMPARE(second.target, QStringLiteral("2.chest"));

    QCOMPARE(snapshot.objects.at(3).target, QStringLiteral("corpse"));
}

void TestRoomContents::charactersLeftOutTest()
{
    RoomContentsTracker tracker;
    // Room.Chars as MUME sends it: the desc is the character's line in the room.
    tracker.receiveChars(GmcpMessage::fromRawBytes(QByteArray{
        R"(Room.Chars.Set [{"id":101,"name":"a rooster","desc":"A rooster is here, strutting about."}])"}));

    XmlElement player = element(XmlTagEnum::PLAYER, QStringLiteral("Kazadoe"));
    const RoomContentsSnapshot snapshot = shown(tracker,
                                                room(QStringLiteral("Market Square"), {player}),
                                                QStringLiteral(
                                                    "A rooster is here, strutting about.\n"
                                                    "Kazadoe the Dwarf is standing here.\n"
                                                    "The corpse of a rooster is lying here.\n"));
    QCOMPARE(snapshot.objects.size(), size_t{1});
    QCOMPARE(snapshot.objects.at(0).line, QStringLiteral("The corpse of a rooster is lying here."));
    QCOMPARE(snapshot.objects.at(0).index, 0);

    // Once the rooster is gone, a line like its own is an object again.
    tracker.receiveChars(GmcpMessage::fromRawBytes(QByteArray{"Room.Chars.Remove 101"}));
    const RoomContentsSnapshot later = shown(tracker,
                                             room(QStringLiteral("Market Square")),
                                             QStringLiteral(
                                                 "A rooster is here, strutting about.\n"));
    QCOMPARE(later.objects.size(), size_t{1});
}

void TestRoomContents::terrainLeftOutTest()
{
    // MMapper's buffer of dynamic lines takes in the <terrain> text too, which is the ground,
    // not an object; and an <object> element inside a line names it.
    RoomContentsTracker tracker;
    XmlElement terrain = element(XmlTagEnum::TERRAIN,
                                 QStringLiteral("There is a lot of snow on the ground."));
    XmlElement object = element(XmlTagEnum::OBJECT, QStringLiteral("A large stone chest"));
    const RoomContentsSnapshot snapshot
        = shown(tracker,
                room(QStringLiteral("A Snowy Glade"), {terrain, object}),
                QStringLiteral("There is a lot of snow on the ground.\n"
                               "\x1b[33mA large stone chest is here.\x1b[0m\n"));
    QCOMPARE(snapshot.objects.size(), size_t{1});
    QCOMPARE(snapshot.objects.at(0).line, QStringLiteral("A large stone chest is here."));
    QCOMPARE(snapshot.objects.at(0).name, QStringLiteral("A large stone chest"));
}

void TestRoomContents::unseenRoomTest()
{
    RoomContentsTracker tracker;
    XmlElement dark = element(XmlTagEnum::ROOM);
    dark.children.push_back(element(XmlTagEnum::NAME, QStringLiteral("It is pitch black...")));
    const RoomContentsSnapshot snapshot = shown(tracker,
                                                dark,
                                                QStringLiteral("You see nothing but darkness.\n"));
    QVERIFY(!snapshot.seen);
    QVERIFY(snapshot.objects.empty());
}

void TestRoomContents::onlyAtPromptTest()
{
    RoomContentsTracker tracker;
    // A prompt with no room display before it says nothing about the room.
    QVERIFY(!tracker.receive(element(XmlTagEnum::PROMPT), QString{}, QString{}).has_value());
    QVERIFY(!tracker
                 .receive(room(QStringLiteral("The Guard Room")),
                          QStringLiteral("A wooden chest stands in the corner.\n"),
                          QStringLiteral("1"))
                 .has_value());
    QVERIFY(!tracker.receive(element(XmlTagEnum::WEATHER), QString{}, QString{}).has_value());
    QVERIFY(tracker.receive(element(XmlTagEnum::PROMPT), QString{}, QString{}).has_value());
    QVERIFY(!tracker.receive(element(XmlTagEnum::PROMPT), QString{}, QString{}).has_value());
}

void TestRoomContents::commandTest()
{
    const auto open = parseContainerCommand(QStringLiteral("op chest"));
    QVERIFY(open.has_value());
    QCOMPARE(open->action, ContainerActionEnum::OPEN);
    QCOMPARE(open->target, QStringLiteral("chest"));
    QVERIFY(open->container);

    const auto second = parseContainerCommand(QStringLiteral("unlock 2.chest"));
    QVERIFY(second.has_value());
    QCOMPARE(second->action, ContainerActionEnum::UNLOCK);
    QCOMPARE(second->target, QStringLiteral("2.chest"));
    QCOMPARE(second->word, QStringLiteral("chest"));
    QCOMPARE(second->ordinal, 2);

    const auto examine = parseContainerCommand(QStringLiteral("exami chest"));
    QVERIFY(examine.has_value());
    QCOMPARE(examine->action, ContainerActionEnum::LOOK);

    const auto lookIn = parseContainerCommand(QStringLiteral("look in corpse"));
    QVERIFY(lookIn.has_value());
    QCOMPARE(lookIn->action, ContainerActionEnum::LOOK);
    QCOMPARE(lookIn->target, QStringLiteral("corpse"));

    const auto get = parseContainerCommand(QStringLiteral("get all chest"));
    QVERIFY(get.has_value());
    QCOMPARE(get->action, ContainerActionEnum::GET);
    QCOMPARE(get->item, QStringLiteral("all"));
    QCOMPARE(get->target, QStringLiteral("chest"));

    const auto put = parseContainerCommand(QStringLiteral("put all backpack"));
    QVERIFY(put.has_value());
    QCOMPARE(put->action, ContainerActionEnum::PUT);

    // Doors keep their place in line but are not containers.
    const auto door = parseContainerCommand(QStringLiteral("open exit w"));
    QVERIFY(door.has_value());
    QVERIFY(!door->container);
    const auto sarcophagusDoor = parseContainerCommand(QStringLiteral("open sarcophagus d"));
    QVERIFY(sarcophagusDoor.has_value());
    QVERIFY(!sarcophagusDoor->container);

    // Not container commands at all.
    QVERIFY(!parseContainerCommand(QStringLiteral("get torch")).has_value());
    QVERIFY(!parseContainerCommand(QStringLiteral("look chest")).has_value());
    QVERIFY(!parseContainerCommand(QStringLiteral("north")).has_value());
    QVERIFY(!parseContainerCommand(QStringLiteral("o chest")).has_value());
}

void TestRoomContents::itemTest()
{
    const ContainerItem ring = parseContainerItem(QStringLiteral("a gold ring"));
    QCOMPARE(ring.name, QStringLiteral("a gold ring"));
    QCOMPARE(ring.count, 1);

    const ContainerItem scrolls = parseContainerItem(QStringLiteral("three azure scrolls"));
    QCOMPARE(scrolls.name, QStringLiteral("azure scrolls"));
    QCOMPARE(scrolls.count, 3);

    const ContainerItem sword = parseContainerItem(QStringLiteral(
        "the black sword (flawless); it glows blue; it emits a faint humming sound"));
    QCOMPARE(sword.name, QStringLiteral("the black sword"));
    QCOMPARE(sword.text,
             QStringLiteral(
                 "the black sword (flawless); it glows blue; it emits a faint humming sound"));

    QCOMPARE(parseContainerItem(QStringLiteral("a nasty orkish fang (flawless)")).name,
             QStringLiteral("a nasty orkish fang"));
}

void TestRoomContents::openTest()
{
    ContainerTracker tracker = trackerInRoom();
    const ContainerEvent opened = only(exchange(tracker, QStringLiteral("op chest"), {"Ok.", ""}));
    QCOMPARE(opened.command.action, ContainerActionEnum::OPEN);
    QCOMPARE(opened.result, ContainerResultEnum::OPENED);
    QCOMPARE(opened.index, 0);
    QCOMPARE(opened.text, QStringLiteral("Ok."));

    // A fight line in between is not a reply.
    const ContainerEvent already = only(
        exchange(tracker,
                 QStringLiteral("op chest"),
                 {"But it's already open!",
                  "\x1b[31;1mA mastiff\x1b[31;1m tries to hit you, but your parry is "
                  "successful.\x1b[0m",
                  ""}));
    QCOMPARE(already.result, ContainerResultEnum::ALREADY_OPEN);

    const ContainerEvent closed = only(exchange(tracker, QStringLiteral("close chest"), {"Ok."}));
    QCOMPARE(closed.result, ContainerResultEnum::CLOSED);
}

void TestRoomContents::lockedTest()
{
    ContainerTracker tracker = trackerInRoom();
    const ContainerEvent locked = only(
        exchange(tracker, QStringLiteral("op 2.chest"), {"It seems to be locked.", ""}));
    QCOMPARE(locked.result, ContainerResultEnum::LOCKED);
    QCOMPARE(locked.index, 2);

    const ContainerEvent noKey = only(
        exchange(tracker, QStringLiteral("unlock 2.chest"), {"You don't have the proper key."}));
    QCOMPARE(noKey.result, ContainerResultEnum::NO_KEY);

    const RoomContentsSnapshot now = tracker.current();
    QCOMPARE(now.objects.at(2).state.locked, std::optional<bool>{true});
    QCOMPARE(now.objects.at(2).state.open, std::optional<bool>{false});
    QCOMPARE(now.objects.at(2).state.known, NOW);
    // The other chest is still unknown.
    QVERIFY(!now.objects.at(0).state.locked.has_value());
}

void TestRoomContents::unlockTest()
{
    ContainerTracker tracker = trackerInRoom();
    // As the logs have it: both commands sent before the replies, the click and the broken key
    // answering the unlock, and the open's own reply lost in the fight.
    tracker.receiveCommand(QStringLiteral("unlock chest"));
    const std::vector<ContainerEvent> events = exchange(
        tracker,
        QStringLiteral("open chest"),
        {"*click*",
         "As you unlock the stonechest there is a sharp crack, and you are left holding only "
         "the handle of the key.",
         ""});
    QCOMPARE(events.size(), size_t{2});
    QCOMPARE(events.at(0).command.action, ContainerActionEnum::UNLOCK);
    QCOMPARE(events.at(0).result, ContainerResultEnum::UNLOCKED);
    QCOMPARE(events.at(1).command.action, ContainerActionEnum::UNLOCK);
    QCOMPARE(events.at(1).result, ContainerResultEnum::KEY_BROKE);
    QCOMPARE(tracker.current().objects.at(0).state.locked, std::optional<bool>{false});

    const ContainerEvent already = only(
        exchange(tracker, QStringLiteral("unl chest"), {"It's already unlocked, it seems."}));
    QCOMPARE(already.result, ContainerResultEnum::UNLOCKED);
}

void TestRoomContents::pickTest()
{
    ContainerTracker tracker = trackerInRoom();
    const ContainerEvent picking = only(
        exchange(tracker,
                 QStringLiteral("pick chest"),
                 {"Using your lockpicks, you try to pick the lock..."}));
    QCOMPARE(picking.result, ContainerResultEnum::PICKING);

    // The picking goes on over prompts, and ends on a line with the twiddlers glued on.
    std::ignore = tracker.receivePrompt(NOW);
    std::ignore = tracker.receivePrompt(NOW);
    std::ignore = tracker.receivePrompt(NOW);
    std::ignore = tracker.receivePrompt(NOW);
    const ContainerEvent picked = only(
        exchange(tracker, QString{}, {"/-\\|/The lock finally yields to your skill."}));
    QCOMPARE(picked.result, ContainerResultEnum::PICKED);
    QCOMPARE(picked.text, QStringLiteral("The lock finally yields to your skill."));
    QCOMPARE(tracker.current().objects.at(0).state.locked, std::optional<bool>{false});

    std::ignore = exchange(tracker,
                           QStringLiteral("pick 2.chest"),
                           {"Using your lockpicks, you try to pick the lock..."});
    const ContainerEvent proof = only(
        exchange(tracker, QString{}, {"You seem to be unable to pick this lock."}));
    QCOMPARE(proof.result, ContainerResultEnum::PICKPROOF);
    QCOMPARE(proof.index, 2);
    QCOMPARE(tracker.current().objects.at(2).state.pickproof, std::optional<bool>{true});

    std::ignore = exchange(tracker,
                           QStringLiteral("pick 2.chest"),
                           {"Using your lockpicks, you try to pick the lock..."});
    const ContainerEvent failed = only(
        exchange(tracker, QString{}, {"You failed to pick the lock."}));
    QCOMPARE(failed.result, ContainerResultEnum::PICK_FAILED);

    std::ignore = exchange(tracker,
                           QStringLiteral("pick 2.chest"),
                           {"Using your lockpicks, you try to pick the lock..."});
    const ContainerEvent stopped = only(
        exchange(tracker, QString{}, {"|You stop trying to pick the lock."}));
    QCOMPARE(stopped.result, ContainerResultEnum::PICK_STOPPED);
}

void TestRoomContents::lookInTest()
{
    ContainerTracker tracker = trackerInRoom();
    const ContainerEvent contents = only(
        exchange(tracker,
                 QStringLiteral("exami chest"),
                 {"chest (here) : ", "a gold ring", "a gold coin", ""}));
    QCOMPARE(contents.command.action, ContainerActionEnum::LOOK);
    QCOMPARE(contents.result, ContainerResultEnum::CONTENTS);
    QCOMPARE(contents.index, 0);
    QCOMPARE(contents.items.size(), size_t{2});
    QCOMPARE(contents.items.at(0).name, QStringLiteral("a gold ring"));
    QCOMPARE(contents.items.at(1).name, QStringLiteral("a gold coin"));
    QCOMPARE(tracker.current().objects.at(0).state.open, std::optional<bool>{true});
    QCOMPARE(tracker.current().objects.at(0).state.empty, std::optional<bool>{false});

    // The header names the object by its first keyword, and a line from someone else ends the
    // listing without being one of its items.
    const ContainerEvent interrupted = only(
        exchange(tracker,
                 QStringLiteral("exami 2.chest"),
                 {"stonechest (here) : ",
                  "a silver rod, marked with glyphs",
                  "a nasty orkish fang (flawless)",
                  "Zmej gets a jewelled ring from a sable pouch.",
                  ""}));
    QCOMPARE(interrupted.items.size(), size_t{2});
    QCOMPARE(interrupted.index, 2);

    const ContainerEvent empty = only(
        exchange(tracker, QStringLiteral("examine chest"), {"stonechest (here) : ", "Nothing."}));
    QCOMPARE(empty.result, ContainerResultEnum::EMPTY);
    QVERIFY(empty.items.empty());

    const ContainerEvent closed = only(
        exchange(tracker, QStringLiteral("exa corpse"), {"It is closed."}));
    QCOMPARE(closed.result, ContainerResultEnum::CLOSED);
    QCOMPARE(closed.index, 3);

    // The player's own backpack is not the room's: nothing is published for it.
    QVERIFY(exchange(tracker,
                     QStringLiteral("l in backpack"),
                     {"backpack (used) : ", "an amethyst", "two amethysts"})
                .empty());
}

void TestRoomContents::getTest()
{
    ContainerTracker tracker = trackerInRoom();
    const ContainerEvent taken = only(
        exchange(tracker,
                 QStringLiteral("get all chest"),
                 {"You get a leather wallet from a silver-embellished chest.",
                  "You get a chunk of metal from a silver-embellished chest.",
                  "You get a pile of coins from a silver-embellished chest.",
                  "There was 3 gold coins.",
                  ""}));
    QCOMPARE(taken.command.action, ContainerActionEnum::GET);
    QCOMPARE(taken.result, ContainerResultEnum::CONTENTS);
    QCOMPARE(taken.items.size(), size_t{4});
    QCOMPARE(taken.items.at(0).name, QStringLiteral("a leather wallet"));
    QCOMPARE(taken.items.at(3).name, QStringLiteral("gold coins"));
    QCOMPARE(taken.items.at(3).count, 3);

    const ContainerEvent nothing = only(exchange(tracker,
                                                 QStringLiteral("get all chest"),
                                                 {"You can't find anything in the chest."}));
    QCOMPARE(nothing.result, ContainerResultEnum::EMPTY);
    QCOMPARE(tracker.current().objects.at(0).state.empty, std::optional<bool>{true});

    // "get" reaches the player's own bags first: with no backpack in the room, one of those is
    // what it reached, and it is not the room's business.
    QVERIFY(exchange(tracker,
                     QStringLiteral("get flask backpack"),
                     {"You get a flask of orkish draught from a leather backpack."})
                .empty());
}

void TestRoomContents::doorRepliesTest()
{
    ContainerTracker tracker = trackerInRoom();
    // The door was asked first, so the first "Ok." is its own.
    tracker.receiveCommand(QStringLiteral("open exit w"));
    tracker.receiveCommand(QStringLiteral("op chest"));
    QVERIFY(tracker.receiveLine(QStringLiteral("Ok."), NOW).empty());
    const std::vector<ContainerEvent> events = tracker.receiveLine(QStringLiteral("Ok."), NOW);
    QCOMPARE(events.size(), size_t{1});
    QCOMPARE(events.front().result, ContainerResultEnum::OPENED);

    // With nothing waiting, a reply is somebody else's business.
    QVERIFY(tracker.receiveLine(QStringLiteral("*click*"), NOW).empty());
}

void TestRoomContents::notFoundTest()
{
    ContainerTracker tracker = trackerInRoom();
    const ContainerEvent missing = only(
        exchange(tracker, QStringLiteral("open cabinet"), {"You don't see any cabinet here."}));
    QCOMPARE(missing.result, ContainerResultEnum::NOT_FOUND);
    QCOMPARE(missing.index, -1);

    const ContainerEvent notOne = only(
        exchange(tracker, QStringLiteral("open corpse"), {"That's not a container."}));
    QCOMPARE(notOne.result, ContainerResultEnum::CANNOT);
}

void TestRoomContents::stateRidesAlongTest()
{
    ContainerTracker tracker = trackerInRoom();
    QVERIFY(!tracker.takeChanged());
    std::ignore = exchange(tracker, QStringLiteral("op 2.chest"), {"It seems to be locked."});
    QVERIFY(tracker.takeChanged());
    QVERIFY(!tracker.takeChanged());

    // Coming back to the room: the same lines in the same room keep what was learnt.
    RoomContentsTracker contents;
    RoomContentsSnapshot again = shown(contents,
                                       room(QStringLiteral("The Guard Room")),
                                       QStringLiteral("A wooden chest stands in the corner.\n"
                                                      "A large torch lies here among the dust.\n"
                                                      "A large stone chest is here.\n"
                                                      "The corpse of a rooster is lying here.\n"));
    tracker.decorate(again);
    QCOMPARE(again.objects.at(2).state.locked, std::optional<bool>{true});

    // Another room with a chest in it knows nothing of it.
    RoomContentsSnapshot elsewhere = shown(contents,
                                           room(QStringLiteral("A Dusty Attic")),
                                           QStringLiteral("A large stone chest is here.\n"),
                                           QStringLiteral("5678"));
    tracker.decorate(elsewhere);
    QVERIFY(!elsewhere.objects.at(0).state.locked.has_value());
}

void TestRoomContents::expiryTest()
{
    ContainerTracker tracker = trackerInRoom();
    tracker.receiveCommand(QStringLiteral("op chest"));
    for (int i = 0; i < 4; ++i) {
        std::ignore = tracker.receivePrompt(NOW);
    }
    // Four prompts later the command is given up on, and an "Ok." is not its reply.
    QVERIFY(tracker.receiveLine(QStringLiteral("Ok."), NOW).empty());
}

QTEST_MAIN(TestRoomContents)
