/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
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

#ifndef XSPFPARSER_H
#define XSPFPARSER_H

#include "config.h"

#include <QObject>
#include <QByteArray>
#include <QDir>
#include <QString>
#include <QStringList>

#include "core/shared_ptr.h"
#include "core/song.h"
#include "settings/playlistsettingspage.h"
#include "xmlparser.h"

class QIODevice;
class QXmlStreamReader;
class CollectionBackendInterface;

class XSPFParser : public XMLParser {
  Q_OBJECT

 public:
  explicit XSPFParser(SharedPtr<CollectionBackendInterface> collection_backend, QObject *parent = nullptr);

  QString name() const override { return QStringLiteral("XSPF"); }
  QStringList file_extensions() const override { return QStringList() << QStringLiteral("xspf"); }
  bool load_supported() const override { return true; }
  bool save_supported() const override { return true; }

  bool TryMagic(const QByteArray &data) const override;

  SongList Load(QIODevice *device, const QString &playlist_path = QLatin1String(""), const QDir &dir = QDir(), const bool collection_search = true) const override;
  void Save(const SongList &songs, QIODevice *device, const QDir &dir = QDir(), const PlaylistSettingsPage::PathType path_type = PlaylistSettingsPage::PathType::Automatic) const override;

 private:
  Song ParseTrack(QXmlStreamReader *reader, const QDir &dir, const bool collection_search) const;
};

#endif
