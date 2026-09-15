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

#include "plugin-controller.hpp"
#include "settings-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QAbstractButton>
#include <QMessageBox>

#include <functional>

PluginController::PluginController(QObject *parent) : QObject(parent)
{
	connect(&client, &UpstreamClient::statusChanged, this, &PluginController::statusChanged);
	connect(&client, &UpstreamClient::connected, this, &PluginController::sendHelloAndInputs);
	connect(&client, &UpstreamClient::frameReceived, this, &PluginController::handleFrame);
	connect(&audio, &ObsAudio::inputStateChanged, this, [this](const QJsonObject &state) {
		if (client.isConnected())
			client.sendFrame(makeFrame(protocol::kState, state));
	});
	connect(&audio, &ObsAudio::inputsChanged, this, [this]() {
		if (client.isConnected())
			sendInputs();
	});
	connect(&deviceAuth, &DeviceAuth::openedBrowser, this, [this](const QString &, const QString &userCode) {
		authUserCode = userCode;
		authStatus = QStringLiteral("Enter the code in your browser to finish signing in.");
		emit statusChanged();
	});
	connect(&deviceAuth, &DeviceAuth::completed, this, &PluginController::onAuthCompleted);
	connect(&deviceAuth, &DeviceAuth::failed, this, &PluginController::onAuthFailed);
	connect(&autoUpdate, &AutoUpdate::stateChanged, this, [this]() {
		emit statusChanged();

		if (pendingUpdateInstall && autoUpdate.updateFailed())
			pendingUpdateInstall = false;

		if (pendingUpdateInstall && autoUpdate.updateReady()) {
			autoUpdate.applyUpdateAndQuit();
			return;
		}

		const QString version = autoUpdate.availableVersion();
		if (autoUpdate.updateAvailable() && !autoUpdate.isBusy() && !autoUpdate.updateReady() &&
		    !version.isEmpty() && version != updatePromptShownForVersion) {
			updatePromptShownForVersion = version;
			auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			QMessageBox box(main);
			box.setWindowTitle(QString::fromUtf8(obs_module_text("RemoteDeck.SettingsTitle")));
			box.setText(autoUpdate.statusText());
			box.setInformativeText(QString::fromUtf8(obs_module_text("RemoteDeck.UpdatePromptDetail")));
			box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Ignore);
			box.setDefaultButton(QMessageBox::Yes);
			box.button(QMessageBox::Yes)->setText(QString::fromUtf8(obs_module_text("RemoteDeck.UpdateNow")));
			box.button(QMessageBox::No)->setText(QString::fromUtf8(obs_module_text("RemoteDeck.UpdateLater")));
			box.button(QMessageBox::Ignore)->setText(QString::fromUtf8(obs_module_text("RemoteDeck.UpdateSkip")));
			const int choice = box.exec();
			if (choice == QMessageBox::Yes)
				downloadAndInstallUpdate();
			else if (choice == QMessageBox::Ignore)
				autoUpdate.skipAvailableVersion();
		}
	});
}

void PluginController::downloadAndInstallUpdate()
{
	if (autoUpdate.updateReady()) {
		autoUpdate.applyUpdateAndQuit();
		return;
	}
	pendingUpdateInstall = true;
	autoUpdate.downloadUpdate();
}

PluginController::~PluginController()
{
	stop();
	delete dialog;
}

bool PluginController::init()
{
	currentSettings = loadSettings();
	client.setSettings(currentSettings);
	return true;
}

void PluginController::start()
{
	if (started)
		return;
	started = true;
	audio.start();
	autoUpdate.scheduleCheck();
	if (currentSettings.autoConnect)
		connectUpstream();
}

void PluginController::stop()
{
	client.disconnectFromUpstream();
	audio.stop();
	started = false;
}

void PluginController::refreshSources()
{
	audio.refresh();
	if (client.isConnected())
		sendInputs();
}

void PluginController::showSettings()
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!dialog)
		dialog = new SettingsDialog(this, main);
	else
		dialog->reload();
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void PluginController::saveSettings(const PluginSettings &settings)
{
	currentSettings = settings;
	client.setSettings(currentSettings);
	::saveSettings(currentSettings);
}

void PluginController::connectUpstream()
{
	if (!currentSettings.isAuthenticated())
		return;
	audio.start();
	client.setSettings(currentSettings);
	client.connectToUpstream();
}

void PluginController::disconnectUpstream()
{
	client.disconnectFromUpstream();
}

void PluginController::authenticate()
{
	if (deviceAuth.isBusy())
		return;
	currentSettings.machineLabel = currentSettings.resolvedMachineLabel();
	ensureInstanceId(currentSettings);
	::saveSettings(currentSettings);
	authUserCode.clear();
	authStatus = QStringLiteral("Opening Remote Deck…");
	emit statusChanged();
	obs_log(LOG_INFO, "Remote Deck authenticate requested (api_base=%s machine=%s instance_id=%s)",
		currentSettings.authApiBase().toUtf8().constData(),
		currentSettings.resolvedMachineLabel().toUtf8().constData(),
		currentSettings.instanceId.toUtf8().constData());
	deviceAuth.start(currentSettings.authApiBase(), currentSettings.resolvedMachineLabel(),
			 currentSettings.instanceId, QString::fromUtf8(PLUGIN_VERSION));
}

void PluginController::cancelAuthentication()
{
	if (!deviceAuth.isBusy())
		return;
	deviceAuth.cancel();
	authUserCode.clear();
	authStatus.clear();
	obs_log(LOG_INFO, "Remote Deck authorization cancelled");
	emit statusChanged();
}

void PluginController::signOut()
{
	deviceAuth.cancel();
	client.disconnectFromUpstream();
	currentSettings.remoteDeckToken.clear();
	currentSettings.remoteDeckWssUrl.clear();
	currentSettings.studioId.clear();
	currentSettings.studioName.clear();
	authUserCode.clear();
	authStatus.clear();
	::saveSettings(currentSettings);
	client.setSettings(currentSettings);
	emit statusChanged();
}

bool PluginController::isAuthenticating() const
{
	return deviceAuth.isBusy();
}

void PluginController::onAuthCompleted(const QString &token, const QString &apiBase, const QString &wssUrl,
				       const QString &studioId, const QString &studioName)
{
	currentSettings.remoteDeckToken = token;
	currentSettings.remoteDeckApiBase = apiBase;
	currentSettings.remoteDeckWssUrl = wssUrl;
	currentSettings.studioId = studioId;
	currentSettings.studioName = studioName;
	authUserCode.clear();
	authStatus.clear();
	::saveSettings(currentSettings);
	emit statusChanged();
	connectUpstream();
}

void PluginController::onAuthFailed(const QString &message)
{
	authUserCode.clear();
	authStatus = message;
	emit statusChanged();
}

QString PluginController::statusText() const
{
	if (!authStatus.isEmpty())
		return authStatus;
	if (currentSettings.isAuthenticated() && !currentSettings.studioName.isEmpty() &&
	    !client.isConnected()) {
		return QStringLiteral("Signed in to %1. %2")
			.arg(currentSettings.studioName, client.statusText());
	}
	return client.statusText();
}

void PluginController::sendHelloAndInputs()
{
	QJsonObject hello;
	hello.insert("protocolVersion", kProtocolVersion);
	hello.insert("pluginVersion", QString::fromUtf8(PLUGIN_VERSION));
	hello.insert("machineLabel", currentSettings.resolvedMachineLabel());
	if (!currentSettings.instanceId.isEmpty())
		hello.insert("instanceId", currentSettings.instanceId);
	hello.insert("features", QJsonArray{QString::fromUtf8(protocol::kFeatureFilters)});
	hello.insert("filterKinds", supportedAudioFilterKinds());
	client.sendFrame(makeFrame(protocol::kHello, hello));
	sendInputs();
}

void PluginController::sendInputs(const QString &id)
{
	client.sendFrame(makeFrame(protocol::kInputs, audio.listSnapshot(), id));
}

void PluginController::applyOnObsThread(const std::function<void()> &fn)
{
	auto *heap = new std::function<void()>(fn);
	obs_queue_task(
		OBS_TASK_UI,
		[](void *data) {
			auto *task = static_cast<std::function<void()> *>(data);
			(*task)();
			delete task;
		},
		heap, false);
}

void PluginController::sendFilterResult(FilterOpResult result, const QString &requestId)
{
	switch (result) {
	case FilterOpResult::Ok:
		return;
	case FilterOpResult::InputNotFound:
		client.sendFrame(makeErrorFrame(protocol::kErrInputNotFound, QStringLiteral("Audio input not found"),
						requestId));
		return;
	case FilterOpResult::FilterNotFound:
		client.sendFrame(makeErrorFrame(protocol::kErrFilterNotFound, QStringLiteral("Audio filter not found"),
						requestId));
		return;
	case FilterOpResult::UnsupportedKind:
		client.sendFrame(makeErrorFrame(protocol::kErrUnsupportedFilterKind,
						QStringLiteral("Unsupported audio filter kind"), requestId));
		return;
	case FilterOpResult::NameConflict:
		client.sendFrame(makeErrorFrame(protocol::kErrFilterNameConflict,
						QStringLiteral("A filter with that name already exists"), requestId));
		return;
	case FilterOpResult::CreateFailed:
		client.sendFrame(makeErrorFrame(protocol::kErrFilterCreateFailed,
						QStringLiteral("Failed to create audio filter"), requestId));
		return;
	}
}

void PluginController::handleFrame(const Frame &frame)
{
	const auto hasSourceRef = [](const QJsonObject &payload) {
		return !payload.value(QStringLiteral("name")).toString().isEmpty() ||
		       !payload.value(QStringLiteral("id")).toString().isEmpty();
	};
	const auto hasFilterRef = [](const QJsonObject &payload) {
		return !payload.value(QStringLiteral("filterName")).toString().isEmpty() ||
		       !payload.value(QStringLiteral("filterId")).toString().isEmpty();
	};

	if (frame.type == protocol::kPing) {
		client.sendFrame(makeFrame(protocol::kPong, {}, frame.id));
		return;
	}
	if (frame.type == protocol::kInputsGet) {
		sendInputs(frame.id);
		return;
	}
	if (frame.type == protocol::kInputMute) {
		if (!frame.payload.contains("name") || !frame.payload.contains("muted")) {
			client.sendFrame(makeErrorFrame(protocol::kErrInvalidPayload,
							QStringLiteral("input.mute requires name and muted"),
							frame.id));
			return;
		}
		const QString name = frame.payload.value("name").toString();
		const QString id = frame.payload.value("id").toString();
		const bool muted = frame.payload.value("muted").toBool();
		applyOnObsThread([this, name, id, muted, requestId = frame.id]() {
			if (!audio.setMuted(name, muted, id)) {
				client.sendFrame(makeErrorFrame(protocol::kErrInputNotFound,
								QStringLiteral("Audio input not found"), requestId));
			}
		});
		return;
	}
	if (frame.type == protocol::kInputVolume) {
		if (!frame.payload.contains("name") || !frame.payload.contains("volumeDb")) {
			client.sendFrame(makeErrorFrame(protocol::kErrInvalidPayload,
							QStringLiteral("input.volume requires name and volumeDb"),
							frame.id));
			return;
		}
		const QString name = frame.payload.value("name").toString();
		const QString id = frame.payload.value("id").toString();
		const double volumeDb = frame.payload.value("volumeDb").toDouble();
		applyOnObsThread([this, name, id, volumeDb, requestId = frame.id]() {
			if (!audio.setVolumeDb(name, volumeDb, id)) {
				client.sendFrame(makeErrorFrame(protocol::kErrInputNotFound,
								QStringLiteral("Audio input not found"), requestId));
			}
		});
		return;
	}
	if (frame.type == protocol::kInputFilterEnable) {
		if (!hasSourceRef(frame.payload) || !hasFilterRef(frame.payload) ||
		    !frame.payload.contains(QStringLiteral("enabled"))) {
			client.sendFrame(makeErrorFrame(
				protocol::kErrInvalidPayload,
				QStringLiteral("input.filter.enable requires source, filter, and enabled"), frame.id));
			return;
		}
		const QString name = frame.payload.value("name").toString();
		const QString id = frame.payload.value("id").toString();
		const QString filterName = frame.payload.value("filterName").toString();
		const QString filterId = frame.payload.value("filterId").toString();
		const bool enabled = frame.payload.value("enabled").toBool();
		applyOnObsThread([this, name, id, filterName, filterId, enabled, requestId = frame.id]() {
			sendFilterResult(audio.setFilterEnabled(name, id, filterName, filterId, enabled), requestId);
		});
		return;
	}
	if (frame.type == protocol::kInputFilterUpdate) {
		if (!hasSourceRef(frame.payload) || !hasFilterRef(frame.payload) ||
		    !frame.payload.value(QStringLiteral("settings")).isObject()) {
			client.sendFrame(makeErrorFrame(
				protocol::kErrInvalidPayload,
				QStringLiteral("input.filter.update requires source, filter, and settings object"),
				frame.id));
			return;
		}
		const QString name = frame.payload.value("name").toString();
		const QString id = frame.payload.value("id").toString();
		const QString filterName = frame.payload.value("filterName").toString();
		const QString filterId = frame.payload.value("filterId").toString();
		const QJsonObject settings = frame.payload.value("settings").toObject();
		applyOnObsThread([this, name, id, filterName, filterId, settings, requestId = frame.id]() {
			sendFilterResult(audio.updateFilterSettings(name, id, filterName, filterId, settings),
					 requestId);
		});
		return;
	}
	if (frame.type == protocol::kInputFilterAdd) {
		const QString kind = frame.payload.value("kind").toString();
		if (!hasSourceRef(frame.payload) || kind.isEmpty()) {
			client.sendFrame(makeErrorFrame(protocol::kErrInvalidPayload,
							QStringLiteral("input.filter.add requires source and kind"),
							frame.id));
			return;
		}
		if (frame.payload.contains(QStringLiteral("settings")) &&
		    !frame.payload.value(QStringLiteral("settings")).isObject()) {
			client.sendFrame(makeErrorFrame(protocol::kErrInvalidPayload,
							QStringLiteral("input.filter.add settings must be an object"),
							frame.id));
			return;
		}
		const QString name = frame.payload.value("name").toString();
		const QString id = frame.payload.value("id").toString();
		const QString filterName = frame.payload.value("filterName").toString();
		const QJsonObject settings = frame.payload.value("settings").toObject();
		const bool enabled = frame.payload.contains(QStringLiteral("enabled"))
					     ? frame.payload.value("enabled").toBool()
					     : true;
		applyOnObsThread([this, name, id, kind, filterName, settings, enabled, requestId = frame.id]() {
			sendFilterResult(audio.addFilter(name, id, kind, filterName, settings, enabled), requestId);
		});
		return;
	}
	if (frame.type == protocol::kInputFilterRemove) {
		if (!hasSourceRef(frame.payload) || !hasFilterRef(frame.payload)) {
			client.sendFrame(makeErrorFrame(protocol::kErrInvalidPayload,
							QStringLiteral("input.filter.remove requires source and filter"),
							frame.id));
			return;
		}
		const QString name = frame.payload.value("name").toString();
		const QString id = frame.payload.value("id").toString();
		const QString filterName = frame.payload.value("filterName").toString();
		const QString filterId = frame.payload.value("filterId").toString();
		applyOnObsThread([this, name, id, filterName, filterId, requestId = frame.id]() {
			sendFilterResult(audio.removeFilter(name, id, filterName, filterId), requestId);
		});
		return;
	}
	if (frame.type == protocol::kError)
		return;
	if (frame.type == protocol::kHello || frame.type == protocol::kInputs || frame.type == protocol::kState ||
	    frame.type == protocol::kPong)
		return;

	client.sendFrame(makeErrorFrame(protocol::kErrUnknownMethod,
					QStringLiteral("Unknown method: %1").arg(frame.type), frame.id));
}
