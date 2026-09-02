/*
obs-remote-deck
Copyright (C) 2026 Remote Deck

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "protocol.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

std::optional<Frame> parseFrame(const QString &text)
{
	QJsonParseError error;
	const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject())
		return std::nullopt;

	const QJsonObject obj = doc.object();
	if (!obj.contains("type") || !obj.value("type").isString())
		return std::nullopt;

	Frame frame;
	frame.version = obj.value("v").toInt(kProtocolVersion);
	frame.type = obj.value("type").toString();
	frame.id = obj.value("id").toString();
	if (obj.value("payload").isObject())
		frame.payload = obj.value("payload").toObject();
	return frame;
}

QString serializeFrame(const Frame &frame)
{
	QJsonObject obj;
	obj.insert("v", frame.version > 0 ? frame.version : kProtocolVersion);
	obj.insert("type", frame.type);
	if (!frame.id.isEmpty())
		obj.insert("id", frame.id);
	if (!frame.payload.isEmpty())
		obj.insert("payload", frame.payload);
	return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

Frame makeFrame(const QString &type, const QJsonObject &payload, const QString &id)
{
	Frame frame;
	frame.type = type;
	frame.payload = payload;
	frame.id = id;
	return frame;
}

Frame makeErrorFrame(const QString &code, const QString &message, const QString &id)
{
	QJsonObject payload;
	payload.insert("code", code);
	payload.insert("message", message);
	return makeFrame(protocol::kError, payload, id);
}
