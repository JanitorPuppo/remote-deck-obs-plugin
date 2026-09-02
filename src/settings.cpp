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

#include "settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QHostInfo>
#include <QSysInfo>
#include <QUuid>

namespace {

char *configFilePath()
{
	return obs_module_get_config_path(obs_current_module(), "config.json");
}

void ensureConfigDir()
{
	char *dir = obs_module_get_config_path(obs_current_module(), "");
	if (!dir)
		return;
	os_mkdirs(dir);
	bfree(dir);
}

bool isLoopbackHost(const QString &host)
{
	return host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 ||
	       host == QLatin1String("127.0.0.1") || host == QLatin1String("::1");
}

}

QUrl remoteDeckControlUrl(const QString &apiBase)
{
	QUrl base(apiBase.isEmpty() ? QString::fromUtf8(kDefaultRemoteDeckApiBase) : apiBase.trimmed());
	if (!base.isValid() || base.host().isEmpty())
		base = QUrl(QString::fromUtf8(kDefaultRemoteDeckApiBase));

	QUrl ws;
	const QString scheme = base.scheme().toLower();
	ws.setScheme(scheme == QLatin1String("http") || scheme == QLatin1String("ws") ? QStringLiteral("ws")
										      : QStringLiteral("wss"));
	ws.setHost(base.host());
	if (base.port() > 0)
		ws.setPort(base.port());
	const QString path = base.path();
	if (path.endsWith(QLatin1String("/ws/plugin")))
		ws.setPath(path);
	else if (path.endsWith(QLatin1String("/plugin")))
		ws.setPath(QString::fromUtf8(kRemoteDeckPluginPath));
	else if (path.isEmpty() || path == QLatin1String("/"))
		ws.setPath(QString::fromUtf8(kRemoteDeckPluginPath));
	else
		ws.setPath(path.endsWith(QLatin1Char('/')) ? path + QLatin1String("ws/plugin")
							   : path + QLatin1String("/ws/plugin"));
	return ws;
}

QString PluginSettings::authApiBase() const
{
#ifdef REMOTE_DECK_LOCAL_DEV
	if (!remoteDeckApiBase.trimmed().isEmpty())
		return remoteDeckApiBase.trimmed();
	return QString::fromUtf8(kDefaultLocalRemoteDeckApiBase);
#else
	return QString::fromUtf8(kDefaultRemoteDeckApiBase);
#endif
}

QUrl PluginSettings::controlUrl() const
{
	if (!remoteDeckWssUrl.trimmed().isEmpty()) {
		const QUrl stored(remoteDeckWssUrl.trimmed());
#ifndef REMOTE_DECK_LOCAL_DEV
		if (isLoopbackHost(stored.host()) ||
		    stored.host().compare(QLatin1String("api.remotedeck.gg"), Qt::CaseInsensitive) == 0)
			return remoteDeckControlUrl(QString::fromUtf8(kDefaultRemoteDeckApiBase));
#endif
		return stored;
	}
	return remoteDeckControlUrl(authApiBase());
}

QString PluginSettings::authorizationHeader() const
{
	const QString raw = remoteDeckToken.trimmed();
	if (raw.isEmpty())
		return {};
	if (raw.contains(QLatin1Char(' ')))
		return raw;
	return QStringLiteral("Bearer ") + raw;
}

QString PluginSettings::resolvedMachineLabel() const
{
	if (!machineLabel.trimmed().isEmpty())
		return machineLabel.trimmed();
	const QString host = QHostInfo::localHostName();
	if (!host.isEmpty())
		return host;
	return QSysInfo::machineHostName();
}

QString PluginSettings::hostLabel() const
{
	const QUrl url = controlUrl();
	if (!url.isValid() || url.host().isEmpty())
		return {};
	QString label = url.host();
	if (url.port() > 0)
		label += QLatin1Char(':') + QString::number(url.port());
	return label;
}

bool PluginSettings::requiresTls() const
{
	return !isLoopbackHost(controlUrl().host());
}

PluginSettings loadSettings()
{
	PluginSettings settings;
	char *path = configFilePath();
	if (path) {
		obs_data_t *data = obs_data_create_from_json_file(path);
		bfree(path);
		if (data) {
			settings.remoteDeckApiBase = QString::fromUtf8(obs_data_get_string(data, "remote_deck_api_base"));
			settings.remoteDeckToken = QString::fromUtf8(obs_data_get_string(data, "remote_deck_token"));
			settings.remoteDeckWssUrl = QString::fromUtf8(obs_data_get_string(data, "remote_deck_wss_url"));
			settings.studioId = QString::fromUtf8(obs_data_get_string(data, "studio_id"));
			settings.studioName = QString::fromUtf8(obs_data_get_string(data, "studio_name"));
			settings.machineLabel = QString::fromUtf8(obs_data_get_string(data, "machine_label"));
			settings.instanceId = QString::fromUtf8(obs_data_get_string(data, "instance_id"));
			settings.autoConnect = obs_data_get_bool(data, "auto_connect");
			if (!obs_data_has_user_value(data, "auto_connect"))
				settings.autoConnect = true;

			obs_data_release(data);
			obs_log(LOG_INFO,
				"loaded settings (auto_connect=%s authenticated=%s auth_api=%s wss=%s studio=%s)",
				settings.autoConnect ? "true" : "false", settings.isAuthenticated() ? "true" : "false",
				settings.authApiBase().toUtf8().constData(),
				settings.remoteDeckWssUrl.isEmpty() ? "(default)" : settings.remoteDeckWssUrl.toUtf8().constData(),
				settings.studioName.isEmpty() ? "(none)" : settings.studioName.toUtf8().constData());
		}
	}
	if (ensureInstanceId(settings))
		saveSettings(settings);
	return settings;
}

bool ensureInstanceId(PluginSettings &settings)
{
	const QUuid parsed = QUuid::fromString(settings.instanceId);
	const QString normalized = parsed.isNull() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
						   : parsed.toString(QUuid::WithoutBraces);
	if (settings.instanceId == normalized)
		return false;
	settings.instanceId = normalized;
	return true;
}

bool saveSettings(const PluginSettings &settings)
{
	ensureConfigDir();
	char *path = configFilePath();
	if (!path)
		return false;

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "remote_deck_api_base", settings.remoteDeckApiBase.toUtf8().constData());
	obs_data_set_string(data, "remote_deck_token", settings.remoteDeckToken.toUtf8().constData());
	obs_data_set_string(data, "remote_deck_wss_url", settings.remoteDeckWssUrl.toUtf8().constData());
	obs_data_set_string(data, "studio_id", settings.studioId.toUtf8().constData());
	obs_data_set_string(data, "studio_name", settings.studioName.toUtf8().constData());
	obs_data_set_string(data, "machine_label", settings.machineLabel.toUtf8().constData());
	obs_data_set_string(data, "instance_id", settings.instanceId.toUtf8().constData());
	obs_data_set_bool(data, "auto_connect", settings.autoConnect);

	const bool ok = obs_data_save_json_safe(data, path, "tmp", "bak");
	obs_data_release(data);
	bfree(path);
	if (!ok)
		obs_log(LOG_WARNING, "failed to save settings");
	else
		obs_log(LOG_INFO, "saved settings");
	return ok;
}
