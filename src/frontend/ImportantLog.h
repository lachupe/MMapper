#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/RuleOf5.h"
#include "../global/macros.h"
#include "../parser/LineTags.h"
#include "../proxy/GmcpMessage.h"
#include "LogRules.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTimer>

/// The prioritised Log (MMapper.Log, spec section 37): ranks every line MMapper read from MUME
/// (GameObserver::sig2_lineFacts) by the rules (LogRuleSet) and publishes the lines above 0 as
/// MMapper.Log.Line, one package per line.
///
/// Lines are gathered per prompt window and ranked when it closes -- at the real prompt, at
/// MUME's pager, or 250 ms after its first line when neither came -- so that `hpDrop` sees the
/// whole window. The situation is taken from what MMapper already has: the race from
/// Char.StatusVars, the hit points from Char.Vitals, the group from Group.*, who is in the room
/// from Room.Chars.*, a fight from the blows by or at the player in the last 8 seconds, and
/// whether the player is outdoors from the map's sundeath flag of the current room.
///
/// Frontends edit the user's rules with MMapper.Log.SetRule and .DeleteRule (only the driving
/// one) and ask why a line ranked as it did with MMapper.Log.Explain; MMapper keeps the last
/// 1000 ranked lines for that. Testable without a socket: what goes to every subscriber goes
/// through `publish`, and handle() returns what goes to the frontend that asked.
class NODISCARD ImportantLog final
{
public:
    static constexpr const int WINDOW_MS = 250;
    static constexpr const int64_t FIGHT_MS = 8000;
    static constexpr const size_t KEPT_LINES = 1000;

    using Publish = std::function<void(const GmcpMessage &)>;
    using Outdoors = std::function<bool()>;
    using NowMs = std::function<int64_t()>;

    /// What a request gives: the packages for the frontend that sent it, and whether the rules
    /// changed (MMapper.Log.Rules then goes to every subscriber: rulesMessage()).
    struct NODISCARD Reply final
    {
        std::vector<GmcpMessage> toClient;
        bool rulesChanged = false;
    };

private:
    struct NODISCARD Ranked final
    {
        QString plain;
        LineTagSet tags;
        QStringList names;
        int priority = LogRuleSet::UNKNOWN_PRIORITY;
        QString route;
        QString rule;
    };
    struct NODISCARD Member final
    {
        QString name;
        bool self = false;
    };

    Publish m_publish;
    Outdoors m_outdoors;
    NowMs m_nowMs;
    LogRuleSet m_rules;
    std::vector<LineFacts> m_window;
    QTimer m_timer;
    std::deque<Ranked> m_recent;
    int64_t m_seq = 0;

    QString m_race;
    std::optional<double> m_hp;
    std::optional<double> m_maxhp;
    std::optional<double> m_hpAtPrompt;
    std::optional<int64_t> m_lastFightMs;
    std::map<int64_t, Member> m_group;
    std::map<int64_t, QString> m_roomChars;

public:
    explicit ImportantLog(Publish publish);
    ~ImportantLog();
    DELETE_CTORS_AND_ASSIGN_OPS(ImportantLog);

public:
    /// Whether the current room is outdoors (not no-sundeath); outdoors when not set.
    void setOutdoors(Outdoors outdoors) { m_outdoors = std::move(outdoors); }
    /// The clock, for tests.
    void setNowMs(NowMs nowMs) { m_nowMs = std::move(nowMs); }
    /// The shipped defaults, `:/log/rules.json` unless given.
    void loadDefaults();
    void loadDefaults(const QByteArray &json);
    void loadUserFile(const QString &path) { m_rules.loadUserFile(path); }
    NODISCARD const LogRuleSet &rules() const { return m_rules; }

    void receiveFacts(const LineFacts &facts);
    /// Char.StatusVars, Char.Vitals, Group.*, Room.Chars.*; anything else is ignored.
    void receiveGmcp(const GmcpMessage &msg);
    /// The real prompt: closes the window, and the hit points now are what the next window's
    /// `hpDrop` is measured from.
    void receivePrompt();
    /// MUME's pager closes the window too.
    void receivePager() { flush(); }
    /// Ranks and publishes the window.
    void flush();
    /// A new connection: the window, the fight, the group and the room are forgotten.
    void reset();

    /// MMapper.Log.SetRule, .DeleteRule and .Explain; `driving` says whether the frontend that
    /// sent it drives the session.
    NODISCARD Reply handle(const GmcpMessage &msg, bool driving);
    NODISCARD static bool handles(const GmcpMessage &msg);
    NODISCARD GmcpMessage rulesMessage() const;

    /// The situation as it is now (for tests and Explain).
    NODISCARD LogContext context() const;

private:
    NODISCARD QString aboutOf(const LineFacts &facts) const;
    NODISCARD QStringList knownNames() const;
    NODISCARD const Ranked *findRecent(const QString &plain) const;
    NODISCARD Reply explain(const QJsonObject &payload) const;
};
