#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include <QString>

/// What MUME is asking for at its login, so that a frontend can ask in a window of its own
/// instead of in the terminal. MMapper.Session.State publishes it as `login`.
///
/// MUME's own lines, from the powwow logs (each prompt ends without a newline, on a GO-AHEAD):
///
///   By what name do you wish to be known?               (the account's or a character's name)
///   Account pass phrase:                                (echo off: the pass phrase)
///   Password:  /  Character password:  /  Please enter your account password:
///                                                       (older MUME, a character's own login)
///
/// and its refusals, each followed by the prompt it sends the player back to:
///
///   Wrong password.                                     (then "Account pass phrase:" again)
///   Wrong password buddy, try again!                    (older, then "Password:")
///   No character or account by that name.               (then the name prompt again)
///   Type NEW if you have no other characters on MUME.   (its second line)
///   Illegal name, please try another.                   (then the name prompt again)
///
/// No single log is the specification, so the match is loose (case ignored, the tail of the line
/// read), and anything not known here simply ends the prompt: the terminal still shows it.
///
/// What the player answers is never seen here. The pass phrase in particular is typed while MUME
/// has echo off, and MMapper reports no line sent then (Proxy: observeSentToMud()).
enum class NODISCARD LoginPromptKindEnum : uint8_t {
    /// MUME is not waiting at a login prompt, as far as its lines tell.
    NONE,
    /// MUME asks for the name: an account's or a character's.
    NAME,
    /// MUME asks for the pass phrase (the password) of the name just given.
    PASSWORD,
};

/// The protocol's name for `kind`: "none", "name" or "password".
NODISCARD std::string_view loginPromptKindName(LoginPromptKindEnum kind);

/// The login prompt MUME waits at, with the refusal it printed before it, if any.
struct NODISCARD LoginPrompt final
{
    LoginPromptKindEnum kind = LoginPromptKindEnum::NONE;
    /// The prompt as MUME printed it, trimmed: "Account pass phrase:".
    QString text;
    /// Why MUME asks again: "wrong-password", "no-such-name" or "illegal-name"; empty the first
    /// time it asks.
    QString refusedReason;
    /// MUME's own words for that, its lines joined by a space.
    QString refusedText;
    /// One more for every login prompt MUME has printed since MMapper started, so that a
    /// frontend can tell the prompt printed again (after a refusal, the same kind and text)
    /// from the state merely sent again. 0 with kind NONE.
    int serial = 0;

    NODISCARD bool operator==(const LoginPrompt &other) const
    {
        return kind == other.kind && text == other.text && refusedReason == other.refusedReason
               && refusedText == other.refusedText && serial == other.serial;
    }
    NODISCARD bool operator!=(const LoginPrompt &other) const { return !(*this == other); }
};

/// The kind of login prompt `line` ends in, or NONE. `line` is the line as the user sees it,
/// colour removed. A prompt MUME ended without a GO-AHEAD reaches the parser glued to the front
/// of the next ("By what name do you wish to be known? Account pass phrase: "), so the last
/// prompt on the line is the one that counts.
NODISCARD LoginPromptKindEnum parseLoginPromptLine(const QString &line);

/// The reason of the login refusal `line` is ("wrong-password", "no-such-name",
/// "illegal-name"), or an empty string.
NODISCARD QString parseLoginRefusalLine(const QString &line);

/// Follows MUME's login from its lines: which prompt it waits at, and the refusal before it.
///
/// A pass phrase prompt counts only right after a name prompt or another pass phrase prompt
/// (blank lines and refusals between them allowed), so that the account menu's own questions
/// about a pass phrase, or a line that merely ends in "password:", are not taken for a login.
/// The caller feeds it every chunk of MUME's output, prompts too, while no character is in the
/// game, and resets it when MUME connects or disconnects and when a character enters the game.
class NODISCARD LoginLinesTracker final
{
private:
    LoginPrompt m_prompt;
    QString m_refusedReason;
    QString m_refusedText;
    /// LoginPrompt::serial of the last prompt reported; reset() keeps it, so it never repeats.
    int m_serial = 0;

public:
    /// What `line` changes: the prompt MUME now waits at (also when it is the same prompt
    /// again, which is how a refusal is told), a prompt of kind NONE when the login is over or
    /// went somewhere unknown, or nothing.
    NODISCARD std::optional<LoginPrompt> receiveLine(const QString &line);

    /// Forgets the login, and the refusal waiting for its prompt.
    void reset();

    NODISCARD const LoginPrompt &prompt() const { return m_prompt; }
};
