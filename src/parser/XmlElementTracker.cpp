// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "XmlElementTracker.h"

#include "../global/Consts.h"
#include "../global/TextUtils.h"

#include <utility>

namespace {

/// The tag name is whatever precedes the first space or slash.
NODISCARD QString tagNameOf(const QString &tagBody)
{
    using namespace char_consts;
    qsizetype end = 0;
    while (end < tagBody.length()) {
        const QChar c = tagBody.at(end);
        if (c.isSpace() || c == QChar{C_SLASH}) {
            break;
        }
        ++end;
    }
    return tagBody.left(end).toLower();
}

} // namespace

void XmlElementTracker::receiveTag(const QString &rawTagBody)
{
    using namespace char_consts;

    const QString tagBody = rawTagBody.trimmed();
    if (tagBody.isEmpty()) {
        return;
    }

    if (tagBody.startsWith(QChar{C_SLASH})) {
        closeElement(tagNameOf(tagBody.mid(1)));
        return;
    }

    // <exit dir=north/> opens and closes in one tag, and carries no text.
    const bool selfClosing = tagBody.endsWith(QChar{C_SLASH});
    const QString name = tagNameOf(tagBody);
    if (name.isEmpty()) {
        return;
    }
    openElement(tagBody, name);
    if (selfClosing) {
        closeElement(name);
    }
}

void XmlElementTracker::receiveText(const QString &text)
{
    if (text.isEmpty() || m_open.empty()) {
        // Text outside every tag belongs to no element. It still reaches the user through
        // the ordinary terminal stream.
        return;
    }
    // Appended to every open element rather than folded in when a child closes, so that an
    // element's text is complete the moment it is handed over, children included.
    for (XmlElement &element : m_open) {
        const int room = MAX_TEXT_LENGTH - static_cast<int>(element.text.length());
        if (room <= 0) {
            element.truncated = true;
            continue;
        }
        if (text.length() > room) {
            element.text.append(text.left(room));
            element.truncated = true;
        } else {
            element.text.append(text);
        }
    }
}

std::vector<XmlElement> XmlElementTracker::take()
{
    return std::exchange(m_completed, {});
}

void XmlElementTracker::reset()
{
    m_open.clear();
    m_completed.clear();
    m_suppressed = 0;
}

void XmlElementTracker::openElement(const QString &tagBody, const QString &name)
{
    if (m_open.size() >= MAX_DEPTH) {
        // Refused, and counted so that the matching close is refused too. Letting the close
        // through would otherwise shut an element further out that is still legitimately
        // open.
        ++m_suppressed;
        if (!m_open.empty()) {
            m_open.back().truncated = true;
        }
        return;
    }

    XmlElement element;
    element.name = mmqt::toStdStringUtf8(name);
    element.tag = toXmlTag(element.name);
    element.attributes = parseXmlAttributes(tagBody);
    m_open.emplace_back(std::move(element));
}

void XmlElementTracker::closeElement(const QString &name)
{
    if (m_suppressed > 0) {
        --m_suppressed;
        return;
    }
    if (name.isEmpty() || m_open.empty()) {
        return;
    }

    const std::string wanted = mmqt::toStdStringUtf8(name);
    size_t index = m_open.size();
    for (size_t i = m_open.size(); i-- > 0;) {
        if (m_open[i].name == wanted) {
            index = i;
            break;
        }
    }
    if (index == m_open.size()) {
        // A closing tag for something that was never opened. MUME's XML is not strict, and
        // a client that resubscribes mid-element sees exactly this.
        return;
    }

    // Anything still open inside the element being closed never got its own closing tag.
    while (m_open.size() > index + 1) {
        popTop(true);
    }
    popTop(false);
}

void XmlElementTracker::popTop(const bool implicit)
{
    XmlElement element = std::move(m_open.back());
    m_open.pop_back();
    if (implicit) {
        element.truncated = true;
    }
    // Complete now, children and all, so its line can be read for the way someone went.
    element.direction = deriveMovementDirection(element);

    if (!m_open.empty()) {
        XmlElement &parent = m_open.back();
        // Loss anywhere inside an element is loss of that element: a consumer walking the
        // children would otherwise see a complete-looking tree with levels missing.
        if (implicit || element.truncated) {
            parent.truncated = true;
        }
        parent.children.emplace_back(std::move(element));
        return;
    }

    if (m_completed.size() >= MAX_PENDING) {
        // Nobody is draining. Dropping the newest keeps the oldest, which is the half more
        // likely to still be worth reading in order.
        return;
    }
    m_completed.emplace_back(std::move(element));
}
