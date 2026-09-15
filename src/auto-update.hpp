/*
obs-remote-deck
Copyright (C) 2026 Remote Deck

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class AutoUpdate : public QObject {
	Q_OBJECT

public:
	enum class Phase {
		Idle,
		Checking,
		Available,
		Downloading,
		Ready,
		Error,
	};

	explicit AutoUpdate(QObject *parent = nullptr);
	~AutoUpdate() override;

	void scheduleCheck();
	void checkNow();
	void downloadUpdate();
	void skipAvailableVersion();
	void applyUpdateAndQuit();

	QString statusText() const;
	bool updateAvailable() const;
	bool updateReady() const;
	bool updateFailed() const;
	bool isBusy() const;
	QString availableVersion() const;
	QString notesUrl() const;

signals:
	void stateChanged();

private:
	void beginCheck();
	void onManifestReply(QNetworkReply *reply);
	void onDownloadReply(QNetworkReply *reply);
	void setPhase(Phase phase, const QString &detail = {});
	bool parseManifest(const QByteArray &body, QString *errorOut);
	bool isAllowedInstallerUrl(const QUrl &url) const;
	bool verifyDownloadedFile(const QString &path) const;
	QString updaterExecutablePath() const;

	QNetworkAccessManager *nam = nullptr;
	QTimer *delayTimer = nullptr;
	QNetworkReply *activeReply = nullptr;

	Phase phase = Phase::Idle;
	QString detailText;
	QString manifestVersion;
	QString manifestNotesUrl;
	QString manifestInstallerUrl;
	QString manifestSha256;
	QString downloadedInstallerPath;
	QString skippedVersion;
};

int compareVersions(const QString &left, const QString &right);
