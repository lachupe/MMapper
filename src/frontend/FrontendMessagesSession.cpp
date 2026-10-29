// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/TextUtils.h"
#include "../global/parserutils.h"
#include "../map/ExitDirection.h"
#include "../map/RoomFingerprint.h"
#include "../map/coordinate.h"
#include "../map/mmapper2room.h"
#include "../map/roomid.h"
#include "FrontendJson.h"
#include "FrontendMessages.h"

#include <algorithm>
#include <array>
#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>

using frontend_json::maybeBool;
using frontend_json::orNull;
using frontend_json::putNumber;
using frontend_json::putRoomRef;
using frontend_json::putText;
using frontend_json::roomRef;
using frontend_json::toGmcpJson;

namespace {

NODISCARD QJsonValue idOrNull(const QString &id)
{
    return id.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{id};
}

} // namespace

namespace frontend_messages {

std::string_view toProtocolString(const SendToUserSourceEnum source)
{
    switch (source) {
    case SendToUserSourceEnum::FromMud:
        return "mud";
    case SendToUserSourceEnum::DuplicatePrompt:
        return "duplicate-prompt";
    case SendToUserSourceEnum::SimulatedPrompt:
        return "simulated-prompt";
    case SendToUserSourceEnum::SimulatedOutput:
        return "simulated-output";
    case SendToUserSourceEnum::FromMMapper:
        return "mmapper";
    case SendToUserSourceEnum::NoLongerPrompted:
        return "no-longer-prompted";
    }
    return "unknown";
}

bool applyTerminalFilter(const GmcpMessage &msg, TerminalFilter &filter)
{
    const auto &optJson = msg.getJson();
    const QJsonDocument doc = optJson.has_value() ? QJsonDocument::fromJson(optJson->toQByteArray())
                                                  : QJsonDocument{};
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    TerminalFilter result = filter;
    if (const QJsonValue value = obj.value("roomDescriptions"); !value.isUndefined()) {
        if (!value.isBool()) {
            return false;
        }
        result.roomDescriptions = value.toBool();
    }
    filter = result;
    return true;
}

GmcpMessage makeTerminalFilter(const TerminalFilter &filter)
{
    QJsonObject obj;
    obj["roomDescriptions"] = filter.roomDescriptions;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TERMINAL_FILTER, toGmcpJson(obj)};
}

std::string_view toProtocolString(const TerminalHiddenEnum kind)
{
    switch (kind) {
    case TerminalHiddenEnum::ROOM_DESCRIPTION:
        return "room.description";
    case TerminalHiddenEnum::CHAR_LEVEL:
        return "char.level";
    case TerminalHiddenEnum::CHAR_ABILITIES:
        return "char.abilities";
    }
    return "unknown";
}

GmcpMessage makeTerminalHidden(const TerminalHidden &hidden)
{
    QJsonObject obj;
    obj["kind"] = mmqt::toQStringUtf8(toProtocolString(hidden.kind));
    obj["text"] = hidden.text;
    if (hidden.roomId.has_value()) {
        obj["roomId"] = static_cast<qint64>(*hidden.roomId);
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TERMINAL_HIDDEN, toGmcpJson(obj)};
}

bool wantsTerminalHidden(const TerminalHidden &hidden, const TerminalFilter &filter)
{
    return hidden.kind != TerminalHiddenEnum::ROOM_DESCRIPTION || filter.roomDescriptions;
}

std::optional<GmcpMessage> makeTerminalOutput(const TerminalOutput &out,
                                              const TerminalFilter &filter)
{
    if (!filter.roomDescriptions || out.roomDescription.empty()) {
        return makeTerminalOutput(out.source, out.text, out.goAhead);
    }
    QString rest;
    qsizetype at = 0;
    for (const TerminalSpan &span : out.roomDescription) {
        // Cut to the text, and to what is left of it: the parts are the parser's, in order.
        const qsizetype size = out.text.size();
        const qsizetype start = std::clamp<qsizetype>(span.start, at, size);
        const qsizetype end = std::clamp<qsizetype>(span.start + span.length, start, size);
        rest += out.text.mid(at, start - at);
        at = end;
    }
    rest += out.text.mid(at);
    // The description filled the chunk: what is left is its line end, and perhaps colour.
    QString plain = rest;
    ParserUtils::removeAnsiMarksInPlace(plain);
    if (plain.trimmed().isEmpty()) {
        return std::nullopt;
    }
    return makeTerminalOutput(out.source, rest, out.goAhead);
}

GmcpMessage makeTerminalOutput(const SendToUserSourceEnum source,
                               const QString &text,
                               const bool goAhead)
{
    QJsonObject obj;
    obj["text"] = text;
    obj["format"] = QStringLiteral("ansi");
    obj["encoding"] = QStringLiteral("utf-8");
    obj["source"] = mmqt::toQStringUtf8(toProtocolString(source));
    obj["goAhead"] = goAhead;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TERMINAL_OUTPUT, toGmcpJson(obj)};
}

GmcpMessage makeSessionState(const bool upstreamConnected,
                             const MapIdentity &map,
                             const bool echo,
                             const bool driving,
                             const GameStateEnum game,
                             const QString &viewer,
                             const LoginPrompt &login,
                             const QString &rememberedAccount,
                             const QString &rememberUnavailable)
{
    QJsonObject obj;
    obj["itemCommands"] = 7;
    obj["trade"] = 1;
    obj["quiet"] = 1;
    obj["hidden"] = 1;
    obj["log"] = 1;
    obj["viewer"] = viewer;
    obj["upstream"] = upstreamConnected ? QStringLiteral("connected")
                                        : QStringLiteral("disconnected");
    obj["mapLoaded"] = map.rooms != 0;
    obj["mapName"] = map.name;
    obj["mapRooms"] = map.rooms;
    obj["mapGeneration"] = map.generation;
    obj["echo"] = echo;
    obj["role"] = driving ? QStringLiteral("driving") : QStringLiteral("observing");
    // Only in the game while MUME is there at all.
    obj["game"] = mmqt::toQStringUtf8(
        gameStateName(upstreamConnected ? game : GameStateEnum::UNKNOWN));
    // Only while MUME waits at a login prompt; a frontend that finds none asks nothing.
    if (upstreamConnected && login.kind != LoginPromptKindEnum::NONE) {
        QJsonObject prompt;
        prompt["kind"] = mmqt::toQStringUtf8(loginPromptKindName(login.kind));
        prompt["text"] = login.text;
        prompt["serial"] = login.serial;
        if (!login.refusedReason.isEmpty()) {
            QJsonObject refused;
            refused["reason"] = login.refusedReason;
            refused["text"] = login.refusedText;
            prompt["refused"] = refused;
        }
        obj["login"] = prompt;
    }
    // The account MMapper logs in to by itself, never its pass phrase; null for none.
    obj["rememberedAccount"] = rememberedAccount.isEmpty() ? QJsonValue{QJsonValue::Null}
                                                           : QJsonValue{rememberedAccount};
    if (!rememberUnavailable.isEmpty()) {
        obj["rememberUnavailable"] = rememberUnavailable;
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_SESSION_STATE, toGmcpJson(obj)};
}

GmcpMessage makeError(const QString &code, const QString &message)
{
    QJsonObject obj;
    obj["code"] = code;
    obj["message"] = message;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_SESSION_ERROR, toGmcpJson(obj)};
}

GmcpMessage makeLogLine(const int64_t seq,
                        const QString &text,
                        const QString &plain,
                        const int priority,
                        const QString &route,
                        const QStringList &tags,
                        const QString &about,
                        const QString &ruleId)
{
    QJsonObject obj;
    obj["seq"] = static_cast<qint64>(seq);
    obj["text"] = text;
    obj["plain"] = plain;
    obj["priority"] = priority;
    obj["route"] = route;
    obj["tags"] = QJsonArray::fromStringList(tags);
    obj["about"] = about;
    obj["rule"] = idOrNull(ruleId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_LOG_LINE, toGmcpJson(obj)};
}

GmcpMessage makeLogRules(const QJsonObject &rules)
{
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_LOG_RULES, toGmcpJson(rules)};
}

GmcpMessage makeLogRuleSaved(const QString &requestId,
                             const bool ok,
                             const QString &id,
                             const QString &error,
                             const QString &message,
                             const std::optional<int> samplePriority)
{
    QJsonObject obj;
    obj["requestId"] = requestId;
    obj["ok"] = ok;
    obj["id"] = idOrNull(id);
    if (!ok) {
        obj["error"] = error;
        obj["message"] = message;
    }
    if (samplePriority.has_value()) {
        obj["samplePriority"] = *samplePriority;
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_LOG_RULE_SAVED, toGmcpJson(obj)};
}

GmcpMessage makeLogExplained(const QString &text,
                             const QString &templateText,
                             const QStringList &tags,
                             const QStringList &names,
                             const int priority,
                             const QString &route,
                             const QString &ruleId)
{
    QJsonObject obj;
    obj["text"] = text;
    obj["template"] = templateText;
    obj["tags"] = QJsonArray::fromStringList(tags);
    obj["names"] = QJsonArray::fromStringList(names);
    obj["priority"] = priority;
    obj["route"] = route;
    obj["rule"] = idOrNull(ruleId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_LOG_EXPLAINED, toGmcpJson(obj)};
}

} // namespace frontend_messages
