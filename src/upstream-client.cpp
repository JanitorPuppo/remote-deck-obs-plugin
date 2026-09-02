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

#include "upstream-client.hpp"
#include "ws-transport.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QPair>
#include <QRandomGenerator>
#include <QTimer>

namespace {
constexpr int kMaxBackoffMs = 30000;
}

UpstreamClient::UpstreamClient(QObject *parent) : QObject(parent)
{
	reconnectTimer = new QTimer(this);
	reconnectTimer->setSingleShot(true);
	connect(reconnectTimer, &QTimer::timeout, this, &UpstreamClient::openSocket);
}

UpstreamClient::~UpstreamClient()
{
	userDisconnect = true;
	closeSocket();
}

void UpstreamClient::setSettings(const PluginSettings &next)
{
	settings = next;
}

void UpstreamClient::connectToUpstream()
{
	userDisconnect = false;
	wantConnected = true;
	attempt = 0;
	reconnectTimer->stop();
	openSocket();
}

void UpstreamClient::disconnectFromUpstream()
{
	userDisconnect = true;
	wantConnected = false;
	reconnectTimer->stop();
	closeSocket();
	setStatus(UpstreamStatus::Disconnected);
}

void UpstreamClient::sendFrame(const Frame &frame)
{
	if (!transport || currentStatus != UpstreamStatus::Connected)
		return;
	transport->sendText(serializeFrame(frame));
}

bool UpstreamClient::isConnected() const
{
	return currentStatus == UpstreamStatus::Connected;
}

QString UpstreamClient::statusText() const
{
	switch (currentStatus) {
	case UpstreamStatus::Connecting:
		return QStringLiteral("Connecting to %1…").arg(settings.hostLabel());
	case UpstreamStatus::Connected:
		return QStringLiteral("Connected to %1").arg(settings.hostLabel());
	case UpstreamStatus::Reconnecting:
		return QStringLiteral("Reconnecting to %1…").arg(settings.hostLabel());
	case UpstreamStatus::Error:
		return lastErrorText.isEmpty() ? QStringLiteral("Disconnected") : lastErrorText;
	case UpstreamStatus::Disconnected:
	default:
		return QStringLiteral("Disconnected");
	}
}

void UpstreamClient::setStatus(UpstreamStatus status, const QString &error)
{
	currentStatus = status;
	if (!error.isEmpty())
		lastErrorText = error;
	else if (status != UpstreamStatus::Error)
		lastErrorText.clear();
	emit statusChanged();
}

void UpstreamClient::openSocket()
{
	closeSocket();

	const QUrl url = settings.controlUrl();
	if (!url.isValid() || url.host().isEmpty() ||
	    (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss"))) {
		setStatus(UpstreamStatus::Error, QStringLiteral("Enter a valid ws:// or wss:// server address."));
		wantConnected = false;
		return;
	}
	if (settings.requiresTls() && url.scheme() != QLatin1String("wss")) {
		setStatus(UpstreamStatus::Error, QStringLiteral("Remote Deck requires TLS (wss)."));
		wantConnected = false;
		return;
	}
	if (settings.remoteDeckToken.trimmed().isEmpty()) {
		setStatus(UpstreamStatus::Error, QStringLiteral("Authenticate with Remote Deck first."));
		wantConnected = false;
		return;
	}

	setStatus(attempt == 0 ? UpstreamStatus::Connecting : UpstreamStatus::Reconnecting);
	obs_log(LOG_INFO, "connecting to %s://%s", url.scheme().toUtf8().constData(),
		settings.hostLabel().toUtf8().constData());

	QList<QPair<QByteArray, QByteArray>> headers;
	const QString auth = settings.authorizationHeader();
	if (!auth.isEmpty())
		headers.append({QByteArray("Authorization"), auth.toUtf8()});

	transport = new WsTransport(this);
	connect(transport, &WsTransport::connected, this, &UpstreamClient::onTransportConnected);
	connect(transport, &WsTransport::disconnected, this, &UpstreamClient::onTransportDisconnected);
	connect(transport, &WsTransport::errorOccurred, this, &UpstreamClient::onTransportError);
	connect(transport, &WsTransport::textMessageReceived, this, &UpstreamClient::onTransportText);
	transport->connectTo(url, headers);
}

void UpstreamClient::closeSocket()
{
	if (!transport)
		return;
	transport->disconnect();
	transport->disconnectFromServer();
	transport->deleteLater();
	transport = nullptr;
}

void UpstreamClient::scheduleReconnect()
{
	if (!wantConnected || userDisconnect)
		return;
	attempt = qMin(attempt + 1, 8);
	const int base = qMin(kMaxBackoffMs, 1000 * (1 << qMin(attempt, 5)));
	const int jitter = QRandomGenerator::global()->bounded(qMax(1, base / 4));
	const int delay = qMax(250, base - jitter / 2 + jitter);
	obs_log(LOG_INFO, "reconnect scheduled in %d ms", delay);
	setStatus(UpstreamStatus::Reconnecting);
	reconnectTimer->start(delay);
}

void UpstreamClient::onTransportConnected()
{
	attempt = 0;
	setStatus(UpstreamStatus::Connected);
	obs_log(LOG_INFO, "connected to %s", settings.hostLabel().toUtf8().constData());
	emit connected();
}

void UpstreamClient::onTransportDisconnected()
{
	closeSocket();
	if (userDisconnect || !wantConnected) {
		if (currentStatus != UpstreamStatus::Error && currentStatus != UpstreamStatus::Disconnected)
			setStatus(UpstreamStatus::Disconnected);
		return;
	}
	scheduleReconnect();
}

void UpstreamClient::onTransportError(const QString &message)
{
	obs_log(LOG_WARNING, "upstream error: %s", message.toUtf8().constData());
	lastErrorText = message;
}

void UpstreamClient::onTransportText(const QString &text)
{
	auto frame = parseFrame(text);
	if (!frame) {
		sendFrame(makeErrorFrame(protocol::kErrProtocol, QStringLiteral("Invalid JSON frame")));
		return;
	}
	emit frameReceived(*frame);
}
