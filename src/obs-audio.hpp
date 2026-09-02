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

#include <obs.h>

#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

class ObsAudio : public QObject {
	Q_OBJECT

public:
	explicit ObsAudio(QObject *parent = nullptr);
	~ObsAudio() override;

	void start();
	void stop();
	void refresh();

	QJsonArray listInputs() const;
	bool setMuted(const QString &name, bool muted, const QString &id = {});
	bool setVolumeDb(const QString &name, double volumeDb, const QString &id = {});

signals:
	void inputStateChanged(const QJsonObject &state);
	void inputsChanged();

private:
	static bool isAudioInput(obs_source_t *source);
	static QJsonObject describeSource(obs_source_t *source);
	static bool enumInputsCallback(void *param, obs_source_t *source);

	obs_source_t *findSource(const QString &name, const QString &id) const;
	void trackSource(obs_source_t *source);
	void untrackSource(obs_source_t *source);
	void emitState(obs_source_t *source);
	void invokeOnQt(const std::function<void()> &fn);

	static void onSourceCreate(void *data, calldata_t *cd);
	static void onSourceDestroy(void *data, calldata_t *cd);
	static void onSourceMute(void *data, calldata_t *cd);
	static void onSourceVolume(void *data, calldata_t *cd);
	static void onSourceRename(void *data, calldata_t *cd);

	QSet<QString> trackedIds;
	bool running = false;
};
