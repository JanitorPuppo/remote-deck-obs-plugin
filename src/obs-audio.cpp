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

#include "obs-audio.hpp"
#include "protocol.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QPointer>
#include <QSet>

namespace {

constexpr double kMinVolumeDb = -100.0;
constexpr double kMaxVolumeDb = 26.0;

double volumeToDb(float mul)
{
	if (mul <= 0.0f)
		return kMinVolumeDb;
	return static_cast<double>(obs_mul_to_db(mul));
}

float dbToVolume(double db)
{
	if (db <= kMinVolumeDb)
		return 0.0f;
	return obs_db_to_mul(static_cast<float>(db));
}

const QSet<QString> &supportedFilterKindSet()
{
	static const QSet<QString> kinds = []() {
		QSet<QString> set;
		const QJsonArray list = supportedAudioFilterKinds();
		for (const QJsonValue &value : list)
			set.insert(value.toString());
		return set;
	}();
	return kinds;
}

obs_data_t *obsDataFromJson(const QJsonObject &obj)
{
	if (obj.isEmpty())
		return obs_data_create();
	const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
	obs_data_t *data = obs_data_create_from_json(json.constData());
	return data ? data : obs_data_create();
}

QString uniquifyFilterName(obs_source_t *source, QString base)
{
	if (base.isEmpty())
		base = QStringLiteral("Filter");
	auto taken = [source](const QString &candidate) {
		obs_source_t *existing = obs_source_get_filter_by_name(source, candidate.toUtf8().constData());
		if (!existing)
			return false;
		obs_source_release(existing);
		return true;
	};
	if (!taken(base))
		return base;
	for (int i = 2; i < 10000; ++i) {
		const QString candidate = QStringLiteral("%1 %2").arg(base).arg(i);
		if (!taken(candidate))
			return candidate;
	}
	return base + QStringLiteral(" copy");
}

}

ObsAudio::ObsAudio(QObject *parent) : QObject(parent) {}

ObsAudio::~ObsAudio()
{
	stop();
}

bool ObsAudio::isAudioInput(obs_source_t *source)
{
	if (!source)
		return false;
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return false;
	return (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

bool ObsAudio::isSupportedAudioFilter(obs_source_t *filter)
{
	if (!filter)
		return false;
	if (obs_source_get_type(filter) != OBS_SOURCE_TYPE_FILTER)
		return false;
	if ((obs_source_get_output_flags(filter) & OBS_SOURCE_AUDIO) == 0)
		return false;
	const char *kind = obs_source_get_unversioned_id(filter);
	if (!kind || !kind[0])
		return false;
	return supportedFilterKindSet().contains(QString::fromUtf8(kind));
}

QJsonObject ObsAudio::filterSettings(obs_source_t *filter)
{
	obs_data_t *settings = obs_source_get_settings(filter);
	if (!settings)
		return {};
	const char *json = obs_data_get_json(settings);
	QJsonObject obj;
	if (json && json[0]) {
		const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
		if (doc.isObject())
			obj = doc.object();
	}
	obs_data_release(settings);
	return obj;
}

QJsonObject ObsAudio::describeFilter(obs_source_t *filter, int index)
{
	QJsonObject obj;
	const char *uuid = obs_source_get_uuid(filter);
	const char *name = obs_source_get_name(filter);
	const char *kind = obs_source_get_unversioned_id(filter);
	obj.insert("id", QString::fromUtf8(uuid ? uuid : ""));
	obj.insert("name", QString::fromUtf8(name ? name : ""));
	obj.insert("kind", QString::fromUtf8(kind ? kind : ""));
	obj.insert("enabled", obs_source_enabled(filter));
	obj.insert("index", index);
	obj.insert("settings", filterSettings(filter));
	return obj;
}

QJsonArray ObsAudio::describeFilters(obs_source_t *source)
{
	QJsonArray filters;
	struct Ctx {
		QJsonArray *list;
		int index;
	} ctx{&filters, 0};
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			auto *c = static_cast<Ctx *>(param);
			if (isSupportedAudioFilter(filter))
				c->list->append(describeFilter(filter, c->index));
			c->index++;
		},
		&ctx);
	return filters;
}

struct SceneMembershipWalk {
	QString topSceneName;
	QSet<QString> walking;
	QHash<QString, QStringList> *membership = nullptr;
};

QJsonObject ObsAudio::describeSource(obs_source_t *source, const QHash<QString, QStringList> *membership)
{
	QJsonObject obj;
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	obj.insert("id", id);
	obj.insert("name", QString::fromUtf8(name ? name : ""));
	obj.insert("muted", obs_source_muted(source));
	obj.insert("volumeDb", volumeToDb(obs_source_get_volume(source)));
	obj.insert("filters", describeFilters(source));
	if (membership && !id.isEmpty()) {
		const QStringList sceneNames = membership->value(id);
		if (!sceneNames.isEmpty()) {
			QJsonArray names;
			for (const QString &sceneName : sceneNames)
				names.append(sceneName);
			obj.insert("sceneNames", names);
		}
	}
	return obj;
}

bool ObsAudio::enumSceneItems(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<SceneMembershipWalk *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src || !ctx || !ctx->membership)
		return true;

	if (isAudioInput(src)) {
		const char *uuid = obs_source_get_uuid(src);
		const QString id = QString::fromUtf8(uuid ? uuid : "");
		if (!id.isEmpty() && !ctx->membership->value(id).contains(ctx->topSceneName))
			(*ctx->membership)[id].append(ctx->topSceneName);
	}

	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, enumSceneItems, param);
		return true;
	}

	if (obs_source_get_type(src) != OBS_SOURCE_TYPE_SCENE)
		return true;

	const char *uuid = obs_source_get_uuid(src);
	const QString nestedId = QString::fromUtf8(uuid ? uuid : "");
	obs_scene_t *nested = obs_scene_from_source(src);
	if (!nested || nestedId.isEmpty() || ctx->walking.contains(nestedId))
		return true;
	ctx->walking.insert(nestedId);
	obs_scene_enum_items(nested, enumSceneItems, param);
	ctx->walking.remove(nestedId);
	return true;
}

QHash<QString, QStringList> ObsAudio::buildSceneMembership()
{
	QHash<QString, QStringList> membership;
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *sceneSource = list.sources.array[i];
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		const char *name = obs_source_get_name(sceneSource);
		if (!scene || !name || !name[0])
			continue;
		SceneMembershipWalk ctx;
		ctx.topSceneName = QString::fromUtf8(name);
		ctx.membership = &membership;
		const char *uuid = obs_source_get_uuid(sceneSource);
		if (uuid)
			ctx.walking.insert(QString::fromUtf8(uuid));
		obs_scene_enum_items(scene, enumSceneItems, &ctx);
	}
	obs_frontend_source_list_free(&list);
	return membership;
}

QJsonArray ObsAudio::listInputs() const
{
	return listSnapshot().value(QStringLiteral("inputs")).toArray();
}

QJsonObject ObsAudio::listSnapshot() const
{
	const QHash<QString, QStringList> membership = buildSceneMembership();
	QJsonArray inputs;
	struct Ctx {
		QJsonArray *list;
		const QHash<QString, QStringList> *membership;
	} ctx{&inputs, &membership};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *c = static_cast<Ctx *>(param);
			if (isAudioInput(source))
				c->list->append(describeSource(source, c->membership));
			return true;
		},
		&ctx);

	QJsonArray scenes;
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *name = obs_source_get_name(list.sources.array[i]);
		if (name && name[0])
			scenes.append(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&list);

	QString current;
	obs_source_t *program = obs_frontend_get_current_scene();
	if (program) {
		const char *name = obs_source_get_name(program);
		if (name)
			current = QString::fromUtf8(name);
		obs_source_release(program);
	}

	QJsonObject payload;
	payload.insert("inputs", inputs);
	if (!current.isEmpty())
		payload.insert("currentProgramScene", current);
	if (!scenes.isEmpty())
		payload.insert("scenes", scenes);
	return payload;
}

void ObsAudio::invokeOnQt(const std::function<void()> &fn)
{
	QPointer<ObsAudio> self(this);
	QMetaObject::invokeMethod(
		this,
		[self, fn]() {
			if (self)
				fn();
		},
		Qt::QueuedConnection);
}

void ObsAudio::onSourceCreate(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->trackSource(ref);
		obs_source_release(ref);
		emit self->inputsChanged();
	});
}

void ObsAudio::onSourceDestroy(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !source)
		return;
	const char *uuid = obs_source_get_uuid(source);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	self->invokeOnQt([self, id]() {
		self->forgetFiltersForParent(id);
		self->trackedIds.remove(id);
		emit self->inputsChanged();
	});
}

void ObsAudio::onSourceMute(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->emitState(ref);
		obs_source_release(ref);
	});
}

void ObsAudio::onSourceVolume(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->emitState(ref);
		obs_source_release(ref);
	});
}

void ObsAudio::onSourceRename(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->emitState(ref);
		emit self->inputsChanged();
		obs_source_release(ref);
	});
}

void ObsAudio::trackSource(obs_source_t *source)
{
	if (!isAudioInput(source))
		return;
	const char *uuid = obs_source_get_uuid(source);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	if (id.isEmpty())
		return;
	if (!trackedIds.contains(id)) {
		trackedIds.insert(id);
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		signal_handler_connect(sh, "mute", onSourceMute, this);
		signal_handler_connect(sh, "volume", onSourceVolume, this);
		signal_handler_connect(sh, "rename", onSourceRename, this);
		signal_handler_connect(sh, "filter_add", onFilterAdd, this);
		signal_handler_connect(sh, "filter_remove", onFilterRemove, this);
		signal_handler_connect(sh, "reorder_filters", onFiltersReordered, this);
	}
	trackFilters(source);
}

void ObsAudio::untrackSource(obs_source_t *source)
{
	if (!source)
		return;
	untrackFilters(source);
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_disconnect(sh, "mute", onSourceMute, this);
	signal_handler_disconnect(sh, "volume", onSourceVolume, this);
	signal_handler_disconnect(sh, "rename", onSourceRename, this);
	signal_handler_disconnect(sh, "filter_add", onFilterAdd, this);
	signal_handler_disconnect(sh, "filter_remove", onFilterRemove, this);
	signal_handler_disconnect(sh, "reorder_filters", onFiltersReordered, this);
	const char *uuid = obs_source_get_uuid(source);
	trackedIds.remove(QString::fromUtf8(uuid ? uuid : ""));
}

void ObsAudio::trackFilter(obs_source_t *filter)
{
	if (!isSupportedAudioFilter(filter))
		return;
	const char *uuid = obs_source_get_uuid(filter);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	if (id.isEmpty() || trackedFilterIds.contains(id))
		return;
	trackedFilterIds.insert(id);
	obs_source_t *parent = obs_filter_get_parent(filter);
	if (parent) {
		const char *parentUuid = obs_source_get_uuid(parent);
		filterParentIds.insert(id, QString::fromUtf8(parentUuid ? parentUuid : ""));
	}
	signal_handler_t *sh = obs_source_get_signal_handler(filter);
	signal_handler_connect(sh, "enable", onFilterChanged, this);
	signal_handler_connect(sh, "rename", onFilterChanged, this);
	signal_handler_connect(sh, "update", onFilterChanged, this);
}

void ObsAudio::untrackFilter(obs_source_t *filter)
{
	if (!filter)
		return;
	signal_handler_t *sh = obs_source_get_signal_handler(filter);
	signal_handler_disconnect(sh, "enable", onFilterChanged, this);
	signal_handler_disconnect(sh, "rename", onFilterChanged, this);
	signal_handler_disconnect(sh, "update", onFilterChanged, this);
	const char *uuid = obs_source_get_uuid(filter);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	trackedFilterIds.remove(id);
	filterParentIds.remove(id);
}

void ObsAudio::trackFilters(obs_source_t *source)
{
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			static_cast<ObsAudio *>(param)->trackFilter(filter);
		},
		this);
}

void ObsAudio::untrackFilters(obs_source_t *source)
{
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			static_cast<ObsAudio *>(param)->untrackFilter(filter);
		},
		this);
}

void ObsAudio::forgetFiltersForParent(const QString &parentId)
{
	for (auto it = filterParentIds.begin(); it != filterParentIds.end();) {
		if (it.value() == parentId) {
			trackedFilterIds.remove(it.key());
			it = filterParentIds.erase(it);
		} else {
			++it;
		}
	}
}

void ObsAudio::onFilterAdd(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	obs_source_t *filter = static_cast<obs_source_t *>(calldata_ptr(cd, "filter"));
	if (!self || !source || !filter)
		return;
	obs_source_t *sourceRef = obs_source_get_ref(source);
	obs_source_t *filterRef = obs_source_get_ref(filter);
	self->invokeOnQt([self, sourceRef, filterRef]() {
		self->trackFilter(filterRef);
		self->emitState(sourceRef);
		obs_source_release(sourceRef);
		obs_source_release(filterRef);
	});
}

void ObsAudio::onFilterRemove(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	obs_source_t *filter = static_cast<obs_source_t *>(calldata_ptr(cd, "filter"));
	if (!self || !source)
		return;
	obs_source_t *sourceRef = obs_source_get_ref(source);
	obs_source_t *filterRef = filter ? obs_source_get_ref(filter) : nullptr;
	self->invokeOnQt([self, sourceRef, filterRef]() {
		if (filterRef) {
			self->untrackFilter(filterRef);
			obs_source_release(filterRef);
		}
		self->emitState(sourceRef);
		obs_source_release(sourceRef);
	});
}

void ObsAudio::onFiltersReordered(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !source)
		return;
	obs_source_t *sourceRef = obs_source_get_ref(source);
	self->invokeOnQt([self, sourceRef]() {
		self->emitState(sourceRef);
		obs_source_release(sourceRef);
	});
}

void ObsAudio::onFilterChanged(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *filter = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !filter)
		return;
	obs_source_t *parent = obs_filter_get_parent(filter);
	if (!parent)
		return;
	obs_source_t *parentRef = obs_source_get_ref(parent);
	self->invokeOnQt([self, parentRef]() {
		self->emitState(parentRef);
		obs_source_release(parentRef);
	});
}

void ObsAudio::emitState(obs_source_t *source)
{
	if (!isAudioInput(source))
		return;
	const QHash<QString, QStringList> membership = buildSceneMembership();
	emit inputStateChanged(describeSource(source, &membership));
}

void ObsAudio::onSceneItemMutated(void *data, calldata_t *)
{
	auto *self = static_cast<ObsAudio *>(data);
	if (!self)
		return;
	self->invokeOnQt([self]() { emit self->inputsChanged(); });
}

void ObsAudio::trackScenes()
{
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *src = list.sources.array[i];
		const char *uuid = obs_source_get_uuid(src);
		const QString id = QString::fromUtf8(uuid ? uuid : "");
		if (id.isEmpty() || trackedSceneIds.contains(id))
			continue;
		trackedSceneIds.insert(id);
		signal_handler_t *sh = obs_source_get_signal_handler(src);
		signal_handler_connect(sh, "item_add", onSceneItemMutated, this);
		signal_handler_connect(sh, "item_remove", onSceneItemMutated, this);
	}
	obs_frontend_source_list_free(&list);
}

void ObsAudio::untrackScenes()
{
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		signal_handler_t *sh = obs_source_get_signal_handler(list.sources.array[i]);
		signal_handler_disconnect(sh, "item_add", onSceneItemMutated, this);
		signal_handler_disconnect(sh, "item_remove", onSceneItemMutated, this);
	}
	obs_frontend_source_list_free(&list);
	trackedSceneIds.clear();
}

void ObsAudio::start()
{
	if (running)
		return;
	running = true;
	signal_handler_t *core = obs_get_signal_handler();
	signal_handler_connect(core, "source_create", onSourceCreate, this);
	signal_handler_connect(core, "source_destroy", onSourceDestroy, this);
	refresh();
	trackScenes();
}

void ObsAudio::stop()
{
	if (!running)
		return;
	running = false;
	signal_handler_t *core = obs_get_signal_handler();
	signal_handler_disconnect(core, "source_create", onSourceCreate, this);
	signal_handler_disconnect(core, "source_destroy", onSourceDestroy, this);
	untrackScenes();

	struct Ctx {
		ObsAudio *self;
	} ctx{this};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			static_cast<Ctx *>(param)->self->untrackSource(source);
			return true;
		},
		&ctx);
	trackedIds.clear();
	trackedFilterIds.clear();
	filterParentIds.clear();
}

void ObsAudio::refresh()
{
	struct Ctx {
		ObsAudio *self;
	} ctx{this};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			static_cast<Ctx *>(param)->self->trackSource(source);
			return true;
		},
		&ctx);
	trackScenes();
}

obs_source_t *ObsAudio::findSource(const QString &name, const QString &id) const
{
	if (!id.isEmpty()) {
		obs_source_t *byId = obs_get_source_by_uuid(id.toUtf8().constData());
		if (byId)
			return byId;
	}
	if (name.isEmpty())
		return nullptr;
	return obs_get_source_by_name(name.toUtf8().constData());
}

obs_source_t *ObsAudio::acquireAudioInput(const QString &name, const QString &id) const
{
	obs_source_t *source = findSource(name, id);
	if (!source)
		return nullptr;
	if (!isAudioInput(source)) {
		obs_source_release(source);
		return nullptr;
	}
	return source;
}

obs_source_t *ObsAudio::findFilter(obs_source_t *source, const QString &filterName, const QString &filterId) const
{
	if (!source)
		return nullptr;
	if (!filterId.isEmpty()) {
		obs_source_t *byId = obs_get_source_by_uuid(filterId.toUtf8().constData());
		if (byId) {
			if (obs_filter_get_parent(byId) == source && isSupportedAudioFilter(byId))
				return byId;
			obs_source_release(byId);
		}
	}
	if (filterName.isEmpty())
		return nullptr;
	obs_source_t *byName = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
	if (byName && !isSupportedAudioFilter(byName)) {
		obs_source_release(byName);
		return nullptr;
	}
	return byName;
}

bool ObsAudio::setMuted(const QString &name, bool muted, const QString &id)
{
	obs_source_t *source = acquireAudioInput(name, id);
	if (!source)
		return false;
	obs_source_set_muted(source, muted);
	obs_source_release(source);
	return true;
}

bool ObsAudio::setVolumeDb(const QString &name, double volumeDb, const QString &id)
{
	obs_source_t *source = acquireAudioInput(name, id);
	if (!source)
		return false;
	const double clamped = std::clamp(volumeDb, kMinVolumeDb, kMaxVolumeDb);
	obs_source_set_volume(source, dbToVolume(clamped));
	obs_source_release(source);
	return true;
}

FilterOpResult ObsAudio::setFilterEnabled(const QString &name, const QString &id, const QString &filterName,
					  const QString &filterId, bool enabled)
{
	obs_source_t *source = acquireAudioInput(name, id);
	if (!source)
		return FilterOpResult::InputNotFound;
	obs_source_t *filter = findFilter(source, filterName, filterId);
	if (!filter) {
		obs_source_release(source);
		return FilterOpResult::FilterNotFound;
	}
	obs_source_set_enabled(filter, enabled);
	obs_source_release(filter);
	obs_source_release(source);
	return FilterOpResult::Ok;
}

FilterOpResult ObsAudio::updateFilterSettings(const QString &name, const QString &id, const QString &filterName,
					      const QString &filterId, const QJsonObject &settings)
{
	obs_source_t *source = acquireAudioInput(name, id);
	if (!source)
		return FilterOpResult::InputNotFound;
	obs_source_t *filter = findFilter(source, filterName, filterId);
	if (!filter) {
		obs_source_release(source);
		return FilterOpResult::FilterNotFound;
	}
	obs_data_t *data = obsDataFromJson(settings);
	obs_source_update(filter, data);
	obs_data_release(data);
	obs_source_release(filter);
	obs_source_release(source);
	return FilterOpResult::Ok;
}

FilterOpResult ObsAudio::addFilter(const QString &name, const QString &id, const QString &kind,
				   const QString &filterName, const QJsonObject &settings, bool enabled)
{
	if (!supportedFilterKindSet().contains(kind))
		return FilterOpResult::UnsupportedKind;

	obs_source_t *source = acquireAudioInput(name, id);
	if (!source)
		return FilterOpResult::InputNotFound;

	const QByteArray kindUtf8 = kind.toUtf8();
	const char *latest = obs_get_latest_input_type_id(kindUtf8.constData());
	const char *createId = (latest && latest[0]) ? latest : kindUtf8.constData();

	QString resolvedName = filterName.trimmed();
	if (resolvedName.isEmpty()) {
		const char *display = obs_source_get_display_name(createId);
		if (!display || !display[0])
			display = obs_source_get_display_name(kindUtf8.constData());
		resolvedName = uniquifyFilterName(
			source, QString::fromUtf8(display && display[0] ? display : kindUtf8.constData()));
	} else {
		obs_source_t *existing = obs_source_get_filter_by_name(source, resolvedName.toUtf8().constData());
		if (existing) {
			obs_source_release(existing);
			obs_source_release(source);
			return FilterOpResult::NameConflict;
		}
	}

	obs_data_t *data = obsDataFromJson(settings);
	const QByteArray nameUtf8 = resolvedName.toUtf8();
	obs_source_t *filter = obs_source_create(createId, nameUtf8.constData(), data, nullptr);
	obs_data_release(data);
	if (!filter) {
		obs_source_release(source);
		return FilterOpResult::CreateFailed;
	}

	obs_source_filter_add(source, filter);
	if (!enabled)
		obs_source_set_enabled(filter, false);
	obs_source_release(filter);
	obs_source_release(source);
	return FilterOpResult::Ok;
}

FilterOpResult ObsAudio::removeFilter(const QString &name, const QString &id, const QString &filterName,
				      const QString &filterId)
{
	obs_source_t *source = acquireAudioInput(name, id);
	if (!source)
		return FilterOpResult::InputNotFound;
	obs_source_t *filter = findFilter(source, filterName, filterId);
	if (!filter) {
		obs_source_release(source);
		return FilterOpResult::FilterNotFound;
	}
	obs_source_filter_remove(source, filter);
	obs_source_release(filter);
	obs_source_release(source);
	return FilterOpResult::Ok;
}
