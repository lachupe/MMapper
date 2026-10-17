#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <optional>

#include <QString>
#include <QStringList>

/// MUME's answer to `consider <character>` (MMapper.Char.Consider): one line comparing the
/// player's level and the two sides' attack through the other's defence. Not hit points, special
/// attacks or damage (MUME's help, `consider`).
///
/// The line is one of a short set of sentences, matched whole; each is graded from the easiest
/// opponent to the deadliest. The sentences are the classic Diku ladder plus the one the user's
/// logs show ("The perfect match!", mume-logs elvenrunes 2023-2025); a rung nobody has seen live
/// is marked in CharConsider.cpp. The answer to `consider <object>` is a block of item text and
/// matches none of these lines.
struct NODISCARD CharConsider final
{
    /// A stable code, easiest first; see considerVerdicts().
    QString verdict;
    /// 0 (a pushover) upward, the verdict's place on the ladder, for a client that colours or
    /// sorts.
    int grade = 0;
    /// MUME's line as received, colour removed.
    QString text;
    /// What the player typed after `consider` ("orc", "2.orc"), when the command was seen.
    /// Empty when not known.
    QString target;
};

/// The `consider` command's target in a line the player sent ("con orc", "consider 2.orc"), or an
/// empty string for any other line.
NODISCARD QString considerTarget(const QString &sentLine);

/// The verdict `line` is (colour removed, matched whole), or nullopt. `target` is left empty.
NODISCARD std::optional<CharConsider> parseConsiderLine(const QString &line);

/// Every verdict code, easiest first, for the tests and the documentation.
NODISCARD QStringList considerVerdicts();
