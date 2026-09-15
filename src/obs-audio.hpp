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

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

enum class FilterOpResult {
	Ok,
	InputNotFound,
	FilterNotFound,
	UnsupportedKind,
	NameConflict,
	CreateFailed,
};

class ObsAudio : public QObject {
	Q_OBJECT

public:
	explicit ObsAudio(QObject *parent = nullptr);
	~ObsAudio() override;

	void start();
	void stop();
	void refresh();

	QJsonArray listInputs() const;
	QJsonObject listSnapshot() const;
	bool setMuted(const QString &name, bool muted, const QString &id = {});
	bool setVolumeDb(const QString &name, double volumeDb, const QString &id = {});
	FilterOpResult setFilterEnabled(const QString &name, const QString &id, const QString &filterName,
					const QString &filterId, bool enabled);
	FilterOpResult updateFilterSettings(const QString &name, const QString &id, const QString &filterName,
					    const QString &filterId, const QJsonObject &settings);
	FilterOpResult addFilter(const QString &name, const QString &id, const QString &kind,
				 const QString &filterName, const QJsonObject &settings, bool enabled);
	FilterOpResult removeFilter(const QString &name, const QString &id, const QString &filterName,
				    const QString &filterId);

signals:
	void inputStateChanged(const QJsonObject &state);
	void inputsChanged();

private:
	static bool isAudioInput(obs_source_t *source);
	static bool isSupportedAudioFilter(obs_source_t *filter);
	static QJsonObject describeSource(obs_source_t *source,
					 const QHash<QString, QStringList> *membership = nullptr);
	static QJsonArray describeFilters(obs_source_t *source);
	static QJsonObject describeFilter(obs_source_t *filter, int index);
	static QJsonObject filterSettings(obs_source_t *filter);
	static QHash<QString, QStringList> buildSceneMembership();
	static bool enumSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param);

	obs_source_t *findSource(const QString &name, const QString &id) const;
	obs_source_t *findFilter(obs_source_t *source, const QString &filterName, const QString &filterId) const;
	obs_source_t *acquireAudioInput(const QString &name, const QString &id) const;
	void trackSource(obs_source_t *source);
	void untrackSource(obs_source_t *source);
	void trackFilter(obs_source_t *filter);
	void untrackFilter(obs_source_t *filter);
	void trackFilters(obs_source_t *source);
	void untrackFilters(obs_source_t *source);
	void forgetFiltersForParent(const QString &parentId);
	void trackScenes();
	void untrackScenes();
	void emitState(obs_source_t *source);
	void invokeOnQt(const std::function<void()> &fn);

	static void onSourceCreate(void *data, calldata_t *cd);
	static void onSourceDestroy(void *data, calldata_t *cd);
	static void onSourceMute(void *data, calldata_t *cd);
	static void onSourceVolume(void *data, calldata_t *cd);
	static void onSourceRename(void *data, calldata_t *cd);
	static void onSceneItemMutated(void *data, calldata_t *cd);
	static void onFilterAdd(void *data, calldata_t *cd);
	static void onFilterRemove(void *data, calldata_t *cd);
	static void onFiltersReordered(void *data, calldata_t *cd);
	static void onFilterChanged(void *data, calldata_t *cd);

	QSet<QString> trackedIds;
	QSet<QString> trackedSceneIds;
	QSet<QString> trackedFilterIds;
	QHash<QString, QString> filterParentIds;
	bool running = false;
};
