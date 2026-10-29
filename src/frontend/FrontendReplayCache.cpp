// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendReplayCache.h"

#include "../global/TextUtils.h"
#include "FrontendJson.h"

#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using frontend_json::toGmcpJson;

namespace {

NODISCARD QJsonDocument parse(const GmcpMessage &msg)
{
    const auto &optJson = msg.getJson();
    return optJson.has_value() ? QJsonDocument::fromJson(optJson->toQByteArray()) : QJsonDocument{};
}

/// Copies every field of `changed` over `into`, leaving the fields it does not mention alone.
void mergeInto(QJsonObject &into, const QJsonObject &changed)
{
    for (auto it = changed.begin(); it != changed.end(); ++it) {
        into.insert(it.key(), it.value());
    }
}

} // namespace

void FrontendReplayCache::remember(const GmcpMessage &msg)
{
    const ReplayPolicy policy = replayPolicy(msg.getType());
    switch (policy.kind) {
    case ReplayKindEnum::SET_CHANGE:
        if (policy.changes.has_value()) {
            applyToSet(*policy.changes, msg);
        }
        break;
    case ReplayKindEnum::MERGE_FIELDS:
        mergeFields(msg);
        break;
    case ReplayKindEnum::WHOLE:
    case ReplayKindEnum::KEYED_SET:
        store(msg);
        break;
    case ReplayKindEnum::EVENT:
    case ReplayKindEnum::SNAPSHOT:
    case ReplayKindEnum::CONTAINER_LRU:
        break; // Not state this cache folds: events, and what FrontendReplayState holds itself.
    }
}

const GmcpMessage *FrontendReplayCache::find(const GmcpMessageTypeEnum type) const
{
    const auto it = m_messages.find(type);
    return it == m_messages.end() ? nullptr : &it->second;
}

void FrontendReplayCache::clear(const ReplayScopeEnum scope)
{
    for (auto it = m_messages.begin(); it != m_messages.end();) {
        if (isClearedBy(replayPolicy(it->first), scope)) {
            it = m_messages.erase(it);
        } else {
            ++it;
        }
    }
}

void FrontendReplayCache::store(const GmcpMessage &msg)
{
    // GmcpMessage is copy constructible but not copy assignable, so replace rather than
    // assign.
    m_messages.erase(msg.getType());
    m_messages.emplace(msg.getType(), msg);
}

void FrontendReplayCache::mergeFields(const GmcpMessage &msg)
{
    const QJsonDocument changed = parse(msg);
    if (!changed.isObject()) {
        return; // Nothing to merge; what is already known still stands.
    }

    QJsonObject fields;
    if (const auto it = m_messages.find(msg.getType()); it != m_messages.end()) {
        fields = parse(it->second).object();
    }
    mergeInto(fields, changed.object());
    store(GmcpMessage{msg.getType(), toGmcpJson(QJsonDocument{fields})});
}

void FrontendReplayCache::applyToSet(const GmcpMessageTypeEnum setType, const GmcpMessage &msg)
{
    const auto cached = m_messages.find(setType);
    if (cached == m_messages.end()) {
        return; // No Set yet, so there is nothing this could be a change to.
    }

    // A Set whose payload is not an array describes nobody, as it does to a frontend.
    QJsonArray members = parse(cached->second).array();
    const auto indexOf = [&members](const qint64 id) -> std::optional<qsizetype> {
        for (qsizetype i = 0; i < members.size(); ++i) {
            if (members.at(i).toObject().value(QStringLiteral("id")).toInteger() == id) {
                return i;
            }
        }
        return std::nullopt;
    };

    if (msg.isGroupRemove() || msg.isRoomCharsRemove()) {
        // The payload is the bare id, which QJsonDocument cannot parse on its own; JsonDoc
        // reads it as a number instead.
        const auto &optDoc = msg.getJsonDocument();
        const OptJsonInt id = optDoc.has_value() ? optDoc->getInt() : std::nullopt;
        const auto index = id.has_value() ? indexOf(*id) : std::nullopt;
        if (!index.has_value()) {
            return; // Someone who was not there has not changed anything.
        }
        members.removeAt(*index);
    } else {
        const QJsonDocument change = parse(msg);
        const QJsonObject fields = change.object();
        const qint64 id = fields.value(QStringLiteral("id")).toInteger();
        if (!change.isObject() || id <= 0) {
            return; // Without an id there is no telling whom it is about.
        }
        if (const auto index = indexOf(id)) {
            // An Update carries only what changed, and an Add for someone already present
            // is MUME restating them; either way the rest of what is known about them stays.
            QJsonObject member = members.at(*index).toObject();
            mergeInto(member, fields);
            members.replace(*index, member);
        } else {
            // Someone new, or described before their arrival was: at the end, where a
            // frontend also puts an arrival.
            members.append(fields);
        }
    }

    store(GmcpMessage{setType, toGmcpJson(QJsonDocument{members})});
}
