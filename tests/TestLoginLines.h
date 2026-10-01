#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestLoginLines final : public QObject
{
    Q_OBJECT

public:
    TestLoginLines() = default;
    ~TestLoginLines() override = default;

private Q_SLOTS:
    static void promptLinesTest();
    static void refusalLinesTest();
    static void loginTest();
    static void wrongPasswordTest();
    static void wrongNameTest();
    static void notLoginTest();
    static void resetTest();
};
