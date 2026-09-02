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

#include "qt-tls-setup.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>
#include <QSslSocket>

bool ensureQtTlsBackend()
{
	if (QSslSocket::supportsSsl())
		return true;

	auto *app = QCoreApplication::instance();
	if (!app) {
		obs_log(LOG_WARNING, "Qt TLS setup skipped: QCoreApplication is not available");
		return false;
	}

	const char *dataPath = obs_get_module_data_path(obs_current_module());
	if (!dataPath || !*dataPath) {
		obs_log(LOG_WARNING, "Qt TLS setup skipped: module data path is unavailable");
		return false;
	}

	char *backendPath = obs_module_file("tls/qschannelbackend.dll");
	if (!backendPath) {
		obs_log(LOG_WARNING, "Qt TLS backend not bundled with plugin (expected data/tls/qschannelbackend.dll)");
		return false;
	}

	const QString backendFile = QString::fromUtf8(backendPath);
	bfree(backendPath);
	if (!QFileInfo::exists(backendFile)) {
		obs_log(LOG_WARNING, "Qt TLS backend path is missing on disk: %s", backendFile.toUtf8().constData());
		return false;
	}

	const QString dataRoot = QString::fromUtf8(dataPath);
	if (!app->libraryPaths().contains(dataRoot))
		app->addLibraryPath(dataRoot);

	QPluginLoader loader(backendFile);
	if (!loader.load()) {
		obs_log(LOG_WARNING, "Qt TLS backend failed to load: %s", loader.errorString().toUtf8().constData());
		return false;
	}

	if (QSslSocket::supportsSsl()) {
		obs_log(LOG_INFO, "Qt TLS enabled via bundled backend (%s)",
			QSslSocket::sslLibraryVersionString().toUtf8().constData());
		return true;
	}

	obs_log(LOG_WARNING, "Qt TLS backend loaded but SSL is still unavailable (data path: %s)",
		dataRoot.toUtf8().constData());
	return false;
}
