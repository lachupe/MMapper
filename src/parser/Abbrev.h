#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2019 The MMapper Authors
// Author: Nils Schimmelmann <nschimme@gmail.com> (Jahara)

#include <cassert>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <QCharRef>

#include "../global/StringView.h"

bool isAbbrev(StringView input, const char *const command, const int minAbbrev);

struct Abbrev
{
private:
    const char *command = nullptr;
    int minAbbrev = 0;
    int len = 0;

public:
    explicit Abbrev() = default;
    explicit Abbrev(std::nullptr_t) = delete;
    explicit Abbrev(std::nullptr_t, int) = delete;
    explicit Abbrev(const char *const arg_command, const int arg_minAbbrev = -1);

    bool matches(StringView input) const { return ::isAbbrev(input, command, minAbbrev); }
    explicit operator bool() const;
    const char *getCommand() const { return command; }
    int getMinAbbrev() const { return minAbbrev; }
    int getLength() const { return len; }
    QString describe() const;
};
