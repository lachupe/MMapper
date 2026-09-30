// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestTradeLines.h"

#include "../src/frontend/TradeMessages.h"
#include "../src/global/Signal2.h"
#include "../src/observer/gameobserver.h"
#include "../src/parser/TradeLines.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

// Every reply here is copied from a log (prompts and the player's typing taken off), unless a
// comment says it was changed; the file each comes from is named above it. "elvenrunes" is
// mume-logs/elvenrunes, "powwow" is powwow/logs.

namespace {

/// Marks where MUME's prompt came in a transcript fed to feed().
const char *const PROMPT = "\x01prompt";
/// A pager line at 50%, and the same line again, as MUME repeats it after other output.
const char *const PAGER = "*** Return: continue, b: back, r: redisplay, q: quit (50%) *** ";
const char *const PAGER_ONE_PAGE
    = "*** Return: continue, b: back one page, r: redisplay, q: quit (73%) *** ";

/// Feeds `lines` through `tracker`, the way TradeReaders does: a prompt at each PROMPT, a pager
/// for a line that is one, and gathers what came out.
NODISCARD TradeReplies feedInto(TradeLinesTracker &tracker,
                                const std::initializer_list<const char *> lines)
{
    TradeReplies all;
    for (const char *const line : lines) {
        const QString text = QString::fromUtf8(line);
        if (QByteArray{line} == QByteArray{PROMPT}) {
            all.append(tracker.receivePrompt());
        } else if (const std::optional<PagerLine> pager = parsePagerLine(text)) {
            tracker.receivePager(*pager);
        } else {
            all.append(tracker.receiveLine(text));
        }
    }
    return all;
}

NODISCARD TradeReplies feed(const std::initializer_list<const char *> lines)
{
    TradeLinesTracker tracker;
    return feedInto(tracker, lines);
}

NODISCARD QStringList list(const std::initializer_list<const char *> names)
{
    QStringList result;
    for (const char *const name : names) {
        result.append(QString::fromUtf8(name));
    }
    return result;
}

NODISCARD QJsonObject json(const GmcpMessage &msg)
{
    return QJsonDocument::fromJson(msg.getJson()->toQByteArray()).object();
}

NODISCARD std::optional<int64_t> copper(const char *const words)
{
    const std::optional<Money> money = parseMoney(QString::fromUtf8(words));
    return money.has_value() ? std::optional<int64_t>{money->copper} : std::nullopt;
}

// powwow logs/archives/log-2005.10.26-01.18.32.txt:1044-1051
const std::initializer_list<const char *>
    g_helms{"You can buy:",
            "",
            " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver.",
            " 513. fourteen great helms (flawless, new) up to 8 gold 15 silver.",
            "-----",
            " 636. thirty-two reinforced leather helms (flawless, new) up to 4 silver 41 copper.",
            "-----",
            " 728. fourteen pot helms (flawless, new) up to one gold 2 silver.",
            ""};

} // namespace

void TestTradeLines::pagerLineTest()
{
    // elvenrunes, 133 and 37 times: both wordings, the space after the stars included.
    const std::optional<PagerLine> a = parsePagerLine(QString::fromUtf8(PAGER));
    QVERIFY(a.has_value());
    QCOMPARE(a->percent, 50);
    QCOMPARE(a->text,
             QStringLiteral("*** Return: continue, b: back, r: redisplay, q: quit (50%) ***"));
    const std::optional<PagerLine> b = parsePagerLine(QString::fromUtf8(PAGER_ONE_PAGE));
    QVERIFY(b.has_value());
    QCOMPARE(b->percent, 73);
    // elvenrunes: "q:quit" without the space, and ">: bottom" before the percentage.
    QCOMPARE(parsePagerLine(
                 QStringLiteral(
                     "*** Return: continue, b: back one page, r: redisplay, q:quit (4%) ***"))
                 ->percent,
             4);
    QCOMPARE(parsePagerLine(QStringLiteral("*** Return: continue, b: back, r: redisplay, q: "
                                           "quit, >: bottom (100%) *** "))
                 ->percent,
             100);
    // Coloured, as a client could get it.
    QVERIFY(parsePagerLine(QStringLiteral("\x1b[1m*** Return: continue, b: back, r: redisplay, q: "
                                          "quit (9%) ***\x1b[0m"))
                .has_value());
    // Not pagers: the trophy heading, a prompt, a pager with a line after it.
    QVERIFY(!parsePagerLine(QStringLiteral("*** TROPHY *** (Number Killed, Knowledge, Mobile)"))
                 .has_value());
    QVERIFY(!parsePagerLine(QStringLiteral("!# C Mana:Hot>")).has_value());
    QVERIFY(
        !parsePagerLine(QString::fromUtf8(PAGER) + QStringLiteral("You are using:")).has_value());

    // elvenrunes: the pager glued to the next line when no GA came.
    QString glued = QStringLiteral("*** Return: continue, b: back one page, r: redisplay, q:quit "
                                   "(88%) *** You are subjected to the following temporary "
                                   "effects:");
    QVERIFY(stripPagerPrefix(glued));
    QCOMPARE(glued, QStringLiteral("You are subjected to the following temporary effects:"));
    // A row's leading space survives; a carriage return wiping the pager is taken off too.
    // (Changed: the wipe is a guess at what MUME may send.)
    QString row = QString::fromUtf8(PAGER) + QStringLiteral(" 513. fourteen great helms");
    QVERIFY(stripPagerPrefix(row));
    QCOMPARE(row, QStringLiteral(" 513. fourteen great helms"));
    QString wiped = QString::fromUtf8(PAGER) + QStringLiteral("\r      \rArmour          Superb");
    QVERIFY(stripPagerPrefix(wiped));
    QCOMPARE(wiped, QStringLiteral("Armour          Superb"));
    QString none = QStringLiteral("You are using:");
    QVERIFY(!stripPagerPrefix(none));
    QCOMPARE(none, QStringLiteral("You are using:"));
}

void TestTradeLines::classifyChunkTest()
{
    const MudChunk pager = classifyMudChunk(true, false, QString::fromUtf8(PAGER));
    QCOMPARE(pager.kind, MudChunkKindEnum::PAGER);
    QVERIFY(pager.pager.has_value());
    const MudChunk prompt = classifyMudChunk(true, false, QStringLiteral("!# C Mana:Hot>"));
    QCOMPARE(prompt.kind, MudChunkKindEnum::PROMPT);
    QVERIFY(!prompt.pager.has_value());
    const MudChunk twiddler = classifyMudChunk(false, true, QStringLiteral("|\b"));
    QCOMPARE(twiddler.kind, MudChunkKindEnum::TWIDDLER);
    const MudChunk glued = classifyMudChunk(false,
                                            false,
                                            QString::fromUtf8(PAGER_ONE_PAGE)
                                                + QStringLiteral("Dispel evil            Superb"));
    QCOMPARE(glued.kind, MudChunkKindEnum::LINE);
    QCOMPARE(glued.pager->percent, 73);
    QCOMPARE(glued.plain, QStringLiteral("Dispel evil            Superb"));
    const MudChunk line = classifyMudChunk(false, false, QStringLiteral("You can buy:"));
    QCOMPARE(line.kind, MudChunkKindEnum::LINE);
    QVERIFY(!line.pager.has_value());
}

void TestTradeLines::moneyTest()
{
    // The list's prices (powwow 2005, stolb.balrog.txt).
    QCOMPARE(copper("4 gold 17 silver"), std::optional<int64_t>{9700});
    QCOMPARE(copper("one gold 2 silver"), std::optional<int64_t>{2200});
    QCOMPARE(copper("4 silver 41 copper"), std::optional<int64_t>{441});
    QCOMPARE(copper("four gold."), std::optional<int64_t>{8000});
    QCOMPARE(copper("seven gold six silver"), std::optional<int64_t>{14600});
    // The keepers' tells (elvenrunes 2014 Akra, 2025 Olugaar) and the inn's (help coins.txt:
    // 1 gold = 20 silver = 2000 copper).
    QCOMPARE(copper("two gold and 11 silver"), std::optional<int64_t>{5100});
    QCOMPARE(copper("38 gold and 18 silver coins"), std::optional<int64_t>{77800});
    QCOMPARE(copper("fifteen gold, eleven silver, and forty-seven copper"),
             std::optional<int64_t>{31147});
    QCOMPARE(copper("12 gold coins, 5 silver pennies, and 94 copper pennies"),
             std::optional<int64_t>{24594});
    QCOMPARE(copper("fifty-seven gold!"), std::optional<int64_t>{114000});
    QCOMPARE(copper("22 copper"), std::optional<int64_t>{22});
    QCOMPARE(copper("1,000 gold"), std::optional<int64_t>{2000000});
    QCOMPARE(copper("one hundred and five copper"), std::optional<int64_t>{105});
    // The short forms (help coins.txt: `put 1g 2s moneybag`) and the elven names (elvenrunes
    // 2015 Wildmagic's `info`).
    QCOMPARE(copper("1g 2s"), std::optional<int64_t>{2200});
    QCOMPARE(copper("3g 4s 5c"), std::optional<int64_t>{6405});
    QCOMPARE(copper("11 lauren coins, 12 celeb pennies, and 95 busc pennies"),
             std::optional<int64_t>{23295});
    QCOMPARE(copper("a gold coin"), std::optional<int64_t>{2000});
    // Not amounts.
    QVERIFY(!copper("a blood-encrusted helm").has_value());
    QVERIFY(!copper("4 gold and a helm").has_value());
    QVERIFY(!copper("gold").has_value());
    QVERIFY(!copper("").has_value());

    const std::optional<Money> said = findMoney(
        QStringLiteral("That'll be two gold and 11 silver, please."));
    QVERIFY(said.has_value());
    QCOMPARE(said->copper, int64_t{5100});
    QCOMPARE(said->text, QStringLiteral("two gold and 11 silver"));
    QCOMPARE(findMoney(QStringLiteral("Here you go... That's 22 copper."))->copper, int64_t{22});
    QCOMPARE(findMoney(QStringLiteral("Hand over eleven silver and forty copper! Now!"))->copper,
             int64_t{1140});
    QCOMPARE(findMoney(QStringLiteral("May it serve you well! I'll take 4 gold 17 silver"))->text,
             QStringLiteral("4 gold 17 silver"));
    QVERIFY(
        !findMoney(QStringLiteral("lol i cant retire because i dont have enough gold")).has_value());
    QVERIFY(!findMoney(QStringLiteral("I will not store a silver rod, marked with glyphs."))
                 .has_value());
}

void TestTradeLines::shopListTest()
{
    TradeLinesTracker tracker;
    tracker.receiveCommand(QStringLiteral("li helm"));
    TradeReplies r = feedInto(tracker, g_helms);
    QVERIFY(r.lists.empty()); // published at the prompt
    r.append(feedInto(tracker, {PROMPT}));
    QCOMPARE(r.lists.size(), size_t{1});
    const ShopList &l = r.lists.front();
    QCOMPARE(l.query, QStringLiteral("helm"));
    QVERIFY(!l.empty);
    QVERIFY(!l.paged);
    QVERIFY(l.complete);
    QCOMPARE(l.rows.size(), size_t{4});
    const ShopRow &first = l.rows[0];
    QCOMPARE(first.number, int64_t{470});
    QCOMPARE(first.count, int64_t{41});
    QCOMPARE(first.name, QStringLiteral("blood-encrusted helms"));
    QCOMPARE(first.singular, QStringLiteral("blood-encrusted helm"));
    QCOMPARE(first.condition, QStringLiteral("flawless"));
    QCOMPARE(first.age, QStringLiteral("new"));
    QCOMPARE(first.priceCopper, std::optional<int64_t>{9700});
    QCOMPARE(first.priceText, QStringLiteral("4 gold 17 silver"));
    QCOMPARE(first.group, 0);
    QCOMPARE(l.rows[1].group, 0);
    QCOMPARE(l.rows[2].count, int64_t{32});
    QCOMPARE(l.rows[2].priceCopper, std::optional<int64_t>{441});
    QCOMPARE(l.rows[2].group, 1);
    QCOMPARE(l.rows[3].priceCopper, std::optional<int64_t>{2200});
    QCOMPARE(l.rows[3].group, 2);
    QVERIFY(l.text.startsWith(QStringLiteral("You can buy:\n\n 470. forty-one")));
    QVERIFY(l.text.endsWith(QStringLiteral("up to one gold 2 silver.")));

    // powwow logs/stolb.balrog.txt:2271-2283, one row: "fresh", words throughout.
    const TradeReplies s = feed(
        {"You can buy:",
         "",
         " 343. eleven blood-encrusted helms (flawless, new) up to five gold one silver.",
         " 355. three great helms (flawless, fresh) up to seven gold six silver.",
         " 385. two fine leather helmets (flawless, seasoned) up to one silver 29 copper.",
         " 388. thirteen full metal helmets (flawless, new) up to four gold.",
         " 426. ten reinforced leather helmets (well-maintained, new) up to three silver 23 "
         "copper.",
         "",
         PROMPT});
    QCOMPARE(s.lists.size(), size_t{1});
    QCOMPARE(s.lists[0].rows.size(), size_t{5});
    QCOMPARE(s.lists[0].rows[0].priceCopper, std::optional<int64_t>{10100});
    QCOMPARE(s.lists[0].rows[1].age, QStringLiteral("fresh"));
    QCOMPARE(s.lists[0].rows[2].singular, QStringLiteral("fine leather helmet"));
    QCOMPARE(s.lists[0].rows[2].priceCopper, std::optional<int64_t>{129});
    QCOMPARE(s.lists[0].rows[3].count, int64_t{13});
    QCOMPARE(s.lists[0].rows[3].priceCopper, std::optional<int64_t>{8000});
    QCOMPARE(s.lists[0].rows[4].condition, QStringLiteral("well-maintained"));

    // powwow logs/cheaters.mov:23325-23329: stock numbers in the thousands, a hyphenated name
    // after the count, and an irregular plural.
    const TradeReplies c = feed(
        {"You can buy:",
         "",
         "2568. thirteen two-handed swords (flawless, new) up to two gold 15 silver.",
         "-----",
         "2606. ten two-handed axes (flawless, new) up to three gold seven silver.",
         "",
         PROMPT});
    QCOMPARE(c.lists[0].rows[0].number, int64_t{2568});
    QCOMPARE(c.lists[0].rows[0].count, int64_t{13});
    QCOMPARE(c.lists[0].rows[0].name, QStringLiteral("two-handed swords"));
    QCOMPARE(c.lists[0].rows[0].singular, QStringLiteral("two-handed sword"));
    QCOMPARE(c.lists[0].rows[1].singular, QStringLiteral("two-handed axe"));
    QCOMPARE(c.lists[0].rows[1].group, 1);
}

void TestTradeLines::shopListEmptyAndForeignTest()
{
    // powwow logs/archives/log-2005.10.26-01.18.32.txt:1041
    TradeLinesTracker tracker;
    tracker.receiveCommand(QStringLiteral("li blood"));
    const TradeReplies none = feedInto(tracker, {"There are no such things for sale.", "", PROMPT});
    QCOMPARE(none.lists.size(), size_t{1});
    QVERIFY(none.lists[0].empty);
    QVERIFY(none.lists[0].rows.empty());
    QCOMPARE(none.lists[0].query, QStringLiteral("blood"));

    // Someone leaving in the middle of the list (changed: "Kilthan leaves south." is from after
    // the buy in the same log) is let through.
    const TradeReplies foreign = feed(
        {"You can buy:",
         "",
         " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver.",
         "Kilthan leaves south.",
         " 513. fourteen great helms (flawless, new) up to 8 gold 15 silver.",
         "",
         PROMPT});
    QCOMPARE(foreign.lists.size(), size_t{1});
    QCOMPARE(foreign.lists[0].rows.size(), size_t{2});
    QVERIFY(!foreign.lists[0].text.contains(QStringLiteral("Kilthan")));
}

void TestTradeLines::shopListPagedTest()
{
    // The helms list cut by the pager after its second row, then continued: paged, complete.
    const TradeReplies continued = feed(
        {"You can buy:",
         "",
         " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver.",
         " 513. fourteen great helms (flawless, new) up to 8 gold 15 silver.",
         PAGER,
         "-----",
         " 636. thirty-two reinforced leather helms (flawless, new) up to 4 silver 41 copper.",
         "",
         PROMPT});
    QCOMPARE(continued.lists.size(), size_t{1});
    QVERIFY(continued.lists[0].paged);
    QVERIFY(continued.lists[0].complete);
    QCOMPARE(continued.lists[0].rows.size(), size_t{3});
    QCOMPARE(continued.lists[0].rows[2].group, 1);

    // The same, quit at the pager: the prompt comes with no line after it.
    const TradeReplies quit = feed(
        {"You can buy:",
         "",
         " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver.",
         PAGER_ONE_PAGE,
         PROMPT});
    QCOMPARE(quit.lists.size(), size_t{1});
    QVERIFY(quit.lists[0].paged);
    QVERIFY(!quit.lists[0].complete);
    QCOMPARE(quit.lists[0].rows.size(), size_t{1});

    // MUME shows the pager again with the same percentage after someone else's output; that is
    // still one pager, and the reply goes on.
    const TradeReplies repeated = feed(
        {"You can buy:",
         "",
         " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver.",
         PAGER,
         "Kilthan leaves south.",
         PAGER,
         " 513. fourteen great helms (flawless, new) up to 8 gold 15 silver.",
         "",
         PROMPT});
    QCOMPARE(repeated.lists.size(), size_t{1});
    QVERIFY(repeated.lists[0].paged);
    QVERIFY(repeated.lists[0].complete);
    QCOMPARE(repeated.lists[0].rows.size(), size_t{2});

    // A pager after the list had ended belongs to whatever came next, not to the list.
    const TradeReplies after = feed(
        {"You can buy:",
         "",
         " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver.",
         "",
         PAGER,
         PROMPT});
    QVERIFY(!after.lists[0].paged);
    QVERIFY(after.lists[0].complete);
}

void TestTradeLines::shopDealTest()
{
    // powwow logs/archives/log-2005.10.26-01.18.32.txt:1054-1055, the tell in MUME's colour.
    const TradeReplies bought = feed(
        {"\x1b[32;1mAngdil tells you 'May it serve you well! I'll take 4 gold 17 silver'\x1b[0m",
         "You now have a blood-encrusted helm.",
         "",
         PROMPT});
    QCOMPARE(bought.deals.size(), size_t{1});
    const ShopDeal &b = bought.deals.front();
    QCOMPARE(b.kind, ShopDealKindEnum::BUY);
    QCOMPARE(b.keeper, QStringLiteral("Angdil"));
    QCOMPARE(b.amountCopper, std::optional<int64_t>{9700});
    QCOMPARE(b.amountText, QStringLiteral("4 gold 17 silver"));
    QCOMPARE(b.items, list({"a blood-encrusted helm"}));
    QCOMPARE(b.said, QStringLiteral("May it serve you well! I'll take 4 gold 17 silver"));
    QVERIFY(b.text.contains(QStringLiteral("You now have a blood-encrusted helm.")));

    // elvenrunes 2014-09-30_A-lot-to-learn..._Akra.txt:143-150: a sale wrapped over two lines,
    // two purchases and a miss between them, all before one prompt.
    const TradeReplies akra = feed(
        {"An armourer tells you 'Here you have 38 gold and 18 silver coins for that.'",
         "You sell a fine metal breastplate, a great helm, a fine pair of metal greaves,",
         "a fine pair of metal gauntlets, and a fine pair of metal vambraces.",
         "An armourer tells you 'That'll be two gold and 11 silver, please.'",
         "You now have a fine chain mail hauberk.",
         "There is no such thing for sale.",
         "An armourer tells you 'That'll be one gold and 13 silver, please.'",
         "You now have a fine pair of chain mail leggings.",
         PROMPT});
    QCOMPARE(akra.deals.size(), size_t{4});
    QCOMPARE(akra.deals[0].kind, ShopDealKindEnum::SELL);
    QCOMPARE(akra.deals[0].keeper, QStringLiteral("An armourer"));
    QCOMPARE(akra.deals[0].amountCopper, std::optional<int64_t>{77800});
    QCOMPARE(akra.deals[0].items,
             list({"a fine metal breastplate",
                   "a great helm",
                   "a fine pair of metal greaves",
                   "a fine pair of metal gauntlets",
                   "a fine pair of metal vambraces"}));
    QCOMPARE(akra.deals[1].kind, ShopDealKindEnum::BUY);
    QCOMPARE(akra.deals[1].amountCopper, std::optional<int64_t>{5100});
    QCOMPARE(akra.deals[1].items, list({"a fine chain mail hauberk"}));
    QCOMPARE(akra.deals[2].kind, ShopDealKindEnum::MISS);
    QVERIFY(akra.deals[2].keeper.isEmpty());
    QCOMPARE(akra.deals[3].kind, ShopDealKindEnum::BUY);
    QCOMPARE(akra.deals[3].amountCopper, std::optional<int64_t>{3300});

    // elvenrunes 2025-04-27_Warrens-Tower-Defense_Olugaar.txt:4046-4049: the amount all in
    // words, and both the tell and the list wrapped.
    const TradeReplies kormock = feed(
        {"Kormock the orkish armourer tells you 'Here is fifteen gold, eleven silver, and",
         "forty-seven copper for you.'",
         "You sell a metal breastplate, a sturdy pair of greaves, a sturdy pair of metal",
         "vambraces, a white pair of metal vambraces, and two metal bucklers.",
         PROMPT});
    QCOMPARE(kormock.deals.size(), size_t{1});
    const ShopDeal &k = kormock.deals.front();
    QCOMPARE(k.kind, ShopDealKindEnum::SELL);
    QCOMPARE(k.keeper, QStringLiteral("Kormock the orkish armourer"));
    QCOMPARE(k.amountCopper, std::optional<int64_t>{31147});
    QCOMPARE(k.said,
             QStringLiteral("Here is fifteen gold, eleven silver, and forty-seven copper for you."));
    QCOMPARE(k.items,
             list({"a metal breastplate",
                   "a sturdy pair of greaves",
                   "a sturdy pair of metal vambraces",
                   "a white pair of metal vambraces",
                   "two metal bucklers"}));

    // elvenrunes (five logs): the butcher's tell wraps before its closing quote.
    const TradeReplies meat = feed({"Gruksh the Butcher tells you 'That stuff is seventeen copper, "
                                    "and now piss",
                                    "off.'",
                                    "You now have a strip of dried meat.",
                                    PROMPT});
    QCOMPARE(meat.deals.size(), size_t{1});
    QCOMPARE(meat.deals[0].amountCopper, std::optional<int64_t>{17});
    QCOMPARE(meat.deals[0].items, list({"a strip of dried meat"}));

    // A pager between the tell and the item keeps the deal open.
    const TradeReplies paged = feed(
        {"An armourer tells you 'That'll be two gold and 11 silver, please.'",
         PAGER,
         "You now have a fine chain mail hauberk.",
         PROMPT});
    QCOMPARE(paged.deals.size(), size_t{1});
    QCOMPARE(paged.deals[0].kind, ShopDealKindEnum::BUY);
}

void TestTradeLines::shopClosedAndNotDealsTest()
{
    // elvenrunes 2017-02-23_Sometimes-I-don-t-die_Adams.txt:411, said on entering.
    const TradeReplies closed = feed(
        {"A grocer tells you 'Sorry, we are closed. We will open in a couple of hours.'", PROMPT});
    QCOMPARE(closed.deals.size(), size_t{1});
    QCOMPARE(closed.deals[0].kind, ShopDealKindEnum::CLOSED);
    QCOMPARE(closed.deals[0].keeper, QStringLiteral("A grocer"));
    QCOMPARE(closed.deals[0].said,
             QStringLiteral("Sorry, we are closed. We will open in a couple of hours."));

    // elvenrunes: none of these is a deal. A level lost, a player's tell about gold, a tell with
    // an amount that nothing followed, a steward's refusal, and a tell narrated by someone.
    const TradeReplies none = feed(
        {"You now have 10,300,000 experience points and 5,530 travel points. You regress to "
         "level 30.",
         "Roostur tells you 'lol i cant retire because i dont have enough gold'",
         "You now have a strip of dried meat.",
         "Morthan Blacksoul tells you 'Let's see...  your taxes come to two gold'",
         "A steward tells you 'I will not store a silver rod, marked with glyphs.'",
         "Osten narrates 'Erienal tells you 'You can recover your confiscated equipment for 587 "
         "gold.''",
         PROMPT});
    QVERIFY(none.deals.empty());
    QVERIFY(none.inns.empty());
}

void TestTradeLines::guildTeacherTest()
{
    // elvenrunes 2020-11-03_Iminye-Umnik-Vidgri---is-a-jackass_Hoobert.txt:61-75
    const TradeReplies r = feed(
        {"You have eleven practice sessions left.",
         "Erestor can teach you the spells below.",
         "",
         "Spell         Sessions  Knowledge  Difficulty  Advice",
         "Block door        8/11        91%  Normal      Hard to improve",
         "Detect magic      1/ 8        51%  Easy        Easy to improve",
         "Dispel magic      0/22         0%  Very hard   Takes some time to learn",
         "Earthquake       16/16       106%  Hard        You know as much as I do",
         "Enchant          10/22        86%  Very hard   Learning more could help you",
         "Identify          0/16         0%  Hard        Takes some time to learn",
         "Locate            0/ 3         0%  Hard        You could learn easily",
         "Locate magic      0/12         0%  Normal      You could learn easily",
         "Portal            0/22         0%  Very hard   Takes a long time to learn",
         "Silence           0/11         0%  Normal      Takes some time to learn",
         "Store            10/16        94%  Hard        Learning more could help you",
         "",
         PROMPT});
    QCOMPARE(r.teachers.size(), size_t{1});
    QVERIFY(r.skills.empty());
    const GuildTeacher &t = r.teachers.front();
    QCOMPARE(t.teacher, QStringLiteral("Erestor"));
    QCOMPARE(t.kind, QStringLiteral("spells"));
    QCOMPARE(t.sessionsLeft, std::optional<int64_t>{11});
    QCOMPARE(t.rows.size(), size_t{11});
    QCOMPARE(t.rows[0].name, QStringLiteral("Block door"));
    QCOMPARE(t.rows[0].used, int64_t{8});
    QCOMPARE(t.rows[0].most, int64_t{11});
    QCOMPARE(t.rows[0].knowledgePct, std::optional<int64_t>{91});
    QCOMPARE(t.rows[0].difficulty, QStringLiteral("Normal"));
    QCOMPARE(t.rows[0].advice, QStringLiteral("Hard to improve"));
    QCOMPARE(t.rows[1].most, int64_t{8});
    QCOMPARE(t.rows[2].difficulty, QStringLiteral("Very hard"));
    QCOMPARE(t.rows[2].advice, QStringLiteral("Takes some time to learn"));
    QCOMPARE(t.rows[3].knowledgePct, std::optional<int64_t>{106});
    QCOMPARE(t.rows[3].advice, QStringLiteral("You know as much as I do"));
    QVERIFY(!t.paged);
    QVERIFY(t.complete);
    QVERIFY(t.text.startsWith(QStringLiteral("You have eleven practice sessions left.\nErestor")));
}

void TestTradeLines::guildTeacherOldFormTest()
{
    // elvenrunes 2015-03-31_Per-and-spell-damage._Wildmagic.txt:42-78: the general table, a
    // teacher's table in the short 2015 heading with no "can teach" line, and a second teacher.
    const TradeReplies r = feed(
        {"You have 27 practice sessions left.",
         "Skill / Spell   Knowledge  Difficulty  Class       Mana  Casting time",
         "Bandage         Superb     Easy        None       ",
         "Leadership      Poor       Normal      None       ",
         "Armour          Superb     Hard        Magic User    31  Very short",
         "Earthquake      Excellent  Hard        Magic User    20  Short",
         "",
         "                    Sessions  Knowl.  Diffic.  Advice",
         "armour                  12/12    105%  Hard     You know as much as I do",
         "control weather          0/ 3      0%  Hard     I can't teach you enough",
         "lightning bolt           9/16     89%  Hard     You reached your current limit",
         "",
         "You have twenty-seven practice sessions left.",
         "Erestor can teach you the spells below.",
         "",
         "              Sessions  Knowledge  Difficulty  Advice",
         "block door        0/11         0%  Normal      Takes a long time to learn",
         "detect magic      3/ 8        79%  Easy        Learning more could help you",
         "",
         PROMPT});
    QCOMPARE(r.skills.size(), size_t{1});
    QCOMPARE(r.skills[0].sessionsLeft, std::optional<int64_t>{27});
    QCOMPARE(r.skills[0].rows.size(), size_t{4});
    QCOMPARE(r.teachers.size(), size_t{2});
    QVERIFY(r.teachers[0].teacher.isEmpty());
    QVERIFY(r.teachers[0].kind.isEmpty());
    QCOMPARE(r.teachers[0].rows.size(), size_t{3});
    QCOMPARE(r.teachers[0].rows[1].name, QStringLiteral("control weather"));
    QCOMPARE(r.teachers[0].rows[1].advice, QStringLiteral("I can't teach you enough"));
    QCOMPARE(r.teachers[0].rows[2].knowledgePct, std::optional<int64_t>{89});
    QCOMPARE(r.teachers[1].teacher, QStringLiteral("Erestor"));
    QCOMPARE(r.teachers[1].sessionsLeft, std::optional<int64_t>{27});
    QCOMPARE(r.teachers[1].rows.size(), size_t{2});
    QCOMPARE(r.teachers[1].rows[1].used, int64_t{3});

    // elvenrunes (two logs): the count below zero, and a skills teacher (changed: no skills
    // teacher's table is in the logs; the words follow the spells one).
    const TradeReplies owed = feed({"You have -2 practice sessions left.",
                                    "Glorfindel can teach you the skills below.",
                                    "",
                                    "Skill         Sessions  Knowledge  Difficulty  Advice",
                                    "Bash              3/11        40%  Hard        Hard to improve",
                                    "",
                                    PROMPT});
    QCOMPARE(owed.teachers.size(), size_t{1});
    QCOMPARE(owed.teachers[0].sessionsLeft, std::optional<int64_t>{-2});
    QCOMPARE(owed.teachers[0].kind, QStringLiteral("skills"));
    QCOMPARE(owed.teachers[0].rows[0].name, QStringLiteral("Bash"));
}

void TestTradeLines::guildTeacherPagedTest()
{
    // The Hoobert table cut by the pager in both wordings; once continued, once quit.
    const TradeReplies continued = feed(
        {"You have eleven practice sessions left.",
         "Erestor can teach you the spells below.",
         "",
         "Spell         Sessions  Knowledge  Difficulty  Advice",
         "Block door        8/11        91%  Normal      Hard to improve",
         PAGER_ONE_PAGE,
         "Detect magic      1/ 8        51%  Easy        Easy to improve",
         "",
         PROMPT});
    QCOMPARE(continued.teachers.size(), size_t{1});
    QVERIFY(continued.teachers[0].paged);
    QVERIFY(continued.teachers[0].complete);
    QCOMPARE(continued.teachers[0].rows.size(), size_t{2});

    const TradeReplies quit = feed({"You have eleven practice sessions left.",
                                    "Erestor can teach you the spells below.",
                                    "",
                                    PAGER,
                                    PROMPT});
    QCOMPARE(quit.teachers.size(), size_t{1});
    QVERIFY(quit.teachers[0].paged);
    QVERIFY(!quit.teachers[0].complete);
    QVERIFY(quit.teachers[0].rows.empty());
}

void TestTradeLines::practisedTest()
{
    // elvenrunes 2015-12-28_Orc-vs-Pointyears_Shadowdancer.txt:1030-1066: ten queued commands,
    // each answered before its own prompt.
    TradeLinesTracker tracker;
    tracker.receiveCommand(QStringLiteral("prac shocking grasp"));
    tracker.receiveCommand(QStringLiteral("prac shocking grasp"));
    const TradeReplies r
        = feedInto(tracker,
                   {"You took 1 out of 11 sessions in this skill. Your knowledge is now 25%.",
                    PROMPT,
                    "You took 2 out of 11 sessions in this skill. Your knowledge is now 35%.",
                    PROMPT});
    QCOMPARE(r.practised.size(), size_t{2});
    QCOMPARE(r.practised[0].name, QStringLiteral("shocking grasp"));
    QCOMPARE(r.practised[0].used, std::optional<int64_t>{1});
    QCOMPARE(r.practised[0].most, std::optional<int64_t>{11});
    QCOMPARE(r.practised[0].knowledgePct, std::optional<int64_t>{25});
    QVERIFY(r.practised[0].refused.isEmpty());
    QCOMPARE(r.practised[1].used, std::optional<int64_t>{2});
    QCOMPARE(r.practised[1].knowledgePct, std::optional<int64_t>{35});

    // The practised line comes out at once, before the prompt.
    TradeLinesTracker once;
    once.receiveCommand(QStringLiteral("practise earthquake"));
    const TradeReplies at = once.receiveLine(
        QStringLiteral("You took 10 out of 11 sessions in this skill. Your knowledge is now 64%."));
    QCOMPARE(at.practised.size(), size_t{1});
    QCOMPARE(at.practised[0].name, QStringLiteral("earthquake"));

    // elvenrunes (one log): refused while not standing.
    TradeLinesTracker sitting;
    sitting.receiveCommand(QStringLiteral("prac block door"));
    const TradeReplies refused = sitting.receiveLine(
        QStringLiteral("You have to stand in order to practice anything."));
    QCOMPARE(refused.practised.size(), size_t{1});
    QCOMPARE(refused.practised[0].refused,
             QStringLiteral("You have to stand in order to practice anything."));
    QCOMPARE(refused.practised[0].name, QStringLiteral("block door"));
    QVERIFY(!refused.practised[0].used.has_value());

    // `prac` alone and `list` are no practice commands.
    TradeLinesTracker bare;
    bare.receiveCommand(QStringLiteral("prac"));
    bare.receiveCommand(QStringLiteral("pray"));
    const TradeReplies unnamed = bare.receiveLine(
        QStringLiteral("You took 3 out of 11 sessions in this skill. Your knowledge is now 41%."));
    QVERIFY(unnamed.practised[0].name.isEmpty());
}

void TestTradeLines::charSkillsTest()
{
    // elvenrunes 2021-04-01_Battle-Coach-I_Ryaln.txt:3441-3453
    const TradeReplies r = feed(
        {"You have 0 practice sessions left.",
         "Skill / Spell        Knowledge  Difficulty  Class       Mana  Casting time",
         "Bandage              Average    Easy        None       ",
         "Wilderness           Very good  Normal      None       ",
         "Cure light           Average    Easy        Cleric        13  Very short",
         "Dodge                Poor       Hard        Thief      ",
         "Two handed weapons   Fair       Normal      Warrior    ",
         "",
         PROMPT});
    QCOMPARE(r.skills.size(), size_t{1});
    QVERIFY(r.teachers.empty());
    const CharSkills &s = r.skills.front();
    QCOMPARE(s.sessionsLeft, std::optional<int64_t>{0});
    QCOMPARE(s.rows.size(), size_t{5});
    QCOMPARE(s.rows[1].name, QStringLiteral("Wilderness"));
    QCOMPARE(s.rows[1].knowledge, QStringLiteral("Very good"));
    QVERIFY(s.rows[1].trained);
    QCOMPARE(s.rows[1].skillClass, QStringLiteral("None"));
    QVERIFY(!s.rows[1].mana.has_value());
    QCOMPARE(s.rows[2].skillClass, QStringLiteral("Cleric"));
    QCOMPARE(s.rows[2].mana, std::optional<int64_t>{13});
    QCOMPARE(s.rows[2].casting, QStringLiteral("Very short"));
    QCOMPARE(s.rows[4].name, QStringLiteral("Two handed weapons"));
    QCOMPARE(s.rows[4].skillClass, QStringLiteral("Warrior"));

    // elvenrunes 2026-07-18_Moria-Panic_Narada.txt:2253-2264: a blank line and a dashed rule
    // under the heading, and no trailing spaces.
    const TradeReplies narada = feed(
        {"You have 0 practice sessions left.",
         "",
         "Skill / Spell        Knowledge  Difficulty  Class       Mana  Casting time",
         "--------------------------------------------------------------------------",
         "Climb                Very good  Very easy   None",
         "Armour               Excellent  Hard        Magic User    25  Very short",
         "Block door           Poor       Normal      Magic User    34  Short",
         "",
         PROMPT});
    QCOMPARE(narada.skills.size(), size_t{1});
    QCOMPARE(narada.skills[0].sessionsLeft, std::optional<int64_t>{0});
    QCOMPARE(narada.skills[0].rows.size(), size_t{3});
    QCOMPARE(narada.skills[0].rows[0].difficulty, QStringLiteral("Very easy"));
    QCOMPARE(narada.skills[0].rows[1].skillClass, QStringLiteral("Magic User"));
    QCOMPARE(narada.skills[0].rows[2].casting, QStringLiteral("Short"));

    // help train.txt: a `*` before the knowledge word marks a skill not being trained
    // (changed: no log shows the marker, so where it stands is a guess).
    const TradeReplies starred = feed(
        {"You have 1 practice session left.",
         "Skill / Spell        Knowledge  Difficulty  Class       Mana  Casting time",
         "Swim                 *Good      Very easy   None       ",
         "Track               *Fair       Normal      None       ",
         "",
         PROMPT});
    QCOMPARE(starred.skills[0].sessionsLeft, std::optional<int64_t>{1});
    QCOMPARE(starred.skills[0].rows[0].knowledge, QStringLiteral("Good"));
    QVERIFY(!starred.skills[0].rows[0].trained);
    QCOMPARE(starred.skills[0].rows[1].name, QStringLiteral("Track"));
    QVERIFY(!starred.skills[0].rows[1].trained);
}

void TestTradeLines::charSkillsPagedTest()
{
    // elvenrunes 2020-12-30_..._Aquator.txt:1716-1747, cut at 73% and continued.
    const TradeReplies r = feed(
        {"You have 1 practice session left.",
         "Skill / Spell          Knowledge  Difficulty  Class       Mana  Casting time",
         "Climb                  Good       Very easy   None       ",
         "Cure blindness         Fair       Normal      Cleric         4  Very short",
         "*** Return: continue, b: back, r: redisplay, q: quit (73%) *** ",
         "Dispel evil            Superb     Hard        Cleric        17  Very short",
         "Pick                   Poor       Normal      Thief      ",
         "",
         PROMPT});
    QCOMPARE(r.skills.size(), size_t{1});
    QVERIFY(r.skills[0].paged);
    QVERIFY(r.skills[0].complete);
    QCOMPARE(r.skills[0].rows.size(), size_t{4});
    QCOMPARE(r.skills[0].rows[2].mana, std::optional<int64_t>{17});

    // Quit at the pager.
    const TradeReplies quit = feed(
        {"You have 1 practice session left.",
         "Skill / Spell          Knowledge  Difficulty  Class       Mana  Casting time",
         "Climb                  Good       Very easy   None       ",
         PAGER,
         PROMPT});
    QVERIFY(quit.skills[0].paged);
    QVERIFY(!quit.skills[0].complete);
}

void TestTradeLines::innOfferTest()
{
    // elvenrunes: the shady innkeeper's quote, wrapped.
    TradeLinesTracker tracker;
    TradeReplies r = feedInto(tracker,
                              {"The shady innkeeper tells you 'It will cost you 12 gold coins, 5 "
                               "silver",
                               "pennies, and 94 copper pennies per day.'"});
    QVERIFY(r.inns.empty());
    // The quote is out as soon as "You have enough money" completes it: `rent` may end the
    // session before the next prompt.
    r = feedInto(tracker, {"You have enough money for at least five years!"});
    QCOMPARE(r.inns.size(), size_t{1});
    const InnOffer &o = r.inns.front();
    QCOMPARE(o.keeper, QStringLiteral("The shady innkeeper"));
    QCOMPARE(o.perDayCopper, std::optional<int64_t>{24594});
    QCOMPARE(o.perDayText, QStringLiteral("12 gold coins, 5 silver pennies, and 94 copper pennies"));
    QCOMPARE(o.lastsText, QStringLiteral("You have enough money for at least five years!"));
    QVERIFY(!o.retireAsksRepeat);
    QCOMPARE(o.said,
             QStringLiteral("It will cost you 12 gold coins, 5 silver pennies, and 94 copper "
                            "pennies per day."));
    QVERIFY(feedInto(tracker, {PROMPT}).inns.empty());

    // elvenrunes: an orkish warden, on one line; without the "enough money" line it comes at
    // the prompt (changed: the second line is left out).
    const TradeReplies warden = feed(
        {"Takhr the orkish warden tells you 'It will cost you 5 gold coins, 15 silver pennies, "
         "and 76 copper pennies per day.'",
         PROMPT});
    QCOMPARE(warden.inns.size(), size_t{1});
    QCOMPARE(warden.inns[0].perDayCopper, std::optional<int64_t>{11576});
    QVERIFY(warden.inns[0].lastsText.isEmpty());

    // elvenrunes: `rent retire`, the request wrapped.
    const TradeReplies retire = feed(
        {"If you retire now, you will not be able to unretire for 7 days. If you really want to "
         "retire, please",
         " repeat that request."});
    QCOMPARE(retire.inns.size(), size_t{1});
    QVERIFY(retire.inns[0].retireAsksRepeat);
    QVERIFY(retire.inns[0].text.contains(QStringLiteral("repeat that request.")));
}

void TestTradeLines::trophiesTest()
{
    // elvenrunes 2013-09-23_Quick-fights_Zinobie.txt:1063-1069 (the first rows).
    const TradeReplies r = feed(
        {"*** TROPHY *** (Number Killed, Knowledge, Mobile)",
         "",
         "   1,  1%,  #Tuunbaq                  |   1,  0%,  #Thelxinoë                |",
         "   1,  1%,  #Oclash                   |   1,  0%,  #Scab                     |",
         "   1,  0%,  #Imminent                 |   2,  2%,  #Morrow                   |",
         PROMPT});
    QCOMPARE(r.trophies.size(), size_t{1});
    const CharTrophies &t = r.trophies.front();
    QCOMPARE(t.rows.size(), size_t{6});
    QCOMPARE(t.rows[0].name, QStringLiteral("Tuunbaq"));
    QVERIFY(t.rows[0].player);
    QCOMPARE(t.rows[0].kills, int64_t{1});
    QCOMPARE(t.rows[0].knowledgePct, int64_t{1});
    QCOMPARE(t.rows[1].name, QString::fromUtf8("Thelxinoë"));
    QCOMPARE(t.rows[5].kills, int64_t{2});
    QCOMPARE(t.rows[5].knowledgePct, int64_t{2});
    QVERIFY(!t.totalKills.has_value());
    QVERIFY(!t.paged);
    QVERIFY(t.complete);

    // elvenrunes 2016-03-17_Daily-log-1-Fighting-for-the-throne_Herumor.txt:2064-2067: mobs,
    // and a centred heading.
    const TradeReplies mobs = feed(
        {"        *** TROPHY *** (Number Killed, Knowledge, Mobile)",
         "",
         "   1,  1%,  #Minde                    |   1,  0%,  a male magpie             |",
         "   1,  0%,  #Paju                     |   2,  3%,  a dales-pony              |",
         PROMPT});
    QCOMPARE(mobs.trophies[0].rows.size(), size_t{4});
    QCOMPARE(mobs.trophies[0].rows[1].name, QStringLiteral("a male magpie"));
    QVERIFY(!mobs.trophies[0].rows[1].player);
    QCOMPARE(mobs.trophies[0].rows[3].knowledgePct, int64_t{3});

    // elvenrunes: the newer heading, three columns, an empty cell, and the totals.
    const TradeReplies newer = feed(
        {"                    *** TROPHY *** (Kills, Knowledge, Name)",
         "",
         "   1,  1%,  #Yussif                       |   1,  0%,  #Sugar                        |   "
         "1,  0%,  #Tintutdar                    |",
         "   2,  1%,  #Sert                     |   1,  1%,  #Rickety                  |",
         "   1,  1%,  #Hiper                    |                                      |",
         "",
         "Total kills: 63 (53 distinct).",
         PROMPT});
    QCOMPARE(newer.trophies.size(), size_t{1});
    QCOMPARE(newer.trophies[0].rows.size(), size_t{6});
    QCOMPARE(newer.trophies[0].rows[2].name, QStringLiteral("Tintutdar"));
    QCOMPARE(newer.trophies[0].rows[5].name, QStringLiteral("Hiper"));
    QCOMPARE(newer.trophies[0].totalKills, std::optional<int64_t>{63});
    QCOMPARE(newer.trophies[0].distinct, std::optional<int64_t>{53});
    QVERIFY(newer.trophies[0].text.endsWith(QStringLiteral("Total kills: 63 (53 distinct).")));

    // "trop #" with a filter.
    const TradeReplies matching = feed({"*** TROPHY *** (Kills, Knowledge, Name)",
                                        "",
                                        "   1,  0%,  #Bofut                    |",
                                        "",
                                        "Total matching kills: 27 (25 distinct)",
                                        PROMPT});
    QCOMPARE(matching.trophies[0].totalKills, std::optional<int64_t>{27});
}

void TestTradeLines::trophiesPagedTest()
{
    // The long list is the pager's main case: continued twice, then quit.
    const TradeReplies quit = feed(
        {"*** TROPHY *** (Number Killed, Knowledge, Mobile)",
         "",
         "   1,  1%,  #Tuunbaq                  |   1,  0%,  #Thelxinoë                |",
         "*** Return: continue, b: back one page, r: redisplay, q: quit (4%) *** ",
         "   1,  1%,  #Oclash                   |   1,  0%,  #Scab                     |",
         PAGER,
         PROMPT});
    QCOMPARE(quit.trophies.size(), size_t{1});
    QVERIFY(quit.trophies[0].paged);
    QVERIFY(!quit.trophies[0].complete);
    QCOMPARE(quit.trophies[0].rows.size(), size_t{4});

    const TradeReplies done = feed(
        {"*** TROPHY *** (Number Killed, Knowledge, Mobile)",
         "",
         "   1,  1%,  #Tuunbaq                  |   1,  0%,  #Thelxinoë                |",
         PAGER,
         "   1,  1%,  #Oclash                   |   1,  0%,  #Scab                     |",
         "",
         "Total kills: 4 (4 distinct).",
         PROMPT});
    QVERIFY(done.trophies[0].paged);
    QVERIFY(done.trophies[0].complete);
}

void TestTradeLines::readersOrderTest()
{
    // MumeXmlParser's order for each chunk: beginChunk, the line readers, the prompt readers,
    // endChunk. What it publishes is recorded in order.
    GameObserver observer;
    Signal2Lifetime lifetime;
    QStringList events;
    observer.sig2_shopList.connect(lifetime, [&events](const ShopList &list) {
        events.append(QStringLiteral("list:%1:%2").arg(list.rows.size()).arg(list.complete));
    });
    observer.sig2_guildPractised.connect(lifetime, [&events](const GuildPractised &) {
        events.append(QStringLiteral("practised"));
    });
    observer.sig2_pager.connect(lifetime, [&events](const PagerLine &pager) {
        events.append(QStringLiteral("pager:%1").arg(pager.percent));
    });
    observer.sig2_realPrompt.connect(lifetime,
                                     [&events]() { events.append(QStringLiteral("prompt")); });
    TradeReaders readers{observer};
    const auto chunk = [&readers](const bool goAhead, const QString &text) {
        const MudChunk c = readers.beginChunk(goAhead, false, text);
        if (c.kind == MudChunkKindEnum::LINE) {
            readers.receiveLine(c.plain);
        }
        if (c.kind == MudChunkKindEnum::PROMPT) {
            readers.receivePrompt();
        }
        readers.endChunk();
        return c.kind;
    };

    QVERIFY(readers.receiveCommand(QStringLiteral("list")));
    QCOMPARE(chunk(false, QStringLiteral("You can buy:")), MudChunkKindEnum::LINE);
    std::ignore = chunk(false, QStringLiteral(""));
    std::ignore = chunk(false,
                        QStringLiteral(" 470. forty-one blood-encrusted helms (flawless, new) up "
                                       "to 4 gold 17 silver."));
    // The pager on its GA: no prompt for anyone, and the reply stays open.
    QCOMPARE(chunk(true, QString::fromUtf8(PAGER)), MudChunkKindEnum::PAGER);
    QCOMPARE(events, list({"pager:50"}));
    QVERIFY(readers.pagerOpen());
    // Should the parser call the prompt readers anyway, a pager still closes nothing.
    readers.receivePrompt();
    readers.endChunk();
    QCOMPARE(events, list({"pager:50"}));
    // Its answer, whoever sent it, is no command: a `list` sent now names no query.
    QVERIFY(!readers.receiveCommand(QStringLiteral("")));
    QVERIFY(!readers.pagerOpen());
    QVERIFY(readers.receiveCommand(QStringLiteral("prac earthquake")));
    std::ignore = chunk(false,
                        QStringLiteral(" 513. fourteen great helms (flawless, new) up to 8 gold "
                                       "15 silver."));
    std::ignore = chunk(false, QStringLiteral(""));
    // A line comes out at once.
    std::ignore = chunk(false,
                        QStringLiteral("You took 1 out of 11 sessions in this skill. Your "
                                       "knowledge is now 25%."));
    QCOMPARE(events, list({"pager:50", "practised"}));
    // The real prompt: the list first, then sig2_realPrompt.
    QCOMPARE(chunk(true, QStringLiteral("!# C Mana:Hot>")), MudChunkKindEnum::PROMPT);
    QCOMPARE(events, list({"pager:50", "practised", "list:2:1", "prompt"}));

    // Without a GA the pager is glued to the next line: published, and taken off the line.
    events.clear();
    std::ignore = chunk(false, QStringLiteral("You can buy:"));
    std::ignore = chunk(false, QStringLiteral(""));
    std::ignore = chunk(false,
                        QStringLiteral(" 470. forty-one blood-encrusted helms (flawless, new) up "
                                       "to 4 gold 17 silver."));
    QCOMPARE(chunk(false,
                   QString::fromUtf8(PAGER_ONE_PAGE)
                       + QStringLiteral(" 513. fourteen great helms (flawless, new) up to 8 gold "
                                        "15 silver.")),
             MudChunkKindEnum::LINE);
    QVERIFY(!readers.pagerOpen());
    std::ignore = chunk(true, QStringLiteral("!# C Mana:Hot>"));
    QCOMPARE(events, list({"pager:73", "list:2:1", "prompt"}));

    // A prompt with nothing open still comes, and a real prompt closes a pager left open.
    events.clear();
    std::ignore = chunk(true, QString::fromUtf8(PAGER));
    QVERIFY(readers.pagerOpen());
    std::ignore = chunk(true, QStringLiteral("!# C Mana:Hot>"));
    QVERIFY(!readers.pagerOpen());
    QCOMPARE(events, list({"pager:50", "prompt"}));
}

void TestTradeLines::messagesTest()
{
    TradeReplies r = feed(g_helms);
    r.append(feed({"An armourer tells you 'Here you have 38 gold and 18 silver coins for that.'",
                   "You sell a fine metal breastplate, a great helm.",
                   PROMPT}));
    TradeLinesTracker tracker;
    tracker.receiveCommand(QStringLiteral("list helm"));
    r.append(feedInto(tracker, g_helms));
    r.append(feedInto(tracker, {PROMPT}));
    QCOMPARE(r.lists.size(), size_t{1});

    const GmcpMessage listMsg = frontend_messages::makeShopList(r.lists.front());
    QCOMPARE(listMsg.getName().getStdStringUtf8(), std::string{"MMapper.Shop.List"});
    const QJsonObject list = json(listMsg);
    QCOMPARE(list["query"].toString(), QStringLiteral("helm"));
    QCOMPARE(list["complete"].toBool(), true);
    QCOMPARE(list["paged"].toBool(), false);
    const QJsonObject row = list["rows"].toArray().at(0).toObject();
    QCOMPARE(row["number"].toInteger(), 470);
    QCOMPARE(row["count"].toInteger(), 41);
    QCOMPARE(row["singular"].toString(), QStringLiteral("blood-encrusted helm"));
    QCOMPARE(row["priceCopper"].toInteger(), 9700);
    QCOMPARE(row["priceText"].toString(), QStringLiteral("4 gold 17 silver"));
    QVERIFY(list["text"].toString().startsWith(QStringLiteral("You can buy:")));

    QCOMPARE(r.deals.size(), size_t{1});
    const QJsonObject deal = json(frontend_messages::makeShopDeal(r.deals.front()));
    QCOMPARE(deal["kind"].toString(), QStringLiteral("sell"));
    QCOMPARE(deal["keeper"].toString(), QStringLiteral("An armourer"));
    QCOMPARE(deal["amountCopper"].toInteger(), 77800);
    QCOMPARE(deal["items"].toArray().size(), 2);

    GuildPractised refused;
    refused.refused = QStringLiteral("You have to stand in order to practice anything.");
    refused.text = refused.refused;
    const GmcpMessage practisedMsg = frontend_messages::makeGuildPractised(refused);
    QCOMPARE(practisedMsg.getType(), GmcpMessageTypeEnum::MMAPPER_GUILD_PRACTISED);
    const QJsonObject practised = json(practisedMsg);
    QVERIFY(practised.contains(QStringLiteral("refused")));
    QVERIFY(!practised.contains(QStringLiteral("used")));

    CharSkills skills;
    skills.sessionsLeft = 3;
    CharSkillRow skill;
    skill.name = QStringLiteral("Swim");
    skill.knowledge = QStringLiteral("Good");
    skill.trained = false;
    skill.skillClass = QStringLiteral("None");
    skills.rows.push_back(skill);
    const QJsonObject skillsJson = json(frontend_messages::makeCharSkills(skills));
    QCOMPARE(skillsJson["sessionsLeft"].toInteger(), 3);
    const QJsonObject skillRow = skillsJson["rows"].toArray().at(0).toObject();
    QCOMPARE(skillRow["class"].toString(), QStringLiteral("None"));
    QCOMPARE(skillRow["trained"].toBool(), false);
    QVERIFY(!skillRow.contains(QStringLiteral("mana")));

    GuildTeacher teacher;
    teacher.teacher = QStringLiteral("Erestor");
    teacher.kind = QStringLiteral("spells");
    teacher.rows.push_back(GuildRow{QStringLiteral("Portal"),
                                    0,
                                    22,
                                    0,
                                    QStringLiteral("Very hard"),
                                    QStringLiteral("Takes a long time to learn")});
    const QJsonObject teacherJson = json(frontend_messages::makeGuildTeacher(teacher));
    QCOMPARE(teacherJson["rows"].toArray().at(0).toObject()["most"].toInteger(), 22);
    QVERIFY(!teacherJson.contains(QStringLiteral("sessionsLeft")));

    InnOffer offer;
    offer.keeper = QStringLiteral("The shady innkeeper");
    offer.perDayCopper = 24594;
    const QJsonObject inn = json(frontend_messages::makeInnOffer(offer));
    QCOMPARE(inn["perDayCopper"].toInteger(), 24594);
    QCOMPARE(inn["retireAsksRepeat"].toBool(), false);
    QCOMPARE(frontend_messages::makeInnOffer(offer).getName().getStdStringUtf8(),
             std::string{"MMapper.Inn.Offer"});

    CharTrophies trophies;
    trophies.rows.push_back(TrophyRow{QStringLiteral("Tuunbaq"), 1, 1, true});
    trophies.paged = true;
    trophies.complete = false;
    const QJsonObject trop = json(frontend_messages::makeCharTrophies(trophies));
    QCOMPARE(trop["rows"].toArray().at(0).toObject()["player"].toBool(), true);
    QCOMPARE(trop["complete"].toBool(), false);
    QCOMPARE(frontend_messages::makeCharTrophies(trophies).getName().getStdStringUtf8(),
             std::string{"MMapper.Char.Trophies"});

    const GmcpMessage view = frontend_messages::makeViewText(
        ViewText{QStringLiteral("Help"), QStringLiteral("Text")});
    QCOMPARE(view.getName().getStdStringUtf8(), std::string{"MMapper.View.Text"});
    QCOMPARE(json(view)["title"].toString(), QStringLiteral("Help"));

    // The names of package B's messages resolve too.
    QCOMPARE(GmcpMessage{GmcpMessageName{"MMapper.Trade.Request"}}.getType(),
             GmcpMessageTypeEnum::MMAPPER_TRADE_REQUEST);
    QCOMPARE(GmcpMessage{GmcpMessageName{"mmapper.trade.cancel"}}.getType(),
             GmcpMessageTypeEnum::MMAPPER_TRADE_CANCEL);
    QCOMPARE(GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TRADE_OPERATION}.getName().getStdStringUtf8(),
             std::string{"MMapper.Trade.Operation"});
}

void TestTradeLines::resetTest()
{
    TradeLinesTracker tracker;
    tracker.receiveCommand(QStringLiteral("list sword"));
    std::ignore = feedInto(tracker,
                           {"You can buy:",
                            "",
                            " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold "
                            "17 silver."});
    tracker.reset();
    const TradeReplies r = feedInto(tracker, {PROMPT});
    QVERIFY(r.empty());
    // The list's query went with it.
    const TradeReplies after = feedInto(tracker, {"There are no such things for sale.", PROMPT});
    QVERIFY(after.lists[0].query.isEmpty());
}

QTEST_MAIN(TestTradeLines)
