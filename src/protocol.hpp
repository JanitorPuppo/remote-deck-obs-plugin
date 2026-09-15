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

#pragma once

#include <optional>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

constexpr int kProtocolVersion = 1;

namespace protocol {

constexpr auto kHello = "hello";
constexpr auto kInputs = "inputs";
constexpr auto kState = "state";
constexpr auto kInputMute = "input.mute";
constexpr auto kInputVolume = "input.volume";
constexpr auto kInputFilterEnable = "input.filter.enable";
constexpr auto kInputFilterUpdate = "input.filter.update";
constexpr auto kInputFilterAdd = "input.filter.add";
constexpr auto kInputFilterRemove = "input.filter.remove";
constexpr auto kInputsGet = "inputs.get";
constexpr auto kPing = "ping";
constexpr auto kPong = "pong";
constexpr auto kError = "error";

constexpr auto kErrUnknownMethod = "unknown_method";
constexpr auto kErrInvalidPayload = "invalid_payload";
constexpr auto kErrInputNotFound = "input_not_found";
constexpr auto kErrFilterNotFound = "filter_not_found";
constexpr auto kErrUnsupportedFilterKind = "unsupported_filter_kind";
constexpr auto kErrFilterNameConflict = "filter_name_conflict";
constexpr auto kErrFilterCreateFailed = "filter_create_failed";
constexpr auto kErrProtocol = "protocol_error";

constexpr auto kFeatureFilters = "filters";

}

QJsonArray supportedAudioFilterKinds();

struct Frame {
	int version = kProtocolVersion;
	QString type;
	QString id;
	QJsonObject payload;
};

std::optional<Frame> parseFrame(const QString &text);
QString serializeFrame(const Frame &frame);

Frame makeFrame(const QString &type, const QJsonObject &payload = {}, const QString &id = {});
Frame makeErrorFrame(const QString &code, const QString &message, const QString &id = {});
