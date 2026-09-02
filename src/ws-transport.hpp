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

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

class QAbstractSocket;
class QTimer;

class WsTransport : public QObject {
	Q_OBJECT

public:
	explicit WsTransport(QObject *parent = nullptr);
	~WsTransport() override;

	void connectTo(const QUrl &url, const QList<QPair<QByteArray, QByteArray>> &headers);
	void disconnectFromServer();
	void sendText(const QString &text);

signals:
	void connected();
	void disconnected();
	void textMessageReceived(const QString &text);
	void errorOccurred(const QString &message);

private:
	void abortSocket();
	void fail(const QString &message);
	void sendHandshake();
	void onReadyRead();
	void readHandshake();
	void readFrames();
	void handleFrame(bool fin, quint8 opcode, const QByteArray &payload);
	void finishFragment();

	QAbstractSocket *socket = nullptr;
	QTimer *connectTimer = nullptr;
	QUrl target;
	QList<QPair<QByteArray, QByteArray>> extraHeaders;
	QByteArray secKey;
	QByteArray expectedAccept;
	QByteArray handshakeBuf;
	QByteArray frameBuf;
	QByteArray fragment;
	quint8 fragmentOpcode = 0;
	bool handshaking = false;
};
