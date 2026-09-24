// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestWeatherLines.h"

#include "../src/parser/WeatherLines.h"
#include "../src/parser/XmlElement.h"
#include "../src/parser/XmlElementTracker.h"

#include <optional>
#include <vector>

#include <QtTest/QtTest>

// Every sentence here is one MUME printed in the powwow logs, colour codes included where it
// sent them. The XML around them is the logs' own where they were recorded in XML mode; where
// a test puts a document together, it says so.

namespace {

NODISCARD WeatherLine read(const char *const line)
{
    return parseWeatherLine(QString::fromUtf8(line));
}

NODISCARD QString str(const std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

/// Feeds a document the way MumeXmlParser does -- tags without their angle brackets, and the
/// text between them, in order -- and returns the elements in the order they closed.
NODISCARD std::vector<XmlElement> elementsOf(const QString &document)
{
    XmlElementTracker tracker;
    QString text;
    QString tag;
    bool inTag = false;
    for (const QChar c : document) {
        if (inTag) {
            if (c == QChar{'>'}) {
                inTag = false;
                tracker.receiveTag(tag);
                tag.clear();
            } else {
                tag.append(c);
            }
            continue;
        }
        if (c == QChar{'<'}) {
            tracker.receiveText(text);
            text.clear();
            inTag = true;
            continue;
        }
        text.append(c);
    }
    tracker.receiveText(text);
    return tracker.take();
}

/// What `tracker` reports over `document`, fed as MumeXmlParser feeds it.
NODISCARD std::vector<GroundState> groundOf(GroundTracker &tracker, const QString &document)
{
    std::vector<GroundState> reports;
    for (const XmlElement &element : elementsOf(document)) {
        if (const auto ground = tracker.receive(element, parseWeatherElement(element))) {
            reports.push_back(*ground);
        }
    }
    return reports;
}

} // namespace

void TestWeatherLines::groundLinesTest()
{
    const WeatherLine some = read("There is some snow on the ground.");
    QCOMPARE(some.kind, WeatherLineKindEnum::SNOW);
    QCOMPARE(str(some.level), QString("some"));
    QVERIFY(!some.changing);
    QCOMPARE(str(read("There is a lot of snow on the ground.").level), QString("lot"));
    QCOMPARE(str(read("The snow is so deep that it is almost impossible to move.").level),
             QString("deep"));
    QCOMPARE(str(read("You don't see anymore snow here.").level), QString("none"));

    // Colour is MUME's, not part of the sentence.
    const WeatherLine frozen = read("\x1b[34mThe ground is frozen solid.\x1b[0m");
    QCOMPARE(frozen.kind, WeatherLineKindEnum::FROST);
    QCOMPARE(str(frozen.level), QString("frozen"));
    QCOMPARE(frozen.text, QString("The ground is frozen solid."));
    QCOMPARE(str(read("The ground is slightly frosty.").level), QString("slight"));
    QCOMPARE(str(read("The ground no longer feels frosty.").level), QString("none"));

    // A change reports the state it is heading for.
    const WeatherLine hardening = read("The ground is hardening with frost.");
    QCOMPARE(hardening.kind, WeatherLineKindEnum::FROST);
    QVERIFY(hardening.changing);
    QCOMPARE(str(hardening.level), QString("very"));

    const WeatherLine film = read("\x1b[34mThere is a thin film of ice on the water.\x1b[0m");
    QCOMPARE(film.kind, WeatherLineKindEnum::ICE);
    QCOMPARE(str(film.level), QString("film"));
    QCOMPARE(str(read("The water is frozen solid.").level), QString("frozen"));
    const WeatherLine melting = read("The ice is slowly starting to melt.");
    QVERIFY(melting.changing);
    QCOMPARE(str(melting.level), QString("some"));
}

void TestWeatherLines::fogTest()
{
    const WeatherLine drifting = read("You see some fog coming from the north.");
    QCOMPARE(drifting.kind, WeatherLineKindEnum::FOG);
    QCOMPARE(str(drifting.level), QString("light"));
    QVERIFY(drifting.changing);
    QCOMPARE(str(drifting.direction), QString("north"));

    const WeatherLine dense = read("The fog begins to be very dense.");
    QCOMPARE(str(dense.level), QString("dense"));
    QVERIFY(dense.changing);
    QVERIFY(dense.direction.empty());
    QCOMPARE(str(read("The fog disappears.").level), QString("none"));
}

void TestWeatherLines::stormTest()
{
    const WeatherLine stormy = read("The weather becomes stormy.");
    QCOMPARE(stormy.kind, WeatherLineKindEnum::STORM);
    QVERIFY(stormy.precipitation.empty());
    QVERIFY(!stormy.magic);

    const WeatherLine snowing = read("The sky cracks and booms, and snow falls on you.");
    QCOMPARE(snowing.kind, WeatherLineKindEnum::STORM);
    QCOMPARE(str(snowing.precipitation), QString("snow"));
}

void TestWeatherLines::lightningAndThunderTest()
{
    // Worded differently from place to place.
    for (const char *const line :
         {"A flare of lightning branches out into several small streaks above the mountains.",
          "Dark clouds in the sky light up, as lightning strikes from within.",
          "Bolts of lightning come crashing down from the sky, striking the mountain peaks and "
          "the mountainsides.",
          "Above the branches and leaves of the trees, a flare of lightning crosses the sky, "
          "lighting the trees up for a brief moment."}) {
        const WeatherLine flash = read(line);
        QCOMPARE(flash.kind, WeatherLineKindEnum::LIGHTNING);
        QVERIFY2(flash.level.empty(), line);
    }

    // "Down from the sky" is not a side.
    QVERIFY(read("Bolts of lightning come crashing down from the sky, striking the mountain peaks "
                 "and the mountainsides.")
                .direction.empty());

    const WeatherLine elsewhere = read(
        "Lightning lights the sky, followed by a rolling thunder from the west.");
    QCOMPARE(elsewhere.kind, WeatherLineKindEnum::LIGHTNING);
    QCOMPARE(str(elsewhere.direction), QString("west"));

    const WeatherLine stopped = read("The lightning has stopped.");
    QCOMPARE(stopped.kind, WeatherLineKindEnum::LIGHTNING);
    QCOMPARE(str(stopped.level), QString("none"));

    const WeatherLine heard = read("You hear a crackle of thunder in the distance from the south.");
    QCOMPARE(heard.kind, WeatherLineKindEnum::THUNDER);
    QCOMPARE(str(heard.direction), QString("south"));
    const WeatherLine rolling = read("You hear rolling thunder.");
    QCOMPARE(rolling.kind, WeatherLineKindEnum::THUNDER);
    QVERIFY(rolling.direction.empty());
}

void TestWeatherLines::magicTest()
{
    const WeatherLine strange = read("Lightning starts to show in the sky. Quite strange!");
    QCOMPARE(strange.kind, WeatherLineKindEnum::LIGHTNING);
    QVERIFY(strange.magic);

    const WeatherLine veryStrange = read(
        "The weather suddenly becomes extremely stormy. How very strange!");
    QCOMPARE(veryStrange.kind, WeatherLineKindEnum::STORM);
    QVERIFY(veryStrange.magic);
    QVERIFY(read("The weather suddenly becomes extremely stormy, how strange...").magic);
    QVERIFY(read("The weather suddenly becomes extremely stormy, how strange.").magic);
    QVERIFY(!read("The weather suddenly becomes extremely stormy.").magic);
}

void TestWeatherLines::darknessTest()
{
    const WeatherLine start = read("Arda seems to wither as an evil power begins to grow...");
    QCOMPARE(start.kind, WeatherLineKindEnum::DARKNESS);
    QCOMPARE(str(start.level), QString("start"));
    QCOMPARE(str(read("\x1b[34mShrouds of dark clouds roll in above you, blotting out the "
                      "skies.\x1b[0m")
                     .level),
             QString("present"));
    QCOMPARE(str(read("The evil power begins to regress...").level), QString("end"));
}

void TestWeatherLines::unknownTest()
{
    // Weather MUME states in Char.Vitals too, and the time of day, which Event.Sun carries.
    for (const char *const line : {"The clouds suddenly disappear.", "The night has begun."}) {
        const WeatherLine other = read(line);
        QCOMPARE(other.kind, WeatherLineKindEnum::UNKNOWN);
        QCOMPARE(other.text, QString::fromUtf8(line));
        QVERIFY(other.level.empty());
    }
}

void TestWeatherLines::elementTest()
{
    const std::vector<XmlElement> elements = elementsOf(
        "<weather>\x1b[34mThe water is frozen solid.\x1b[0m\n</weather>"
        "<prompt>*% CRW HP:Fine&gt;</prompt>");
    QCOMPARE(elements.size(), size_t{2});
    const std::optional<WeatherLine> ice = parseWeatherElement(elements.at(0));
    QVERIFY(ice.has_value());
    QCOMPARE(ice->kind, WeatherLineKindEnum::ICE);
    QCOMPARE(str(ice->level), QString("frozen"));
    // Only <weather> and <terrain> carry weather.
    QVERIFY(!parseWeatherElement(elements.at(1)).has_value());

    // A terrain line that says nothing about the weather is still passed on, as unknown.
    const std::vector<XmlElement> road = elementsOf("<terrain>You are on a road.\n</terrain>");
    QCOMPARE(road.size(), size_t{1});
    const std::optional<WeatherLine> onRoad = parseWeatherElement(road.at(0));
    QVERIFY(onRoad.has_value());
    QCOMPARE(onRoad->kind, WeatherLineKindEnum::UNKNOWN);
}

void TestWeatherLines::snowInTerrainTest()
{
    // The snow lying in a room is inside its <terrain> (valamir.mov; the log goes on to the
    // login messages, so the prompt is added).
    GroundTracker tracker;
    const auto reports = groundOf(
        tracker,
        "<room><name>\x1b[32mSandy Hills\x1b[0m\n</name>"
        "<terrain>There is some snow on the ground.\n</terrain>A large eagle soars above you.\n"
        "</room><exits>Exits: north, west.\n</exits><prompt>*! W HP:Fine&gt;</prompt>");
    QCOMPARE(reports.size(), size_t{1});
    QCOMPARE(str(reports.at(0).snow), QString("some"));
    QCOMPARE(str(reports.at(0).frost), QString("none"));
    QCOMPARE(str(reports.at(0).ice), QString("none"));
    QVERIFY(reports.at(0).entered);

    // The next room shows none, so there is none (put together).
    const auto next = groundOf(tracker,
                               "<movement dir=east/><room><name>\x1b[32mRolling Hills\x1b[0m\n"
                               "</name></room><prompt>*! W HP:Fine&gt;</prompt>");
    QCOMPARE(next.size(), size_t{1});
    QCOMPARE(str(next.at(0).snow), QString("none"));
    QVERIFY(next.at(0).entered);
}

void TestWeatherLines::frostAfterExitsTest()
{
    // MUME of the time these logs were made put frost and ice after the exits, outside the room.
    GroundTracker tracker;
    const auto reports = groundOf(
        tracker,
        "<movement dir=east/><room><name>\x1b[32mRolling Hills\x1b[0m\n</name>"
        "<description>\x1b[34mThe land gently rises and falls around here creating some pleasant "
        "hills.\x1b[0m\n</description></room><exits>Exits: north, east, south, west.\n</exits>"
        "<weather>\x1b[34mThe ground is frozen solid.\x1b[0m\n</weather>"
        "<prompt>*. CRW HP:Fine&gt;</prompt>");
    QCOMPARE(reports.size(), size_t{1});
    QCOMPARE(str(reports.at(0).frost), QString("frozen"));
    QCOMPARE(str(reports.at(0).snow), QString("none"));
    QVERIFY(reports.at(0).entered);
}

void TestWeatherLines::unseenRoomTest()
{
    // In the dark the terrain still shows ("You are on a road."), but nothing of the ground.
    GroundTracker tracker;
    const auto dark = groundOf(tracker,
                               "<movement dir=west/><room><name>It is pitch black...\n</name>"
                               "<terrain>You are on a road.\n</terrain>Someone is here.\n</room>"
                               "<prompt>o+ W HP:Fine&gt;</prompt>");
    QCOMPARE(dark.size(), size_t{1});
    QCOMPARE(str(dark.at(0).snow), QString("unknown"));
    QCOMPARE(str(dark.at(0).frost), QString("unknown"));
    QCOMPARE(str(dark.at(0).ice), QString("unknown"));

    // The logs have dense fog only without XML ("You just see a dense fog around you..." where
    // the name would be); this puts it where the dark display puts "It is pitch black...".
    const auto fog = groundOf(tracker,
                              "<movement dir=north/><room><name>You just see a dense fog around "
                              "you...\n</name></room><prompt>~= W HP:Fine&gt;</prompt>");
    QCOMPARE(fog.size(), size_t{1});
    QCOMPARE(str(fog.at(0).snow), QString("unknown"));
}

void TestWeatherLines::changeWhereStandingTest()
{
    // Put together from lines the logs have in <weather>.
    GroundTracker tracker;
    const auto frosty = groundOf(tracker,
                                 "<room><name>Rolling Hills\n</name></room>"
                                 "<weather>The ground is frosty.\n</weather>"
                                 "<prompt>*. W HP:Fine&gt;</prompt>");
    QCOMPARE(frosty.size(), size_t{1});
    QCOMPARE(str(frosty.at(0).frost), QString("frosty"));

    // Standing still: a change of the ground, folded into what was there.
    const auto thawing = groundOf(tracker,
                                  "<weather>\x1b[34mThe ground no longer feels frosty.\x1b[0m\n"
                                  "</weather><prompt>*. W HP:Fine&gt;</prompt>");
    QCOMPARE(thawing.size(), size_t{1});
    QCOMPARE(str(thawing.at(0).frost), QString("none"));
    QVERIFY(!thawing.at(0).entered);

    // A prompt with nothing new, and weather that is not about the ground, report nothing.
    QVERIFY(groundOf(tracker, "<prompt>*. W HP:Fine&gt;</prompt>").empty());
    QVERIFY(groundOf(tracker,
                     "<weather>The lightning has stopped.\n</weather><prompt>*. W HP:Fine&gt;"
                     "</prompt>")
                .empty());
    QCOMPARE(str(tracker.state().frost), QString("none"));
}

void TestWeatherLines::resetTest()
{
    GroundTracker tracker;
    std::ignore = groundOf(tracker,
                           "<room><name>A Narrow Track at Mountain Ridge\n</name>"
                           "<terrain>There is a lot of snow on the ground.\n</terrain></room>"
                           "<prompt>!* CW HP:Fine&gt;</prompt>");
    QCOMPARE(str(tracker.state().snow), QString("lot"));
    tracker.reset();
    QCOMPARE(str(tracker.state().snow), QString("none"));
    QVERIFY(groundOf(tracker, "<prompt>!* CW HP:Fine&gt;</prompt>").empty());
}

QTEST_MAIN(TestWeatherLines)
