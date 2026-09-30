// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestAccountLines.h"

#include "../src/parser/AccountLines.h"

#include <vector>

#include <QtTest/QtTest>

// Every line here is copied from the powwow logs (colour taken off), except the hosts at the
// end of `list`'s rows, which are replaced by "host.example"; the file each comes from is
// named above it.

namespace {

/// Marks where MUME's prompt came (with a GO-AHEAD) in a transcript fed to feed().
const char *const PROMPT = "\x01prompt";

NODISCARD AccountReplies feed(const std::vector<const char *> &lines)
{
    AccountLinesTracker tracker;
    AccountReplies all;
    for (const char *const line : lines) {
        if (QByteArray{line} == QByteArray{PROMPT}) {
            all.append(tracker.receivePrompt());
        } else {
            all.append(tracker.receiveLine(QString::fromUtf8(line)));
        }
    }
    return all;
}

// powwow/logs/moria.gjurza.mov:10-27, the menu after the pass phrase, up to its prompt.
const std::vector<const char *> MENU_2006 = {
    "Account menu",
    "",
    "  Add <name>    - Add an existing character <name> to your account.",
    "  Create <name> - Create new character with name <name>",
    "  Play <name>   - Log the character <name> into MUME",
    "  Time          - Display game time",
    "",
    "  List [<sort>] - Lists all your characters",
    "                  The optional <sort> parameter can be one of",
    "                    side, race, level, alphabetic, custom",
    "  Move <name> [up|down] [-]n",
    "                - Move a character around in the custom list",
    "  Pass          - Change account pass phrase",
    "  Help          - Displays account help",
    "  Menu          - Displays this menu",
    "  Bye           - Leave the account menu (logs you out)",
    "",
    "Account> ",
};

} // namespace

void TestAccountLines::menuTest()
{
    const AccountReplies out = feed(MENU_2006);
    QCOMPARE(out.menus.size(), size_t{1});
    QVERIFY(out.lists.empty());
    const AccountMenu &menu = out.menus.front();
    QCOMPARE(menu.commands.size(), size_t{10});

    QStringList names;
    for (const AccountMenuCommand &command : menu.commands) {
        names.append(command.name);
    }
    QCOMPARE(names,
             (QStringList{"add", "create", "play", "time", "list", "move", "pass", "help", "menu",
                          "bye"}));

    const AccountMenuCommand &play = menu.commands.at(2);
    QCOMPARE(play.usage, QStringLiteral("Play <name>"));
    QCOMPARE(play.help, QStringLiteral("Log the character <name> into MUME"));

    const AccountMenuCommand &list = menu.commands.at(4);
    QCOMPARE(list.usage, QStringLiteral("List [<sort>]"));
    QCOMPARE(list.help,
             QStringLiteral("Lists all your characters The optional <sort> parameter can be one "
                            "of side, race, level, alphabetic, custom"));

    // The usage filled the line; the help is on the next.
    const AccountMenuCommand &move = menu.commands.at(5);
    QCOMPARE(move.usage, QStringLiteral("Move <name> [up|down] [-]n"));
    QCOMPARE(move.help, QStringLiteral("Move a character around in the custom list"));

    QCOMPARE(menu.sorts, (QStringList{"side", "race", "level", "alphabetic", "custom"}));

    // The prompt with a GO-AHEAD closes it as well; the prompt glued to the next line too.
    const std::vector<const char *> noPromptLine(MENU_2006.begin(), MENU_2006.end() - 1);
    AccountLinesTracker tracker;
    for (const char *const line : noPromptLine) {
        QVERIFY(tracker.receiveLine(QString::fromUtf8(line)).empty());
    }
    QCOMPARE(tracker.receivePrompt().menus.size(), size_t{1});
    for (const char *const line : noPromptLine) {
        QVERIFY(tracker.receiveLine(QString::fromUtf8(line)).empty());
    }
    // powwow/logs/archives/log-2006.01.08-19.15.21.txt:4594
    const AccountReplies glued = tracker.receiveLine(
        QStringLiteral("Account> You must wait  8 mins before you can log in this character!"));
    QCOMPARE(glued.menus.size(), size_t{1});
    QCOMPARE(glued.replies.size(), size_t{1});
    QCOMPARE(glued.replies.front().seconds, std::optional<int64_t>{480});
}

void TestAccountLines::menuMergeVariantTest()
{
    // powwow/logs/old/bns/feagwa~1.txt:91-108: Merge, and Help without Menu.
    const AccountReplies out = feed({
        "Account menu",
        "",
        "  Add <name>    - Add an existing character <name> to your account.",
        "  Create <name> - Create new character with name <name>",
        "  Play <name>   - Log the character <name> into MUME",
        "  Time          - Display game time",
        "",
        "  List [<sort>] - Lists all your characters",
        "                  The optional <sort> parameter can be one of",
        "                    side, race, level, alphabetic, custom",
        "  Move <name> [up|down] [-]n",
        "                - Move a character around in the custom list",
        "  Pass          - Change account pass phrase",
        "  Merge <acc>   - Move all characters from <acc> into this account, and",
        "                  delete the account <acc>.",
        "  Help          - Displays this menu",
        "  Bye           - Leave the account menu (logs you out)",
        "",
        PROMPT,
    });
    QCOMPARE(out.menus.size(), size_t{1});
    const AccountMenu &menu = out.menus.front();
    QCOMPARE(menu.commands.size(), size_t{10});
    const AccountMenuCommand &merge = menu.commands.at(7);
    QCOMPARE(merge.name, QStringLiteral("merge"));
    QCOMPARE(merge.usage, QStringLiteral("Merge <acc>"));
    QCOMPARE(merge.help,
             QStringLiteral("Move all characters from <acc> into this account, and delete the "
                            "account <acc>."));
}

void TestAccountLines::menuStrippedTagsTest()
{
    // powwow/logs/archives/log-2006.04.19-13.48.14.mov:35: a client that took "<name>" for an
    // XML tag. The command word is still the first.
    const AccountReplies out = feed({
        "Account menu",
        "",
        "  Add     - Add an existing character  to your account.",
        "  Create  - Create new character with name ",
        "  Play    - Log the character  into MUME",
        "  List [] - Lists all your characters",
        "Account> ",
    });
    QCOMPARE(out.menus.size(), size_t{1});
    const AccountMenu &menu = out.menus.front();
    QCOMPARE(menu.commands.size(), size_t{4});
    QCOMPARE(menu.commands.at(2).name, QStringLiteral("play"));
    QCOMPARE(menu.commands.at(2).usage, QStringLiteral("Play"));
    QCOMPARE(menu.commands.at(3).usage, QStringLiteral("List []"));
    QVERIFY(menu.sorts.isEmpty());
}

void TestAccountLines::listNarrowTest()
{
    // powwow/logs/moria.gjurza.mov:30-55 (some rows left out): the 2006 layout, the name
    // column 13 wide, a host wrapped onto a line of its own (moria.gjurza.mov:49-50).
    const AccountReplies out = feed({
        "li lev",
        "Characters in account \"dmitry\"",
        "Name         Rce Lvl   Logon Area     Rent    Delete Host",
        "Porien            Mc  8 days Valinor    free   never host.example",
        "Azazello     orc 100  7 mths DolGldr    free retired host.example",
        "Woland        bn 100 39 days Ettenmr    free retired host.example",
        "Gjurza       orc W40  8 days OrcCave  9 mths   never host.example",
        "Abramovich   zau  34  8 days Misties 11 yrs    never host.example",
        "Kolbasjenish  bn  34 72 days Ettenmr    free retired host.example",
        "Bimba        tro W31 13 mths Warrens    free retired host.exa-",
        "mple",
        "Finadriel    elf T30 13 mths Rivendl    free retired host.example",
        "",
        "Account> ",
    });
    QVERIFY(out.menus.empty());
    QCOMPARE(out.lists.size(), size_t{1});
    const AccountChars &list = out.lists.front();
    QCOMPARE(list.account, QStringLiteral("dmitry"));
    QCOMPARE(list.chars.size(), size_t{8});

    const AccountChar &porien = list.chars.at(0);
    QCOMPARE(porien.name, QStringLiteral("Porien"));
    QVERIFY(porien.race.isEmpty());
    QCOMPARE(porien.lvl, QStringLiteral("Mc"));
    QVERIFY(!porien.level.has_value());
    QVERIFY(porien.cls.isEmpty());

    const AccountChar &azazello = list.chars.at(1);
    QCOMPARE(azazello.race, QStringLiteral("orc"));
    QCOMPARE(azazello.level, std::optional<int64_t>{100});
    QVERIFY(azazello.cls.isEmpty());
    QCOMPARE(azazello.logon, QStringLiteral("7 mths"));
    QCOMPARE(azazello.area, QStringLiteral("DolGldr"));
    QCOMPARE(azazello.rent, QStringLiteral("free"));
    QCOMPARE(azazello.deletion, QStringLiteral("retired"));

    const AccountChar &gjurza = list.chars.at(3);
    QCOMPARE(gjurza.cls, QStringLiteral("W"));
    QCOMPARE(gjurza.level, std::optional<int64_t>{40});
    QCOMPARE(gjurza.rent, QStringLiteral("9 mths"));
    QCOMPARE(gjurza.deletion, QStringLiteral("never"));
    QVERIFY(!gjurza.playing);

    QCOMPARE(list.chars.at(4).rent, QStringLiteral("11 yrs"));
    QCOMPARE(list.chars.at(5).name, QStringLiteral("Kolbasjenish"));
    QCOMPARE(list.chars.at(5).race, QStringLiteral("bn"));
    QCOMPARE(list.chars.at(6).name, QStringLiteral("Bimba"));
    QCOMPARE(list.chars.at(7).name, QStringLiteral("Finadriel"));
    QCOMPARE(list.chars.at(7).cls, QStringLiteral("T"));
}

void TestAccountLines::listWideTest()
{
    // powwow/logs/archives/log-2005.09.15-18.43.38.mov:41, .../log-2005.12.06-00.26.10.mov:46,
    // .../log-2006.05.13-22.20.33.mov:48, .../log-2005.10.08-15.55.31.mov:277 and
    // azazello.mov:33, under the 2005 header (name column 14 wide), closed by the prompt.
    const AccountReplies out = feed({
        "Name          Rce Lvl   Logon Area     Rent    Delete Host",
        "Porien              M 20 hrs  Valinor    free   never host.example",
        "Rumata        dwa W58 19 days Rivendl 73 days 11 mths host.example",
        "Vasilisa      elf M40  3 mths Lórien     free retired host.example",
        "Illidan       elf M27 40 days GH      forever 10 mths host.example",
        "Woland         bn  89 37 hrs  Misties 34 yrs  12 mths host.example",
        PROMPT,
    });
    QCOMPARE(out.lists.size(), size_t{1});
    const AccountChars &list = out.lists.front();
    QVERIFY(list.account.isEmpty());
    QCOMPARE(list.chars.size(), size_t{5});
    QCOMPARE(list.chars.at(0).lvl, QStringLiteral("M"));
    QCOMPARE(list.chars.at(0).logon, QStringLiteral("20 hrs"));
    QCOMPARE(list.chars.at(1).rent, QStringLiteral("73 days"));
    QCOMPARE(list.chars.at(1).deletion, QStringLiteral("11 mths"));
    QCOMPARE(list.chars.at(2).area, QStringLiteral("Lórien"));
    QCOMPARE(list.chars.at(2).cls, QStringLiteral("M"));
    QCOMPARE(list.chars.at(3).area, QStringLiteral("GH"));
    QCOMPARE(list.chars.at(3).rent, QStringLiteral("forever"));
    QCOMPARE(list.chars.at(4).race, QStringLiteral("bn"));
    QCOMPARE(list.chars.at(4).level, std::optional<int64_t>{89});
    QCOMPARE(list.chars.at(4).deletion, QStringLiteral("12 mths"));
}

void TestAccountLines::listInGameTest()
{
    // powwow/logs/archives/log-2005.09.05-19.23.16.mov:25653-25670: `account lev` typed in
    // the game lists the characters too, the one playing without a logon or an area.
    const AccountReplies out = feed({
        "Characters in account \"dmitry\"",
        "Name          Rce Lvl   Logon Area     Rent    Delete Host",
        "Porien              M  4 hrs  Valinor    free   never host.example",
        "Woland         bn  87 playing         unknown 12 mths host.example",
        "Ennor         dwa  55  9 days Valinor unknown 11 mths host.example",
        "Abramovich    zau W25 64 days Misties unknown 55 days host.example",
        "",
    });
    QCOMPARE(out.lists.size(), size_t{1});
    const AccountChar &woland = out.lists.front().chars.at(1);
    QVERIFY(woland.playing);
    QVERIFY(woland.logon.isEmpty());
    QVERIFY(woland.area.isEmpty());
    QCOMPARE(woland.rent, QStringLiteral("unknown"));
    QCOMPARE(woland.deletion, QStringLiteral("12 mths"));
    QCOMPARE(out.lists.front().chars.at(3).deletion, QStringLiteral("55 days"));
}

void TestAccountLines::rowTest()
{
    // The header's "Rce" says where the columns are.
    const QString wide = QStringLiteral(
        "Gjurza        orc W40  8 days OrcCave  9 mths   never host.example");
    QVERIFY(parseAccountCharRow(wide, 14).has_value());
    QVERIFY(!parseAccountCharRow(wide, 13).has_value());
    // No host at all.
    QVERIFY(parseAccountCharRow(QStringLiteral(
                                    "Gjurza        orc W40  8 days OrcCave  9 mths   never"),
                                14)
                .has_value());
    // The end of a wrapped host, the header, and a line of the game are not rows.
    QVERIFY(!parseAccountCharRow(QStringLiteral("harburg.de"), 14).has_value());
    QVERIFY(!parseAccountCharRow(
                 QStringLiteral("Name          Rce Lvl   Logon Area     Rent    Delete Host"), 14)
                 .has_value());
    QVERIFY(!parseAccountCharRow(
                 QStringLiteral("A large orc is standing here, guarding the door to the north."),
                 14)
                 .has_value());
}

void TestAccountLines::repliesTest()
{
    // powwow/logs/lbfort.gjurza.mov:29, :41; trolls_roots.mov:132855;
    // archives/log-2005.12.14-01.29.22.mov:28122; 2vs2_kolbas.mov:131 (Unknown ... 'look').
    const auto sixMins = parseAccountReplyLine(
        QStringLiteral("You must wait  6 mins before you can log in this character!"));
    QVERIFY(sixMins.has_value());
    QCOMPARE(sixMins->kind, AccountReplyKindEnum::WAIT);
    QCOMPARE(sixMins->seconds, std::optional<int64_t>{360});
    QCOMPARE(parseAccountReplyLine(
                 QStringLiteral("You must wait  1 min  before you can log in this character!"))
                 ->seconds,
             std::optional<int64_t>{60});
    QCOMPARE(parseAccountReplyLine(
                 QStringLiteral("You must wait  1 sec  before you can log in this character!"))
                 ->seconds,
             std::optional<int64_t>{1});
    QCOMPARE(parseAccountReplyLine(
                 QStringLiteral("You must wait 23 mins before you can log in this character!"))
                 ->seconds,
             std::optional<int64_t>{1380});

    const auto unknown = parseAccountReplyLine(QStringLiteral("Unknown account command 'look'"));
    QVERIFY(unknown.has_value());
    QCOMPARE(unknown->kind, AccountReplyKindEnum::UNKNOWN_COMMAND);
    QCOMPARE(unknown->command, QStringLiteral("look"));
    QCOMPARE(QString::fromUtf8(accountReplyKindName(unknown->kind)), QStringLiteral("unknown"));
}

void TestAccountLines::notAccountTest()
{
    // Lines of the game, and a menu that never came to its prompt, give nothing.
    const AccountReplies out = feed({
        "  Add <name>    - Add an existing character <name> to your account.",
        "You must wait for the right moment.",
        "Characters in account",
        "",
        PROMPT,
    });
    QVERIFY(out.empty());

    // A title with no header after it is given up.
    const AccountReplies noHeader = feed({
        "Characters in account \"dmitry\"",
        "One",
        "Two",
        "Three",
        "Four",
        "Name at the end of a line",
        "",
        PROMPT,
    });
    QVERIFY(noHeader.empty());
}

QTEST_MAIN(TestAccountLines)
