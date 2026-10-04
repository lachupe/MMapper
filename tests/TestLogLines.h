#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestLogLines final : public QObject
{
    Q_OBJECT

public:
    TestLogLines() = default;
    ~TestLogLines() override = default;

private Q_SLOTS:
    static void logLinesTest();
};
