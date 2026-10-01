// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestFollowers.h"

#include "../src/parser/CharFollowers.h"

#include <optional>

#include <QtTest/QtTest>

// The lines are MUME's own, from the powwow logs under /home/aza/data/powwow; each row names
// where it was found, with "logs/archives/" left off.

namespace {

using L = FollowerLineEnum;
using K = FollowerKindEnum;
using S = FollowerStateEnum;
using R = FollowerReplyEnum;

struct NODISCARD LineCase final
{
    const char *line;
    L kind;
    const char *name;
    const char *label;
    const char *leader;
    const char *source;
};

const LineCase g_lines[] = {
    // The bond made.
    {"A mother eagle starts following you.", L::FOLLOWS, "a mother eagle", "", "you",
     "log-2005.11.07-15.57.10.txt:37021"},
    {"An orc prisoner starts following you.", L::FOLLOWS, "an orc prisoner", "", "you",
     "log-2006.02.03-00.39.44.txt:47606"},
    {"A starving warg starts following you.", L::FOLLOWS, "a starving warg", "", "you",
     "log-2006.02.05-19.56.25.txt:169079"},
    {"A trained horse (my) starts following you.", L::FOLLOWS, "a trained horse", "my", "you",
     "log-2005.11.07-15.57.10.txt:3219"},
    {"A pony (my) starts following you.", L::FOLLOWS, "a pony", "my", "you",
     "log-2005.08.31-14.00.43.txt:1310"},
    {"Stolb (S) starts following you.", L::FOLLOWS, "Stolb", "S", "you",
     "71 lines; a player"},
    {"Zmej starts following you.", L::FOLLOWS, "Zmej", "", "you",
     "log-2005.09.07-02.50.20.txt:733"},
    {"Something starts following you.", L::FOLLOWS, "Something", "", "you",
     "log-2005.11.07-15.57.10.txt:3449"},
    {"Grayelf now follows you.", L::FOLLOWS, "Grayelf", "", "you",
     "log-2005.09.03-02.28.34.txt:1903"},
    // Somebody else's: read, and told apart by the leader.
    {"An enslaved shadow now follows Farseer (F).", L::FOLLOWS, "an enslaved shadow", "",
     "Farseer", "log-2006.02.09-19.48.52.txt:131913"},
    {"A hungry warg now follows Stolb.", L::FOLLOWS, "a hungry warg", "", "Stolb",
     "log-2005.08.31-14.00.43.txt:12898"},
    // The bond ended.
    {"A wild dog stops following you.", L::STOPS, "a wild dog", "", "you",
     "log-2005.11.07-15.57.10.txt:102024"},
    {"A trained horse (my) stops following you.", L::STOPS, "a trained horse", "my", "you",
     "log-2005.11.07-15.57.10.txt:3259"},
    {"An enslaved shadow (one) stops following Farseer (F).", L::STOPS, "an enslaved shadow",
     "one", "Farseer", "log-2006.02.06-18.58.18.txt:90615"},
    {"A wild dog hates your guts!", L::HATES, "a wild dog", "", "",
     "log-2005.11.07-15.57.10.txt:102023"},
    {"A large hawk hates your guts!", L::HATES, "a large hawk", "", "",
     "log-2006.02.03-00.39.44.txt:20266"},
    // Left behind: him, her, it.
    {"ACK! A pack horse didn't follow you, you lost him.", L::LOST, "a pack horse", "", "",
     "log-2005.08.31-14.00.43.txt:3223"},
    {"ACK! A mother eagle didn't follow you, you lost her.", L::LOST, "a mother eagle", "", "",
     "log-2005.11.08-15.42.24.txt:129227"},
    {"ACK! A raging bear didn't follow you, you lost it.", L::LOST, "a raging bear", "", "",
     "log-2006.01.04-22.01.05.txt:117999"},
    {"ACK! Harle the Hobbit didn't follow you, you lost him.", L::LOST, "Harle the Hobbit", "",
     "", "22 lines"},
    // An order refused.
    {"You failed to control a mother eagle (one).", L::FAILED, "a mother eagle", "one", "",
     "log-2005.11.07-15.57.10.txt:38029"},
    {"You failed to control an orc prisoner (one).", L::FAILED, "an orc prisoner", "one", "",
     "log-2006.02.05-19.56.25.txt:170000"},
    {"You failed to control Harle the Hobbit.", L::FAILED, "Harle the Hobbit", "", "",
     "log-2006.01.04-22.01.05.txt:11825"},
    // The other answers to `order`.
    {"You have no loyal subjects here.", L::NONE_HERE, "", "", "",
     "log-2005.11.07-05.28.32.txt:16600"},
    {"Order who to do what?", L::SYNTAX, "", "", "", "log-2005.11.03-21.45.23.txt:2566"},
    {"In your dreams, or what?", L::ASLEEP, "", "", "", "log-2006.01.04-22.01.05.txt:99813"},
    {"Ok.", L::OK, "", "", "", "log-2005.11.07-15.57.10.txt:38030"},
    // The spinner of the charm being cast, on the front of its "Ok.".
    {"/-\\|/-Ok.", L::OK, "", "", "", "log-2006.02.05-19.56.25.txt:169078"},
    {"A mother eagle (one) is now a group member.", L::GROUPED, "a mother eagle", "one", "",
     "log-2005.11.07-15.57.10.txt:37082"},
    // Deaths.
    {"A mother eagle (one) is dead! R.I.P.", L::DIED, "a mother eagle", "one", "",
     "log-2005.11.07-15.57.10.txt:43808"},
    {"A pack horse (my) is dead! R.I.P.", L::DIED, "a pack horse", "my", "",
     "log-2005.09.07-02.50.20.txt:2503"},
    {"An enslaved shadow (one) disappears into nothing.", L::DIED, "an enslaved shadow", "one",
     "", "log-2006.02.09-19.48.52.txt:132543"},
    {"The sage has drawn his last breath! R.I.P.", L::DIED, "the sage", "", "",
     "CombatLines g_death"},
    // In the room, or out of it.
    {"A mother eagle (one) has arrived from the north.", L::ARRIVED, "a mother eagle", "one",
     "", "log-2005.11.07-15.57.10.txt:37049"},
    {"A mother eagle has arrived from above.", L::ARRIVED, "a mother eagle", "", "",
     "log-2005.11.07-15.57.10.txt:37029"},
    {"A pony (my) has suddenly arrived.", L::ARRIVED, "a pony", "my", "",
     "log-2005.08.31-14.00.43.txt:1980"},
    {"An enslaved shadow (one) leaves north.", L::WENT, "an enslaved shadow", "one", "",
     "log-2006.02.06-18.58.18.txt:88997"},
    {"A mother eagle (one) is standing here.", L::SEEN, "a mother eagle", "one", "",
     "log-2005.11.07-15.57.10.txt:37167"},
    {"An old man (one) is resting here.", L::SEEN, "an old man", "one", "",
     "log-2005.11.08-15.42.24.txt:180315"},
    {"A mother eagle (one) is sleeping here.", L::SEEN, "a mother eagle", "one", "",
     "log-2005.11.08-15.42.24.txt:129390"},
    {"A raging bear (one) is here, fighting Nagash the Dark.", L::SEEN, "a raging bear", "one",
     "", "log-2006.01.04-22.01.05.txt:115891"},
    {"An enslaved shadow (one), wielding a two-handed axe, is standing here.", L::SEEN,
     "an enslaved shadow", "one", "", "log-2006.02.06-18.58.18.txt:105450"},
    // What is ridden is a mount.
    {"You pick up a trained horse (my)'s reins, and start riding him.", L::RIDING,
     "a trained horse", "my", "", "log-2005.11.07-15.57.10.txt:3260"},
    {"You pick up a hungry warg (my)'s reins, and start riding it.", L::RIDING, "a hungry warg",
     "my", "", "480 lines"},
    {"You pick up Something's reins, and start riding him.", L::RIDING, "Something", "", "",
     "log-2005.11.07-15.57.10.txt:3609"},
    {"You stop riding a trained horse (my).", L::RODE, "a trained horse", "my", "",
     "log-2005.11.07-15.57.10.txt:3218"},
    {"You stop riding Gwaihir the Windlord.", L::RODE, "Gwaihir the Windlord", "", "",
     "log-2005.09.15-21.01.17.txt:100896"},
    {"You are dead! Sorry...", L::YOU_DIED, "", "", "", "CombatLines g_youDead"},
    // Whom the player follows (243 "You now follow", 186 "You stop following", 87 "You will
    // not follow anyone else now." in logs/archives).
    {"You now follow Grayelf.", L::YOU_FOLLOW, "Grayelf", "", "",
     "log-2005.09.02-23.20.17.txt:22833"},
    {"You now follow a black sorcerer.", L::YOU_FOLLOW, "a black sorcerer", "", "",
     "log-2005.11.03-21.45.23.txt:5134"},
    {"You now follow Kazadoe (K).", L::YOU_FOLLOW, "Kazadoe", "K", "",
     "log-2005.12.12-16.09.18.txt:2077"},
    {"You now follow Ukzlug (Uk).", L::YOU_FOLLOW, "Ukzlug", "Uk", "",
     "log-2005.11.02-01.27.58.txt:55778"},
    {"You stop following Zmej.", L::YOU_STOP, "Zmej", "", "",
     "log-2005.09.21-01.39.48.txt:29976"},
    {"You stop following Pimba.", L::YOU_STOP, "Pimba", "", "",
     "log-2005.09.05-19.23.16.txt:66234"},
    {"You will not follow anyone else now.", L::YOU_FOLLOW_NOBODY, "", "", "",
     "log-2005.11.03-21.45.23.txt:5425"},
    // The move after the leader: 37091 lines.
    {"You follow Orhzul.", L::YOU_WENT_AFTER, "Orhzul", "", "",
     "log-2005.10.08-15.55.31.txt:32976"},
    {"You follow Grayelf.", L::YOU_WENT_AFTER, "Grayelf", "", "",
     "log-2005.09.02-23.20.17.txt:22835"},
    // Sent away (71 "him.", 3 "her."), refused (2 "him!"), a loop (16).
    {"Zmej doesn't want you to follow him.", L::FOLLOW_DENIED, "Zmej", "", "",
     "log-2005.09.05-19.23.16.txt:42094"},
    {"Pimba doesn't want you to follow her.", L::FOLLOW_DENIED, "Pimba", "", "",
     "log-2005.09.05-19.23.16.txt:67617"},
    {"-\\|Orhzul doesn't want you to follow him.", L::FOLLOW_DENIED, "Orhzul", "", "",
     "log-2005.10.08-15.55.31.txt:47374"},
    {"An old man doesn't want you to follow him!", L::FOLLOW_DENIED, "an old man", "", "",
     "log-2006.02.03-00.39.44.txt:109205"},
    {"Sorry, but following in 'loops' is not allowed.", L::FOLLOW_DENIED, "", "", "",
     "log-2005.09.05-19.23.16.txt:66214"},
    // Protecting: 67 on, 11 off, 18 lists, 2 + 5 nobody, 3 refused.
    {"You will now try to protect Budach (B).", L::PROTECTS, "Budach", "B", "",
     "log-2005.09.15-21.01.17.txt:156"},
    {"You will now try to protect Elerin.", L::PROTECTS, "Elerin", "", "",
     "log-2005.12.19-22.31.26.txt:26643"},
    {"You will no longer try to protect Kazadoe (k).", L::UNPROTECTS, "Kazadoe", "k", "",
     "log-2005.11.17-17.17.33.txt:83438"},
    {"You will no longer try to protect Barzikon.", L::UNPROTECTS, "Barzikon", "", "",
     "log-2006.01.08-19.15.21.txt:28232"},
    {"You will try to protect:", L::PROTECT_LIST, "", "", "",
     "log-2005.12.19-20.51.29.txt:88011"},
    {"You aren't trying to protect anyone.", L::PROTECT_NONE, "", "", "",
     "log-2006.01.08-19.15.21.txt:29266"},
    {"Very well, you concentrate on your own health.", L::PROTECT_NONE, "", "", "",
     "log-2005.11.17-17.17.33.txt:44529"},
    {"You can only protect those in your group.", L::PROTECT_DENIED, "", "", "",
     "log-2005.12.24-02.05.16.txt:130436"},
};

/// Lines about following and orders that are nobody's bond with the player.
const char *const g_notLines[] = {
    // Somebody else ordering: log-2005.10.06-18.57.04.txt:88248,
    // log-2006.02.06-18.58.18.txt:91239.
    "Zmej issues the order 'bash'.",
    "Farseer (F) gives an enslaved shadow (one) an order.",
    // A room's description, not a move after a leader: log-2005.09.05-19.23.16.txt:72197,
    // log-2005.12.21-19.59.01.txt:26485.
    "You follow the trail up to a hill.",
    "You follow a gentle slope downward to some shallow water.",
    // Other answers to `follow` and `protect`, which change nothing and name no bond:
    // log-2005.09.05-19.23.16.txt (21 after a typed follow), log-2005.10.27-01.19.35.txt:9216,
    // log-2006.01.04-22.01.05.txt:52758, log-2005.10.08-15.55.31.txt:4133.
    "I see no person by that name here!",
    "You want to be protected from WHAT? Look at yourself...",
    "But he is not following you!",
    "A pack horse (my) is already following you!",
    // Others' following said another way (log-2006.05.02-16.20.44.txt:3965), and a client's
    // own line (log-2005.11.03-21.45.23.txt:5133).
    "Olks starts to follow Mehine.",
    "-- [Pandora: Following leader : You",
    // log-2006.02.03-00.39.44.txt:20260, log-2005.09.03-02.28.34.txt:1906.
    "Vip has been kicked out of the group!",
    "Grayelf joins your group.",
    // pow/mapper.pow:47,54: the mount's own refusals, which are moves refused.
    "Your mount refuses to follow your orders!",
    "You don't control your mount!",
    // log-2005.11.07-15.57.10.txt:38033, :37022.
    "A mother eagle (one) joins your fight.",
    "Donk has arrived from the east riding a hungry warg.",
    "Ok",
    "Stolb says 'Ok.'",
    "",
};

NODISCARD QString text(const std::string_view sv)
{
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

/// A tracker with a clock the test moves, fed as MumeXmlParser feeds it.
struct NODISCARD Game final
{
    CharFollowersTracker tracker;
    int64_t now = 1000;
    /// The last message, when the last line or prompt caused one.
    std::optional<CharFollowers> last;

    void send(const char *const command) { tracker.receiveCommand(QString::fromUtf8(command), now); }
    /// True when the line changed the followers.
    bool line(const char *const text)
    {
        last = tracker.receiveLine(QString::fromUtf8(text), now);
        return last.has_value();
    }
    /// True when the prompt ended an order's answer.
    bool prompt()
    {
        last = tracker.receivePrompt(now);
        return last.has_value();
    }
    NODISCARD const CharFollower &at(const size_t index) const
    {
        return tracker.followers().at(index);
    }
    NODISCARD size_t count() const { return tracker.followers().size(); }
};

#define COMPARE_STATE(actual, expected) QCOMPARE(text(to_string_view(actual)), text(to_string_view(expected)))

} // namespace

void TestFollowers::linesTest()
{
    for (const LineCase &c : g_lines) {
        const std::optional<FollowerLine> line = parseFollowerLine(QString::fromUtf8(c.line));
        QVERIFY2(line.has_value(), c.line);
        QVERIFY2(line->kind == c.kind, c.line);
        QVERIFY2(line->name == QString::fromUtf8(c.name), c.line);
        QVERIFY2(line->label == QString::fromUtf8(c.label), c.line);
        QVERIFY2(line->leader == QString::fromUtf8(c.leader), c.line);
        QVERIFY2(c.source[0] != '\0', c.line);
    }
    // The square-bracket labels of the oldest logs (log-2005.09.02-23.20.17.txt:21668).
    const auto old = parseFollowerLine(QStringLiteral("A hungry warg [my] starts following you."));
    QVERIFY(old.has_value());
    QCOMPARE(old->name, QStringLiteral("a hungry warg"));
    QCOMPARE(old->label, QStringLiteral("my"));
}

void TestFollowers::notFollowerLinesTest()
{
    for (const char *const line : g_notLines) {
        QVERIFY2(!parseFollowerLine(QString::fromUtf8(line)).has_value(), line);
    }
}

void TestFollowers::commandsTest()
{
    using T = FollowerCommand::TypeEnum;
    // pow/bn:25, pow/broga:8.
    auto c = parseFollowerCommand(QStringLiteral("order followers assist"));
    QVERIFY(c.has_value());
    QVERIFY(c->type == T::ORDER);
    QCOMPARE(c->who, QStringLiteral("followers"));
    QCOMPARE(c->order, QStringLiteral("assist"));
    // log-2005.11.07-05.28.32.txt:16599.
    c = parseFollowerCommand(QStringLiteral("order followers hit orc"));
    QVERIFY(c.has_value());
    QCOMPARE(c->order, QStringLiteral("hit orc"));
    // log-2006.02.05-19.56.25.txt:169998, log-2006.01.04-22.01.05.txt:11823.
    c = parseFollowerCommand(QStringLiteral("order one sleep"));
    QVERIFY(c.has_value());
    QCOMPARE(c->who, QStringLiteral("one"));
    QCOMPARE(c->order, QStringLiteral("sleep"));
    c = parseFollowerCommand(QStringLiteral("  Order Harle rest \n"));
    QVERIFY(c.has_value());
    QCOMPARE(c->who, QStringLiteral("harle"));
    QCOMPARE(c->order, QStringLiteral("rest"));
    // A bare one (log-2005.11.03-21.45.23.txt:2565), and the abbreviations.
    c = parseFollowerCommand(QStringLiteral("order followers"));
    QVERIFY(c.has_value());
    QCOMPARE(c->who, QStringLiteral("followers"));
    QVERIFY(c->order.isEmpty());
    c = parseFollowerCommand(QStringLiteral("ord fol stand"));
    QVERIFY(c.has_value());
    QCOMPARE(c->who, QStringLiteral("followers"));
    QCOMPARE(c->order, QStringLiteral("stand"));
    c = parseFollowerCommand(QStringLiteral("order"));
    QVERIFY(c.has_value());
    QVERIFY(c->who.isEmpty());

    // log-2005.11.07-15.57.10.txt:37032, and "label 2.trained my" (5 in the logs).
    c = parseFollowerCommand(QStringLiteral("label eagle one"));
    QVERIFY(c.has_value());
    QVERIFY(c->type == T::LABEL);
    QCOMPARE(c->who, QStringLiteral("eagle"));
    QCOMPARE(c->label, QStringLiteral("one"));
    QCOMPARE(c->ordinal, 1);
    c = parseFollowerCommand(QStringLiteral("lab 2.trained my"));
    QVERIFY(c.has_value());
    QCOMPARE(c->who, QStringLiteral("trained"));
    QCOMPARE(c->ordinal, 2);
    QCOMPARE(c->label, QStringLiteral("my"));

    // Not these: the one-word label (pow/binds.pow:3), and words that only begin alike.
    QVERIFY(!parseFollowerCommand(QStringLiteral("label buh")).has_value());
    QVERIFY(!parseFollowerCommand(QStringLiteral("orders")).has_value());
    QVERIFY(!parseFollowerCommand(QStringLiteral("or keybind x")).has_value());
    QVERIFY(!parseFollowerCommand(QStringLiteral("open door")).has_value());
    QVERIFY(!parseFollowerCommand(QStringLiteral("say order followers assist")).has_value());
}

void TestFollowers::namesTest()
{
    // An NPC's name begins with an article; a player's does not, nor an enemy's.
    QVERIFY(isNpcName(QStringLiteral("a mother eagle")));
    QVERIFY(isNpcName(QStringLiteral("an orc prisoner")));
    QVERIFY(isNpcName(QStringLiteral("the sage")));
    QVERIFY(isNpcName(QStringLiteral("a Great Eagle")));
    QVERIFY(!isNpcName(QStringLiteral("Stolb")));
    QVERIFY(!isNpcName(QStringLiteral("*an Orc*")));
    QVERIFY(!isNpcName(QStringLiteral("Harle the Hobbit")));
    QVERIFY(!isNpcName(QStringLiteral("Something")));

    QVERIFY(followerKindOf(QStringLiteral("a mother eagle")) == K::CHARMIE);
    QVERIFY(followerKindOf(QStringLiteral("a starving warg")) == K::CHARMIE);
    QVERIFY(followerKindOf(QStringLiteral("a trained horse")) == K::MOUNT);
    QVERIFY(followerKindOf(QStringLiteral("a pack horse")) == K::MOUNT);
    QVERIFY(followerKindOf(QStringLiteral("a mountain mule")) == K::MOUNT);
    QVERIFY(followerKindOf(QStringLiteral("a hungry warg")) == K::MOUNT);
    QVERIFY(followerKindOf(QStringLiteral("a pony")) == K::MOUNT);
    QVERIFY(followerKindOf(QStringLiteral("a horse of the Rohirrim")) == K::MOUNT);
    QVERIFY(followerKindOf(QStringLiteral("an enslaved shadow")) == K::SUMMONED);
    QVERIFY(followerKindOf(QStringLiteral("Harle the Hobbit")) == K::UNKNOWN);
    QVERIFY(followerKindOf(QStringLiteral("Stolb")) == K::UNKNOWN);

    QCOMPARE(text(to_string_view(K::CHARMIE)), QStringLiteral("charmie"));
    QCOMPARE(text(to_string_view(K::MOUNT)), QStringLiteral("mount"));
    QCOMPARE(text(to_string_view(K::SUMMONED)), QStringLiteral("summoned"));
    QCOMPARE(text(to_string_view(K::UNKNOWN)), QStringLiteral("unknown"));
    QCOMPARE(text(to_string_view(S::FOLLOWING)), QStringLiteral("following"));
    QCOMPARE(text(to_string_view(S::REFUSING)), QStringLiteral("refusing"));
    QCOMPARE(text(to_string_view(S::LOST)), QStringLiteral("lost"));
    QCOMPARE(text(to_string_view(S::LEFT)), QStringLiteral("left"));
    QCOMPARE(text(to_string_view(S::DEAD)), QStringLiteral("dead"));
    QCOMPARE(text(to_string_view(R::OK)), QStringLiteral("ok"));
    QCOMPARE(text(to_string_view(R::FAILED)), QStringLiteral("failed"));
    QCOMPARE(text(to_string_view(R::NONE_HERE)), QStringLiteral("none-here"));
    QCOMPARE(text(to_string_view(R::SYNTAX)), QStringLiteral("syntax"));
    QCOMPARE(text(to_string_view(R::ASLEEP)), QStringLiteral("asleep"));
}

// charm -> follows -> label -> order ok -> refused -> lost -> follows again -> dies, as
// log-2005.11.07-15.57.10.txt has it from line 37019 on.
void TestFollowers::sequenceTest()
{
    Game g;

    // The charm's own "Ok." answers no command of the player's that is kept here.
    g.send("cast 'charm' eagle");
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.line("A mother eagle starts following you."));
    QVERIFY(!g.prompt());
    QCOMPARE(g.count(), size_t{1});
    QCOMPARE(g.at(0).name, QStringLiteral("a mother eagle"));
    QVERIFY(g.at(0).label.isEmpty());
    QVERIFY(g.at(0).kind == K::CHARMIE);
    QVERIFY(g.at(0).here);
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);
    QCOMPARE(g.at(0).since, std::optional<int64_t>{1000});

    // It comes along: nothing new.
    g.now = 1010;
    QVERIFY(!g.line("A mother eagle has arrived from above."));
    QVERIFY(!g.prompt());

    // The label: the command, then its "Ok.".
    g.send("label eagle one");
    QVERIFY(g.line("Ok."));
    QVERIFY(!g.last->reply.has_value());
    QVERIFY(!g.prompt());
    QCOMPARE(g.at(0).label, QStringLiteral("one"));
    QVERIFY(!g.line("A mother eagle (one) has arrived from the north."));
    QVERIFY(!g.line("A mother eagle (one) is now a group member."));

    // An order taken: told at the prompt that ends the answer, with the reply.
    g.now = 1020;
    g.send("order followers assist");
    QVERIFY(!g.line("A mother eagle (one) joins your fight."));
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply.has_value());
    QVERIFY(g.last->reply->result == R::OK);
    QCOMPARE(g.last->reply->order, QStringLiteral("assist"));
    QCOMPARE(g.last->reply->who, QStringLiteral("followers"));
    QVERIFY(g.last->reply->failed.isEmpty());
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("assist"));
    QVERIFY(g.at(0).lastRefused.isEmpty());
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);
    // The next prompt says nothing more.
    QVERIFY(!g.prompt());

    // An order refused: "You failed to control ..." before the "Ok." (:42903-42905).
    g.now = 1030;
    g.send("order followers ride donk");
    QVERIFY(!g.line("You failed to control a mother eagle (one)."));
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::FAILED);
    QCOMPARE(g.last->reply->order, QStringLiteral("ride donk"));
    QCOMPARE(g.last->reply->failed, QStringList{QStringLiteral("a mother eagle")});
    COMPARE_STATE(g.at(0).state, S::REFUSING);
    QCOMPARE(g.at(0).lastRefused, QStringLiteral("ride donk"));
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("assist"));
    // Given again, and taken (:42908-42909): no longer refusing.
    g.send("order followers ride donk");
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::OK);
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("ride donk"));
    QCOMPARE(g.at(0).lastRefused, QStringLiteral("ride donk"));

    // Left behind: the ACK line shows no label (log-2005.11.08-15.42.24.txt:129227).
    g.now = 1040;
    QVERIFY(g.line("ACK! A mother eagle didn't follow you, you lost her."));
    COMPARE_STATE(g.at(0).state, S::LOST);
    QVERIFY(!g.at(0).here);
    QCOMPARE(g.at(0).label, QStringLiteral("one"));
    // The same again changes nothing.
    QVERIFY(!g.line("ACK! A mother eagle didn't follow you, you lost her."));

    // Back in its room it is seen, and follows again with no more than "has arrived"
    // (:129234, :129241). The bond is the old one: `since` stays.
    g.now = 1050;
    QVERIFY(g.line("A mother eagle (one) is standing here."));
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);
    QVERIFY(g.at(0).here);
    QVERIFY(!g.line("A mother eagle (one) has arrived from the east."));
    QCOMPARE(g.at(0).since, std::optional<int64_t>{1000});
    // Or straight from "lost" by arriving.
    QVERIFY(g.line("ACK! A mother eagle didn't follow you, you lost her."));
    QVERIFY(g.line("A mother eagle (one) has arrived from the east."));
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);

    // It dies (log-2005.11.07-15.57.10.txt:43808): told once, as dead, and gone after.
    g.now = 1060;
    QVERIFY(g.line("A mother eagle (one) is dead! R.I.P."));
    QCOMPARE(g.last->followers.size(), size_t{1});
    COMPARE_STATE(g.last->followers.at(0).state, S::DEAD);
    QCOMPARE(g.count(), size_t{0});
    QVERIFY(lastingFollowers(*g.last).followers.empty());
    QVERIFY(!g.line("A mother eagle (one) is dead! R.I.P."));
}

void TestFollowers::repliesTest()
{
    Game g;
    QVERIFY(g.line("An orc prisoner starts following you."));
    QVERIFY(g.line("A starving warg starts following you."));
    g.send("label orc one");
    QVERIFY(g.line("Ok."));
    g.send("label warg two");
    QVERIFY(g.line("Ok."));
    QCOMPARE(g.at(0).label, QStringLiteral("one"));
    QCOMPARE(g.at(1).label, QStringLiteral("two"));

    // ok, to all: both took it.
    g.send("order followers kill orc");
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::OK);
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("kill orc"));
    QCOMPARE(g.at(1).lastOrder, QStringLiteral("kill orc"));

    // failed, by one of two: the other took it.
    g.send("order followers assist");
    QVERIFY(!g.line("You failed to control a starving warg (two)."));
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::FAILED);
    QCOMPARE(g.last->reply->failed, QStringList{QStringLiteral("a starving warg")});
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("assist"));
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);
    QCOMPARE(g.at(1).lastOrder, QStringLiteral("kill orc"));
    QCOMPARE(g.at(1).lastRefused, QStringLiteral("assist"));
    COMPARE_STATE(g.at(1).state, S::REFUSING);

    // failed, by both.
    g.send("order followers assist");
    QVERIFY(!g.line("You failed to control an orc prisoner (one)."));
    QVERIFY(!g.line("You failed to control a starving warg (two)."));
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::FAILED);
    QCOMPARE(g.last->reply->failed,
             (QStringList{QStringLiteral("an orc prisoner"), QStringLiteral("a starving warg")}));
    COMPARE_STATE(g.at(0).state, S::REFUSING);

    // To one, by its label: the "Ok." comes first, the refusal after it
    // (log-2006.02.05-19.56.25.txt:169998-170000).
    g.send("order one sleep");
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.line("You failed to control an orc prisoner (one)."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::FAILED);
    QCOMPARE(g.last->reply->who, QStringLiteral("one"));
    QCOMPARE(g.last->reply->order, QStringLiteral("sleep"));
    QCOMPARE(g.at(0).lastRefused, QStringLiteral("sleep"));
    // And taken (:170002-170004): only the one named took it.
    g.send("order one sleep");
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.line("An orc prisoner (one) lies down and falls asleep."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::OK);
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("sleep"));
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);
    QCOMPARE(g.at(1).lastOrder, QStringLiteral("kill orc"));
    COMPARE_STATE(g.at(1).state, S::REFUSING);
    // By a word of its name.
    g.send("order warg stand");
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QCOMPARE(g.at(1).lastOrder, QStringLiteral("stand"));
    COMPARE_STATE(g.at(1).state, S::FOLLOWING);

    // none-here (log-2005.11.07-05.28.32.txt:16599-16600): the reply, and no follower changed.
    g.send("order followers hit orc");
    QVERIFY(!g.line("You have no loyal subjects here."));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::NONE_HERE);
    QCOMPARE(g.last->reply->order, QStringLiteral("hit orc"));
    QVERIFY(g.last->reply->failed.isEmpty());
    QCOMPARE(g.last->followers.size(), size_t{2});
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("sleep"));
    QVERIFY(g.at(0).here);

    // syntax (log-2005.11.03-21.45.23.txt:2565-2566).
    g.send("order followers");
    QVERIFY(!g.line("Order who to do what?"));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::SYNTAX);
    QCOMPARE(g.last->reply->who, QStringLiteral("followers"));
    QVERIFY(g.last->reply->order.isEmpty());

    // asleep (log-2006.01.04-22.01.05.txt:99812-99813).
    g.send("order followers hit burly");
    QVERIFY(!g.line("In your dreams, or what?"));
    QVERIFY(g.prompt());
    QVERIFY(g.last->reply->result == R::ASLEEP);
    QCOMPARE(g.last->reply->order, QStringLiteral("hit burly"));
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("sleep"));
}

void TestFollowers::pairingTest()
{
    Game g;
    QVERIFY(g.line("A raging bear starts following you."));

    // With no order sent, these lines answer nothing: no reply at the prompt.
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.line("In your dreams, or what?"));
    QVERIFY(!g.line("You have no loyal subjects here."));
    QVERIFY(!g.prompt());

    // A refusal with no order of the player's waiting (log-2005.11.07-15.57.10.txt:43512):
    // told at once, without a reply.
    QVERIFY(g.line("You failed to control a raging bear."));
    QVERIFY(!g.last->reply.has_value());
    COMPARE_STATE(g.at(0).state, S::REFUSING);
    QVERIFY(g.at(0).lastRefused.isEmpty());
    QVERIFY(!g.prompt());

    // One that refuses and was never seen to start following is a follower all the same,
    // whatever its name (log-2006.01.04-22.01.05.txt:11823-11825).
    g.send("order harle rest");
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.line("You failed to control Harle the Hobbit."));
    QVERIFY(g.prompt());
    QCOMPARE(g.count(), size_t{2});
    QCOMPARE(g.at(1).name, QStringLiteral("Harle the Hobbit"));
    QVERIFY(g.at(1).kind == K::UNKNOWN);
    QVERIFY(!g.at(1).since.has_value());
    COMPARE_STATE(g.at(1).state, S::REFUSING);
    QCOMPARE(g.last->reply->failed, QStringList{QStringLiteral("Harle the Hobbit")});

    // Two orders sent before either answer: answered in the order sent.
    g.send("order followers stand");
    g.send("order followers assist");
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QCOMPARE(g.last->reply->order, QStringLiteral("stand"));
    QVERIFY(!g.line("You failed to control a raging bear."));
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QCOMPARE(g.last->reply->order, QStringLiteral("assist"));
    QVERIFY(g.last->reply->result == R::FAILED);
    QCOMPARE(g.at(0).lastOrder, QStringLiteral("stand"));
    QCOMPARE(g.at(0).lastRefused, QStringLiteral("assist"));

    // A label's "Ok." is not an order's, and an order's is no label's.
    g.send("label bear one");
    g.send("order followers stand");
    QVERIFY(g.line("Ok."));
    QVERIFY(!g.last->reply.has_value());
    QCOMPARE(g.at(0).label, QStringLiteral("one"));
    QVERIFY(!g.prompt());
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QCOMPARE(g.last->reply->order, QStringLiteral("stand"));

    // Moves sent before an order: their prompts come first, and the order still has its
    // answer.
    g.send("order followers assist");
    for (int i = 0; i < 6; ++i) {
        QVERIFY(!g.prompt());
    }
    QVERIFY(!g.line("Ok."));
    QVERIFY(g.prompt());
    QCOMPARE(g.last->reply->order, QStringLiteral("assist"));

    // An order whose answer never came is given up on, after some prompts and some time: a
    // later "Ok." is not its answer.
    g.send("order followers flee");
    g.now += 60;
    for (int i = 0; i < 4; ++i) {
        QVERIFY(!g.prompt());
    }
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.prompt());
}

void TestFollowers::playersTest()
{
    Game g;
    // A player who starts following is no follower of this kind: no article. It is kept by
    // name in `players`, and the followers stay empty.
    QVERIFY(g.line("Stolb (S) starts following you."));
    QVERIFY(g.last->followers.empty());
    QCOMPARE(g.last->players, QStringList{QStringLiteral("Stolb")});
    QVERIFY(g.line("Zmej starts following you."));
    QVERIFY(g.line("Grayelf now follows you."));
    QVERIFY(!g.line("ACK! Stolb didn't follow you, you lost him."));
    QVERIFY(g.line("Stolb (S) stops following you."));
    QVERIFY(g.line("Zmej stops following you."));
    QVERIFY(g.line("Grayelf stops following you."));
    QVERIFY(g.tracker.players().isEmpty());
    // Nor is what cannot be named.
    QVERIFY(!g.line("Something starts following you."));
    QVERIFY(!g.line("ACK! Someone didn't follow you, you lost him."));
    // Somebody else's follower is not the player's.
    QVERIFY(!g.line("An enslaved shadow now follows Farseer (F)."));
    QVERIFY(!g.line("A hungry warg now follows Stolb."));
    QVERIFY(!g.line("An enslaved shadow (one) stops following Farseer (F)."));
    // Nor are creatures that only come, go, die or get grouped.
    QVERIFY(!g.line("A hungry warg has arrived from above."));
    QVERIFY(!g.line("An enslaved shadow is now a group member."));
    QVERIFY(!g.line("A shadow (buh) disappears into nothing."));
    QVERIFY(!g.line("A wild dog hates your guts!"));
    QCOMPARE(g.count(), size_t{0});

    // The player's own summoned one, by the wording somebody else's has (the player's own
    // was not in the logs).
    QVERIFY(g.line("An enslaved shadow starts following you."));
    QVERIFY(g.at(0).kind == K::SUMMONED);
    // Somebody else's of the same name leaving its leader is not this one leaving.
    QVERIFY(!g.line("An enslaved shadow stops following Farseer (F)."));
    QVERIFY(g.line("An enslaved shadow disappears into nothing."));
    COMPARE_STATE(g.last->followers.at(0).state, S::DEAD);
    QCOMPARE(g.count(), size_t{0});

    // The player hits its own follower (log-2005.11.07-15.57.10.txt:102023-102024): gone at
    // the first of the two lines, and the second finds nothing to change.
    QVERIFY(g.line("A wild dog starts following you."));
    QVERIFY(g.line("A wild dog hates your guts!"));
    COMPARE_STATE(g.last->followers.at(0).state, S::LEFT);
    QVERIFY(!g.line("A wild dog stops following you."));
    QCOMPARE(g.count(), size_t{0});
}

void TestFollowers::mountTest()
{
    Game g;
    // Led after dismounting (log-2005.11.07-15.57.10.txt:3218-3219).
    QVERIFY(!g.line("You stop riding a trained horse (my)."));
    QVERIFY(g.line("A trained horse (my) starts following you."));
    QVERIFY(g.at(0).kind == K::MOUNT);
    QCOMPARE(g.at(0).label, QStringLiteral("my"));
    QCOMPARE(g.at(0).since, std::optional<int64_t>{1000});
    QVERIFY(!g.line("A trained horse (my) has arrived from above."));

    // Left behind and found again.
    QVERIFY(g.line("ACK! A trained horse didn't follow you, you lost him."));
    COMPARE_STATE(g.at(0).state, S::LOST);
    QVERIFY(g.line("A trained horse (my) is standing here."));
    COMPARE_STATE(g.at(0).state, S::FOLLOWING);

    // Mounted (:3259-3260): it stops following, which is all the package says of it; a
    // ridden mount is no follower.
    QVERIFY(g.line("A trained horse (my) stops following you."));
    COMPARE_STATE(g.last->followers.at(0).state, S::LEFT);
    QVERIFY(!g.line("You pick up a trained horse (my)'s reins, and start riding him."));
    QCOMPARE(g.count(), size_t{0});

    // Led again: a new bond.
    g.now = 2000;
    QVERIFY(!g.line("You stop riding a trained horse (my)."));
    QVERIFY(g.line("A trained horse (my) starts following you."));
    QCOMPARE(g.at(0).since, std::optional<int64_t>{2000});

    // Mounted in the dark (:3608-3609): "Something" names nobody, but with one mount led the
    // reins picked up are its own.
    QVERIFY(!g.line("Something stops following you."));
    QVERIFY(g.line("You pick up Something's reins, and start riding him."));
    COMPARE_STATE(g.last->followers.at(0).state, S::LEFT);
    QCOMPARE(g.count(), size_t{0});
    // Dismounted in the dark (:3448-3449): nothing to name it by.
    QVERIFY(!g.line("You stop riding Something."));
    QVERIFY(!g.line("Something starts following you."));
    QCOMPARE(g.count(), size_t{0});

    // A mount by no name the table has, known by having been ridden
    // (log-2005.09.15-21.01.17.txt:100896); without that it would be a player's name.
    QVERIFY(g.line("Gwaihir the Windlord starts following you."));
    QCOMPARE(g.count(), size_t{0});
    QCOMPARE(g.last->players, QStringList{QStringLiteral("Gwaihir the Windlord")});
    QVERIFY(!g.line("You stop riding Gwaihir the Windlord."));
    QVERIFY(g.line("Gwaihir the Windlord starts following you."));
    QVERIFY(g.at(0).kind == K::MOUNT);
    QVERIFY(g.last->players.isEmpty());
    // And one with an article that is no stable's mount.
    QVERIFY(!g.line("You stop riding a Great Eagle."));
    QVERIFY(g.line("A Great Eagle starts following you."));
    QVERIFY(g.at(1).kind == K::MOUNT);
    QCOMPARE(g.at(1).name, QStringLiteral("a Great Eagle"));

    // A mount dies (log-2005.09.07-02.50.20.txt:2503).
    QVERIFY(g.line("A pack horse (my) starts following you."));
    QVERIFY(g.line("A pack horse (my) is dead! R.I.P."));
    QCOMPARE(g.count(), size_t{2});
}

void TestFollowers::labelsTest()
{
    Game g;
    QVERIFY(g.line("A wild dog starts following you."));

    // Another of the same name, labelled by the player as an enemy, is not the follower:
    // what any creature says is matched by name and label both.
    QVERIFY(!g.line("A wild dog (buh) has arrived from the east."));
    QVERIFY(!g.line("A wild dog (buh) is dead! R.I.P."));
    QVERIFY(!g.line("A wild dog (buh) leaves north."));
    QCOMPARE(g.count(), size_t{1});
    QVERIFY(g.at(0).label.isEmpty());

    // The label learned from a line only a follower says, when the command was not seen.
    QVERIFY(g.line("You failed to control a wild dog (one)."));
    QCOMPARE(g.at(0).label, QStringLiteral("one"));
    COMPARE_STATE(g.at(0).state, S::REFUSING);
    // Labelled anew by a command not seen: the only one of that name is still the one meant.
    QVERIFY(g.line("You failed to control a wild dog (two)."));
    QCOMPARE(g.at(0).label, QStringLiteral("two"));
    QCOMPARE(g.count(), size_t{1});
    // With the label known, the unlabelled creature of that name is another.
    QVERIFY(!g.line("A wild dog leaves north."));
    QVERIFY(g.at(0).here);
    QVERIFY(g.line("A wild dog (two) leaves north."));
    QVERIFY(!g.at(0).here);
    COMPARE_STATE(g.at(0).state, S::REFUSING);
    QVERIFY(g.line("A wild dog (two) has arrived from the north."));
    QVERIFY(g.at(0).here);

    // The label command: by a word of the name, by its count among those here, by the old
    // label; a word that matches nobody changes nothing.
    QVERIFY(g.line("A wild dog starts following you."));
    QCOMPARE(g.count(), size_t{2});
    g.send("label 2.dog three");
    QVERIFY(g.line("Ok."));
    QCOMPARE(g.at(0).label, QStringLiteral("two"));
    QCOMPARE(g.at(1).label, QStringLiteral("three"));
    g.send("label three four");
    QVERIFY(g.line("Ok."));
    QCOMPARE(g.at(1).label, QStringLiteral("four"));
    g.send("label troll buh");
    QVERIFY(!g.line("Ok."));
    g.send("label wild two");
    QVERIFY(!g.line("Ok."));
    QCOMPARE(g.at(0).label, QStringLiteral("two"));

    // Stopping is a follower's line too: found by the label shown.
    QVERIFY(g.line("A wild dog (four) stops following you."));
    QCOMPARE(g.count(), size_t{1});
    QCOMPARE(g.at(0).label, QStringLiteral("two"));
}

void TestFollowers::ownDeathTest()
{
    Game g;
    QVERIFY(!g.line("You are dead! Sorry..."));
    QVERIFY(g.line("A mother eagle starts following you."));
    g.send("order followers assist");
    QVERIFY(g.line("You are dead! Sorry..."));
    COMPARE_STATE(g.at(0).state, S::LOST);
    QVERIFY(!g.at(0).here);
    // The order died with the character: a later "Ok." answers nothing.
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.prompt());
    QVERIFY(!g.line("You are dead! Sorry..."));
}

void TestFollowers::limitsTest()
{
    Game g;
    // One left behind for good is never said to be gone: the oldest of those makes room.
    QVERIFY(g.line("A mother eagle starts following you."));
    QVERIFY(g.line("ACK! A mother eagle didn't follow you, you lost her."));
    for (int i = 0; i < 15; ++i) {
        const QByteArray line = "A wild dog (d" + QByteArray::number(i) + ") starts following you.";
        QVERIFY(g.line(line.constData()));
    }
    QCOMPARE(g.count(), size_t{16});
    QCOMPARE(g.at(0).name, QStringLiteral("a mother eagle"));
    QVERIFY(g.line("A raging bear starts following you."));
    QCOMPARE(g.count(), size_t{16});
    QCOMPARE(g.at(0).name, QStringLiteral("a wild dog"));
    QCOMPARE(g.at(15).name, QStringLiteral("a raging bear"));
}

void TestFollowers::resetTest()
{
    Game g;
    QVERIFY(!g.line("You stop riding Gwaihir the Windlord."));
    QVERIFY(g.line("A mother eagle starts following you."));
    g.send("order followers assist");
    g.tracker.reset();
    QCOMPARE(g.count(), size_t{0});
    // Nothing waits for an answer, and what was ridden is forgotten.
    QVERIFY(!g.line("Ok."));
    QVERIFY(!g.prompt());
    QVERIFY(!g.line("A mother eagle (one) is dead! R.I.P."));
    // Without the ride remembered the name is a player's by the look of it.
    QVERIFY(g.line("Gwaihir the Windlord starts following you."));
    QCOMPARE(g.count(), size_t{0});
    QCOMPARE(g.tracker.players(), QStringList{QStringLiteral("Gwaihir the Windlord")});

    // The leader, the players that follow and the protected are forgotten too.
    QVERIFY(g.line("You now follow Grayelf."));
    QVERIFY(g.line("You will now try to protect Budach (B)."));
    QVERIFY(!g.line("You will try to protect:"));
    g.tracker.reset();
    QVERIFY(g.tracker.following().isEmpty());
    QVERIFY(g.tracker.players().isEmpty());
    QVERIFY(!g.tracker.protecting().has_value());
    // The list that was being read is over: this is no name of it.
    QVERIFY(!g.line("   Kazadoe (K)"));
    QVERIFY(!g.prompt());
    QVERIFY(!g.tracker.protecting().has_value());
    QVERIFY(!g.line("You stop following Grayelf."));
}

void TestFollowers::followingTest()
{
    Game g;
    QVERIFY(!leaderOf(CharFollowers{}).has_value());

    // log-2005.09.02-23.20.17.txt:22832-22835: `fol grayelf`, the bond, the first move.
    QVERIFY(g.line("You now follow Grayelf."));
    QCOMPARE(g.last->following, QStringLiteral("Grayelf"));
    auto leader = leaderOf(*g.last);
    QVERIFY(leader.has_value());
    QCOMPARE(leader->name, QStringLiteral("Grayelf"));
    QVERIFY(!leader->you);
    QVERIFY(!g.line("Grayelf leaves north."));
    QVERIFY(!g.line("You follow Grayelf."));
    // log-2005.09.21-01.39.48.txt:29976-29977: `fol me`.
    QVERIFY(g.line("You stop following Grayelf."));
    QVERIFY(g.last->following.isEmpty());
    QVERIFY(!leaderOf(*g.last).has_value());
    QVERIFY(!g.line("You will not follow anyone else now."));

    // log-2005.09.05-19.23.16.txt:42094-42095: sent away; the second line ends it.
    QVERIFY(g.line("You now follow Zmej."));
    QVERIFY(!g.line("Zmej doesn't want you to follow him."));
    QCOMPARE(g.tracker.following(), QStringLiteral("Zmej"));
    QVERIFY(g.line("You stop following Zmej."));
    QVERIFY(g.tracker.following().isEmpty());

    // From one leader to another: "You stop following X." / "You now follow Y." (44 pairs).
    QVERIFY(g.line("You now follow Kazadoe (K)."));
    QCOMPARE(g.tracker.following(), QStringLiteral("Kazadoe"));
    QVERIFY(!g.line("You now follow Kazadoe (K)."));
    QVERIFY(g.line("You stop following Kazadoe (K)."));
    QVERIFY(g.line("You now follow a black sorcerer."));
    QCOMPARE(g.tracker.following(), QStringLiteral("a black sorcerer"));
    // Refusals change nothing.
    QVERIFY(!g.line("Sorry, but following in 'loops' is not allowed."));
    QVERIFY(!g.line("An old man doesn't want you to follow him!"));
    // `fol me` with a leader, the last line alone when the first went unread.
    QVERIFY(g.line("You will not follow anyone else now."));
    QVERIFY(g.tracker.following().isEmpty());

    // A bond made before MMapper was watching is learned from the first move after the
    // leader; a room's description is not one.
    QVERIFY(!g.line("You follow the trail up to a hill."));
    QVERIFY(!g.line("You follow a gentle slope downward to some shallow water."));
    QVERIFY(g.tracker.following().isEmpty());
    QVERIFY(g.line("You follow Orhzul."));
    QCOMPARE(g.last->following, QStringLiteral("Orhzul"));
    QVERIFY(!g.line("You follow Orhzul."));
    // In the dark nobody is named, and nothing is kept.
    QVERIFY(g.line("You stop following Orhzul."));
    QVERIFY(!g.line("You now follow someone."));
    QVERIFY(g.tracker.following().isEmpty());
}

void TestFollowers::leaderTest()
{
    Game g;
    // Others follow the player, who follows nobody: the player leads.
    // log-2005.10.16-23.42.44.txt:3680, log-2005.12.12-16.09.18.txt:1853.
    QVERIFY(g.line("Budach (B) starts following you."));
    QCOMPARE(g.last->players, QStringList{QStringLiteral("Budach")});
    auto leader = leaderOf(*g.last);
    QVERIFY(leader.has_value());
    QVERIFY(leader->you);
    QVERIFY(leader->name.isEmpty());
    QVERIFY(!g.line("Budach (B) starts following you."));
    QVERIFY(g.line("Kazadoe (K) starts following you."));
    QCOMPARE(g.last->players, (QStringList{QStringLiteral("Budach"), QStringLiteral("Kazadoe")}));
    // log-2005.10.16-23.42.44.txt:30777.
    QVERIFY(g.line("Budach (B) stops following you."));
    QCOMPARE(g.last->players, QStringList{QStringLiteral("Kazadoe")});
    QVERIFY(leaderOf(*g.last)->you);
    // The player follows somebody: that one leads, whoever still follows the player.
    QVERIFY(g.line("You now follow Zmej."));
    leader = leaderOf(*g.last);
    QVERIFY(!leader->you);
    QCOMPARE(leader->name, QStringLiteral("Zmej"));
    QVERIFY(g.line("You stop following Zmej."));
    QVERIFY(leaderOf(*g.last)->you);
    // log-2005.09.21-01.39.48.txt:29980 "Zmej now follows Rhuka.": one who follows somebody
    // else follows the player no longer.
    QVERIFY(g.line("Kazadoe (K) now follows Rhuka."));
    QVERIFY(g.last->players.isEmpty());
    QVERIFY(!leaderOf(*g.last).has_value());
    QVERIFY(!g.line("Kazadoe (K) stops following you."));

    // A bound follower makes the player a leader as well, until it is gone.
    QVERIFY(g.line("A mother eagle starts following you."));
    QVERIFY(leaderOf(*g.last)->you);
    QVERIFY(g.line("A mother eagle is dead! R.I.P."));
    QVERIFY(!leaderOf(lastingFollowers(*g.last)).has_value());

    // One taken for a player by its name that refuses an order was a follower.
    QVERIFY(g.line("Harle the Hobbit starts following you."));
    QCOMPARE(g.last->players, QStringList{QStringLiteral("Harle the Hobbit")});
    QVERIFY(g.line("You failed to control Harle the Hobbit."));
    QVERIFY(g.last->players.isEmpty());
    QCOMPARE(g.count(), size_t{1});
    // The player's own death leaves what it follows and who follows it as they were: no line
    // of the logs says otherwise.
    QVERIFY(g.line("Budach (B) starts following you."));
    QVERIFY(g.line("You are dead! Sorry..."));
    QCOMPARE(g.last->players, QStringList{QStringLiteral("Budach")});
}

void TestFollowers::protectTest()
{
    Game g;
    QVERIFY(!g.tracker.protecting().has_value());
    // log-2005.09.15-21.01.17.txt:155-156: `protect b`.
    QVERIFY(g.line("You will now try to protect Budach (B)."));
    QVERIFY(g.last->protecting.has_value());
    QCOMPARE(*g.last->protecting, QStringList{QStringLiteral("Budach")});
    QVERIFY(!g.line("You will now try to protect Budach (B)."));
    // Several at once: log-2005.11.17-17.17.33.txt:5534, :13895.
    QVERIFY(g.line("You will now try to protect Sisalik."));
    QCOMPARE(*g.last->protecting, (QStringList{QStringLiteral("Budach"), QStringLiteral("Sisalik")}));
    QVERIFY(!g.line("You can only protect those in your group."));
    // log-2005.11.17-17.17.33.txt:83438: the same command turns it off.
    QVERIFY(g.line("You will no longer try to protect Budach (B)."));
    QCOMPARE(*g.last->protecting, QStringList{QStringLiteral("Sisalik")});
    // `protect self`, log-2005.11.17-17.17.33.txt:44529: nobody is protected.
    QVERIFY(g.line("Very well, you concentrate on your own health."));
    QVERIFY(g.last->protecting.has_value());
    QVERIFY(g.last->protecting->isEmpty());
    QVERIFY(!g.line("You aren't trying to protect anyone."));

    // The list of a bare `protect` (log-2005.12.19-20.51.29.txt:88011-88013) states it whole,
    // and is told at the line or the prompt that ends it.
    QVERIFY(!g.line("You will try to protect:"));
    QVERIFY(!g.line("   Kazadoe (K)"));
    QVERIFY(g.line(""));
    QCOMPARE(*g.last->protecting, QStringList{QStringLiteral("Kazadoe")});
    QVERIFY(!g.prompt());
    QVERIFY(!g.line("You will try to protect:"));
    QVERIFY(!g.line("   Kazadoe (K)"));
    QVERIFY(!g.line("   Harle the Hobbit"));
    QVERIFY(g.prompt());
    QCOMPARE(*g.last->protecting,
             (QStringList{QStringLiteral("Kazadoe"), QStringLiteral("Harle the Hobbit")}));
    // The same again changes nothing; the line that ends a list is still read.
    QVERIFY(!g.line("You will try to protect:"));
    QVERIFY(!g.line("   Kazadoe (K)"));
    QVERIFY(!g.line("   Harle the Hobbit"));
    QVERIFY(g.line("You now follow Zmej."));
    QCOMPARE(g.last->following, QStringLiteral("Zmej"));
    QCOMPARE(g.last->protecting->size(), 2);
    // What lasts for a replay: all of it.
    const CharFollowers lasting = lastingFollowers(*g.last);
    QCOMPARE(lasting.following, QStringLiteral("Zmej"));
    QCOMPARE(lasting.protecting->size(), 2);
    // A list with no name read tells nothing.
    QVERIFY(!g.line("You will try to protect:"));
    QVERIFY(!g.prompt());
    QCOMPARE(g.tracker.protecting()->size(), 2);

    // Known from the first line that says anything, whatever it says.
    Game h;
    QVERIFY(h.line("You will no longer try to protect Barzikon."));
    QVERIFY(h.last->protecting->isEmpty());
    Game i;
    QVERIFY(i.line("You aren't trying to protect anyone."));
    QVERIFY(i.last->protecting->isEmpty());
}

QTEST_MAIN(TestFollowers)
