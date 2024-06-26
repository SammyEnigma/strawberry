/*
 * Strawberry Music Player
 * Copyright 2020-2024, Jonas Kvinge <jonas@jkvinge.net>
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

#include <algorithm>

#include <QtGlobal>
#include <QObject>
#include <QList>
#include <QVariant>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QMessageBox>

#include "core/shared_ptr.h"
#include "core/application.h"
#include "core/networkaccessmanager.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/localredirectserver.h"
#include "utilities/randutils.h"
#include "utilities/timeconstants.h"
#include "streaming/streamingservices.h"
#include "spotify/spotifyservice.h"
#include "albumcoverfetcher.h"
#include "jsoncoverprovider.h"
#include "spotifycoverprovider.h"

namespace {
constexpr char kApiUrl[] = "https://api.spotify.com/v1";
constexpr int kLimit = 10;
}  // namespace

SpotifyCoverProvider::SpotifyCoverProvider(Application *app, SharedPtr<NetworkAccessManager> network, QObject *parent)
    : JsonCoverProvider(QStringLiteral("Spotify"), true, true, 2.5, true, true, app, network, parent),
      service_(app->streaming_services()->Service<SpotifyService>()) {}

SpotifyCoverProvider::~SpotifyCoverProvider() {

  while (!replies_.isEmpty()) {
    QNetworkReply *reply = replies_.takeFirst();
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }

}

bool SpotifyCoverProvider::StartSearch(const QString &artist, const QString &album, const QString &title, const int id) {

  if (!IsAuthenticated()) return false;

  if (artist.isEmpty() && album.isEmpty() && title.isEmpty()) return false;

  QString type;
  QString extract;
  QString query = artist;
  if (album.isEmpty() && !title.isEmpty()) {
    type = QLatin1String("track");
    extract = QLatin1String("tracks");
    if (!query.isEmpty()) query.append(QLatin1Char(' '));
    query.append(title);
  }
  else {
    type = QLatin1String("album");
    extract = QLatin1String("albums");
    if (!album.isEmpty()) {
      if (!query.isEmpty()) query.append(QLatin1Char(' '));
      query.append(album);
    }
  }

  ParamList params = ParamList() << Param(QStringLiteral("q"), query)
                                 << Param(QStringLiteral("type"), type)
                                 << Param(QStringLiteral("limit"), QString::number(kLimit));

  QUrlQuery url_query;
  for (const Param &param : params) {
    url_query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(param.first)), QString::fromLatin1(QUrl::toPercentEncoding(param.second)));
  }

  QUrl url(QLatin1String(kApiUrl) + QStringLiteral("/search"));
  url.setQuery(url_query);
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
  req.setRawHeader("Authorization", "Bearer " + service_->access_token().toUtf8());

  QNetworkReply *reply = network_->get(req);
  replies_ << reply;
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, id, extract]() { HandleSearchReply(reply, id, extract); });

  return true;

}

void SpotifyCoverProvider::CancelSearch(const int id) { Q_UNUSED(id); }

QByteArray SpotifyCoverProvider::GetReplyData(QNetworkReply *reply) {

  QByteArray data;

  if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
    data = reply->readAll();
  }
  else {
    if (reply->error() != QNetworkReply::NoError && reply->error() < 200) {
      // This is a network error, there is nothing more to do.
      Error(QStringLiteral("%1 (%2)").arg(reply->errorString()).arg(reply->error()));
    }
    else {
      data = reply->readAll();
      QJsonParseError parse_error;
      QJsonDocument json_doc = QJsonDocument::fromJson(data, &parse_error);
      QString error;
      if (parse_error.error == QJsonParseError::NoError && !json_doc.isEmpty() && json_doc.isObject()) {
        QJsonObject json_obj = json_doc.object();
        if (!json_obj.isEmpty() && json_obj.contains(QLatin1String("error")) && json_obj[QLatin1String("error")].isObject()) {
          QJsonObject obj_error = json_obj[QLatin1String("error")].toObject();
          if (obj_error.contains(QLatin1String("status")) && obj_error.contains(QLatin1String("message"))) {
            int status = obj_error[QLatin1String("status")].toInt();
            QString message = obj_error[QLatin1String("message")].toString();
            error = QStringLiteral("%1 (%2)").arg(message).arg(status);
            if (status == 401) Deauthenticate();
          }
        }
      }
      if (error.isEmpty()) {
        if (reply->error() != QNetworkReply::NoError) {
          if (reply->error() == 204) Deauthenticate();
          error = QStringLiteral("%1 (%2)").arg(reply->errorString()).arg(reply->error());
        }
        else {
          error = QStringLiteral("Received HTTP code %1").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        }
      }
      Error(error);
    }
    return QByteArray();
  }

  return data;

}

void SpotifyCoverProvider::HandleSearchReply(QNetworkReply *reply, const int id, const QString &extract) {

  if (!replies_.contains(reply)) return;
  replies_.removeAll(reply);
  QObject::disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();

  QByteArray data = GetReplyData(reply);
  if (data.isEmpty()) {
    emit SearchFinished(id, CoverProviderSearchResults());
    return;
  }

  QJsonObject json_obj = ExtractJsonObj(data);
  if (json_obj.isEmpty()) {
    emit SearchFinished(id, CoverProviderSearchResults());
    return;
  }

  if (!json_obj.contains(extract) || !json_obj[extract].isObject()) {
    Error(QStringLiteral("Json object is missing %1 object.").arg(extract), json_obj);
    emit SearchFinished(id, CoverProviderSearchResults());
    return;
  }
  json_obj = json_obj[extract].toObject();

  if (!json_obj.contains(QLatin1String("items")) || !json_obj[QLatin1String("items")].isArray()) {
    Error(QStringLiteral("%1 object is missing items array.").arg(extract), json_obj);
    emit SearchFinished(id, CoverProviderSearchResults());
    return;
  }

  QJsonArray array_items = json_obj[QLatin1String("items")].toArray();
  if (array_items.isEmpty()) {
    emit SearchFinished(id, CoverProviderSearchResults());
    return;
  }

  CoverProviderSearchResults results;
  for (const QJsonValueRef value_item : array_items) {

    if (!value_item.isObject()) {
      continue;
    }
    QJsonObject obj_item = value_item.toObject();

    QJsonObject obj_album = obj_item;
    if (obj_item.contains(QLatin1String("album")) && obj_item[QLatin1String("album")].isObject()) {
      obj_album = obj_item[QLatin1String("album")].toObject();
    }

    if (!obj_album.contains(QLatin1String("artists")) || !obj_album.contains(QLatin1String("name")) || !obj_album.contains(QLatin1String("images")) || !obj_album[QLatin1String("artists")].isArray() || !obj_album[QLatin1String("images")].isArray()) {
      continue;
    }
    QJsonArray array_artists = obj_album[QLatin1String("artists")].toArray();
    QJsonArray array_images = obj_album[QLatin1String("images")].toArray();
    QString album = obj_album[QLatin1String("name")].toString();

    QStringList artists;
    for (const QJsonValueRef value_artist : array_artists) {
      if (!value_artist.isObject()) continue;
      QJsonObject obj_artist = value_artist.toObject();
      if (!obj_artist.contains(QLatin1String("name"))) continue;
      artists << obj_artist[QLatin1String("name")].toString();
    }

    for (const QJsonValueRef value_image : array_images) {
      if (!value_image.isObject()) continue;
      QJsonObject obj_image = value_image.toObject();
      if (!obj_image.contains(QLatin1String("url")) || !obj_image.contains(QLatin1String("width")) || !obj_image.contains(QLatin1String("height"))) continue;
      int width = obj_image[QLatin1String("width")].toInt();
      int height = obj_image[QLatin1String("height")].toInt();
      if (width < 300 || height < 300) continue;
      QUrl url(obj_image[QLatin1String("url")].toString());
      CoverProviderSearchResult result;
      result.album = album;
      result.image_url = url;
      result.image_size = QSize(width, height);
      if (!artists.isEmpty()) result.artist = artists.first();
      results << result;
    }

  }
  emit SearchFinished(id, results);

}

void SpotifyCoverProvider::Error(const QString &error, const QVariant &debug) {

  qLog(Error) << "Spotify:" << error;
  if (debug.isValid()) qLog(Debug) << debug;

}
