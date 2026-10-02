#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "XmlElement.h"

#include <cstdint>
#include <vector>

#include <QString>

/// The quiet command: a line MMapper sends to MUME for a frontend, which nobody typed
/// (MMapper.Input.Quiet; TradeOperations runs it). Its reply is not the player's, so it is
/// kept out of the terminal and handed to the frontend instead. Every reader still reads it,
/// so the packages made from it (MMapper.Char.Skills from a `prac`, MMapper.Char.Stat from a
/// `stat`) come out as for a typed command.
///
/// MUME does not bracket its output by command, so what the reply is has to be told from the
/// stream. The window opens when the command has reached MUME and closes at the first real
/// prompt after a line of the reply. Inside it, MUME's XML mode says which lines are other
/// traffic: a room display, speech, a movement, a blow, magic, the weather. Those are shown as
/// ever and are no part of the reply. Plain text is the reply. This knows nothing of any
/// particular command.

/// Whether `line` may be sent as a quiet command. A command the player never sees must not act:
/// only the commands of the table in QuietCapture.cpp are let through, which read and change
/// nothing. `line` is taken as MUME would take it: trimmed, its first word looked up without
/// regard to case, in full or shortened as far as MUME's help says that command may be (so
/// `pr` ... `practice`, and never a prefix MUME gives to another command); what follows the
/// word is the command's arguments, which some of the commands must not have. False as well
/// for an empty line, one with a line break or any other control character, and one with a
/// `;` in it, which a client may take to chain commands: such a line is refused whole.
NODISCARD bool quietCommandAllowed(const QString &line);

/// What the runner tells the parser about its quiet command.
enum class NODISCARD QuietCommandEnum : uint8_t {
    /// The command has reached MUME: the window opens.
    BEGIN,
    /// Another line has gone to MUME since: its reply comes after the quiet command's own
    /// prompt, so the next real prompt closes the window whatever came before it.
    FOREIGN,
    /// The request is over, however it ended: nothing more is hidden.
    END
};

/// What a line is to the window, as far as MUME's markup tells.
enum class NODISCARD QuietTrafficEnum : uint8_t {
    /// Plain text outside every element that marks other traffic: the reply.
    REPLY,
    /// Inside an element of other traffic, or wholly covered by one.
    OTHER,
    /// Neither can be said: the fight reader took a line no element marks, an element of other
    /// traffic covers only part of the line, or the element is one whose meaning is not known.
    UNSURE
};

/// Tells what one line of MUME's output is. `inRoom` is true while a room display is being
/// read; `combatProse` when the fight reader made an event of the line; `line` is the line,
/// colour removed and trimmed; `closed` are the elements that closed on it, and `open` the tags
/// of those still open after it, outermost first.
NODISCARD QuietTrafficEnum quietTrafficOf(bool inRoom,
                                          bool combatProse,
                                          const QString &line,
                                          const std::vector<XmlElement> &closed,
                                          const std::vector<XmlTagEnum> &open);

/// The window of one quiet command: decides for each chunk whether the terminal is sent it and
/// whether it is part of the reply. Fed by TradeReaders, which knows a pager from a prompt.
///
/// - A REPLY line is hidden and captured. A blank line is too, unless it is OTHER.
/// - An OTHER line is shown and not captured.
/// - An UNSURE line is shown and captured: nothing of the player's is lost, and the frontend
///   has it if it was the reply's.
/// - MUME's pager is hidden once a line of the reply came, which is what tells the runner to
///   answer it; before that it is in a reply of somebody else's, and shown.
/// - A real prompt before any line of the reply is the prompt of something else (other
///   traffic, a prompt already on its way): shown, and the window stays open. But once another
///   line has gone to MUME (FOREIGN), the next real prompt closes the window in any case: the
///   reply after it is that line's.
/// - The real prompt after a line of the reply closes the window. It is hidden, unless a line
///   was shown since the last prompt that was: the terminal must not be left without one.
///
/// What can be hidden wrongly is plain text that MUME sends, unasked and unmarked, between the
/// command reaching MUME and the reply's prompt; the runner bounds how long that can be.
class NODISCARD QuietCapture final
{
public:
    struct NODISCARD Verdict final
    {
        /// Not sent to the terminal.
        bool hidden = false;
        /// Part of the reply.
        bool captured = false;
        /// This chunk, a real prompt, closed the window.
        bool ended = false;
    };

private:
    bool m_open = false;
    /// A line of the reply that is not blank has been captured.
    bool m_replied = false;
    /// A line or a pager has been shown since the last prompt that was shown.
    bool m_shown = false;
    /// Another line has gone to MUME since the window opened.
    bool m_foreign = false;

public:
    void receiveCommand(QuietCommandEnum what);
    NODISCARD bool isOpen() const { return m_open; }
    /// Whether a line of the reply has come.
    NODISCARD bool hasReplied() const { return m_open && m_replied; }

    /// A line that ends in a newline; `plain` is its text with colour removed.
    NODISCARD Verdict receiveLine(const QString &plain, QuietTrafficEnum traffic);
    /// MUME's pager line.
    NODISCARD Verdict receivePager();
    /// A real prompt.
    NODISCARD Verdict receivePrompt();
    void reset();
};
