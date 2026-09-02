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

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class DeviceAuth : public QObject {
	Q_OBJECT

public:
	explicit DeviceAuth(QObject *parent = nullptr);

	void start(const QString &apiBase, const QString &machineLabel, const QString &instanceId,
		   const QString &pluginVersion);
	void cancel();
	bool isBusy() const { return busy; }

signals:
	void openedBrowser(const QString &verificationUri);
	void completed(const QString &token, const QString &apiBase, const QString &wssUrl, const QString &studioId,
		       const QString &studioName);
	void failed(const QString &message);

private:
	void postJson(const QUrl &url, const QByteArray &body, void (DeviceAuth::*handler)(QNetworkReply *));
	void onDeviceStarted(QNetworkReply *reply);
	void onPollReply(QNetworkReply *reply);
	void pollOnce();
	void finishError(const QString &message);

	QNetworkAccessManager *nam = nullptr;
	QTimer *pollTimer = nullptr;
	QString apiBase;
	QString deviceCode;
	QString pollToken;
	int intervalMs = 3000;
	bool busy = false;
};
