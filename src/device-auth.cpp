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

#include "device-auth.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

bool isSafeBrowserUrl(const QUrl &url)
{
	if (!url.isValid() || url.host().isEmpty())
		return false;
	const QString scheme = url.scheme().toLower();
	if (scheme == QLatin1String("https"))
		return true;
	if (scheme == QLatin1String("http")) {
		const QString host = url.host();
		return host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 ||
		       host == QLatin1String("127.0.0.1") || host == QLatin1String("::1");
	}
	return false;
}

}

DeviceAuth::DeviceAuth(QObject *parent) : QObject(parent)
{
	nam = new QNetworkAccessManager(this);
	pollTimer = new QTimer(this);
	pollTimer->setSingleShot(true);
	connect(pollTimer, &QTimer::timeout, this, &DeviceAuth::pollOnce);
}

void DeviceAuth::start(const QString &apiBase_, const QString &machineLabel, const QString &instanceId,
		       const QString &pluginVersion)
{
	cancel();
	busy = true;
	apiBase = apiBase_;
	while (apiBase.endsWith(QLatin1Char('/')))
		apiBase.chop(1);

	QJsonObject body;
	body.insert("machineLabel", machineLabel);
	body.insert("clientId", QStringLiteral("obs-remote-deck"));
	body.insert("clientVersion", pluginVersion);
	if (!instanceId.isEmpty())
		body.insert("instanceId", instanceId);
	const QUrl url(apiBase + QStringLiteral("/public/plugin-auth/v1/device"));
	postJson(url, QJsonDocument(body).toJson(QJsonDocument::Compact), &DeviceAuth::onDeviceStarted);
}

void DeviceAuth::cancel()
{
	pollTimer->stop();
	deviceCode.clear();
	pollToken.clear();
	busy = false;
}

void DeviceAuth::postJson(const QUrl &url, const QByteArray &body, void (DeviceAuth::*handler)(QNetworkReply *))
{
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	auto *reply = nam->post(req, body);
	connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
		reply->deleteLater();
		(this->*handler)(reply);
	});
}

void DeviceAuth::onDeviceStarted(QNetworkReply *reply)
{
	if (!busy)
		return;
	if (reply->error() != QNetworkReply::NoError) {
		finishError(QStringLiteral("Could not reach Remote Deck. Check your network and try again."));
		return;
	}

	const auto doc = QJsonDocument::fromJson(reply->readAll());
	if (!doc.isObject()) {
		finishError(QStringLiteral("Remote Deck returned an unexpected response."));
		return;
	}
	const QJsonObject obj = doc.object();
	deviceCode = obj.value("deviceCode").toString();
	pollToken = obj.value("pollToken").toString();
	const QString uri = obj.value("verificationUriComplete").toString();
	intervalMs = qMax(1000, obj.value("interval").toInt(3) * 1000);
	const QUrl verificationUrl(uri);
	if (deviceCode.isEmpty() || pollToken.isEmpty() || uri.isEmpty() || !isSafeBrowserUrl(verificationUrl)) {
		finishError(QStringLiteral("Remote Deck returned an unexpected response."));
		return;
	}

	obs_log(LOG_INFO, "opened Remote Deck authorization in the browser");
	QDesktopServices::openUrl(verificationUrl);
	emit openedBrowser(uri);
	pollTimer->start(intervalMs);
}

void DeviceAuth::pollOnce()
{
	if (!busy)
		return;
	QJsonObject body;
	body.insert("deviceCode", deviceCode);
	body.insert("pollToken", pollToken);
	const QUrl url(apiBase + QStringLiteral("/public/plugin-auth/v1/token"));
	postJson(url, QJsonDocument(body).toJson(QJsonDocument::Compact), &DeviceAuth::onPollReply);
}

void DeviceAuth::onPollReply(QNetworkReply *reply)
{
	if (!busy)
		return;

	const auto doc = QJsonDocument::fromJson(reply->readAll());
	const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

	if (reply->error() != QNetworkReply::NoError) {
		const QString err = obj.value("error").toString();
		if (err == QLatin1String("denied")) {
			finishError(QStringLiteral("Authorization was denied."));
			return;
		}
		if (err == QLatin1String("expired") || err == QLatin1String("invalid_device")) {
			finishError(QStringLiteral("Authorization expired. Try Authenticate again."));
			return;
		}
		if (reply->error() == QNetworkReply::TimeoutError ||
		    reply->error() == QNetworkReply::TemporaryNetworkFailureError) {
			pollTimer->start(intervalMs);
			return;
		}
		finishError(QStringLiteral("Could not finish authorization. Try again."));
		return;
	}

	const QString status = obj.value("status").toString();
	if (status == QLatin1String("pending")) {
		pollTimer->start(intervalMs);
		return;
	}
	if (status == QLatin1String("complete")) {
		const QString token = obj.value("studioPluginToken").toString();
		const QString nextApi = obj.value("apiBase").toString();
		const QString wss = obj.value("wssUrl").toString();
		const QString studioId = obj.value("studioId").toString();
		const QString studioName = obj.value("studioName").toString();
		if (token.isEmpty() || wss.isEmpty()) {
			finishError(QStringLiteral("Remote Deck returned an unexpected response."));
			return;
		}
		busy = false;
		deviceCode.clear();
		pollToken.clear();
		obs_log(LOG_INFO, "Remote Deck authorization complete");
		emit completed(token, nextApi, wss, studioId, studioName);
		return;
	}

	finishError(QStringLiteral("Remote Deck returned an unexpected response."));
}

void DeviceAuth::finishError(const QString &message)
{
	cancel();
	obs_log(LOG_WARNING, "Remote Deck authorization failed");
	emit failed(message);
}
