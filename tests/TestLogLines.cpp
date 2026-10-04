// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestLogLines.h"

#include "../src/parser/CharFollowers.h"
#include "../src/parser/CombatLines.h"
#include "../src/parser/RoomDoors.h"
#include "LogLinesData.h"

#include <QStringList>
#include <QtTest/QtTest>

// The logs as parser tests (mume3d TODO G.15): every distinct line of the user's powwow logs of the door, follower and
// refused-move shapes MMapper reads must be read. A line that is not is listed, so that a pattern can be widened.
void TestLogLines::logLinesTest()
{
    QStringList missed;
    int seen = 0;
    for (const LogLine &one : LOG_LINES) {
        const QString kind = QString::fromLatin1(one.kind);
        const QString line = QString::fromUtf8(one.line);
        bool read = false;
        if (kind.startsWith(QStringLiteral("door"))) {
            read = parseDoorLine(line).has_value();
        } else if (kind == QStringLiteral("followers")) {
            read = parseFollowerLine(line).has_value();
        } else if (kind == QStringLiteral("move-refused")) {
            const auto event = parseCombatLine(line);
            read = event.has_value() && event->kind == CombatKindEnum::REFUSED;
        }
        ++seen;
        if (!read) {
            missed.append(kind + QStringLiteral(": ") + line);
        }
    }
    qInfo().noquote() << seen << "log lines," << missed.size() << "not read";
    for (const QString &line : missed) {
        qInfo().noquote() << "  " << line;
    }
    QVERIFY2(missed.isEmpty(), qPrintable(QStringLiteral("%1 log lines not read").arg(missed.size())));
}

QTEST_MAIN(TestLogLines)
