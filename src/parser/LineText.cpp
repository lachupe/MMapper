// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "LineText.h"

#include "../global/parserutils.h"
#include "XmlElement.h"

#include <QRegularExpression>

namespace line_text {

namespace {

const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};
const QRegularExpression g_twiddlersBeforeLowercase{
    QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*<a-z]))")};

} // namespace

void removeTwiddlers(QString &line)
{
    line.remove(g_twiddlers);
}

void removeTwiddlersBeforeLowercase(QString &line)
{
    line.remove(g_twiddlersBeforeLowercase);
}

QString plainOf(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    return text;
}

QString cleaned(const QString &line)
{
    return plainOf(line).simplified();
}

QString cleanedWithoutTwiddlers(const QString &line)
{
    QString text = cleaned(line);
    removeTwiddlers(text);
    return text;
}

QString cleanedForItems(const QString &line)
{
    QString text = cleaned(line);
    removeTwiddlersBeforeLowercase(text);
    return text;
}

QString trimmedWithoutTwiddlers(const QString &line)
{
    QString text = line.trimmed();
    removeTwiddlers(text);
    return text;
}

QString rawLine(const QString &line)
{
    QString text = plainOf(line);
    while (!text.isEmpty() && text.back().isSpace()) {
        text.chop(1);
    }
    return text;
}

std::optional<int64_t> toNumber(QString digits)
{
    digits.remove(QLatin1Char(','));
    bool ok = false;
    const qlonglong value = digits.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<int64_t>(value);
}

bool isItemLine(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    const QChar first = text.front();
    return first.isLower() || first.isDigit() || text == QStringLiteral("Something.");
}

bool seesTheRoom(const XmlElement &room)
{
    for (const XmlElement &child : room.children) {
        if (child.tag != XmlTagEnum::NAME) {
            continue;
        }
        const QString name = cleaned(child.text);
        return !name.isEmpty() && !name.contains(QStringLiteral("fog around you"))
               && !name.contains(QStringLiteral("pitch black"))
               && !name.contains(QStringLiteral("too dark"));
    }
    return false;
}

} // namespace line_text
