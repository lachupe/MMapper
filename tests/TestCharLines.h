#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestCharLines final : public QObject
{
    Q_OBJECT

public:
    TestCharLines() = default;
    ~TestCharLines() override = default;

private Q_SLOTS:
    static void statCurrentTest();
    static void statEmptyAffectsTest();
    static void statOldWordingTest();
    static void statVariantsTest();
    static void statProxyAdditionsTest();
    static void statBlankAndPromptTest();
    static void scoreLineTest();
    static void infoSheetTest();
    static void infoNoArmourTest();
    static void infoMissingArmourTest();
    static void infoOldWordingTest();
    static void infoHighestLevelTest();
    static void burdenTest();
    static void numberWordsTest();
    static void levelLineTest();
    static void notCharTest();
    static void resetTest();
};
