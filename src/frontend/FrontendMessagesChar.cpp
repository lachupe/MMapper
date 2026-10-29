// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendMessages.h"

#include "FrontendJson.h"

#include "../global/TextUtils.h"
#include "../global/parserutils.h"
#include "../map/ExitDirection.h"
#include "../map/RoomFingerprint.h"
#include "../map/coordinate.h"
#include "../map/mmapper2room.h"
#include "../map/roomid.h"

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

NODISCARD QJsonArray listedItems(const std::vector<ListedItem> &items,
                                 const bool equipment,
                                 const QString &snapshotId)
{
    QJsonArray result;
    int index = 0;
    for (const ListedItem &item : items) {
        QJsonObject entry;
        if (!snapshotId.isEmpty()) {
            entry["handle"] = snapshotId + QLatin1Char(':') + QString::number(index);
            entry["index"] = index;
            entry["selector"] = QJsonValue::Null;
        }
        ++index;
        if (equipment) {
            entry["slot"] = item.slot;
            entry["label"] = item.label;
            if (item.twoHanded) {
                entry["twoHanded"] = true;
            }
        }
        entry["name"] = item.name;
        entry["count"] = item.count;
        entry["condition"] = item.condition.isEmpty() ? QJsonValue{QJsonValue::Null}
                                                      : QJsonValue{item.condition};
        entry["flags"] = QJsonArray::fromStringList(item.flags);
        entry["text"] = item.text;
        result.append(entry);
    }
    return result;
}


} // namespace

namespace frontend_messages {

GmcpMessage makeCharEquipment(const ItemBlock &block)
{
    QJsonObject obj;
    if (!block.snapshotId.isEmpty()) {
        obj["snapshotId"] = block.snapshotId;
        obj["text"] = block.text;
    }
    obj["owner"] = block.owner;
    obj["items"] = listedItems(block.items, true, block.snapshotId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_EQUIPMENT, toGmcpJson(obj)};
}

GmcpMessage makeCharInventory(const ItemBlock &block)
{
    QJsonObject obj;
    if (!block.snapshotId.isEmpty()) {
        obj["snapshotId"] = block.snapshotId;
        obj["text"] = block.text;
    }
    obj["owner"] = block.owner.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{block.owner};
    obj["peek"] = block.peek;
    obj["items"] = listedItems(block.items, false, block.snapshotId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_INVENTORY, toGmcpJson(obj)};
}

GmcpMessage makeCharContainer(const ItemBlock &block)
{
    QJsonObject obj;
    if (!block.snapshotId.isEmpty()) {
        obj["snapshotId"] = block.snapshotId;
        obj["text"] = block.text;
    }
    if (!block.target.isEmpty()) {
        obj["target"] = block.target;
    }
    obj["listingMode"] = block.listingMode;
    obj["contentsKnown"] = !block.closed;
    obj["keyword"] = block.keyword;
    obj["where"] = block.where.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{block.where};
    obj["closed"] = block.closed;
    obj["items"] = listedItems(block.items, false, block.snapshotId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_CONTAINER, toGmcpJson(obj)};
}

GmcpMessage makeCharItem(const ItemEvent &event)
{
    QJsonObject obj;
    obj["action"] = mmqt::toQStringUtf8(to_string_view(event.action));
    putText(obj, "item", event.item);
    putText(obj, "container", event.container);
    putText(obj, "place", event.place);
    putText(obj, "slot", event.slot);
    putText(obj, "other", event.other);
    putText(obj, "reason", event.reason);
    obj["text"] = event.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_ITEM, toGmcpJson(obj)};
}

GmcpMessage makeCharStat(const CharStat &stat)
{
    QJsonObject obj;
    putNumber(obj, "ob", stat.ob);
    putNumber(obj, "db", stat.db);
    putNumber(obj, "pb", stat.pb);
    putNumber(obj, "armour", stat.armour);
    putNumber(obj, "wimpy", stat.wimpy);
    putText(obj, "mood", stat.mood);
    putText(obj, "alert", stat.alert);
    putNumber(obj, "neededXp", stat.neededXp);
    putNumber(obj, "neededTp", stat.neededTp);
    putNumber(obj, "gold", stat.gold);
    putNumber(obj, "wp", stat.wp);
    if (!stat.condition.isEmpty()) {
        obj["condition"] = QJsonArray::fromStringList(stat.condition);
    }
    obj["affects"] = QJsonArray::fromStringList(stat.affects);
    obj["wounds"] = QJsonArray::fromStringList(stat.wounds);
    obj["text"] = stat.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_STAT, toGmcpJson(obj)};
}

GmcpMessage makeCharScore(const CharScore &score)
{
    static const std::array<const char *, 7>
        abilityKeys{"str", "int", "wis", "dex", "con", "wil", "per"};
    QJsonObject obj;
    putText(obj, "reply", score.reply);
    QJsonObject abilities;
    for (size_t i = 0; i < abilityKeys.size(); ++i) {
        putNumber(abilities, abilityKeys[i], score.abilities[i]);
    }
    if (!abilities.isEmpty()) {
        obj["abilities"] = abilities;
    }
    putNumber(obj, "ob", score.ob);
    putNumber(obj, "db", score.db);
    putNumber(obj, "pb", score.pb);
    putNumber(obj, "armour", score.armour);
    putNumber(obj, "hp", score.hp);
    putNumber(obj, "maxhp", score.maxhp);
    putNumber(obj, "mana", score.mana);
    putNumber(obj, "maxmana", score.maxmana);
    putNumber(obj, "mp", score.mp);
    putNumber(obj, "maxmp", score.maxmp);
    putText(obj, "mood", score.mood);
    putNumber(obj, "wimpy", score.wimpy);
    putNumber(obj, "xp", score.xp);
    putNumber(obj, "tp", score.tp);
    putText(obj, "renown", score.renown);
    putNumber(obj, "wp", score.wp);
    putNumber(obj, "neededXp", score.neededXp);
    putNumber(obj, "neededTp", score.neededTp);
    putNumber(obj, "gold", score.gold);
    putNumber(obj, "silver", score.silver);
    putNumber(obj, "copper", score.copper);
    putText(obj, "language", score.language);
    putText(obj, "swim", score.swim);
    putText(obj, "climb", score.climb);
    // The head of the sheet.
    putText(obj, "sex", score.sex);
    putText(obj, "race", score.race);
    QJsonObject age;
    putNumber(age, "years", score.ageYears);
    putNumber(age, "months", score.ageMonths);
    putNumber(age, "days", score.ageDays);
    if (!age.isEmpty()) {
        obj["age"] = age;
    }
    putText(obj, "played", score.played);
    putText(obj, "session", score.session);
    putText(obj, "name", score.name);
    putText(obj, "title", score.title);
    putNumber(obj, "level", score.level);
    putText(obj, "height", score.height);
    putText(obj, "weight", score.weight);
    QJsonObject perception;
    putNumber(perception, "vision", score.vision);
    putNumber(perception, "hearing", score.hearing);
    putNumber(perception, "smell", score.smell);
    if (!perception.isEmpty()) {
        obj["perception"] = perception;
    }
    putText(obj, "alertness", score.alertness);
    putText(obj, "alignment", score.alignment);
    if (!score.welcome.isEmpty()) {
        obj["welcome"] = QJsonArray::fromStringList(score.welcome);
    }
    putText(obj, "war", score.war);
    if (score.effectsKnown) {
        obj["effects"] = QJsonArray::fromStringList(score.effects);
        obj["wounds"] = QJsonArray::fromStringList(score.wounds);
    }
    obj["text"] = score.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_SCORE, toGmcpJson(obj)};
}

GmcpMessage makeCharBurden(const CharBurden &burden)
{
    QJsonObject obj;
    obj["pounds"] = static_cast<qint64>(burden.pounds);
    putText(obj, "word", burden.word);
    obj["text"] = burden.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_BURDEN, toGmcpJson(obj)};
}

GmcpMessage makeCharLevel(const CharLevel &level)
{
    QJsonObject obj;
    obj["level"] = static_cast<qint64>(level.level);
    putNumber(obj, "xp", level.xp);
    putNumber(obj, "neededXp", level.neededXp);
    putNumber(obj, "tp", level.tp);
    putNumber(obj, "neededTp", level.neededTp);
    obj["text"] = level.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_LEVEL, toGmcpJson(obj)};
}

GmcpMessage makeCharAbilities(const CharAbilitySet &set)
{
    static const std::array<const char *, 7>
        abilityKeys{"str", "int", "wis", "dex", "con", "wil", "per"};
    QJsonObject abilities;
    for (size_t i = 0; i < abilityKeys.size(); ++i) {
        putNumber(abilities, abilityKeys[i], set.abilities[i]);
    }
    QJsonObject obj;
    obj[set.set] = abilities;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_ABILITIES, toGmcpJson(obj)};
}

GmcpMessage makeCharLanguages(const CharLanguages &languages)
{
    QJsonArray rows;
    QJsonValue speaking = QJsonValue::Null;
    for (const CharLanguage &language : languages.rows) {
        QJsonObject row;
        row["name"] = language.name;
        row["knowledge"] = static_cast<qint64>(language.knowledge);
        row["speaking"] = language.speaking;
        rows.append(row);
        if (language.speaking) {
            speaking = language.name;
        }
    }
    QJsonObject obj;
    obj["languages"] = rows;
    obj["speaking"] = speaking;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_LANGUAGES, toGmcpJson(obj)};
}

GmcpMessage makeCharImproved(const CharImproved &improved)
{
    QJsonObject obj;
    obj["skill"] = improved.skill.isEmpty() ? QJsonValue{QJsonValue::Null}
                                            : QJsonValue{improved.skill};
    obj["text"] = improved.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_IMPROVED, toGmcpJson(obj)};
}

GmcpMessage makeCharWimpy(const CharWimpy &wimpy)
{
    QJsonObject obj;
    obj["wimpy"] = static_cast<qint64>(wimpy.wimpy);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_WIMPY, toGmcpJson(obj)};
}

GmcpMessage makeCharAffects(const std::vector<CharAffect> &affects)
{
    QJsonArray rows;
    for (const CharAffect &affect : affects) {
        QJsonObject row;
        row["name"] = affect.name;
        putNumber(row, "since", affect.since);
        putNumber(row, "refreshed", affect.refreshed);
        row["source"] = affect.fromLine ? QStringLiteral("line") : QStringLiteral("stat");
        if (affect.wound.has_value()) {
            const CharWound &wound = *affect.wound;
            row["severity"] = wound.severity;
            row["location"] = wound.location;
            row["state"] = wound.state;
            row["dirty"] = wound.dirty();
            row["bandaged"] = wound.bandaged();
        }
        rows.append(row);
    }
    QJsonObject obj;
    obj["affects"] = rows;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_AFFECTS, toGmcpJson(obj)};
}

GmcpMessage makeCharFollowers(const CharFollowers &followers)
{
    const auto text = [](const std::string_view sv) {
        return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
    };
    QJsonArray rows;
    for (const CharFollower &follower : followers.followers) {
        QJsonObject row;
        row["name"] = follower.name;
        row["label"] = follower.label;
        row["kind"] = text(to_string_view(follower.kind));
        row["here"] = follower.here;
        row["state"] = text(to_string_view(follower.state));
        putNumber(row, "since", follower.since);
        putText(row, "lastOrder", follower.lastOrder);
        putText(row, "lastRefused", follower.lastRefused);
        rows.append(row);
    }
    QJsonObject obj;
    obj["followers"] = rows;
    putText(obj, "following", followers.following);
    if (const auto leader = leaderOf(followers)) {
        QJsonObject who;
        putText(who, "name", leader->name);
        who["you"] = leader->you;
        obj["leader"] = who;
    }
    obj["players"] = QJsonArray::fromStringList(followers.players);
    if (followers.protecting.has_value()) {
        QJsonObject protect;
        protect["protecting"] = QJsonArray::fromStringList(*followers.protecting);
        obj["protect"] = protect;
    }
    if (followers.reply.has_value()) {
        QJsonObject reply;
        reply["order"] = followers.reply->order;
        reply["who"] = followers.reply->who;
        reply["result"] = text(to_string_view(followers.reply->result));
        reply["failed"] = QJsonArray::fromStringList(followers.reply->failed);
        obj["reply"] = reply;
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_FOLLOWERS, toGmcpJson(obj)};
}

GmcpMessage makeCharConsider(const CharConsider &consider)
{
    QJsonObject obj;
    obj["verdict"] = consider.verdict;
    obj["grade"] = consider.grade;
    obj["text"] = consider.text;
    putText(obj, "level", consider.level);
    putText(obj, "target", consider.target);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_CONSIDER, toGmcpJson(obj)};
}

GmcpMessage makeCharRefused(const CharRefused &refused)
{
    QJsonObject obj;
    putText(obj, "action", refused.action);
    obj["reason"] = refused.reason;
    obj["text"] = refused.text;
    putText(obj, "target", refused.target);
    putText(obj, "dir", refused.dir);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_REFUSED, toGmcpJson(obj)};
}

GmcpMessage makeCharCommand(const ItemCommandObservation &command)
{
    QJsonObject obj;
    obj["id"] = command.id;
    obj["command"] = command.command;
    obj["action"] = command.action;
    obj["status"] = command.status;
    obj["successes"] = command.successes;
    obj["refusals"] = command.refusals;
    QJsonArray replies;
    for (const auto &reply : command.replies) {
        QJsonObject evidence;
        evidence["action"] = mmqt::toQStringUtf8(to_string_view(reply.action));
        evidence["item"] = reply.item;
        evidence["container"] = reply.container;
        evidence["other"] = reply.other;
        evidence["slot"] = reply.slot;
        evidence["reason"] = reply.reason;
        evidence["text"] = reply.text;
        replies.append(evidence);
    }
    obj["replies"] = replies;
    obj["text"] = QJsonArray::fromStringList(command.text);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_COMMAND, toGmcpJson(obj)};
}

} // namespace frontend_messages
