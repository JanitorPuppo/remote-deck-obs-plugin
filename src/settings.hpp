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

#include <QString>
#include <QUrl>

struct PluginSettings {
	QString remoteDeckApiBase;
	QString remoteDeckToken;
	QString remoteDeckWssUrl;
	QString studioId;
	QString studioName;
	QString machineLabel;
	QString instanceId;
	QString skippedUpdateVersion;
	bool autoConnect = true;

	bool isAuthenticated() const { return !remoteDeckToken.trimmed().isEmpty(); }
	QString authApiBase() const;

	QUrl controlUrl() const;
	QString authorizationHeader() const;
	QString resolvedMachineLabel() const;
	QString hostLabel() const;
	bool requiresTls() const;
};

constexpr auto kDefaultRemoteDeckApiBase = "https://www.remotedeck.gg";
constexpr auto kDefaultLocalRemoteDeckApiBase = "http://localhost:3000";
constexpr auto kRemoteDeckPluginPath = "/ws/plugin";

PluginSettings loadSettings();
bool saveSettings(const PluginSettings &settings);
bool ensureInstanceId(PluginSettings &settings);
QUrl remoteDeckControlUrl(const QString &apiBase);
