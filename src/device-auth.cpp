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
#include "network-log.hpp"

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

QString httpAuthErrorMessage(int status)
{
	if (status == 405)
		return QStringLiteral(
			"Remote Deck rejected the request (HTTP 405). Use https://www.remotedeck.gg as the API URL.");
	if (status >= 400)
		return QStringLiteral("Remote Deck returned HTTP %1. Check the API URL and try again.").arg(status);
	return {};
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

	obs_log(LOG_INFO, "Remote Deck device auth starting (api_base=%s)", apiBase.toUtf8().constData());

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
	if (activeReply) {
		activeReply->abort();
		activeReply = nullptr;
	}
	deviceCode.clear();
	pollToken.clear();
	busy = false;
}

void DeviceAuth::postJson(const QUrl &url, const QByteArray &body, void (DeviceAuth::*handler)(QNetworkReply *))
{
	if (activeReply) {
		activeReply->abort();
		activeReply = nullptr;
	}

	remote_deck_log::logHttpRequest("Remote Deck device auth", url, "POST");
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	auto *reply = nam->post(req, body);
	activeReply = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
		if (activeReply == reply)
			activeReply = nullptr;
		reply->deleteLater();
		(this->*handler)(reply);
	});
}

void DeviceAuth::onDeviceStarted(QNetworkReply *reply)
{
	if (!busy)
		return;

	const QByteArray body = reply->readAll();
	remote_deck_log::logHttpReply("Remote Deck device auth", reply, body);

	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (const QString httpMessage = httpAuthErrorMessage(status); !httpMessage.isEmpty()) {
		finishError(httpMessage);
		return;
	}

	if (reply->error() != QNetworkReply::NoError) {
		const QString errText = reply->errorString();
		if (errText.contains(QLatin1String("TLS"), Qt::CaseInsensitive)) {
			finishError(QStringLiteral("Secure connection failed (TLS). Reinstall the plugin or restart OBS."));
			return;
		}
		finishError(QStringLiteral("Could not reach Remote Deck. Check your network and try again."));
		return;
	}

	const auto doc = QJsonDocument::fromJson(body);
	if (!doc.isObject()) {
		finishError(QStringLiteral("Remote Deck returned an unexpected response."));
		return;
	}
	const QJsonObject obj = doc.object();
	deviceCode = obj.value("deviceCode").toString();
	const QString userCode = obj.value("userCode").toString().trimmed();
	pollToken = obj.value("pollToken").toString();
	QString uri = obj.value("verificationUriComplete").toString();
	if (uri.isEmpty())
		uri = obj.value("verificationUri").toString();
	intervalMs = qMax(1000, obj.value("interval").toInt(3) * 1000);
	const QUrl verificationUrl(uri);
	if (deviceCode.isEmpty() || userCode.isEmpty() || pollToken.isEmpty() || uri.isEmpty() ||
	    !isSafeBrowserUrl(verificationUrl)) {
		obs_log(LOG_WARNING,
			"Remote Deck device auth response missing fields (deviceCode=%s userCode=%s pollToken=%s uri=%s)",
			deviceCode.isEmpty() ? "missing" : "ok", userCode.isEmpty() ? "missing" : "ok",
			pollToken.isEmpty() ? "missing" : "ok", uri.isEmpty() ? "missing" : "ok");
		finishError(QStringLiteral("Remote Deck returned an unexpected response."));
		return;
	}

	obs_log(LOG_INFO, "Remote Deck device auth started; opening browser (user_code=%s verification_uri=%s)",
		userCode.toUtf8().constData(), uri.toUtf8().constData());
	QDesktopServices::openUrl(verificationUrl);
	emit openedBrowser(uri, userCode);
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

	const QByteArray body = reply->readAll();
	const auto doc = QJsonDocument::fromJson(body);
	const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();

	if (reply->error() != QNetworkReply::NoError) {
		const QString err = obj.value("error").toString();
		if (err == QLatin1String("denied")) {
			remote_deck_log::logHttpReply("Remote Deck auth poll", reply, body);
			finishError(QStringLiteral("Authorization was denied."));
			return;
		}
		if (err == QLatin1String("expired") || err == QLatin1String("invalid_device")) {
			remote_deck_log::logHttpReply("Remote Deck auth poll", reply, body);
			finishError(QStringLiteral("Authorization expired. Try Authenticate again."));
			return;
		}
		if (reply->error() == QNetworkReply::TimeoutError ||
		    reply->error() == QNetworkReply::TemporaryNetworkFailureError) {
			obs_log(LOG_INFO, "Remote Deck auth poll transient error (%s); retrying",
				reply->errorString().toUtf8().constData());
			pollTimer->start(intervalMs);
			return;
		}
		remote_deck_log::logHttpReply("Remote Deck auth poll", reply, body);
		finishError(QStringLiteral("Could not finish authorization. Try again."));
		return;
	}

	const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (httpStatus >= 400) {
		remote_deck_log::logHttpReply("Remote Deck auth poll", reply, body);
		finishError(QStringLiteral("Remote Deck returned an unexpected response."));
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
			remote_deck_log::logHttpReply("Remote Deck auth poll", reply, body);
			finishError(QStringLiteral("Remote Deck returned an unexpected response."));
			return;
		}
		busy = false;
		deviceCode.clear();
		pollToken.clear();
		obs_log(LOG_INFO, "Remote Deck authorization complete (studio=%s wss=%s api=%s)",
			studioName.toUtf8().constData(), wss.toUtf8().constData(), nextApi.toUtf8().constData());
		emit completed(token, nextApi, wss, studioId, studioName);
		return;
	}

	finishError(QStringLiteral("Remote Deck returned an unexpected response."));
}

void DeviceAuth::finishError(const QString &message)
{
	cancel();
	obs_log(LOG_WARNING, "Remote Deck authorization failed: %s", message.toUtf8().constData());
	emit failed(message);
}
