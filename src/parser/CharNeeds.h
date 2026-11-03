#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <optional>

#include <QString>

/// A line of MUME's that turns the player's hunger or thirst on or off (MMapper.Char.Needs).
///
/// MUME says the needs only in words: each tick while the character needs food or drink ("You
/// begin to feel hungry.", "You are hungry.", the same for thirsty), and the lines of eating and
/// drinking end them ("You eat a large biscuit.", "You are full.", "You drink the water.", "You
/// do not feel thirsty."). GMCP Group's own member carries hungry/thirsty only at login, and
/// `stat`'s condition comes only when asked.
struct NODISCARD CharNeedLine final
{
    /// "hungry" or "thirsty".
    QString need;
    /// True for a line that says the need is on, false for one that ends it.
    bool on = false;
};

/// The need `line` (colour removed, matched whole) turns on or off, or nullopt.
NODISCARD std::optional<CharNeedLine> parseNeedLine(const QString &line);

/// The needs as the lines last said them; a need no line has spoken of is unknown, which is not
/// "not needed". Free of Qt networking types.
class NODISCARD CharNeedsTracker final
{
public:
    /// -1 when no line has said it, 0 off, 1 on.
    int hungry = -1;
    int thirsty = -1;

public:
    /// True when the state changed, or the need was told for the first time.
    NODISCARD bool receive(const CharNeedLine &line);
    NODISCARD bool known() const { return hungry >= 0 || thirsty >= 0; }
    void reset();
};
