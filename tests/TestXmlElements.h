#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestXmlElements final : public QObject
{
    Q_OBJECT

public:
    TestXmlElements() = default;
    ~TestXmlElements() override = default;

private Q_SLOTS:
    static void tagNamesTest();
    static void simpleElementTest();
    static void nestedElementTest();
    static void attributesTest();
    static void selfClosingTest();
    static void unknownTagTest();
    static void strayCloseTest();
    static void mismatchedCloseTest();
    static void textOutsideElementsTest();
    static void multiLineElementTest();
    static void resetTest();
    static void depthLimitTest();
    static void textLimitTest();
    static void movementDirectionTest();
};
