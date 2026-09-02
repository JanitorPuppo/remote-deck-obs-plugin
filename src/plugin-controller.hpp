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

#include "device-auth.hpp"
#include "obs-audio.hpp"
#include "settings.hpp"
#include "upstream-client.hpp"

#include <functional>

#include <QObject>
#include <QString>

class SettingsDialog;

class PluginController : public QObject {
	Q_OBJECT

public:
	explicit PluginController(QObject *parent = nullptr);
	~PluginController() override;

	bool init();
	void start();
	void stop();
	void refreshSources();
	void showSettings();

	PluginSettings settings() const { return currentSettings; }
	void saveSettings(const PluginSettings &settings);
	void connectUpstream();
	void disconnectUpstream();
	void authenticate();
	void cancelAuthentication();
	void signOut();
	bool isAuthenticating() const;
	QString userCode() const { return authUserCode; }
	QString statusText() const;

signals:
	void statusChanged();

private:
	void sendHelloAndInputs();
	void sendInputs(const QString &id = {});
	void handleFrame(const Frame &frame);
	void applyOnObsThread(const std::function<void()> &fn);
	void onAuthCompleted(const QString &token, const QString &apiBase, const QString &wssUrl,
			     const QString &studioId, const QString &studioName);
	void onAuthFailed(const QString &message);

	PluginSettings currentSettings;
	ObsAudio audio;
	UpstreamClient client;
	DeviceAuth deviceAuth;
	SettingsDialog *dialog = nullptr;
	QString authStatus;
	QString authUserCode;
	bool started = false;
};
