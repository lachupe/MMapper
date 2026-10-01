#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestExitLooks final : public QObject
{
    Q_OBJECT

public:
    TestExitLooks() = default;
    ~TestExitLooks() override = default;

private Q_SLOTS:
    // MMapper.Room.Look
    static void directionTest();
    static void answerKindsTest();
    static void asyncInsideTest();
    static void asyncPromptTest();
    static void fightTest();
    static void otherCommandsTest();
    static void severalLooksTest();
    static void movedTest();
    static void roomDisplayTest();
    static void emptyAndLostTest();
};
