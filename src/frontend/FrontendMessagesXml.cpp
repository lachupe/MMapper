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

NODISCARD QJsonObject toJson(const XmlElement &element)
{
    QJsonObject obj;
    obj["tag"] = mmqt::toQStringUtf8(element.name);
    obj["category"] = mmqt::toQStringUtf8(to_string_view(toXmlCategory(element.tag)));
    obj["text"] = element.text;

    if (!element.attributes.empty()) {
        QJsonObject attributes;
        for (const auto &attribute : element.attributes) {
            attributes[mmqt::toQStringUtf8(attribute.first)] = mmqt::toQStringUtf8(attribute.second);
        }
        obj["attributes"] = attributes;
    }

    if (!element.children.empty()) {
        QJsonArray children;
        for (const XmlElement &child : element.children) {
            children.append(toJson(child));
        }
        obj["children"] = children;
    }

    // Only stated when true, so that the ordinary case stays quiet on the wire.
    if (element.truncated) {
        obj["truncated"] = true;
    }

    // MMapper's reading of a movement line: where someone came from (move_in) or went
    // (move_out), or MUME's own `dir` for the player's movement. Kept out of `attributes`,
    // which stay exactly what MUME sent, and omitted when there is no direction to state.
    if (!element.direction.empty()) {
        obj["direction"] = mmqt::toQStringUtf8(element.direction);
    }
    return obj;
}

} // namespace

namespace frontend_messages {

GmcpMessage makeCombatEvent(const CombatEvent &event)
{
    QJsonObject obj;
    obj["kind"] = mmqt::toQStringUtf8(to_string_view(event.kind));
    obj["text"] = event.text;
    // Quiet on the wire: a field is only sent when the line said something for it.
    putText(obj, "actor", event.actor);
    putText(obj, "target", event.target);
    if (event.kind == CombatKindEnum::BLOW) {
        obj["outcome"] = mmqt::toQStringUtf8(to_string_view(event.outcome));
    }
    if (event.phase != CombatPhaseEnum::NONE) {
        obj["phase"] = mmqt::toQStringUtf8(to_string_view(event.phase));
    }
    putText(obj, "verb", event.verb);
    putText(obj, "part", event.part);
    putText(obj, "quality", event.quality);
    putText(obj, "severity", event.severity);
    putText(obj, "effect", event.effect);
    putText(obj, "detail", event.detail);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_COMBAT_EVENT, toGmcpJson(obj)};
}

GmcpMessage makeXmlElement(const XmlElement &element)
{
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_XML_ELEMENT, toGmcpJson(toJson(element))};
}

} // namespace frontend_messages
