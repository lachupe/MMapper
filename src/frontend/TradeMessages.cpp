// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TradeMessages.h"

#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

NODISCARD GmcpJson toGmcpJson(const QJsonObject &obj)
{
    const QString json = QJsonDocument{obj}.toJson(QJsonDocument::Compact);
    return GmcpJson{json};
}

void putNumber(QJsonObject &obj, const char *const key, const std::optional<int64_t> &value)
{
    if (value.has_value()) {
        obj[key] = static_cast<qint64>(*value);
    }
}

void putText(QJsonObject &obj, const char *const key, const QString &value)
{
    if (!value.isEmpty()) {
        obj[key] = value;
    }
}

/// A table's pager state, sent always so that a frontend can tell a short reply from a cut one.
void putPaged(QJsonObject &obj, const bool paged, const bool complete)
{
    obj["paged"] = paged;
    obj["complete"] = complete;
}

} // namespace

namespace frontend_messages {

std::string_view toProtocolString(const ShopDealKindEnum kind)
{
    switch (kind) {
    case ShopDealKindEnum::BUY:
        return "buy";
    case ShopDealKindEnum::SELL:
        return "sell";
    case ShopDealKindEnum::VALUE:
        return "value";
    case ShopDealKindEnum::MISS:
        return "miss";
    case ShopDealKindEnum::CLOSED:
        return "closed";
    case ShopDealKindEnum::REFUSED:
        return "refused";
    }
    return "miss";
}

GmcpMessage makeShopList(const ShopList &list)
{
    QJsonObject obj;
    obj["keeper"] = list.keeper;
    obj["query"] = list.query;
    QJsonArray rows;
    for (const ShopRow &row : list.rows) {
        QJsonObject r;
        r["number"] = static_cast<qint64>(row.number);
        r["count"] = static_cast<qint64>(row.count);
        r["name"] = row.name;
        r["singular"] = row.singular;
        r["condition"] = row.condition;
        r["age"] = row.age;
        putNumber(r, "priceCopper", row.priceCopper);
        r["priceText"] = row.priceText;
        r["group"] = row.group;
        rows.append(r);
    }
    obj["rows"] = rows;
    obj["empty"] = list.empty;
    putPaged(obj, list.paged, list.complete);
    obj["text"] = list.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_SHOP_LIST, toGmcpJson(obj)};
}

GmcpMessage makeShopDeal(const ShopDeal &deal)
{
    QJsonObject obj;
    obj["kind"] = QString::fromLatin1(toProtocolString(deal.kind));
    putText(obj, "keeper", deal.keeper);
    putNumber(obj, "amountCopper", deal.amountCopper);
    putText(obj, "amountText", deal.amountText);
    obj["items"] = QJsonArray::fromStringList(deal.items);
    putText(obj, "said", deal.said);
    obj["text"] = deal.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_SHOP_DEAL, toGmcpJson(obj)};
}

GmcpMessage makeGuildTeacher(const GuildTeacher &teacher)
{
    QJsonObject obj;
    obj["teacher"] = teacher.teacher;
    putText(obj, "kind", teacher.kind);
    putNumber(obj, "sessionsLeft", teacher.sessionsLeft);
    QJsonArray rows;
    for (const GuildRow &row : teacher.rows) {
        QJsonObject r;
        r["name"] = row.name;
        r["used"] = static_cast<qint64>(row.used);
        r["most"] = static_cast<qint64>(row.most);
        putNumber(r, "knowledgePct", row.knowledgePct);
        r["difficulty"] = row.difficulty;
        r["advice"] = row.advice;
        rows.append(r);
    }
    obj["rows"] = rows;
    putPaged(obj, teacher.paged, teacher.complete);
    obj["text"] = teacher.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_GUILD_TEACHER, toGmcpJson(obj)};
}

GmcpMessage makeGuildPractised(const GuildPractised &practised)
{
    QJsonObject obj;
    putText(obj, "name", practised.name);
    putNumber(obj, "used", practised.used);
    putNumber(obj, "most", practised.most);
    putNumber(obj, "knowledgePct", practised.knowledgePct);
    putText(obj, "refused", practised.refused);
    obj["text"] = practised.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_GUILD_PRACTISED, toGmcpJson(obj)};
}

GmcpMessage makeCharSkills(const CharSkills &skills)
{
    QJsonObject obj;
    putNumber(obj, "sessionsLeft", skills.sessionsLeft);
    QJsonArray rows;
    for (const CharSkillRow &row : skills.rows) {
        QJsonObject r;
        r["name"] = row.name;
        r["knowledge"] = row.knowledge;
        r["trained"] = row.trained;
        r["difficulty"] = row.difficulty;
        r["class"] = row.skillClass;
        putNumber(r, "mana", row.mana);
        putText(r, "casting", row.casting);
        rows.append(r);
    }
    obj["rows"] = rows;
    putPaged(obj, skills.paged, skills.complete);
    obj["text"] = skills.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_SKILLS, toGmcpJson(obj)};
}

GmcpMessage makeInnOffer(const InnOffer &offer)
{
    QJsonObject obj;
    putText(obj, "keeper", offer.keeper);
    putNumber(obj, "perDayCopper", offer.perDayCopper);
    putText(obj, "perDayText", offer.perDayText);
    putText(obj, "lastsText", offer.lastsText);
    putText(obj, "confiscated", offer.confiscated);
    putText(obj, "said", offer.said);
    obj["retireAsksRepeat"] = offer.retireAsksRepeat;
    obj["text"] = offer.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_INN_OFFER, toGmcpJson(obj)};
}

GmcpMessage makeCharTrophies(const CharTrophies &trophies)
{
    QJsonObject obj;
    QJsonArray rows;
    for (const TrophyRow &row : trophies.rows) {
        QJsonObject r;
        r["name"] = row.name;
        r["kills"] = static_cast<qint64>(row.kills);
        r["knowledgePct"] = static_cast<qint64>(row.knowledgePct);
        r["player"] = row.player;
        rows.append(r);
    }
    obj["rows"] = rows;
    putNumber(obj, "totalKills", trophies.totalKills);
    putNumber(obj, "distinct", trophies.distinct);
    putPaged(obj, trophies.paged, trophies.complete);
    obj["text"] = trophies.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_TROPHIES, toGmcpJson(obj)};
}

GmcpMessage makeViewText(const ViewText &view)
{
    QJsonObject obj;
    obj["title"] = view.title;
    obj["text"] = view.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_VIEW_TEXT, toGmcpJson(obj)};
}

} // namespace frontend_messages
