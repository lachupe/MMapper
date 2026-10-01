#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

#include <QString>
#include <QStringList>

/// MUME's answers to `look <direction>`, paired with the looks that asked them
/// (MMapper.Room.Look).
///
/// mume3d's room reading wants what MUME says along each exit, which the map does not keep. The
/// player sends the looks -- typed, from an alias, or with `_lookexits`, which sends one for each
/// exit Room.Info listed -- from any client; MMapper sees every line on its way to MUME and every
/// line back, so it pairs them here and a frontend, driving or observing, only records them.
///
/// PAIRING. MUME answers the lines it is sent in order, each answer ending with a prompt. So the
/// tracker keeps one entry per line sent and not yet answered, and each prompt closes the oldest.
/// But MUME also sends text nobody asked for -- a narrate, a tell, someone arriving, a blow, the
/// weather -- sometimes inside an answer and sometimes with a prompt of its own
/// (log-2006.05.06-20.16.22.txt:827-829: a narrate, then its own prompt). The caller says what
/// each line is, from MUME's XML markup around it (LineKindEnum), and:
///
///   - an asynchronous line inside an answer is left out of the look's text, and kept in `raw`
///     and `dropped` (log-2005.09.03-02.28.34.txt:11384-11388: a say inside a look's answer);
///   - a window between two prompts that holds asynchronous lines only (no line of an answer)
///     closes nothing: it is MUME's prompt after the narrate, not the answer to a line sent;
///     the look waiting first is marked uncertain ("extra-prompt");
///   - a fight's lines are left out the same way, and the look is marked uncertain ("fight");
///   - a room display inside the window (the player moved, or looked at the room) is left out
///     and marks the look uncertain ("room-display"); another room in Room.Info while a look
///     waits marks it "moved";
///   - an answer with no line left ("empty"), or one MUME did not take as a look ("other"), is
///     uncertain too.
///
/// Uncertain looks are still published, with every line seen, so that the reader decides and the
/// player can look again. Nothing here reads MMapper's map: the direction is the player's own.
enum class NODISCARD LineKindEnum : uint8_t {
    /// Part of an answer, as far as the markup tells.
    TEXT,
    /// Speech, a movement, magic or the weather: MUME's own, asked by no line.
    ASYNC,
    /// A blow or anything of a fight.
    COMBAT,
    /// A line of a room display.
    ROOM
};

enum class NODISCARD ExitLookKindEnum : uint8_t {
    /// What lies that way, in MUME's words (a door's line may follow, in `door`).
    DESCRIPTION,
    /// "You see nothing special there..."
    NOTHING,
    /// Only a door's state: "The gate is closed.", "A broken door."
    DOOR,
    /// Too dark, or the character cannot see.
    DARK,
    /// "Alas, you cannot go that way..."
    NO_EXIT,
    /// No line was left of the answer.
    EMPTY,
    /// Something MUME said that is no answer to a look ("Arglebargle, glop-glyf!?!").
    OTHER
};
NODISCARD std::string_view to_string_view(ExitLookKindEnum kind);

/// One MMapper.Room.Look.
struct NODISCARD ExitLook final
{
    /// MUME's id of the room the look was sent in, from Room.Info; unset when it gave none.
    std::optional<int64_t> room;
    /// The direction as a word: north, east, south, west, up, down.
    QString dir;
    /// The line as it was sent ("l n").
    QString command;
    ExitLookKindEnum kind = ExitLookKindEnum::EMPTY;
    /// The answer, its lines joined with "\n", the asynchronous ones left out.
    QString text;
    QStringList lines;
    /// The door the answer named ("The gate is closed." gives gate), lowercase as MUME wrote it,
    /// and its state: open, closed or broken. Empty when the answer named none.
    QString door;
    QString doorState;
    bool uncertain = false;
    /// Why it is uncertain: extra-prompt, fight, room-display, moved, empty, other, lost.
    QStringList reasons;
    /// The lines left out of `text`.
    QStringList dropped;
    /// Every line seen from the look's sending to its answer's prompt, in order.
    QStringList raw;

    NODISCARD bool operator==(const ExitLook &other) const = default;
};

/// The direction word a line sent to MUME looks in: "l n", "look north", "lo e"; empty for any
/// other line (`look`, `look door`, `examine north`).
NODISCARD QString lookedDirection(const QString &input);

/// Pairs the looks with MUME's answers. Free of the map, of Qt's networking types and of the
/// clock.
class NODISCARD ExitLookTracker final
{
private:
    struct NODISCARD Pending final
    {
        QString command;
        /// The look's direction word; empty for any other line, which is only counted.
        QString dir;
        std::optional<int64_t> room;
        int prompts = 0;
        QStringList raw;
        QStringList kept;
        QStringList dropped;
        QStringList reasons;
    };
    std::deque<Pending> m_pending;
    std::optional<int64_t> m_room;
    /// The lines since the last prompt, with what each is.
    std::vector<std::pair<QString, LineKindEnum>> m_window;

public:
    /// The most lines kept waiting for their prompt; older ones are given up.
    static constexpr size_t MAX_PENDING = 24;
    /// A look whose answer has not come after so many windows was lost track of.
    static constexpr int MAX_PROMPTS = 6;

    /// Room.Info's id: the room the next looks are sent in.
    void receiveRoom(std::optional<int64_t> room);
    /// A line on its way to MUME, whoever sent it.
    void receiveCommand(const QString &input);
    /// One line of MUME's output, colour removed, and what the markup says it is.
    void receiveLine(const QString &line, LineKindEnum kind);
    /// MUME's prompt: the looks it answered.
    NODISCARD std::vector<ExitLook> receivePrompt();
    /// Whether any look waits for its answer.
    NODISCARD bool waiting() const;
    void reset();

private:
    NODISCARD static ExitLook finish(const Pending &pending);
};
