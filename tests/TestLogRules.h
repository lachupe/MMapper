#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestLogRules final : public QObject
{
    Q_OBJECT

public:
    TestLogRules() = default;
    ~TestLogRules() override = default;

private Q_SLOTS:
    // The rules (LogRules).
    static void templateTest();
    static void precedenceTest();
    static void whenTest();
    static void sunriseTest();
    static void hpDropTest();
    static void errorCodesTest();
    static void saveLoadTest();
    static void newerFileTest();
    static void suggestTest();
    static void performanceTest();
    static void shippedDefaultsTest();
    // The tags (LineTags), with the readers driven in MumeXmlParser::parse()'s order.
    static void roomTagsTest();
    static void promptTagsTest();
    static void blowTagsTest();
    static void shopAcrossPagerTest();
    static void statBlockTest();
    static void narrateJoinedTest();
    static void quietHiddenTest();
};
