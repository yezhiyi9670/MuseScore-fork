//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __SYSTEMTHEME_H__
#define __SYSTEMTHEME_H__

#include <QObject>
#include <QString>

#ifdef HAVE_QTDBUS
#include <QtDBus/QDBusVariant>
#endif

class QFileSystemWatcher;
class QTimer;

namespace Ms {

//---------------------------------------------------------
//   SystemTheme
//    Whether the operating system currently asks for a dark or
//    a light color scheme, and notifications when that changes.
//
//    Linux: XDG desktop portal (org.freedesktop.appearance
//           color-scheme), with the KDE/GTK configuration files
//           and the platform palette as fallbacks.
//    Windows: the AppsUseLightTheme registry value.
//    macOS: the Cocoa bridge, which also provides the
//           "AppleInterfaceThemeChangedNotification" change signal.
//---------------------------------------------------------

class SystemTheme : public QObject {
      Q_OBJECT

   public:
      enum class Scheme : char {
            UNKNOWN = 0,
            LIGHT,
            DARK
            };

      static SystemTheme* instance();

      void start();                             // detect now and subscribe to changes
      Scheme scheme() const { return _scheme; }

   signals:
      void schemeChanged();

   private:
      SystemTheme();
      ~SystemTheme() override = default;

      void refresh();
      void setScheme(Scheme s);
      Scheme detect();
      Scheme portalScheme();
      bool startPortalWatch();
      void watchConfigFiles();

#ifdef HAVE_QTDBUS
   private slots:
      void portalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value);
#endif

   private:
      Scheme _scheme { Scheme::UNKNOWN };
      bool _started { false };
      bool _initialWindowIsDark { false };
      bool _portalDead { false };
      QFileSystemWatcher* _watcher { nullptr };
      QTimer* _pollTimer { nullptr };
      };

} // namespace Ms

#endif
