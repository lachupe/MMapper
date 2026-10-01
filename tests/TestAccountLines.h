#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestAccountLines final : public QObject
{
    Q_OBJECT

public:
    TestAccountLines() = default;
    ~TestAccountLines() override = default;

private Q_SLOTS:
    static void menuTest();
    static void menuMergeVariantTest();
    static void menuStrippedTagsTest();
    static void listNarrowTest();
    static void listWideTest();
    static void listInGameTest();
    static void listSubTest();
    static void listPagerTest();
    static void rowTest();
    static void repliesTest();
    static void notAccountTest();
};
