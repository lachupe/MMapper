#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestLineReaders final : public QObject
{
    Q_OBJECT

public:
    TestLineReaders() = default;
    ~TestLineReaders() override = default;

private Q_SLOTS:
    // ResetReason and the readers' declarations
    static void everyReaderResetsOnDisconnectTest();
    static void resetOnlyWhereDeclaredTest();
    static void stateIsForgottenTest();
    // One command for each line sent
    static void splitSentCommandsTest();
    static void everyReaderSeesEveryLineTest();
    static void pagerAnswerIsNoCommandTest();
    // line_text
    static void twiddlersTest();
    static void sharedCleanersTest();
};
