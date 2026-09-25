// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestItemLines.h"

#include "../src/parser/ItemLines.h"

#include <QtTest/QtTest>

// The listings and replies here are MUME's own, from the powwow logs (1999-2012): azazello.txt,
// stonedoor.txt, trolls_roots.txt and the archives. Those are one player's settings with a
// client's additions mixed in, so what is tested is the stable core of each message.

namespace {

/// Feeds `lines` one at a time, then a prompt, and returns every listing that came of it.
NODISCARD std::vector<ItemBlock> feed(ItemBlockTracker &tracker, const QStringList &lines)
{
    std::vector<ItemBlock> blocks;
    for (const QString &line : lines) {
        for (ItemBlock &block : tracker.receiveLine(line)) {
            blocks.push_back(std::move(block));
        }
    }
    for (ItemBlock &block : tracker.receivePrompt()) {
        blocks.push_back(std::move(block));
    }
    return blocks;
}

NODISCARD ItemEvent eventOf(const QString &line)
{
    const auto event = parseItemEvent(line);
    if (!event.has_value()) {
        QTest::qFail(qPrintable(QStringLiteral("not an item event: ") + line), __FILE__, __LINE__);
        return ItemEvent{};
    }
    return *event;
}

// azazello.txt:474-496, the reply to "equ".
const QStringList AZAZELLO_EQUIPMENT{
    QStringLiteral("You are using:"),
    QStringLiteral("<wielded>            an ornate, steel-shafted warhammer"),
    QStringLiteral("<worn as shield>     a defiled dwarven shield (flawless)"),
    QStringLiteral("<worn on head>       a white chain mail coif (flawless)"),
    QStringLiteral("<worn on body>       a shining breastplate (flawless)"),
    QStringLiteral("<worn about body>    a fine grey cloak (flawless)"),
    QStringLiteral("<worn on arms>       a shining pair of vambraces (flawless)"),
    QStringLiteral("<worn on hands>      a fine pair of metal gauntlets (flawless)"),
    QStringLiteral("<worn on legs>       a shining pair of greaves (flawless)"),
    QStringLiteral("<worn on feet>       a fine pair of metal boots (flawless)"),
    QStringLiteral("<worn around neck>   an old length of iron chain"),
    QStringLiteral("<worn around neck>   a dark stone"),
    QStringLiteral("<worn on wrist>      a keyring with several keys and a set of lock picks"),
    QStringLiteral("<worn on wrist>      a fine silver bracelet"),
    QStringLiteral("<worn on finger>     a ruby ring"),
    QStringLiteral("<worn on finger>     a diamond ring"),
    QStringLiteral("<worn on back>       a leather backpack"),
    QStringLiteral("<worn as belt>       a gleaming belt"),
    QStringLiteral("<worn on belt>       a gem-inlaid knife (flawless)"),
    QStringLiteral("<worn on belt>       a fragrant-smelling bag"),
    QStringLiteral("<worn on belt>       a sable pouch"),
    QStringLiteral("<worn on belt>       a water skin"),
    QStringLiteral("<worn on belt>       a small pouch"),
    QStringLiteral(""),
};

} // namespace

void TestItemLines::slotTest()
{
    // The table of the protocol spec.
    QCOMPARE(equipmentSlot(QStringLiteral("used as light")), QStringLiteral("light"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on finger")), QStringLiteral("finger"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn around neck")), QStringLiteral("neck"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on body")), QStringLiteral("body"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on head")), QStringLiteral("head"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on legs")), QStringLiteral("legs"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on feet")), QStringLiteral("feet"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on hands")), QStringLiteral("hands"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn as belt")), QStringLiteral("belt"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on belt")), QStringLiteral("belt"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on arms")), QStringLiteral("arms"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn as shield")), QStringLiteral("shield"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn about body")), QStringLiteral("about"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn about waist")), QStringLiteral("waist"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn around wrist")), QStringLiteral("wrist"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on wrist")), QStringLiteral("wrist"));
    QCOMPARE(equipmentSlot(QStringLiteral("wielded")), QStringLiteral("wielded"));
    QCOMPARE(equipmentSlot(QStringLiteral("held")), QStringLiteral("held"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on back")), QStringLiteral("back"));
    // The other wordings the logs have for the same places.
    QCOMPARE(equipmentSlot(QStringLiteral("used as shield")), QStringLiteral("shield"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn on forearm")), QStringLiteral("shield"));
    QCOMPARE(equipmentSlot(QStringLiteral("wielded two-handed")), QStringLiteral("wielded"));
    QCOMPARE(equipmentSlot(QStringLiteral("as two-handed weapon")), QStringLiteral("wielded"));
    QCOMPARE(equipmentSlot(QStringLiteral("as right weapon")), QStringLiteral("wielded"));
    QCOMPARE(equipmentSlot(QStringLiteral("worn across back")), QStringLiteral("across_back"));
    // A label nobody listed is kept, not refused.
    QCOMPARE(equipmentSlot(QStringLiteral("Worn Over Shoulder")),
             QStringLiteral("worn_over_shoulder"));
}

void TestItemLines::listedItemTest()
{
    // Container listings group identical objects with a number word (azazello.txt:1134).
    const ListedItem scrolls = parseListedItem(QStringLiteral("two azure scrolls"));
    QCOMPARE(scrolls.name, QStringLiteral("azure scrolls"));
    QCOMPARE(scrolls.count, 2);
    QVERIFY(scrolls.condition.isEmpty());
    QVERIFY(scrolls.flags.isEmpty());
    QCOMPARE(scrolls.text, QStringLiteral("two azure scrolls"));

    const ListedItem boots = parseListedItem(
        QStringLiteral("a black pair of padded boots (satisfactory)"));
    QCOMPARE(boots.name, QStringLiteral("a black pair of padded boots"));
    QCOMPARE(boots.count, 1);
    QCOMPARE(boots.condition, QStringLiteral("satisfactory"));

    const ListedItem worn = parseListedItem(
        QStringLiteral("a sturdy pair of metal boots (worn out)"));
    QCOMPARE(worn.condition, QStringLiteral("worn out"));

    // Flags after semicolons, in MUME's words.
    const ListedItem ruby = parseListedItem(
        QStringLiteral("a red ruby; it glows blue; it has a soft glowing aura"));
    QCOMPARE(ruby.name, QStringLiteral("a red ruby"));
    QVERIFY(ruby.condition.isEmpty());
    QCOMPARE(ruby.flags,
             (QStringList{QStringLiteral("it glows blue"),
                          QStringLiteral("it has a soft glowing aura")}));

    const ListedItem sword = parseListedItem(
        QStringLiteral("an engraved broadsword (flawless); it glows blue"));
    QCOMPARE(sword.name, QStringLiteral("an engraved broadsword"));
    QCOMPARE(sword.condition, QStringLiteral("flawless"));
    QCOMPARE(sword.flags, QStringList{QStringLiteral("it glows blue")});

    // "(invisible)" is not a condition.
    const ListedItem necklace = parseListedItem(QStringLiteral("a necklace (invisible)"));
    QCOMPARE(necklace.name, QStringLiteral("a necklace"));
    QVERIFY(necklace.condition.isEmpty());
    QCOMPARE(necklace.flags, QStringList{QStringLiteral("invisible")});

    // An object the player cannot see.
    const ListedItem something = parseListedItem(QStringLiteral("Something."));
    QCOMPARE(something.name, QStringLiteral("Something"));
}

void TestItemLines::equipmentLineTest()
{
    const auto sword = parseEquipmentLine(
        QStringLiteral("<wielded>            an engraved broadsword (flawless); it glows blue"));
    QVERIFY(sword.has_value());
    QCOMPARE(sword->label, QStringLiteral("wielded"));
    QCOMPARE(sword->slot, QStringLiteral("wielded"));
    QCOMPARE(sword->name, QStringLiteral("an engraved broadsword"));
    QCOMPARE(sword->condition, QStringLiteral("flawless"));
    QCOMPARE(sword->flags, QStringList{QStringLiteral("it glows blue")});
    QVERIFY(!sword->twoHanded);
    // Runs of spaces made one, as the protocol's examples show it.
    QCOMPARE(sword->text,
             QStringLiteral("<wielded> an engraved broadsword (flawless); it glows blue"));

    // A label too long for the padding leaves one space (stolb.balrog.txt, Uldor).
    const auto warsword = parseEquipmentLine(
        QStringLiteral("<wielded two-handed> a great warsword (flawless); it glows blue"));
    QVERIFY(warsword.has_value());
    QCOMPARE(warsword->slot, QStringLiteral("wielded"));
    QCOMPARE(warsword->label, QStringLiteral("wielded two-handed"));
    QVERIFY(warsword->twoHanded);

    // A light is held in the main era (stonedoor.txt:1955), used as one on the 2005 test server.
    const auto lantern = parseEquipmentLine(
        QStringLiteral("<held>               a lantern; it is lit"));
    QVERIFY(lantern.has_value());
    QCOMPARE(lantern->slot, QStringLiteral("held"));
    QCOMPARE(lantern->flags, QStringList{QStringLiteral("it is lit")});
    const auto stone = parseEquipmentLine(
        QStringLiteral("<used as light>        a blue stone; it has glowing aura"));
    QVERIFY(stone.has_value());
    QCOMPARE(stone->slot, QStringLiteral("light"));

    const auto sheath = parseEquipmentLine(QStringLiteral("<worn across back>   the black sheath"));
    QVERIFY(sheath.has_value());
    QCOMPARE(sheath->slot, QStringLiteral("across_back"));
    QCOMPARE(sheath->name, QStringLiteral("the black sheath"));

    QVERIFY(!parseEquipmentLine(QStringLiteral("a ruby ring")).has_value());
    QVERIFY(!parseEquipmentLine(QStringLiteral("<wielded>")).has_value());
}

void TestItemLines::equipmentBlockTest()
{
    ItemBlockTracker tracker;
    // The blank line after the last item closes it, before the prompt.
    std::vector<ItemBlock> blocks;
    for (const QString &line : AZAZELLO_EQUIPMENT) {
        for (ItemBlock &block : tracker.receiveLine(line)) {
            blocks.push_back(std::move(block));
        }
    }
    QCOMPARE(blocks.size(), size_t{1});
    QVERIFY(tracker.receivePrompt().empty());

    const ItemBlock &block = blocks.front();
    QCOMPARE(block.kind, ItemBlockKindEnum::EQUIPMENT);
    QCOMPARE(block.owner, QStringLiteral("you"));
    QCOMPARE(block.items.size(), size_t{22});
    QCOMPARE(block.items.at(0).slot, QStringLiteral("wielded"));
    QCOMPARE(block.items.at(0).name, QStringLiteral("an ornate, steel-shafted warhammer"));
    QVERIFY(block.items.at(0).condition.isEmpty());
    QCOMPARE(block.items.at(1).slot, QStringLiteral("shield"));
    QCOMPARE(block.items.at(4).slot, QStringLiteral("about"));
    QCOMPARE(block.items.at(4).name, QStringLiteral("a fine grey cloak"));
    // Repeated slots keep MUME's order.
    QCOMPARE(block.items.at(9).slot, QStringLiteral("neck"));
    QCOMPARE(block.items.at(9).name, QStringLiteral("an old length of iron chain"));
    QCOMPARE(block.items.at(10).slot, QStringLiteral("neck"));
    QCOMPARE(block.items.at(10).name, QStringLiteral("a dark stone"));
    QCOMPARE(block.items.at(11).slot, QStringLiteral("wrist"));
    QCOMPARE(block.items.at(15).slot, QStringLiteral("back"));
    // The belt itself and the things on it share a slot; the label tells them apart.
    QCOMPARE(block.items.at(16).slot, QStringLiteral("belt"));
    QCOMPARE(block.items.at(16).label, QStringLiteral("worn as belt"));
    QCOMPARE(block.items.at(17).label, QStringLiteral("worn on belt"));
    QCOMPARE(block.items.at(21).name, QStringLiteral("a small pouch"));
    QVERIFY(block.text.startsWith(QStringLiteral("You are using:\n<wielded> an ornate")));

    // Nothing worn at all.
    const auto naked = feed(tracker, {QStringLiteral("You are using:"), QStringLiteral("Nothing.")});
    QCOMPARE(naked.size(), size_t{1});
    QCOMPARE(naked.front().kind, ItemBlockKindEnum::EQUIPMENT);
    QVERIFY(naked.front().items.empty());
}

void TestItemLines::othersEquipmentTest()
{
    // azazello.txt:4295, a look at someone; MUME leaves a space after the colon.
    ItemBlockTracker tracker;
    const auto blocks = feed(tracker,
                             {QStringLiteral("You see nothing special about him."),
                              QStringLiteral("Vardamir Nolimon, the Uncrowned King is in an "
                                             "excellent condition."),
                              QStringLiteral("Vardamir Nolimon, the Uncrowned King is using: "),
                              QStringLiteral("<wielded>            a black runed dagger (flawless)"),
                              QStringLiteral("<used as shield>     a reinforced oak staff"),
                              QStringLiteral("<worn on head>       a twisted crown"),
                              QStringLiteral("<worn on belt>       a red ruby; it has a soft "
                                             "glowing aura"),
                              QStringLiteral("")});
    QCOMPARE(blocks.size(), size_t{1});
    const ItemBlock &block = blocks.front();
    QCOMPARE(block.kind, ItemBlockKindEnum::EQUIPMENT);
    QCOMPARE(block.owner, QStringLiteral("Vardamir Nolimon, the Uncrowned King"));
    QCOMPARE(block.items.size(), size_t{4});
    QCOMPARE(block.items.at(1).slot, QStringLiteral("shield"));
    QCOMPARE(block.items.at(1).label, QStringLiteral("used as shield"));
    QCOMPARE(block.items.at(3).flags, QStringList{QStringLiteral("it has a soft glowing aura")});

    // A group label is taken off the name, as the fight events do.
    const auto labelled = feed(tracker,
                               {QStringLiteral("Stolb (S) is using:"),
                                QStringLiteral("<worn on body>       a shining breastplate")});
    QCOMPARE(labelled.size(), size_t{1});
    QCOMPARE(labelled.front().owner, QStringLiteral("Stolb"));
}

void TestItemLines::peekTest()
{
    // stonedoor.txt:1954-1959: the look at an enemy, then a thief's peek in the same reply.
    ItemBlockTracker tracker;
    const auto blocks = feed(tracker,
                             {QStringLiteral("*Herby the Dwarf* is using: "),
                              QStringLiteral("<held>               a lantern; it is lit"),
                              QStringLiteral(""),
                              QStringLiteral("You attempt to peek at the inventory:"),
                              QStringLiteral("You don't see anything."),
                              QStringLiteral("")});
    QCOMPARE(blocks.size(), size_t{2});
    QCOMPARE(blocks.at(0).owner, QStringLiteral("*Herby the Dwarf*"));
    QCOMPARE(blocks.at(1).kind, ItemBlockKindEnum::INVENTORY);
    QVERIFY(blocks.at(1).peek);
    QCOMPARE(blocks.at(1).owner, QStringLiteral("*Herby the Dwarf*"));
    QVERIFY(blocks.at(1).items.empty());

    // stonedoor.txt:302-308, a peek that saw something. The prompt before it ended the look, so
    // the owner is not known.
    const auto seen = feed(tracker,
                           {QStringLiteral("You attempt to peek at the inventory:"),
                            QStringLiteral("a wooden pipe"),
                            QStringLiteral("a nasty orkish fang (satisfactory)"),
                            QStringLiteral("a fine grey cloak (flawless)"),
                            QStringLiteral("a large skin"),
                            QStringLiteral("a large skin"),
                            QStringLiteral("")});
    QCOMPARE(seen.size(), size_t{1});
    QVERIFY(seen.front().peek);
    QVERIFY(seen.front().owner.isEmpty());
    QCOMPARE(seen.front().items.size(), size_t{5});
    QCOMPARE(seen.front().items.at(1).condition, QStringLiteral("satisfactory"));
}

void TestItemLines::inventoryBlockTest()
{
    // azazello.txt:5206-5215, the reply to "i".
    ItemBlockTracker tracker;
    const auto blocks = feed(tracker,
                             {QStringLiteral("You are carrying:"),
                              QStringLiteral("a sturdy pair of chain mail sleeves (used)"),
                              QStringLiteral("a sturdy chain mail hauberk (flawless)"),
                              QStringLiteral("a sturdy pair of metal boots (flawless)"),
                              QStringLiteral("a sturdy pair of metal boots (flawless)"),
                              QStringLiteral("an engraved broadsword (flawless)")});
    // No blank line: the prompt closes it.
    QCOMPARE(blocks.size(), size_t{1});
    const ItemBlock &block = blocks.front();
    QCOMPARE(block.kind, ItemBlockKindEnum::INVENTORY);
    QCOMPARE(block.owner, QStringLiteral("you"));
    QVERIFY(!block.peek);
    // One line per object: inventory repeats a line rather than counting.
    QCOMPARE(block.items.size(), size_t{5});
    QCOMPARE(block.items.at(0).condition, QStringLiteral("used"));
    QCOMPARE(block.items.at(2).name, QStringLiteral("a sturdy pair of metal boots"));
    QCOMPARE(block.items.at(3).name, QStringLiteral("a sturdy pair of metal boots"));
    QVERIFY(block.items.at(0).label.isEmpty());
    QVERIFY(block.items.at(0).slot.isEmpty());

    const auto empty = feed(tracker,
                            {QStringLiteral("You are carrying:"), QStringLiteral("Nothing.")});
    QCOMPARE(empty.size(), size_t{1});
    QVERIFY(empty.front().items.empty());
    QCOMPARE(empty.front().text, QStringLiteral("You are carrying:\nNothing."));
}

void TestItemLines::containerBlockTest()
{
    // azazello.txt:1133-1149, "l in backpack".
    ItemBlockTracker tracker;
    tracker.receiveCommand(QStringLiteral("l in backpack"));
    const auto blocks = feed(tracker,
                             {QStringLiteral("backpack (used) : "),
                              QStringLiteral("two azure scrolls"),
                              QStringLiteral("a black pair of padded boots (satisfactory)"),
                              QStringLiteral("a dozen flasks of orkish draught"),
                              QStringLiteral("three handfuls of narrow leaves"),
                              QStringLiteral("some short, black fur"),
                              QStringLiteral("four small metal flasks"),
                              QStringLiteral("")});
    QCOMPARE(blocks.size(), size_t{1});
    const ItemBlock &block = blocks.front();
    QCOMPARE(block.kind, ItemBlockKindEnum::CONTAINER);
    QCOMPARE(block.keyword, QStringLiteral("backpack"));
    QCOMPARE(block.where, QStringLiteral("used"));
    QVERIFY(!block.closed);
    QCOMPARE(block.items.size(), size_t{6});
    QCOMPARE(block.items.at(0).count, 2);
    QCOMPARE(block.items.at(0).name, QStringLiteral("azure scrolls"));
    QCOMPARE(block.items.at(3).count, 3);
    QCOMPARE(block.items.at(5).count, 4);
    QCOMPARE(block.items.at(5).name, QStringLiteral("small metal flasks"));

    // MUME's own first keyword, whatever was typed (trolls_roots.txt:150511), and a corpse.
    tracker.receiveCommand(QStringLiteral("exami chest"));
    const auto chest = feed(tracker,
                            {QStringLiteral("stonechest (here) :"), QStringLiteral("Nothing.")});
    QCOMPARE(chest.size(), size_t{1});
    QCOMPARE(chest.front().keyword, QStringLiteral("stonechest"));
    QCOMPARE(chest.front().where, QStringLiteral("here"));
    QVERIFY(chest.front().items.empty());

    const auto corpse = feed(tracker,
                             {QStringLiteral("the corpse of *an Elf* (here):"),
                              QStringLiteral("a pile of coins"),
                              QStringLiteral("a metal buckler")});
    QCOMPARE(corpse.size(), size_t{1});
    QCOMPARE(corpse.front().keyword, QStringLiteral("the corpse of *an Elf*"));
    QCOMPARE(corpse.front().items.size(), size_t{2});

    const auto pouch = feed(tracker,
                            {QStringLiteral("pouch (carried) :"), QStringLiteral("a red ruby")});
    QCOMPARE(pouch.front().where, QStringLiteral("carried"));
}

void TestItemLines::closedContainerTest()
{
    ItemBlockTracker tracker;
    std::ignore = feed(tracker,
                       {QStringLiteral("backpack (used) :"), QStringLiteral("a vellum scroll")});

    // A look into a closed container is answered without a header; the command names it.
    tracker.receiveCommand(QStringLiteral("l in backpack"));
    const auto closed = feed(tracker, {QStringLiteral("It is closed.")});
    QCOMPARE(closed.size(), size_t{1});
    QCOMPARE(closed.front().kind, ItemBlockKindEnum::CONTAINER);
    QCOMPARE(closed.front().keyword, QStringLiteral("backpack"));
    // Where it was last listed.
    QCOMPARE(closed.front().where, QStringLiteral("used"));
    QVERIFY(closed.front().closed);
    QVERIFY(closed.front().items.empty());
    QCOMPARE(closed.front().text, QStringLiteral("It is closed."));

    // Without a look, "It is closed." is someone else's business.
    QVERIFY(feed(tracker, {QStringLiteral("It is closed.")}).empty());

    // A container never listed has no place; MUME may name it in full instead of "It".
    tracker.receiveCommand(QStringLiteral("look in cabinet"));
    const auto cabinet = feed(tracker, {QStringLiteral("A large cabinet is closed.")});
    QCOMPARE(cabinet.size(), size_t{1});
    QCOMPARE(cabinet.front().keyword, QStringLiteral("cabinet"));
    QVERIFY(cabinet.front().where.isEmpty());

    // A look in a direction is answered the same way for a door, which is not a container.
    tracker.receiveCommand(QStringLiteral("look in pouch"));
    QVERIFY(feed(tracker, {QStringLiteral("The door is closed.")}).empty());

    // The look is given up on after a couple of prompts.
    tracker.receiveCommand(QStringLiteral("look in pouch"));
    std::ignore = tracker.receivePrompt();
    std::ignore = tracker.receivePrompt();
    std::ignore = tracker.receivePrompt();
    QVERIFY(feed(tracker, {QStringLiteral("It is closed.")}).empty());
}

void TestItemLines::interruptedTest()
{
    ItemBlockTracker tracker;
    // A sentence ends the listing and is read on its own; so is another header.
    const auto blocks = feed(tracker,
                             {QStringLiteral("You are carrying:"),
                              QStringLiteral("a wooden pipe"),
                              QStringLiteral("backpack (carried) :"),
                              QStringLiteral("a lembas wafer"),
                              QStringLiteral("Zmej gets a lembas wafer from a leather backpack."),
                              QStringLiteral("a lembas wafer")});
    QCOMPARE(blocks.size(), size_t{2});
    QCOMPARE(blocks.at(0).kind, ItemBlockKindEnum::INVENTORY);
    QCOMPARE(blocks.at(0).items.size(), size_t{1});
    QCOMPARE(blocks.at(1).kind, ItemBlockKindEnum::CONTAINER);
    QCOMPARE(blocks.at(1).items.size(), size_t{1});

    // An equipment listing takes only equipment lines.
    const auto equipment = feed(tracker,
                                {QStringLiteral("You are using:"),
                                 QStringLiteral("<worn on body>       a shining breastplate"),
                                 QStringLiteral("You are carrying:"),
                                 QStringLiteral("a lembas wafer")});
    QCOMPARE(equipment.size(), size_t{2});
    QCOMPARE(equipment.at(0).items.size(), size_t{1});
    QCOMPARE(equipment.at(1).kind, ItemBlockKindEnum::INVENTORY);

    // Twiddlers and colour on the front of a line.
    const auto coloured = feed(tracker,
                               {QStringLiteral("\x1b[32mYou are carrying:\x1b[0m"),
                                QStringLiteral("/-\\|a wooden pipe")});
    QCOMPARE(coloured.size(), size_t{1});
    QCOMPARE(coloured.front().items.front().name, QStringLiteral("a wooden pipe"));

    // Chatter with no header is nobody's listing.
    QVERIFY(feed(tracker, {QStringLiteral("a wooden pipe"), QStringLiteral("Nothing.")}).empty());
}

void TestItemLines::runawayTest()
{
    // A listing whose end went by unseen is dropped rather than published half-read.
    ItemBlockTracker tracker;
    QVERIFY(tracker.receiveLine(QStringLiteral("You are carrying:")).empty());
    for (int i = 0; i < 300; ++i) {
        QVERIFY(tracker.receiveLine(QStringLiteral("a large skin")).empty());
    }
    QVERIFY(tracker.receivePrompt().empty());
    // And the next one is read as usual.
    QCOMPARE(feed(tracker, {QStringLiteral("You are carrying:"), QStringLiteral("a large skin")})
                 .size(),
             size_t{1});
}

void TestItemLines::itemEventTest()
{
    using A = ItemActionEnum;

    ItemEvent e = eventOf(QStringLiteral("You wear a forest green cloak about your body."));
    QCOMPARE(e.action, A::WEAR);
    QCOMPARE(e.item, QStringLiteral("a forest green cloak"));
    QCOMPARE(e.place, QStringLiteral("about body"));
    QCOMPARE(e.slot, QStringLiteral("about"));
    QCOMPARE(e.text, QStringLiteral("You wear a forest green cloak about your body."));

    e = eventOf(QStringLiteral("You wear a pair of iron-shod boots on your feet."));
    QCOMPARE(e.place, QStringLiteral("feet"));
    QCOMPARE(e.slot, QStringLiteral("feet"));
    e = eventOf(QStringLiteral("You wear an old length of iron chain around your neck."));
    QCOMPARE(e.slot, QStringLiteral("neck"));
    e = eventOf(QStringLiteral("You wear a keyring with a set of lock picks on your right wrist."));
    QCOMPARE(e.item, QStringLiteral("a keyring with a set of lock picks"));
    QCOMPARE(e.place, QStringLiteral("right wrist"));
    QCOMPARE(e.slot, QStringLiteral("wrist"));

    e = eventOf(QStringLiteral("You fasten a sable pouch on your belt."));
    QCOMPARE(e.action, A::WEAR);
    QCOMPARE(e.item, QStringLiteral("a sable pouch"));
    QCOMPARE(e.slot, QStringLiteral("belt"));
    e = eventOf(
        QStringLiteral("You fasten a bejewelled shield on your arm, becoming very impressive."));
    QCOMPARE(e.item, QStringLiteral("a bejewelled shield"));
    QCOMPARE(e.place, QStringLiteral("arm"));
    QCOMPARE(e.slot, QStringLiteral("shield"));

    e = eventOf(QStringLiteral("You put a ruby ring on your right finger."));
    QCOMPARE(e.action, A::WEAR);
    QCOMPARE(e.place, QStringLiteral("right finger"));
    QCOMPARE(e.slot, QStringLiteral("finger"));
    e = eventOf(QStringLiteral("You put a leather backpack on your back."));
    QCOMPARE(e.action, A::WEAR);
    QCOMPARE(e.slot, QStringLiteral("back"));

    e = eventOf(QStringLiteral("You wield a mighty dwarven axe, looking evil-minded."));
    QCOMPARE(e.action, A::WIELD);
    QCOMPARE(e.item, QStringLiteral("a mighty dwarven axe"));
    QVERIFY(e.place.isEmpty());
    e = eventOf(QStringLiteral("You wield an ornate, steel-shafted warhammer."));
    QCOMPARE(e.item, QStringLiteral("an ornate, steel-shafted warhammer"));
    e = eventOf(QStringLiteral(
        "You wield an oak staff as two-handed weapon, having no clue which end is up."));
    QCOMPARE(e.item, QStringLiteral("an oak staff"));
    QCOMPARE(e.place, QStringLiteral("two-handed"));

    e = eventOf(QStringLiteral("You hold a wooden pipe."));
    QCOMPARE(e.action, A::HOLD);
    QCOMPARE(e.item, QStringLiteral("a wooden pipe"));
    e = eventOf(QStringLiteral("You grab a blue stone."));
    QCOMPARE(e.action, A::HOLD);
    QCOMPARE(e.item, QStringLiteral("a blue stone"));
    e = eventOf(QStringLiteral("You light a lantern."));
    QCOMPARE(e.action, A::LIGHT);
    QCOMPARE(e.item, QStringLiteral("a lantern"));

    e = eventOf(QStringLiteral("You stop using an engraved broadsword."));
    QCOMPARE(e.action, A::REMOVE);
    QCOMPARE(e.item, QStringLiteral("an engraved broadsword"));
    e = eventOf(QStringLiteral("You take a water skin off the belt."));
    QCOMPARE(e.action, A::REMOVE);
    QCOMPARE(e.item, QStringLiteral("a water skin"));
    QCOMPARE(e.place, QStringLiteral("belt"));
    QCOMPARE(e.slot, QStringLiteral("belt"));
    e = eventOf(QStringLiteral("You take a sable pouch off the girdle."));
    QCOMPARE(e.slot, QStringLiteral("belt"));

    e = eventOf(QStringLiteral("You get a flask of orkish draught from a leather backpack."));
    QCOMPARE(e.action, A::GET);
    QCOMPARE(e.item, QStringLiteral("a flask of orkish draught"));
    QCOMPARE(e.container, QStringLiteral("a leather backpack"));
    e = eventOf(QStringLiteral("You get a copper penny."));
    QCOMPARE(e.action, A::GET);
    QVERIFY(e.container.isEmpty());
    e = eventOf(QStringLiteral("You remove a crude metal key from your keyring."));
    QCOMPARE(e.action, A::GET);
    QCOMPARE(e.item, QStringLiteral("a crude metal key"));
    QCOMPARE(e.container, QStringLiteral("your keyring"));
    e = eventOf(QStringLiteral(
        "You remove a fine chain hauberk from the corpse of *an Elf* and put into your inventory."));
    QCOMPARE(e.action, A::GET);
    QCOMPARE(e.container, QStringLiteral("the corpse of *an Elf*"));

    e = eventOf(QStringLiteral("You put a lembas wafer in a leather backpack."));
    QCOMPARE(e.action, A::PUT);
    QCOMPARE(e.item, QStringLiteral("a lembas wafer"));
    QCOMPARE(e.container, QStringLiteral("a leather backpack"));
    e = eventOf(QStringLiteral("You put a Tharbad gate key on your keyring."));
    QCOMPARE(e.action, A::PUT);
    QCOMPARE(e.container, QStringLiteral("your keyring"));
    e = eventOf(QStringLiteral("You put a saddle on a trained horse (my)'s back."));
    QCOMPARE(e.action, A::PUT);
    QCOMPARE(e.container, QStringLiteral("a trained horse (my)'s back"));

    e = eventOf(QStringLiteral("You drop the key."));
    QCOMPARE(e.action, A::DROP);
    QCOMPARE(e.item, QStringLiteral("the key"));

    e = eventOf(QStringLiteral("You give the ticket to the keeper of Elrond's stable."));
    QCOMPARE(e.action, A::GIVE);
    QCOMPARE(e.item, QStringLiteral("the ticket"));
    QCOMPARE(e.other, QStringLiteral("the keeper of Elrond's stable"));
    e = eventOf(QStringLiteral("Stolb (S) gives you a red ruby."));
    QCOMPARE(e.action, A::RECEIVE);
    QCOMPARE(e.item, QStringLiteral("a red ruby"));
    QCOMPARE(e.other, QStringLiteral("Stolb"));

    // Colour and twiddlers come off.
    e = eventOf(QStringLiteral("\x1b[0m|/-You drop the key.\x1b[0m"));
    QCOMPARE(e.text, QStringLiteral("You drop the key."));
}

void TestItemLines::refusedTest()
{
    using A = ItemActionEnum;

    ItemEvent e = eventOf(QStringLiteral("You are already wearing something on your legs."));
    QCOMPARE(e.action, A::REFUSED);
    QCOMPARE(e.reason, QStringLiteral("slot-taken"));
    QCOMPARE(e.place, QStringLiteral("legs"));
    QCOMPARE(e.slot, QStringLiteral("legs"));
    e = eventOf(QStringLiteral("You are already wearing something around your waist."));
    QCOMPARE(e.place, QStringLiteral("around waist"));
    QCOMPARE(e.slot, QStringLiteral("waist"));

    QCOMPARE(eventOf(QStringLiteral("You are already holding too much.")).reason,
             QStringLiteral("hands-full"));
    QCOMPARE(eventOf(QStringLiteral("You need two hands free to wield that.")).reason,
             QStringLiteral("two-hands"));
    QCOMPARE(eventOf(QStringLiteral("You can't carry that many items.")).reason,
             QStringLiteral("too-many"));

    e = eventOf(
        QStringLiteral("You can't get a sable pouch, you are carrying too many items already."));
    QCOMPARE(e.reason, QStringLiteral("too-many"));
    QCOMPARE(e.item, QStringLiteral("a sable pouch"));
    e = eventOf(QStringLiteral("You can't get a frozen corpse, it's too heavy."));
    QCOMPARE(e.reason, QStringLiteral("too-heavy"));
    QCOMPARE(e.item, QStringLiteral("a frozen corpse"));
    e = eventOf(QStringLiteral("You can't remove the black sword. It appears to be cursed."));
    QCOMPARE(e.reason, QStringLiteral("cursed"));
    QCOMPARE(e.item, QStringLiteral("the black sword"));
    e = eventOf(QStringLiteral("A leather backpack won't fit in a large sack."));
    QCOMPARE(e.reason, QStringLiteral("wont-fit"));
    QCOMPARE(e.item, QStringLiteral("A leather backpack"));
    QCOMPARE(e.container, QStringLiteral("a large sack"));
    e = eventOf(QStringLiteral("You can't put an engraved broadsword in a fragrant-smelling bag."));
    QCOMPARE(e.reason, QStringLiteral("cannot"));
    QCOMPARE(e.container, QStringLiteral("a fragrant-smelling bag"));
    e = eventOf(QStringLiteral("You don't have a sword."));
    QCOMPARE(e.reason, QStringLiteral("not-carried"));
    QCOMPARE(e.item, QStringLiteral("a sword"));
    e = eventOf(QStringLiteral("You aren't wearing a shield."));
    QCOMPARE(e.reason, QStringLiteral("not-worn"));
    QCOMPARE(e.item, QStringLiteral("a shield"));
    e = eventOf(QStringLiteral("You are not wearing any belt."));
    QCOMPARE(e.reason, QStringLiteral("not-worn"));
    QCOMPARE(e.item, QStringLiteral("belt"));
}

void TestItemLines::notItemEventTest()
{
    // Lines that start the same way and move nothing.
    for (const QString &line :
         {QStringLiteral("You get a shock as the Necromancer grasps at you."),
          QStringLiteral("You hold on tight when climbing down."),
          QStringLiteral("You grab the handle firmly and begin to turn a wooden crank clockwise."),
          QStringLiteral("You grab the ladder with both hands and feet, and glide down into the "
                         "guardhouse."),
          QStringLiteral("You put all your weight against the rock, trying to open it."),
          QStringLiteral("You give a final, mighty thrust of the pickaxe and break down the last "
                         "chunk of the wall, but the strain breaks your pickaxe as well."),
          QStringLiteral("You don't have the proper key."),
          QStringLiteral("You are already standing."),
          QStringLiteral("Zmej tells you 'Stolb gives you a red ruby.'"),
          QStringLiteral("You are carrying:"),
          QStringLiteral("a wooden pipe"),
          QStringLiteral("")}) {
        QVERIFY2(!parseItemEvent(line).has_value(), qPrintable(line));
    }
}

QTEST_MAIN(TestItemLines)
