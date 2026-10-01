// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "ExitLooks.h"

#include "RoomDoors.h"

#include <QRegularExpression>

// The answers are MUME's own, from the powwow logs under /home/aza/data/powwow/logs/archives (2005
// and 2006): 251 looks at a side. "You see nothing special there..." 82 times; a door's state
// alone, "The giant, thick wooden is open." (log-2005.09.03-02.28.34.txt:11403-11406) or "A broken
// gate." (log-2005.09.05-19.23.16.txt:29369); a description, with a door's line after it when
// there is one ("A really ugly wall of thorns. ..." then "The thorns is open.",
// log-2005.09.05-19.23.16.txt:108228-108230); and a say inside an answer
// (log-2005.09.03-02.28.34.txt:11384-11388).

namespace {

const QRegularExpression g_doorState{QStringLiteral(R"(^The (.+) (?:is|are) (open|closed)\.$)")};
const QRegularExpression g_broken{QStringLiteral(R"(^A broken (.+)\.$)")};

NODISCARD QString wordOf(const QString &letter)
{
    if (letter == QStringLiteral("n")) {
        return QStringLiteral("north");
    } else if (letter == QStringLiteral("e")) {
        return QStringLiteral("east");
    } else if (letter == QStringLiteral("s")) {
        return QStringLiteral("south");
    } else if (letter == QStringLiteral("w")) {
        return QStringLiteral("west");
    } else if (letter == QStringLiteral("u")) {
        return QStringLiteral("up");
    } else if (letter == QStringLiteral("d")) {
        return QStringLiteral("down");
    }
    return QString{};
}

NODISCARD bool startsWithAny(const QString &line, std::initializer_list<const char *> starts)
{
    for (const char *start : starts) {
        if (line.startsWith(QLatin1String(start))) {
            return true;
        }
    }
    return false;
}

void addReason(QStringList &reasons, const char *reason)
{
    const QString text = QString::fromLatin1(reason);
    if (!reasons.contains(text)) {
        reasons.append(text);
    }
}

} // namespace

std::string_view to_string_view(const ExitLookKindEnum kind)
{
    switch (kind) {
    case ExitLookKindEnum::DESCRIPTION:
        return "description";
    case ExitLookKindEnum::NOTHING:
        return "nothing";
    case ExitLookKindEnum::DOOR:
        return "door";
    case ExitLookKindEnum::DARK:
        return "dark";
    case ExitLookKindEnum::NO_EXIT:
        return "no-exit";
    case ExitLookKindEnum::EMPTY:
        return "empty";
    case ExitLookKindEnum::OTHER:
        return "other";
    }
    return "other";
}

QString lookedDirection(const QString &input)
{
    const auto aim = parseDoorAim(input);
    if (!aim.has_value() || aim->verb != QStringLiteral("look")) {
        return QString{};
    }
    return wordOf(aim->dir);
}

void ExitLookTracker::receiveRoom(const std::optional<int64_t> room)
{
    if (room != m_room) {
        for (Pending &pending : m_pending) {
            if (!pending.dir.isEmpty() && pending.room != room) {
                addReason(pending.reasons, "moved");
            }
        }
    }
    m_room = room;
}

void ExitLookTracker::receiveCommand(const QString &input)
{
    Pending pending;
    pending.command = input.trimmed();
    pending.dir = lookedDirection(pending.command);
    pending.room = m_room;
    m_pending.push_back(std::move(pending));
    while (m_pending.size() > MAX_PENDING) {
        m_pending.pop_front();
    }
}

void ExitLookTracker::receiveLine(const QString &line, const LineKindEnum kind)
{
    const QString text = line.trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_window.emplace_back(text, kind);
}

std::vector<ExitLook> ExitLookTracker::receivePrompt()
{
    std::vector<ExitLook> done;
    auto window = std::move(m_window);
    m_window.clear();
    if (m_pending.empty()) {
        return done;
    }
    const bool anyText = std::any_of(window.begin(), window.end(), [](const auto &entry) {
        return entry.second == LineKindEnum::TEXT;
    });
    Pending &head = m_pending.front();
    for (const auto &[text, kind] : window) {
        head.raw.append(text);
        switch (kind) {
        case LineKindEnum::TEXT:
            head.kept.append(text);
            break;
        case LineKindEnum::ASYNC:
            head.dropped.append(text);
            break;
        case LineKindEnum::COMBAT:
            head.dropped.append(text);
            addReason(head.reasons, "fight");
            break;
        case LineKindEnum::ROOM:
            head.dropped.append(text);
            addReason(head.reasons, "room-display");
            break;
        }
    }
    // MUME's own prompt after a narrate, an arrival or a fight's round answers no line. Only
    // while a look is the oldest line waiting: the player's own `say` is answered in the same
    // markup, and any other line is closed by any prompt, as before.
    if (!head.dir.isEmpty() && !window.empty() && !anyText) {
        if (++head.prompts >= MAX_PROMPTS) {
            addReason(head.reasons, "lost");
        } else {
            addReason(head.reasons, "extra-prompt");
            return done;
        }
    }
    Pending answered = std::move(head);
    m_pending.pop_front();
    if (!answered.dir.isEmpty()) {
        done.push_back(finish(answered));
    }
    return done;
}

bool ExitLookTracker::waiting() const
{
    return std::any_of(m_pending.begin(), m_pending.end(), [](const Pending &pending) {
        return !pending.dir.isEmpty();
    });
}

void ExitLookTracker::reset()
{
    m_pending.clear();
    m_window.clear();
    m_room.reset();
}

ExitLook ExitLookTracker::finish(const Pending &pending)
{
    ExitLook look;
    look.room = pending.room;
    look.dir = pending.dir;
    look.command = pending.command;
    look.lines = pending.kept;
    look.text = pending.kept.join(QLatin1Char('\n'));
    look.dropped = pending.dropped;
    look.raw = pending.raw;
    look.reasons = pending.reasons;

    const QStringList &lines = pending.kept;
    // The door's line, wherever it stands in the answer: "The gate is closed." or "A broken gate."
    for (const QString &line : lines) {
        if (const auto m = g_doorState.match(line); m.hasMatch()) {
            look.door = m.captured(1).toLower();
            look.doorState = m.captured(2);
        } else if (const auto b = g_broken.match(line); b.hasMatch() && lines.size() == 1) {
            look.door = b.captured(1).toLower();
            look.doorState = QStringLiteral("broken");
        }
    }
    if (lines.isEmpty()) {
        look.kind = ExitLookKindEnum::EMPTY;
        addReason(look.reasons, "empty");
    } else if (lines.front().startsWith(QStringLiteral("You see nothing special"))) {
        look.kind = ExitLookKindEnum::NOTHING;
    } else if (startsWithAny(lines.front(),
                             {"It is pitch black", "It's too dark", "It is too dark",
                              "You can't see", "You cannot see", "You are blind"})) {
        look.kind = ExitLookKindEnum::DARK;
    } else if (startsWithAny(lines.front(), {"Alas, you cannot go that way"})) {
        look.kind = ExitLookKindEnum::NO_EXIT;
    } else if (startsWithAny(lines.front(), {"Arglebargle", "Huh?!", "Sorry, but you cannot do that"})) {
        look.kind = ExitLookKindEnum::OTHER;
        addReason(look.reasons, "other");
    } else if (lines.size() == 1 && !look.door.isEmpty()) {
        look.kind = ExitLookKindEnum::DOOR;
    } else {
        look.kind = ExitLookKindEnum::DESCRIPTION;
    }
    look.uncertain = !look.reasons.isEmpty();
    return look;
}
