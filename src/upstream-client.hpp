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

#include "protocol.hpp"
#include "settings.hpp"

#include <QObject>
#include <QString>

class WsTransport;
class QTimer;

enum class UpstreamStatus { Disconnected, Connecting, Connected, Reconnecting, Error };

class UpstreamClient : public QObject {
	Q_OBJECT

public:
	explicit UpstreamClient(QObject *parent = nullptr);
	~UpstreamClient() override;

	void setSettings(const PluginSettings &settings);
	void connectToUpstream();
	void disconnectFromUpstream();
	void sendFrame(const Frame &frame);

	bool isConnected() const;
	UpstreamStatus status() const { return currentStatus; }
	QString statusText() const;
	QString lastError() const { return lastErrorText; }

signals:
	void statusChanged();
	void connected();
	void frameReceived(const Frame &frame);

private:
	void setStatus(UpstreamStatus status, const QString &error = {});
	void openSocket();
	void closeSocket();
	void scheduleReconnect();
	void onTransportConnected();
	void onTransportDisconnected();
	void onTransportError(const QString &message);
	void onTransportText(const QString &text);

	PluginSettings settings;
	WsTransport *transport = nullptr;
	QTimer *reconnectTimer = nullptr;
	UpstreamStatus currentStatus = UpstreamStatus::Disconnected;
	QString lastErrorText;
	int attempt = 0;
	bool userDisconnect = false;
	bool wantConnected = false;
};
