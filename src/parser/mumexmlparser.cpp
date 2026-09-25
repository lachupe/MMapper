// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2019 The MMapper Authors
// Author: Ulf Hermann <ulfonk_mennhar@gmx.de> (Alve)
// Author: Marek Krejza <krejza@gmail.com> (Caligor)

#include "mumexmlparser.h"

#include "../configuration/configuration.h"
#include "../global/AnsiOstream.h"
#include "../global/Consts.h"
#include "../global/PrintUtils.h"
#include "../global/TextUtils.h"
#include "../global/entities.h"
#include "../global/logging.h"
#include "../global/parserutils.h"
#include "../map/ExitsFlags.h"
#include "../map/ParseTree.h"
#include "../map/PromptFlags.h"
#include "../map/parseevent.h"
#include "../mapdata/mapdata.h"
#include "../proxy/GmcpMessage.h"
#include "../proxy/telnetfilter.h"
#include "CombatLines.h"
#include "abstractparser.h"

#include <cctype>
#include <list>
#include <optional>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDateTime>
#include <QString>

using namespace char_consts;

namespace { // anonymous

// These are garbage.
const QString nullString{};
const QByteArray emptyByteArray{""};

void decodeXmlEntities(QString &s)
{
    s = entities::decode(entities::EncodedString{s});
}

void appendCodepoint(QString &qs, char32_t c)
{
    if ((false)) {
        qs += QString::fromStdU32String(std::u32string{c, 1});
    } else {
        charset::conversion::Utf16StringBuilder builder;
        builder += c;
        auto sv = builder.get_string_view();
        qs += QStringView{sv.data(), static_cast<int>(sv.size())};
    }
}

} // namespace

MumeXmlParser::MumeXmlParser(MapData &md,
                             MumeClock &mc,
                             ProxyMudConnectionApi & /*proxyMudConnection*/,
                             ProxyUserGmcpApi &proxyGmcp,
                             GroupManagerApi &group,
                             GameObserver &observer,
                             HotkeyManager &hm,
                             QObject *parent,
                             AbstractParserOutputs &outputs,
                             ParserCommonData &parserCommonData)
    : MumeXmlParserBase{parent, mc, md, group, hm, proxyGmcp, outputs, parserCommonData}
    , m_observer{observer}
{
    // Every line on its way to MUME, from MMapper's own client and from a frontend's
    // MMapper.Input.Command alike, so that MUME's replies to container commands can be paired
    // with the command that caused them. Lines typed while MUME has echo off, such as a
    // password, are not reported here.
    m_observer.sig2_sentToMudString.connect(m_lifetime, [this](const QString &line) {
        m_containerTracker.receiveCommand(line);
        m_itemTracker.receiveCommand(line);
    });
}

MumeXmlParser::~MumeXmlParser() = default;

void MumeXmlParser::slot_parseNewMudInput(const TelnetData &data)
{
    const bool isPromptOrTwiddlers = data.type == TelnetDataEnum::Prompt
                                     || data.type == TelnetDataEnum::Backspace;
    if (isPromptOrTwiddlers) {
        m_commonData.lastPrompt = QString::fromUtf8(data.line.getQByteArray());
    }
    parse(data, isPromptOrTwiddlers);
}

void MumeXmlParser::parse(const TelnetData &data, const bool isGoAhead)
{
    m_lineToUser.clear();
    m_lineFlags.remove(LineFlagEnum::NONE);

    std::string_view utf8 = mmqt::toStdStringViewRaw(data.line.getQByteArray());
    charset::foreach_codepoint_utf8(utf8, [this](const char32_t c) {
        if (m_readingTag) {
            if (c == char_consts::C_GREATER_THAN) {
                // send tag
                if (!m_tempTag.isEmpty()) {
                    m_xmlTracker.receiveTag(m_tempTag);
                    std::ignore = element(m_tempTag);
                }

                m_tempTag.clear();

                m_readingTag = false;
                return;
            }
            appendCodepoint(m_tempTag, c);

        } else {
            if (c == char_consts::C_LESS_THAN) {
                // characters() decodes entities in place, so the tracker is fed afterwards
                // and sees the same text the user does -- including the parts characters()
                // routes elsewhere rather than returning, such as the exits block.
                m_lineToUser.append(characters(m_tempCharacters));
                m_xmlTracker.receiveText(m_tempCharacters);
                m_tempCharacters.clear();

                m_readingTag = true;
                return;
            }
            appendCodepoint(m_tempCharacters, c);
        }
    });

    if (!m_readingTag) {
        m_lineToUser.append(characters(m_tempCharacters));
        m_xmlTracker.receiveText(m_tempCharacters);
        m_tempCharacters.clear();
    }
    if (!m_lineToUser.isEmpty()) {
        sendToUser(SendToUserSourceEnum::FromMud, m_lineToUser, isGoAhead);

        // Simplify the output and run actions
        QString tempStr = m_lineToUser;
        tempStr = normalizeStringCopy(tempStr.trimmed());
        parseMudCommands(tempStr);

        // A fight's events are read off the text as well, and published as
        // MMapper.Combat.Event, which is what a client animates from: only the prose says
        // where a blow landed and how hard, and it carries the flights, bashes and deaths. The
        // <hit> and <miss> elements MUME brackets each blow with are still relayed below, and
        // feed a client's combat log and the participants they name. Read from the line as
        // the user sees it, colour removed but not otherwise normalised, so that a name keeps
        // the accents Room.Chars gives it and the two can be matched.
        QString plain = m_lineToUser;
        ParserUtils::removeAnsiMarksInPlace(plain);
        if (const auto combat = parseCombatLine(plain)) {
            m_ownCastTracker.receiveEvent(*combat);
            m_observer.observeSentToUserCombat(*combat);
        }
        // Replies to the player's container commands: "Ok.", "*click*", a listing. A prompt
        // is not one, and ends the reply being gathered instead (below).
        if (!isGoAhead) {
            publishContainerEvents(
                m_containerTracker.receiveLine(plain, QDateTime::currentSecsSinceEpoch()));
            // What the player wears and carries: listings that name themselves in their first
            // line and end at a blank line or the prompt, and the one-line replies to wear,
            // remove, get, put, drop and give.
            publishItemBlocks(m_itemTracker.receiveLine(plain));
            if (const auto item = parseItemEvent(plain)) {
                m_observer.observeItemEvent(*item);
            }
        }
    }
    if (isGoAhead) {
        // Every prompt ends a listing, in XML mode and out of it.
        publishItemBlocks(m_itemTracker.receivePrompt());
    }

    // Published after the terminal output of the line that closed them, so a client can
    // line each element up against text it has already been given. An element opened on an
    // earlier line arrives with the line that finally closes it, not the one that began it.
    for (const XmlElement &xml : m_xmlTracker.take()) {
        m_observer.observeSentToUserXml(xml);
        // The weather's prose, read once here: lightning seen, fog, frost, ice, snow lying,
        // storms and magic. The ground's state is complete at the prompt that ends a room
        // display, which comes after MMapper has moved the player, so it describes the room
        // MMapper.Map.Position has just named.
        const std::optional<WeatherLine> weather = parseWeatherElement(xml);
        if (weather.has_value()) {
            m_observer.observeWeatherLine(*weather);
        }
        if (const auto ground = m_groundTracker.receive(xml, weather)) {
            m_observer.observeGround(*ground);
        }

        // The objects in the room, complete at the prompt that ends its display, like the
        // ground above. A prompt also ends any container reply being gathered, which goes
        // first, so that what it said is in the room's objects.
        if (xml.tag == XmlTagEnum::PROMPT) {
            publishContainerEvents(
                m_containerTracker.receivePrompt(QDateTime::currentSecsSinceEpoch()));
            // The prompt coming back is the only sign the player's own spell went off.
            if (const auto done = m_ownCastTracker.receivePrompt()) {
                m_observer.observeSentToUserCombat(*done);
            }
        }
        const QString roomLines = (xml.tag == XmlTagEnum::ROOM
                                   && m_commonData.roomContents.has_value())
                                      ? m_commonData.roomContents->toQString()
                                      : QString{};
        if (auto contents = m_roomContentsTracker.receive(xml, roomLines, roomKey())) {
            m_containerTracker.decorate(*contents);
            std::ignore = m_containerTracker.takeChanged();
            m_observer.observeRoomContents(*contents);
        } else if (xml.tag == XmlTagEnum::PROMPT && m_containerTracker.takeChanged()) {
            // A reply changed what is known about a container here: the room's objects are
            // published again, so that a frontend that subscribes later is told.
            RoomContentsSnapshot changed = m_containerTracker.current();
            changed.entered = false;
            m_observer.observeRoomContents(changed);
        }
    }
}

QString MumeXmlParser::roomKey() const
{
    if (m_serverId != INVALID_SERVER_ROOMID) {
        return QString::number(m_serverId.asUint32());
    }
    // Without MUME's id, the room's name and description are the next best thing: they are
    // what MMapper matches rooms by as well.
    QString key;
    if (m_commonData.roomName.has_value()) {
        key = m_commonData.roomName->toQString();
    }
    if (m_commonData.roomDesc.has_value()) {
        key += QLatin1Char('\n') + m_commonData.roomDesc->toQString();
    }
    return key;
}

void MumeXmlParser::publishContainerEvents(const std::vector<ContainerEvent> &events)
{
    for (const ContainerEvent &event : events) {
        m_observer.observeContainerEvent(event);
    }
}

void MumeXmlParser::publishItemBlocks(const std::vector<ItemBlock> &blocks)
{
    for (const ItemBlock &block : blocks) {
        m_observer.observeItemBlock(block);
    }
}

bool MumeXmlParser::element(const QString &line)
{
    using namespace char_consts;
    const auto length = line.length();

    // The attributes of this tag are parsed by parseXmlAttributes(), which
    // XmlElementTracker feeds to the frontend. The room state machine below has never
    // needed them, so nothing is parsed here.

    switch (m_xmlMode) {
    case XmlModeEnum::NONE:
        if (length > 0) {
            switch (line.front().unicode()) {
            case C_SLASH:
                if (line.startsWith("/snoop")) {
                    m_lineFlags.remove(LineFlagEnum::SNOOP);

                } else if (line.startsWith("/status")) {
                    m_lineFlags.remove(LineFlagEnum::STATUS);

                } else if (line.startsWith("/weather")) {
                    m_lineFlags.remove(LineFlagEnum::WEATHER);
                    // Certain weather events happen on ticks

                } else if (line.startsWith("/xml")) {
                    sendToUser(SendToUserSourceEnum::FromMMapper,
                               "[MMapper] Mapper cannot function without XML mode\n");
                    getQueue().clear();
                    m_lineFlags.clear();
                    m_xmlTracker.reset();
                    m_groundTracker.reset();
                    m_roomContentsTracker.reset();
                    m_containerTracker.reset();
                    m_itemTracker.reset();
                    m_ownCastTracker.reset();
                }
                break;
            case 'p':
                if (line.startsWith("prompt")) {
                    m_xmlMode = XmlModeEnum::PROMPT;
                    m_lineFlags.insert(LineFlagEnum::PROMPT);
                    m_commonData.lastPrompt = emptyByteArray;
                }
                break;
            case 'e':
                if (line.startsWith("exits")) {
                    m_commonData.exits
                        = nullString; // Reset string since payload can be from the 'exit' command
                    m_xmlMode = XmlModeEnum::EXITS;
                    m_lineFlags.insert(LineFlagEnum::EXITS);
                }
                break;
            case 'r':
                if (line.startsWith("room")) {
                    m_xmlMode = XmlModeEnum::ROOM;
                    m_lineFlags.insert(LineFlagEnum::ROOM);
                    if (!m_lineFlags.isSnoop()) {
                        m_descriptionReady = false;
                        m_exitsReady = false;
                        m_commonData.exits = nullString;
                        m_stringBuffer = nullString;
                        m_commonData.roomContents.reset();
                    }
                }
                break;
            case 'w':
                if (line.startsWith("weather")) {
                    m_lineFlags.insert(LineFlagEnum::WEATHER);
                }
                break;
            case 's':
                if (line.startsWith("status")) {
                    m_lineFlags.insert(LineFlagEnum::STATUS);

                } else if (line.startsWith("snoop")) {
                    m_lineFlags.insert(LineFlagEnum::SNOOP);
                }
                break;
            }
        }
        break;

    case XmlModeEnum::ROOM:
        if (length > 0) {
            switch (line.front().unicode()) {
            case 'e':
                if (line.startsWith("exits")) {
                    m_xmlMode = XmlModeEnum::EXITS;
                    m_lineFlags.insert(LineFlagEnum::EXITS);
                    if (!m_lineFlags.isSnoop()) {
                        m_commonData.exits
                            = nullString; // Reset string since payload can be from the 'exit' command
                        m_descriptionReady = true;
                    }
                }
                break;
            case 'n':
                if (line.startsWith("name")) {
                    m_xmlMode = XmlModeEnum::NAME;
                    m_lineFlags.insert(LineFlagEnum::NAME);
                }
                break;
            case 'd':
                if (line.startsWith("description")) {
                    m_xmlMode = XmlModeEnum::DESCRIPTION;
                    m_lineFlags.insert(LineFlagEnum::DESCRIPTION);
                }
                break;
            case 't': // terrain tag only comes up in blindness or fog
                if (line.startsWith("terrain")) {
                    m_xmlMode = XmlModeEnum::TERRAIN;
                    m_lineFlags.insert(LineFlagEnum::TERRAIN);
                }
                break;
            case 'h': // Gods have an "Obvious exits" header
                if (line.startsWith("header")) {
                    m_xmlMode = XmlModeEnum::HEADER;
                    m_lineFlags.insert(LineFlagEnum::HEADER);
                    if (!m_lineFlags.isSnoop()) {
                        m_descriptionReady = true;
                    }
                }
                break;
            case C_SLASH:
                if (line.startsWith("/room")) {
                    m_xmlMode = XmlModeEnum::NONE;
                    m_lineFlags.remove(LineFlagEnum::ROOM);
                    if (!m_lineFlags.isSnoop()) {
                        m_commonData.roomContents = mmqt::makeRoomContents(m_stringBuffer);
                        m_descriptionReady = true;
                        if (!m_exitsReady && getConfig().mumeNative.emulatedExits) {
                            m_exitsReady = true;
                            std::ostringstream os;
                            {
                                AnsiOstream aos{os};
                                emulateExits(aos, m_move);
                            }
                            sendToUser(SendToUserSourceEnum::SimulatedOutput, os.str());
                        }
                    }
                }
                break;
            }
        }
        break;
    case XmlModeEnum::NAME:
        if (line.startsWith("/name")) {
            m_xmlMode = XmlModeEnum::ROOM;
            m_lineFlags.remove(LineFlagEnum::NAME);
        }
        break;
    case XmlModeEnum::DESCRIPTION:
        if (length > 0) {
            switch (line.front().unicode()) {
            case C_SLASH:
                if (line.startsWith("/description")) {
                    m_xmlMode = XmlModeEnum::ROOM;
                    m_lineFlags.remove(LineFlagEnum::DESCRIPTION);
                }
                break;
            }
        }
        break;
    case XmlModeEnum::EXITS:
        if (length > 0) {
            switch (line.front().unicode()) {
            case C_SLASH:
                if (line.startsWith("/exits")) {
                    if (!m_lineFlags.isSnoop()) {
                        std::ostringstream os;
                        parseExits(os);
                        sendToUser(SendToUserSourceEnum::SimulatedOutput, os.str());
                        m_exitsReady = true;
                    }
                    m_lineFlags.remove(LineFlagEnum::EXITS);
                    m_xmlMode = (m_lineFlags.contains(LineFlagEnum::ROOM) ? XmlModeEnum::ROOM
                                                                          : XmlModeEnum::NONE);
                }
                break;
            }
        }
        break;
    case XmlModeEnum::PROMPT:
        if (length > 0) {
            switch (line.front().unicode()) {
            case C_SLASH:
                if (line.startsWith("/prompt")) {
                    m_xmlMode = XmlModeEnum::NONE;
                    m_lineFlags.remove(LineFlagEnum::PROMPT);
                    m_commonData.overrideSendPrompt = false;

                    if (m_eventReady) {
                        move();
                    }
                }
                break;
            }
        }
        break;
    case XmlModeEnum::TERRAIN:
        if (length > 0) {
            switch (line.front().unicode()) {
            case C_SLASH:
                if (line.startsWith("/terrain")) {
                    m_xmlMode = XmlModeEnum::ROOM;
                    m_lineFlags.remove(LineFlagEnum::TERRAIN);
                }
                break;
            }
        }
        break;
    case XmlModeEnum::HEADER:
        if (length > 0) {
            switch (line.front().unicode()) {
            case C_SLASH:
                if (line.startsWith("/header")) {
                    m_xmlMode = XmlModeEnum::ROOM;
                    m_lineFlags.remove(LineFlagEnum::HEADER);
                }
                break;
            }
        }
        break;
    }

    return true;
}

QString MumeXmlParser::characters(QString &ch)
{
    if (ch.isEmpty()) {
        return ch;
    }

    // replace > and < chars
    decodeXmlEntities(ch);

    const auto &config = getConfig();

    QString toUser;

    const XmlModeEnum mode = std::invoke([this]() -> XmlModeEnum {
        if (m_lineFlags.isPrompt()) {
            return XmlModeEnum::PROMPT;
        } else if (m_lineFlags.isExits()) {
            return XmlModeEnum::EXITS;
        } else if (m_lineFlags.isName()) {
            return XmlModeEnum::NAME;
        } else if (m_lineFlags.isDescription()) {
            return XmlModeEnum::DESCRIPTION;
        } else if (m_lineFlags.isRoom()) {
            return XmlModeEnum::ROOM;
        }
        return m_xmlMode;
    });

    switch (mode) {
    case XmlModeEnum::NONE: // non room info
        if (ch.isEmpty()) { // standard end of description parsed
            if (m_descriptionReady && !m_exitsReady && config.mumeNative.emulatedExits
                && !m_lineFlags.isSnoop()) {
                m_exitsReady = true;
                std::ostringstream os;
                {
                    AnsiOstream aos{os};
                    emulateExits(aos, m_move);
                }
                sendToUser(SendToUserSourceEnum::SimulatedOutput, os.str());
            }
        } else {
            m_lineFlags.insert(LineFlagEnum::NONE);
            toUser.append(ch);
        }
        break;

    case XmlModeEnum::ROOM: // dynamic line
        if (!m_descriptionReady && !m_lineFlags.isSnoop()) {
            // REVISIT: Ask a Vala to build GMCP Room.Objects so we can use that and Room.Chars
            m_stringBuffer += ch;
        }
        toUser.append(ch);
        break;

    case XmlModeEnum::NAME:
        toUser.append(ch);
        break;

    case XmlModeEnum::DESCRIPTION: // static line
        toUser.append(ch);
        break;

    case XmlModeEnum::EXITS:
        if (m_lineFlags.isSnoop()) {
            toUser.append(ch);
        } else {
            m_commonData.exits += ch;
        }
        break;

    case XmlModeEnum::PROMPT:
        // Store prompts in case an internal command is executed
        m_commonData.lastPrompt += ch;
        toUser.append(ch);
        break;

    case XmlModeEnum::HEADER:
    case XmlModeEnum::TERRAIN:
    default:
        toUser.append(ch);
        break;
    }

    return toUser;
}

void MumeXmlParser::setMove(const CommandEnum move)
{
    m_move = move;
}

void MumeXmlParser::move()
{
    m_descriptionReady = false;
    m_eventReady = false;

    // non-standard end of description parsed (blindness, fog, dark or so ...)
    if (!m_commonData.roomName.has_value()) {
        m_commonData.roomContents.reset();
        m_commonData.roomDesc.reset();
    }

    const auto emitEvent = [this]() {
        // REVISIT: once this isn't a signal/slot anymore, we won't need to create a shared event?
        auto ev = ParseEvent::createSharedEvent(m_move,
                                                m_serverId,
                                                m_commonData.roomArea.value_or(RoomArea{}),
                                                m_commonData.roomName.value_or(RoomName{}),
                                                m_commonData.roomDesc.value_or(RoomDesc{}),
                                                m_commonData.roomContents.value_or(RoomContents{}),
                                                m_commonData.exitIds,
                                                m_commonData.terrain,
                                                m_commonData.roomExits,
                                                m_commonData.promptFlags,
                                                m_commonData.connectedRoomFlags);

        onHandleParseEvent(SigParseEvent{ev});
    };

    emitEvent();
    auto &queue = getQueue();
    if (!queue.isEmpty()) {
        const CommandEnum c = queue.dequeue();
        pathChanged();
        if (c != m_move) {
            MMLOG() << "[XML parser] move " << getUppercase(m_move) << " doesn't match command "
                    << getUppercase(c);
            queue.clear();
        }
    }

    setMove(CommandEnum::LOOK);
}

void MumeXmlParser::parseMudCommands(const QString &str)
{
    // REVISIT: Add XML tag-based actions that match on a given LineFlag
    const auto stdString = mmqt::toStdStringUtf8(str);
    if (evalActionMap(StringView{stdString})) {
        return;
    }
}
