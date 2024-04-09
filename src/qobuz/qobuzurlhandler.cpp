/*
 * Strawberry Music Player
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include "core/application.h"
#include "core/taskmanager.h"
#include "core/song.h"
#include "qobuz/qobuzservice.h"
#include "qobuzurlhandler.h"

QobuzUrlHandler::QobuzUrlHandler(Application *app, QobuzService *service)
    : UrlHandler(service),
      app_(app),
      service_(service) {

  QObject::connect(service, &QobuzService::StreamURLFailure, this, &QobuzUrlHandler::GetStreamURLFailure);
  QObject::connect(service, &QobuzService::StreamURLSuccess, this, &QobuzUrlHandler::GetStreamURLSuccess);

}

UrlHandler::LoadResult QobuzUrlHandler::StartLoading(const QUrl &url) {

  Request req;
  req.task_id = app_->task_manager()->StartTask(QStringLiteral("Loading %1 stream...").arg(url.scheme()));
  QString error;
  req.id = service_->GetStreamURL(url, error);
  if (req.id == 0) {
    CancelTask(req.task_id);
    return LoadResult(url, LoadResult::Type::Error, error);
  }

  requests_.insert(req.id, req);

  LoadResult ret(url);
  ret.type_ = LoadResult::Type::WillLoadAsynchronously;

  return ret;

}

void QobuzUrlHandler::GetStreamURLFailure(const uint id, const QUrl &media_url, const QString &error) {

  if (!requests_.contains(id)) return;
  Request req = requests_.take(id);
  CancelTask(req.task_id);

  emit AsyncLoadComplete(LoadResult(media_url, LoadResult::Type::Error, error));

}

void QobuzUrlHandler::GetStreamURLSuccess(const uint id, const QUrl &media_url, const QUrl &stream_url, const Song::FileType filetype, const int samplerate, const int bit_depth, const qint64 duration) {

  if (!requests_.contains(id)) return;
  Request req = requests_.take(id);
  CancelTask(req.task_id);

  emit AsyncLoadComplete(LoadResult(media_url, LoadResult::Type::TrackAvailable, stream_url, filetype, samplerate, bit_depth, duration));

}

void QobuzUrlHandler::CancelTask(const int task_id) {
  app_->task_manager()->SetTaskFinished(task_id);
}
