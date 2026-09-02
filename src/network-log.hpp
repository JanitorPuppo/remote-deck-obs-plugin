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

#include <obs-module.h>
#include <plugin-support.h>

#include <QByteArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace remote_deck_log {

inline QByteArray sanitizeBodySnippet(const QByteArray &body, int maxLen = 256)
{
	QByteArray snippet = body.left(maxLen);
	for (char &c : snippet) {
		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
	}
	return snippet;
}

inline void logEndpoint(const char *context, const QUrl &url)
{
	if (!url.isValid() || url.host().isEmpty()) {
		obs_log(LOG_WARNING, "%s: invalid URL", context);
		return;
	}
	obs_log(LOG_INFO, "%s: %s", context, url.toString(QUrl::RemovePassword).toUtf8().constData());
}

inline void logHttpRequest(const char *context, const QUrl &url, const char *method)
{
	obs_log(LOG_INFO, "%s request: %s %s", context, method,
		url.toString(QUrl::RemovePassword).toUtf8().constData());
}

inline void logHttpReply(const char *context, const QNetworkReply *reply, const QByteArray &body = {},
			 bool logBodyOnSuccess = false)
{
	if (!reply)
		return;

	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const QUrl requested = reply->request().url();
	const QUrl effective = reply->url();
	const int qtError = static_cast<int>(reply->error());

	obs_log(LOG_INFO,
		"%s reply: requested=%s effective=%s http=%d qt_error=%d (%s)",
		context, requested.toString(QUrl::RemovePassword).toUtf8().constData(),
		effective.toString(QUrl::RemovePassword).toUtf8().constData(), status, qtError,
		reply->errorString().toUtf8().constData());

	const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
	if (redirect.isValid()) {
		const QUrl target = redirect.toUrl();
		if (target.isValid()) {
			obs_log(LOG_INFO, "%s redirect target: %s", context,
				target.toString(QUrl::RemovePassword).toUtf8().constData());
		}
	}

	const bool failed = qtError != QNetworkReply::NoError || status >= 400;
	if (!body.isEmpty() && (failed || logBodyOnSuccess)) {
		obs_log(failed ? LOG_WARNING : LOG_INFO, "%s response body: %s", context,
			sanitizeBodySnippet(body).constData());
	}
}

} // namespace remote_deck_log
