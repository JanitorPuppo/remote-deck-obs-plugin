/*
obs-remote-deck
Copyright (C) 2026 Remote Deck

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "auto-update.hpp"
#include "network-log.hpp"
#include "settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

constexpr auto kManifestUrl =
	"https://github.com/JanitorPuppo/remote-deck-obs-plugin/releases/latest/download/latest.json";
constexpr auto kGitHubHost = "github.com";
constexpr auto kUpdaterFileName = "obs-remote-deck-updater.exe";
constexpr int kCheckDelayMs = 5000;

QVector<int> parseVersionParts(const QString &version)
{
	QVector<int> parts;
	const QStringList tokens = version.split(QLatin1Char('.'), Qt::SkipEmptyParts);
	for (const QString &token : tokens) {
		bool ok = false;
		const int value = token.toInt(&ok);
		parts.push_back(ok ? value : 0);
	}
	while (parts.size() < 3)
		parts.push_back(0);
	return parts;
}

QString phaseToUserText(AutoUpdate::Phase phase, const QString &detail)
{
	switch (phase) {
	case AutoUpdate::Phase::Idle:
		return QString::fromUtf8(obs_module_text("RemoteDeck.UpdateIdle"));
	case AutoUpdate::Phase::Checking:
		return QString::fromUtf8(obs_module_text("RemoteDeck.UpdateChecking"));
	case AutoUpdate::Phase::Available:
		return detail;
	case AutoUpdate::Phase::Downloading:
		return QString::fromUtf8(obs_module_text("RemoteDeck.UpdateDownloading"));
	case AutoUpdate::Phase::Ready:
		return QString::fromUtf8(obs_module_text("RemoteDeck.UpdateReady"));
	case AutoUpdate::Phase::Error:
		return detail.isEmpty() ? QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError"))
					: detail;
	}
	return {};
}

bool copyFileTo(const QString &source, const QString &dest)
{
	if (QFile::exists(dest) && !QFile::remove(dest))
		return false;
	return QFile::copy(source, dest);
}

}

int compareVersions(const QString &left, const QString &right)
{
	const QVector<int> leftParts = parseVersionParts(left);
	const QVector<int> rightParts = parseVersionParts(right);
	for (int i = 0; i < 3; ++i) {
		if (leftParts[i] < rightParts[i])
			return -1;
		if (leftParts[i] > rightParts[i])
			return 1;
	}
	return 0;
}

AutoUpdate::AutoUpdate(QObject *parent) : QObject(parent)
{
	nam = new QNetworkAccessManager(this);
	delayTimer = new QTimer(this);
	delayTimer->setSingleShot(true);
	connect(delayTimer, &QTimer::timeout, this, &AutoUpdate::beginCheck);

	const PluginSettings settings = loadSettings();
	skippedVersion = settings.skippedUpdateVersion;
}

AutoUpdate::~AutoUpdate()
{
	if (activeReply) {
		activeReply->abort();
		activeReply = nullptr;
	}
}

void AutoUpdate::scheduleCheck()
{
#ifdef REMOTE_DECK_LOCAL_DEV
	return;
#endif
#ifndef _WIN32
	return;
#endif
	if (delayTimer->isActive())
		return;
	delayTimer->start(kCheckDelayMs);
}

void AutoUpdate::checkNow()
{
#ifdef REMOTE_DECK_LOCAL_DEV
	return;
#endif
#ifndef _WIN32
	return;
#endif
	delayTimer->stop();
	beginCheck();
}

void AutoUpdate::beginCheck()
{
	if (phase == Phase::Checking || phase == Phase::Downloading)
		return;

	if (activeReply) {
		activeReply->abort();
		activeReply = nullptr;
	}

	setPhase(Phase::Checking);

	const QUrl url(QString::fromUtf8(kManifestUrl));
	remote_deck_log::logHttpRequest("Remote Deck update check", url, "GET");
	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	auto *reply = nam->get(req);
	activeReply = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if (activeReply == reply)
			activeReply = nullptr;
		reply->deleteLater();
		onManifestReply(reply);
	});
}

void AutoUpdate::onManifestReply(QNetworkReply *reply)
{
	const QByteArray body = reply->readAll();
	remote_deck_log::logHttpReply("Remote Deck update check", reply, body);

	if (reply->error() != QNetworkReply::NoError) {
		setPhase(Phase::Error, reply->errorString());
		return;
	}

	QString parseError;
	if (!parseManifest(body, &parseError)) {
		setPhase(Phase::Error, parseError);
		return;
	}

	const QString current = QString::fromUtf8(PLUGIN_VERSION);
	if (compareVersions(current, manifestVersion) >= 0 || manifestVersion == skippedVersion) {
		setPhase(Phase::Idle);
		return;
	}

	const QString detail = QString::fromUtf8(obs_module_text("RemoteDeck.UpdateAvailable"))
				       .arg(manifestVersion, current);
	setPhase(Phase::Available, detail);
}

bool AutoUpdate::parseManifest(const QByteArray &body, QString *errorOut)
{
	const QJsonDocument doc = QJsonDocument::fromJson(body);
	if (!doc.isObject()) {
		if (errorOut)
			*errorOut = QStringLiteral("Update manifest is not valid JSON.");
		return false;
	}

	const QJsonObject root = doc.object();
	manifestVersion = root.value(QStringLiteral("version")).toString().trimmed();
	manifestNotesUrl = root.value(QStringLiteral("notesUrl")).toString().trimmed();

	const QJsonObject windows = root.value(QStringLiteral("windows")).toObject();
	manifestInstallerUrl = windows.value(QStringLiteral("installerUrl")).toString().trimmed();
	manifestSha256 = windows.value(QStringLiteral("sha256")).toString().trimmed().toLower();

	if (manifestVersion.isEmpty() || manifestInstallerUrl.isEmpty() || manifestSha256.isEmpty()) {
		if (errorOut)
			*errorOut = QStringLiteral("Update manifest is missing required fields.");
		return false;
	}

	const QUrl installerUrl(manifestInstallerUrl);
	if (!isAllowedInstallerUrl(installerUrl)) {
		if (errorOut)
			*errorOut = QStringLiteral("Update installer URL is not allowed.");
		return false;
	}

	return true;
}

bool AutoUpdate::isAllowedInstallerUrl(const QUrl &url) const
{
	if (!url.isValid() || url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0)
		return false;
	if (url.host().compare(QLatin1String(kGitHubHost), Qt::CaseInsensitive) != 0)
		return false;
	return url.path().endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
}

void AutoUpdate::downloadUpdate()
{
	if (phase != Phase::Available && phase != Phase::Error && phase != Phase::Ready)
		return;
	if (manifestInstallerUrl.isEmpty())
		return;

	if (activeReply) {
		activeReply->abort();
		activeReply = nullptr;
	}

	downloadedInstallerPath.clear();
	setPhase(Phase::Downloading);

	const QUrl url(manifestInstallerUrl);
	remote_deck_log::logHttpRequest("Remote Deck update download", url, "GET");
	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	auto *reply = nam->get(req);
	activeReply = reply;
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if (activeReply == reply)
			activeReply = nullptr;
		reply->deleteLater();
		onDownloadReply(reply);
	});
}

void AutoUpdate::onDownloadReply(QNetworkReply *reply)
{
	if (reply->error() != QNetworkReply::NoError) {
		setPhase(Phase::Error, reply->errorString());
		return;
	}

	const QByteArray payload = reply->readAll();
	const QString tempRoot =
		QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/obs-remote-deck-update");
	if (!QDir().mkpath(tempRoot)) {
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError")));
		return;
	}

	downloadedInstallerPath = tempRoot + QStringLiteral("/") +
				  QStringLiteral("obs-remote-deck-") + manifestVersion +
				  QStringLiteral("-windows-installer.exe");

	QFile file(downloadedInstallerPath);
	if (!file.open(QIODevice::WriteOnly)) {
		setPhase(Phase::Error, file.errorString());
		return;
	}
	if (file.write(payload) != payload.size()) {
		file.remove();
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError")));
		return;
	}
	file.close();

	if (!verifyDownloadedFile(downloadedInstallerPath)) {
		QFile::remove(downloadedInstallerPath);
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateHashFailed")));
		return;
	}

	setPhase(Phase::Ready);
}

bool AutoUpdate::verifyDownloadedFile(const QString &path) const
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;

	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
		return false;

	return hash.result().toHex() == manifestSha256.toLatin1();
}

void AutoUpdate::skipAvailableVersion()
{
	if (manifestVersion.isEmpty())
		return;

	skippedVersion = manifestVersion;
	PluginSettings settings = loadSettings();
	settings.skippedUpdateVersion = skippedVersion;
	saveSettings(settings);
	setPhase(Phase::Idle);
}

QString AutoUpdate::updaterExecutablePath() const
{
	char *path = obs_module_file(kUpdaterFileName);
	if (!path)
		return {};
	const QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

void AutoUpdate::applyUpdateAndQuit()
{
	if (phase != Phase::Ready || downloadedInstallerPath.isEmpty())
		return;

	const QString updaterSource = updaterExecutablePath();
	if (updaterSource.isEmpty() || !QFileInfo::exists(updaterSource)) {
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError")));
		return;
	}

	const QString stageRoot =
		QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
		QStringLiteral("/obs-remote-deck-update-run-") + manifestVersion;
	if (!QDir().mkpath(stageRoot)) {
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError")));
		return;
	}

	const QString stagedUpdater = stageRoot + QStringLiteral("/") + QString::fromUtf8(kUpdaterFileName);
	const QString stagedInstaller = stageRoot + QStringLiteral("/installer.exe");

	if (!copyFileTo(updaterSource, stagedUpdater) || !copyFileTo(downloadedInstallerPath, stagedInstaller)) {
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError")));
		return;
	}

	const qint64 obsPid = QCoreApplication::applicationPid();
	const QString obsExe = QCoreApplication::applicationFilePath();

	QStringList args;
	args << QStringLiteral("--wait-pid=%1").arg(obsPid) << QStringLiteral("--installer=%1").arg(stagedInstaller)
	     << QStringLiteral("--relaunch=%1").arg(obsExe);

	if (!QProcess::startDetached(stagedUpdater, args, stageRoot)) {
		setPhase(Phase::Error, QString::fromUtf8(obs_module_text("RemoteDeck.UpdateError")));
		return;
	}

	obs_log(LOG_INFO, "Remote Deck update staged; quitting OBS for silent install (version %s)",
		manifestVersion.toUtf8().constData());
	QCoreApplication::quit();
}

QString AutoUpdate::statusText() const
{
	return phaseToUserText(phase, detailText);
}

bool AutoUpdate::updateAvailable() const
{
	return phase == Phase::Available || phase == Phase::Downloading || phase == Phase::Ready;
}

bool AutoUpdate::updateReady() const
{
	return phase == Phase::Ready;
}

bool AutoUpdate::updateFailed() const
{
	return phase == Phase::Error;
}

bool AutoUpdate::isBusy() const
{
	return phase == Phase::Checking || phase == Phase::Downloading;
}

QString AutoUpdate::availableVersion() const
{
	return manifestVersion;
}

QString AutoUpdate::notesUrl() const
{
	return manifestNotesUrl;
}

void AutoUpdate::setPhase(Phase newPhase, const QString &detail)
{
	phase = newPhase;
	detailText = detail.isEmpty() ? phaseToUserText(newPhase, {}) : detail;
	emit stateChanged();
}
