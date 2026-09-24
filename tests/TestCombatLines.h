#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestCombatLines final : public QObject
{
    Q_OBJECT

public:
    TestCombatLines() = default;
    ~TestCombatLines() override = default;

private Q_SLOTS:
    static void hitTest();
    static void hitOnYouTest();
    static void hitWithoutSeverityTest();
    static void possessiveWithoutSTest();
    static void groupLabelTest();
    static void parryTest();
    static void dodgeTest();
    static void missTest();
    static void fleeTest();
    static void refusedTest();
    static void bashTest();
    static void backstabTest();
    static void deathAndConditionTest();
    static void castTest();
    static void twiddlersTest();
    static void selfTest();
    static void notCombatTest();
};
