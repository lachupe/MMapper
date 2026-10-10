// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "ImportantLog.h"

#include "FrontendMessages.h"

#include <utility>

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>

namespace {

NODISCARD QJsonObject payloadOf(const GmcpMessage &msg)
{
    const auto &json = msg.getJson();
    if (!json.has_value()) {
        return QJsonObject{};
    }
    return QJsonDocument::fromJson(json->toQByteArray()).object();
}

NODISCARD bool isObjectPayload(const GmcpMessage &msg)
{
    const auto &json = msg.getJson();
    return json.has_value() && QJsonDocument::fromJson(json->toQByteArray()).isObject();
}

NODISCARD QString aboutName(const LineAboutEnum about)
{
    switch (about) {
    case LineAboutEnum::YOU:
        return QStringLiteral("you");
    case LineAboutEnum::GROUP:
        return QStringLiteral("group");
    case LineAboutEnum::UNKNOWN:
        break;
    }
    return QStringLiteral("other");
}

NODISCARD QStringList starredNames(const QString &plain)
{
    static const QRegularExpression starred{QStringLiteral(R"(\*([^*\s][^*]*)\*)")};
    QStringList out;
    auto it = starred.globalMatch(plain);
    while (it.hasNext()) {
        out.append(it.next().captured(0));
    }
    return out;
}

} // namespace

ImportantLog::ImportantLog(Publish publish)
    : m_publish{std::move(publish)}
    , m_nowMs{[]() { return QDateTime::currentMSecsSinceEpoch(); }}
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(WINDOW_MS);
    QObject::connect(&m_timer, &QTimer::timeout, [this]() { flush(); });
}

ImportantLog::~ImportantLog() = default;

void ImportantLog::loadDefaults()
{
    QFile file{QStringLiteral(":/log/rules.json")};
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[log] no default rules in the resources";
        return;
    }
    loadDefaults(file.readAll());
}

void ImportantLog::loadDefaults(const QByteArray &json)
{
    QString error;
    if (!m_rules.loadDefaults(json, &error)) {
        qWarning() << "[log] the default rules cannot be read:" << error;
    }
}

void ImportantLog::receiveFacts(const LineFacts &facts)
{
    if (facts.tags.contains(LineTagEnum::COMBAT_BLOW) && facts.about == LineAboutEnum::YOU) {
        m_lastFightMs = m_nowMs();
    }
    m_window.push_back(facts);
    if (facts.tags.contains(LineTagEnum::PAGER)) {
        flush();
        return;
    }
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void ImportantLog::receivePrompt()
{
    flush();
    m_hpAtPrompt = m_hp;
}

void ImportantLog::receiveGmcp(const GmcpMessage &msg)
{
    const auto &optDoc = msg.getJsonDocument();
    if (!optDoc.has_value()) {
        return;
    }
    const auto readMember = [this](const JsonObj &obj) {
        const auto id = obj.getInt("id");
        if (!id.has_value()) {
            return;
        }
        Member &member = m_group[static_cast<int64_t>(*id)];
        if (const auto name = obj.getString("name")) {
            member.name = *name;
        }
        if (const auto type = obj.getString("type")) {
            member.self = *type == QStringLiteral("you");
        }
    };
    const auto readChar = [this](const JsonObj &obj) {
        const auto id = obj.getInt("id");
        if (!id.has_value()) {
            return;
        }
        if (const auto name = obj.getString("name")) {
            m_roomChars[static_cast<int64_t>(*id)] = *name;
        }
    };

    if (msg.isCharStatusVars()) {
        if (const auto obj = optDoc->getObject()) {
            if (const auto race = obj->getString("race")) {
                m_race = race->trimmed().toLower();
            }
        }
    } else if (msg.isCharVitals()) {
        if (const auto obj = optDoc->getObject()) {
            if (const auto hp = obj->getDouble("hp")) {
                m_hp = *hp;
            }
            if (const auto maxhp = obj->getDouble("maxhp")) {
                m_maxhp = *maxhp;
            }
        }
    } else if (msg.isGroupSet()) {
        m_group.clear();
        if (const auto array = optDoc->getArray()) {
            for (const auto &entry : *array) {
                if (const auto obj = entry.getObject()) {
                    readMember(*obj);
                }
            }
        }
    } else if (msg.isGroupAdd() || msg.isGroupUpdate()) {
        if (const auto obj = optDoc->getObject()) {
            readMember(*obj);
        }
    } else if (msg.isGroupRemove()) {
        if (const auto id = optDoc->getInt()) {
            m_group.erase(static_cast<int64_t>(*id));
        }
    } else if (msg.isRoomCharsSet()) {
        m_roomChars.clear();
        if (const auto array = optDoc->getArray()) {
            for (const auto &entry : *array) {
                if (const auto obj = entry.getObject()) {
                    readChar(*obj);
                }
            }
        }
    } else if (msg.isRoomCharsAdd() || msg.isRoomCharsUpdate()) {
        if (const auto obj = optDoc->getObject()) {
            readChar(*obj);
        }
    } else if (msg.isRoomCharsRemove()) {
        if (const auto id = optDoc->getInt()) {
            m_roomChars.erase(static_cast<int64_t>(*id));
        }
    }
}

LogContext ImportantLog::context() const
{
    LogContext context;
    context.inFight = m_lastFightMs.has_value() && m_nowMs() - *m_lastFightMs <= FIGHT_MS;
    for (const auto &[id, member] : m_group) {
        context.grouped = context.grouped || !member.self;
    }
    // Outdoors unless the map says the room is no-sundeath: a troll is warned when in doubt.
    context.outdoors = m_outdoors ? m_outdoors() : true;
    context.race = m_race;
    if (m_hp.has_value() && m_maxhp.has_value() && *m_maxhp > 0) {
        context.hpFraction = *m_hp / *m_maxhp;
        if (m_hpAtPrompt.has_value()) {
            context.hpDrop = (*m_hpAtPrompt - *m_hp) / *m_maxhp;
        }
    }
    return context;
}

QString ImportantLog::aboutOf(const LineFacts &facts) const
{
    if (facts.about != LineAboutEnum::UNKNOWN) {
        return aboutName(facts.about);
    }
    for (const auto &[id, member] : m_group) {
        if (!member.self && !member.name.isEmpty() && facts.names.contains(member.name)) {
            return QStringLiteral("group");
        }
    }
    return QStringLiteral("other");
}

void ImportantLog::flush()
{
    m_timer.stop();
    if (m_window.empty()) {
        return;
    }
    const LogContext situation = context();
    std::vector<LineFacts> window = std::exchange(m_window, {});
    for (const LineFacts &facts : window) {
        LogLine line;
        line.plain = facts.plain;
        line.tags = facts.tags;
        line.about = aboutOf(facts);
        const LogResult result = m_rules.classify(line, situation);
        const QString route = logRouteName(result.route);
        const QString ruleId = result.rule != nullptr ? result.rule->id : QString{};
        ++m_seq;

        m_recent.push_back(
            Ranked{facts.plain, facts.tags, facts.names, result.priority, route, ruleId});
        while (m_recent.size() > KEPT_LINES) {
            m_recent.pop_front();
        }
        if (result.priority <= 0) {
            continue;
        }
        m_publish(frontend_messages::makeLogLine(m_seq,
                                                 facts.text,
                                                 facts.plain,
                                                 result.priority,
                                                 route,
                                                 facts.tags.names(),
                                                 line.about,
                                                 ruleId));
    }
}

void ImportantLog::reset()
{
    m_timer.stop();
    m_window.clear();
    m_lastFightMs.reset();
    m_group.clear();
    m_roomChars.clear();
    m_hp.reset();
    m_maxhp.reset();
    m_hpAtPrompt.reset();
}

bool ImportantLog::handles(const GmcpMessage &msg)
{
    return msg.isMMapperLogSetRule() || msg.isMMapperLogDeleteRule() || msg.isMMapperLogExplain();
}

GmcpMessage ImportantLog::rulesMessage() const
{
    return frontend_messages::makeLogRules(m_rules.toRulesJson());
}

QStringList ImportantLog::knownNames() const
{
    QStringList names;
    for (const auto &[id, member] : m_group) {
        names.append(member.name);
    }
    for (const auto &[id, name] : m_roomChars) {
        names.append(name);
    }
    return names;
}

const ImportantLog::Ranked *ImportantLog::findRecent(const QString &plain) const
{
    const QString wanted = plain.trimmed();
    for (auto it = m_recent.rbegin(); it != m_recent.rend(); ++it) {
        if (it->plain == wanted) {
            return &*it;
        }
    }
    return nullptr;
}

ImportantLog::Reply ImportantLog::explain(const QJsonObject &payload) const
{
    Reply reply;
    const QJsonValue textValue = payload.value(QStringLiteral("text"));
    if (!textValue.isString()) {
        reply.toClient.push_back(
            frontend_messages::makeError(QStringLiteral("invalid-log"),
                                         QStringLiteral(
                                             "MMapper.Log.Explain needs a 'text' string")));
        return reply;
    }
    const QString text = textValue.toString();
    QStringList names = knownNames();
    if (const Ranked *ranked = findRecent(text)) {
        names = ranked->names + names + starredNames(ranked->plain);
        reply.toClient.push_back(
            frontend_messages::makeLogExplained(text,
                                                suggestLogTemplate(ranked->plain, names),
                                                ranked->tags.names(),
                                                ranked->names,
                                                ranked->priority,
                                                ranked->route,
                                                ranked->rule));
        return reply;
    }
    // Older than the lines kept: what the text rules make of it alone.
    LogLine line;
    line.plain = text.trimmed();
    const LogResult result = m_rules.classify(line, context());
    reply.toClient.push_back(
        frontend_messages::makeLogExplained(text,
                                            suggestLogTemplate(line.plain,
                                                               names + starredNames(line.plain)),
                                            QStringList{},
                                            QStringList{},
                                            result.priority,
                                            logRouteName(result.route),
                                            result.rule != nullptr ? result.rule->id : QString{}));
    return reply;
}

ImportantLog::Reply ImportantLog::handle(const GmcpMessage &msg, const bool driving)
{
    Reply reply;
    const QJsonObject payload = payloadOf(msg);
    if (msg.isMMapperLogExplain()) {
        // Reads only: any frontend may ask.
        return explain(payload);
    }
    const QString requestId = payload.value(QStringLiteral("requestId")).toString();
    const auto refuse = [&reply, &requestId](const QString &code, const QString &message) {
        reply.toClient.push_back(frontend_messages::makeLogRuleSaved(requestId,
                                                                     false,
                                                                     QString{},
                                                                     code,
                                                                     message,
                                                                     std::nullopt));
    };
    if (!driving) {
        // The answer to the request, which the frontend can pair with it, and the error every
        // other observer's input gets.
        refuse(QStringLiteral("read-only"),
               QStringLiteral("Another client owns the session; this connection may only "
                              "observe it"));
        reply.toClient.push_back(
            frontend_messages::makeError(QStringLiteral("read-only"),
                                         QStringLiteral("Another client owns the session; this "
                                                        "connection may only observe it")));
        return reply;
    }
    if (!isObjectPayload(msg)) {
        refuse(QStringLiteral("invalid-rule"),
               QStringLiteral("'%1' needs an object payload").arg(msg.getName().toQString()));
        return reply;
    }

    LogRuleSet::Edit edit;
    std::optional<int> samplePriority;
    if (msg.isMMapperLogSetRule()) {
        const QJsonValue rule = payload.value(QStringLiteral("rule"));
        if (!rule.isObject()) {
            refuse(QStringLiteral("invalid-rule"),
                   QStringLiteral("MMapper.Log.SetRule needs a 'rule' object"));
            return reply;
        }
        edit = m_rules.setRule(rule.toObject(),
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        const QString sample = payload.value(QStringLiteral("sample")).toString();
        if (edit.ok && !sample.trimmed().isEmpty()) {
            LogLine line;
            line.plain = sample.trimmed();
            if (const Ranked *ranked = findRecent(sample)) {
                line.tags = ranked->tags;
            }
            samplePriority = m_rules.classify(line, context()).priority;
        }
    } else {
        const QJsonValue id = payload.value(QStringLiteral("id"));
        if (!id.isString() || id.toString().isEmpty()) {
            refuse(QStringLiteral("invalid-rule"),
                   QStringLiteral("MMapper.Log.DeleteRule needs an 'id' string"));
            return reply;
        }
        edit = m_rules.deleteRule(id.toString());
    }
    reply.toClient.push_back(frontend_messages::makeLogRuleSaved(requestId,
                                                                 edit.ok,
                                                                 edit.id,
                                                                 edit.error,
                                                                 edit.message,
                                                                 samplePriority));
    reply.rulesChanged = edit.ok;
    return reply;
}
