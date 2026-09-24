// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestXmlElements.h"

#include "../src/parser/XmlElement.h"
#include "../src/parser/XmlElementTracker.h"

#include <QtTest/QtTest>

namespace {

/// Feeds a document the way MumeXmlParser does: tags without their angle brackets, and the
/// text between them, in order.
void feed(XmlElementTracker &tracker, const QString &document)
{
    QString text;
    bool inTag = false;
    QString tag;
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
}

NODISCARD QString attributeOf(const XmlElement &element, const std::string &key)
{
    for (const auto &attribute : element.attributes) {
        if (attribute.first == key) {
            return QString::fromStdString(attribute.second);
        }
    }
    return QString{};
}

} // namespace

void TestXmlElements::tagNamesTest()
{
    QCOMPARE(toXmlTag("hit"), XmlTagEnum::HIT);
    QCOMPARE(toXmlTag("avoid_damage"), XmlTagEnum::AVOID_DAMAGE);
    QCOMPARE(toXmlTag("object"), XmlTagEnum::OBJECT);
    QCOMPARE(toXmlTag("nonsense"), XmlTagEnum::UNKNOWN);

    // The categories are what lets a client subscribe to combat without knowing every tag.
    QCOMPARE(toXmlCategory(XmlTagEnum::HIT), XmlCategoryEnum::COMBAT);
    QCOMPARE(toXmlCategory(XmlTagEnum::MISS), XmlCategoryEnum::COMBAT);
    QCOMPARE(toXmlCategory(XmlTagEnum::CHARACTER), XmlCategoryEnum::ENTITY);
    QCOMPARE(toXmlCategory(XmlTagEnum::OBJECT), XmlCategoryEnum::ENTITY);
    QCOMPARE(toXmlCategory(XmlTagEnum::TELL), XmlCategoryEnum::COMMUNICATION);
    QCOMPARE(toXmlCategory(XmlTagEnum::UNKNOWN), XmlCategoryEnum::UNKNOWN);

    QCOMPARE(to_string_view(XmlTagEnum::AVOID_DAMAGE), std::string_view{"avoid_damage"});
    QCOMPARE(to_string_view(XmlCategoryEnum::COMBAT), std::string_view{"combat"});
}

void TestXmlElements::simpleElementTest()
{
    XmlElementTracker tracker;
    feed(tracker, "<tell>Gandalf tells you 'Fly, you fools!'</tell>");

    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].tag, XmlTagEnum::TELL);
    QCOMPARE(elements[0].text, QString{"Gandalf tells you 'Fly, you fools!'"});
    QVERIFY(!elements[0].truncated);
    QVERIFY(elements[0].children.empty());

    // Taking drains: the same element is not reported twice.
    QVERIFY(tracker.take().empty());
}

void TestXmlElements::nestedElementTest()
{
    XmlElementTracker tracker;
    feed(tracker,
         "<hit><character>A dirty uruk</character> barely slashes your body and tickles it."
         "</hit>");

    const std::vector<XmlElement> elements = tracker.take();
    // The participants come inside the blow, not as separate messages before it.
    QCOMPARE(elements.size(), static_cast<size_t>(1));

    const XmlElement &hit = elements[0];
    QCOMPARE(hit.tag, XmlTagEnum::HIT);
    QCOMPARE(hit.text, QString{"A dirty uruk barely slashes your body and tickles it."});
    QCOMPARE(hit.children.size(), static_cast<size_t>(1));
    QCOMPARE(hit.children[0].tag, XmlTagEnum::CHARACTER);
    QCOMPARE(hit.children[0].text, QString{"A dirty uruk"});
}

void TestXmlElements::attributesTest()
{
    XmlElementTracker tracker;
    // MUME's own description of its XML is "not very strict": values may be double quoted,
    // single quoted or bare.
    feed(tracker, "<room terrain=\"field\" area='The Shire' id=4711>Bag End</room>");

    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].tag, XmlTagEnum::ROOM);
    QCOMPARE(attributeOf(elements[0], "terrain"), QString{"field"});
    QCOMPARE(attributeOf(elements[0], "area"), QString{"The Shire"});
    QCOMPARE(attributeOf(elements[0], "id"), QString{"4711"});
    QCOMPARE(elements[0].text, QString{"Bag End"});
}

void TestXmlElements::selfClosingTest()
{
    XmlElementTracker tracker;
    feed(tracker, "<exits><exit dir=north/>north</exits>");

    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].tag, XmlTagEnum::EXITS);
    QCOMPARE(elements[0].children.size(), static_cast<size_t>(1));

    const XmlElement &exit = elements[0].children[0];
    QCOMPARE(exit.tag, XmlTagEnum::EXIT);
    QCOMPARE(attributeOf(exit, "dir"), QString{"north"});
    // Closed by its own slash, so the text that follows belongs to the parent.
    QVERIFY(exit.text.isEmpty());
    QCOMPARE(elements[0].text, QString{"north"});
}

void TestXmlElements::unknownTagTest()
{
    XmlElementTracker tracker;
    // MUME's documentation says outright that there will be more tags, so one this build
    // has never heard of is reported rather than dropped.
    feed(tracker, "<somethingnew>content</somethingnew>");

    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].tag, XmlTagEnum::UNKNOWN);
    QCOMPARE(elements[0].name, std::string{"somethingnew"});
    QCOMPARE(elements[0].text, QString{"content"});
}

void TestXmlElements::strayCloseTest()
{
    XmlElementTracker tracker;
    // What a client sees if it attaches in the middle of an element.
    feed(tracker, "already in progress</description>and more");

    QVERIFY(tracker.take().empty());
    QCOMPARE(tracker.depth(), static_cast<size_t>(0));
}

void TestXmlElements::mismatchedCloseTest()
{
    XmlElementTracker tracker;
    feed(tracker, "<hit><character>A dirty uruk</hit>");

    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));

    const XmlElement &hit = elements[0];
    QCOMPARE(hit.tag, XmlTagEnum::HIT);
    // The unclosed child is still reported, marked so that nobody trusts its extent.
    QCOMPARE(hit.children.size(), static_cast<size_t>(1));
    QCOMPARE(hit.children[0].tag, XmlTagEnum::CHARACTER);
    QVERIFY(hit.children[0].truncated);
    QVERIFY(hit.truncated);
    QCOMPARE(tracker.depth(), static_cast<size_t>(0));
}

void TestXmlElements::textOutsideElementsTest()
{
    XmlElementTracker tracker;
    feed(tracker, "You feel hungry.");
    QVERIFY(tracker.take().empty());

    // Ordinary output with no markup produces nothing; it still reaches the user through
    // the terminal stream.
    feed(tracker, "before<em>middle</em>after");
    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].text, QString{"middle"});
}

void TestXmlElements::multiLineElementTest()
{
    XmlElementTracker tracker;
    // A description spans several lines; nothing is reported until it closes.
    feed(tracker, "<description>The road runs east.\n");
    QVERIFY(tracker.take().empty());
    QCOMPARE(tracker.depth(), static_cast<size_t>(1));

    feed(tracker, "A signpost stands here.\n</description>");
    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].tag, XmlTagEnum::DESCRIPTION);
    QCOMPARE(elements[0].text, QString{"The road runs east.\nA signpost stands here.\n"});
    QVERIFY(!elements[0].truncated);
}

void TestXmlElements::resetTest()
{
    XmlElementTracker tracker;
    feed(tracker, "<description>half a room");
    QCOMPARE(tracker.depth(), static_cast<size_t>(1));
    QVERIFY(tracker.hasOpenElements());

    // A new session, or XML mode going away: what was open will never be closed.
    tracker.reset();
    QCOMPARE(tracker.depth(), static_cast<size_t>(0));
    QVERIFY(!tracker.hasOpenElements());
    QVERIFY(tracker.take().empty());
}

void TestXmlElements::depthLimitTest()
{
    XmlElementTracker tracker;
    QString document;
    const size_t excess = XmlElementTracker::MAX_DEPTH + 4;
    for (size_t i = 0; i < excess; ++i) {
        document += "<em>";
    }
    document += "deep";
    for (size_t i = 0; i < excess; ++i) {
        document += "</em>";
    }
    feed(tracker, document);

    // Everything opened past the limit is refused, and its closing tag is refused with it,
    // so the stack comes back to empty rather than drifting.
    QCOMPARE(tracker.depth(), static_cast<size_t>(0));
    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QVERIFY(elements[0].truncated);
}

void TestXmlElements::textLimitTest()
{
    XmlElementTracker tracker;
    const QString huge(XmlElementTracker::MAX_TEXT_LENGTH + 100, QChar{'x'});
    feed(tracker, "<description>" + huge + "</description>");

    const std::vector<XmlElement> elements = tracker.take();
    QCOMPARE(elements.size(), static_cast<size_t>(1));
    QCOMPARE(elements[0].text.length(), static_cast<qsizetype>(XmlElementTracker::MAX_TEXT_LENGTH));
    QVERIFY(elements[0].truncated);
}

/// Which way someone moved, as MMapper reads it for the frontend. MUME states a direction only on
/// the player's own <movement>; for others it is in the line.
void TestXmlElements::movementDirectionTest()
{
    const auto directionOf = [](const QString &document) -> QString {
        XmlElementTracker tracker;
        feed(tracker, document);
        const std::vector<XmlElement> elements = tracker.take();
        if (elements.size() != 1) {
            return QStringLiteral("<%1 elements>").arg(elements.size());
        }
        return QString::fromStdString(elements[0].direction);
    };

    // Lines from real sessions.
    QCOMPARE(directionOf(
                 "<move_in><character>A scholar</character> has arrived from the south.</move_in>"),
             QStringLiteral("south"));
    QCOMPARE(directionOf(
                 "<move_in><character>Tundur the Lamplighter</character> has arrived from the "
                 "north.</move_in>"),
             QStringLiteral("north"));
    QCOMPARE(directionOf(
                 "<move_out><character>A D\u00fanadan soldier</character> leaves east.</move_out>"),
             QStringLiteral("east"));
    QCOMPARE(directionOf(
                 "<move_out><character>Tundur the Lamplighter</character> leaves west.</move_out>"),
             QStringLiteral("west"));
    // Nothing to read, so nothing is claimed.
    QCOMPARE(directionOf(
                 "<move_out><character>An old man</character> leaves with a sigh.</move_out>"),
             QString{});

    // Up and down, in either of the ways MUME words them.
    QCOMPARE(directionOf(
                 "<move_in><character>A dwarf</character> has arrived from above.</move_in>"),
             QStringLiteral("up"));
    QCOMPARE(directionOf("<move_in><character>A mole</character> has arrived from below.</move_in>"),
             QStringLiteral("down"));
    QCOMPARE(directionOf("<move_out><character>A mole</character> leaves down.</move_out>"),
             QStringLiteral("down"));
    QCOMPARE(directionOf("<move_out><character>A climber</character> leaves up.</move_out>"),
             QStringLiteral("up"));

    // The way a character went, not the words in its name, and not the rest of the clause.
    QCOMPARE(directionOf("<move_out><character>The North Wind</character> leaves south.</move_out>"),
             QStringLiteral("south"));
    QCOMPARE(directionOf(
                 "<move_in><character><player>Eastwind</player></character> has arrived from "
                 "the west.</move_in>"),
             QStringLiteral("west"));
    QCOMPARE(directionOf(
                 "<move_in><character>A rider</character> has arrived from the north, riding "
                 "a pony.</move_in>"),
             QStringLiteral("north"));
    QCOMPARE(directionOf("<move_out><character>A trout</character> swims east.</move_out>"),
             QStringLiteral("east"));
    // Some creatures that are meant to seem like plants carry no name markup at all.
    QCOMPARE(directionOf("<move_out>The willow leaves north.</move_out>"), QStringLiteral("north"));

    // MUME's own statement wins wherever it makes one.
    QCOMPARE(directionOf("<move_in dir=east><character>A scholar</character> has arrived from the "
                         "west.</move_in>"),
             QStringLiteral("east"));
    QCOMPARE(directionOf("<movement dir=north/>"), QStringLiteral("north"));
    QCOMPARE(directionOf("<movement/>"), QString{});

    // Only the movement tags carry one.
    QCOMPARE(directionOf("<tell>Gandalf tells you 'go north'</tell>"), QString{});
    QCOMPARE(directionOf("<hit dir=north><character>A dirty uruk</character> hits you.</hit>"),
             QString{});

    // A movement nested inside another element is read too.
    XmlElementTracker tracker;
    feed(tracker,
         "<xml><move_in><character>A cat</character> has arrived from the east.</move_in></xml>");
    const std::vector<XmlElement> outer = tracker.take();
    QCOMPARE(outer.size(), static_cast<size_t>(1));
    QCOMPARE(outer[0].children.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(outer[0].children[0].direction), QStringLiteral("east"));
    QVERIFY(outer[0].direction.empty());
}

QTEST_MAIN(TestXmlElements)
