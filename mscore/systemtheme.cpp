//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "systemtheme.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#ifdef HAVE_QTDBUS
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#endif

#ifdef Q_OS_MAC
#include "macos/cocoabridge.h"
#endif

namespace Ms {

namespace {

//---------------------------------------------------------
//   schemeFromPortalValue
//    org.freedesktop.appearance color-scheme:
//    0 = no preference, 1 = prefer dark, 2 = prefer light
//---------------------------------------------------------

#ifdef HAVE_QTDBUS
SystemTheme::Scheme schemeFromPortalValue(const QVariant& value)
      {
      switch (value.toUInt()) {
            case 1:  return SystemTheme::Scheme::DARK;
            case 2:  return SystemTheme::Scheme::LIGHT;
            default: return SystemTheme::Scheme::UNKNOWN;
            }
      }
#endif

#if defined(Q_OS_LINUX)
QString configFile(const QString& name)
      {
      return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/') + name;
      }

//---------------------------------------------------------
//   kdeScheme
//    Plasma keeps the colors of the current scheme in kdeglobals.
//---------------------------------------------------------

SystemTheme::Scheme kdeScheme()
      {
      const QString path = configFile(QStringLiteral("kdeglobals"));
      if (!QFileInfo::exists(path))
            return SystemTheme::Scheme::UNKNOWN;
      QSettings cfg(path, QSettings::IniFormat);
      const QString background = cfg.value(QStringLiteral("Colors:Window/BackgroundNormal")).toString();
      if (!background.isEmpty()) {
            const QStringList rgb = background.split(QLatin1Char(','));
            if (rgb.size() == 3)
                  return QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt()).lightness() < 128
                        ? SystemTheme::Scheme::DARK : SystemTheme::Scheme::LIGHT;
            }
      const QString scheme = cfg.value(QStringLiteral("General/ColorScheme")).toString();
      if (!scheme.isEmpty())
            return scheme.contains(QLatin1String("dark"), Qt::CaseInsensitive)
                  ? SystemTheme::Scheme::DARK : SystemTheme::Scheme::LIGHT;
      return SystemTheme::Scheme::UNKNOWN;
      }

//---------------------------------------------------------
//   gtkScheme
//---------------------------------------------------------

SystemTheme::Scheme gtkScheme()
      {
      const QStringList names { QStringLiteral("gtk-4.0/settings.ini"), QStringLiteral("gtk-3.0/settings.ini") };
      for (const QString& name : names) {
            const QString path = configFile(name);
            if (!QFileInfo::exists(path))
                  continue;
            QSettings cfg(path, QSettings::IniFormat);
            const QString theme = cfg.value(QStringLiteral("Settings/gtk-theme-name")).toString();
            if (theme.contains(QLatin1String("dark"), Qt::CaseInsensitive))
                  return SystemTheme::Scheme::DARK;
            if (cfg.value(QStringLiteral("Settings/gtk-application-prefer-dark-theme")).toInt())
                  return SystemTheme::Scheme::DARK;
            if (!theme.isEmpty() || cfg.contains(QStringLiteral("Settings/gtk-application-prefer-dark-theme")))
                  return SystemTheme::Scheme::LIGHT;
            }
      return SystemTheme::Scheme::UNKNOWN;
      }
#endif // Q_OS_LINUX

#if defined(Q_OS_WIN)
//---------------------------------------------------------
//   windowsScheme
//---------------------------------------------------------

SystemTheme::Scheme windowsScheme()
      {
      QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                    QSettings::NativeFormat);
      const QVariant value = reg.value(QStringLiteral("AppsUseLightTheme"));
      if (!value.isValid())
            return SystemTheme::Scheme::UNKNOWN;
      return value.toInt() == 0 ? SystemTheme::Scheme::DARK : SystemTheme::Scheme::LIGHT;
      }
#endif // Q_OS_WIN

#ifdef HAVE_QTDBUS
//---------------------------------------------------------
//   portalRead
//    Calls org.freedesktop.portal.Settings.Read for the
//    org.freedesktop.appearance namespace.
//---------------------------------------------------------

QVariant portalRead(const QString& key, bool* ok)
      {
      *ok = false;
      QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Settings"),
            QStringLiteral("Read"));
      msg << QStringLiteral("org.freedesktop.appearance") << key;
      const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
      if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return QVariant();
      QVariant value = reply.arguments().first();
      if (value.canConvert<QDBusVariant>())
            value = value.value<QDBusVariant>().variant();
      *ok = true;
      return value;
      }
#endif // HAVE_QTDBUS

} // namespace

//---------------------------------------------------------
//   instance
//---------------------------------------------------------

SystemTheme* SystemTheme::instance()
      {
      static SystemTheme* inst = new SystemTheme;
      return inst;
      }

SystemTheme::SystemTheme()
   : QObject(nullptr)
      {
      }

//---------------------------------------------------------
//   start
//    Detects the current scheme and subscribes to changes.
//    Must be called before the first UI theme is applied,
//    while the application palette still is the platform one.
//---------------------------------------------------------

void SystemTheme::start()
      {
      if (_started)
            return;
      _started = true;

      _initialWindowIsDark = QApplication::palette().color(QPalette::Window).lightness() < 128;

#if defined(Q_OS_MAC)
      CocoaBridge::observeDarkModeSwitches([this]() {
            setScheme(CocoaBridge::isSystemDarkTheme() ? Scheme::DARK : Scheme::LIGHT);
            });
#elif defined(Q_OS_WIN)
      // Qt 5 does not report dark mode changes, so poll the registry.
      _pollTimer = new QTimer(this);
      _pollTimer->setInterval(2000);
      connect(_pollTimer, &QTimer::timeout, this, &SystemTheme::refresh);
      _pollTimer->start();
#else
      if (!startPortalWatch()) {
            _portalDead = true;
            qDebug("SystemTheme: no desktop portal, using configuration files and the platform palette");
            }
      watchConfigFiles();
#endif
      refresh();
      }

//---------------------------------------------------------
//   startPortalWatch
//    Subscribes to the portal's SettingChanged signal. A missing
//    portal is not fatal, the fallbacks are used instead.
//---------------------------------------------------------

bool SystemTheme::startPortalWatch()
      {
#ifdef HAVE_QTDBUS
      QDBusConnection bus = QDBusConnection::sessionBus();
      if (!bus.isConnected())
            return false;
      return bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"),
                         QStringLiteral("/org/freedesktop/portal/desktop"),
                         QStringLiteral("org.freedesktop.portal.Settings"),
                         QStringLiteral("SettingChanged"),
                         QStringLiteral("ssv"),
                         this,
                         SLOT(portalSettingChanged(QString,QString,QDBusVariant)));
#else
      return false;
#endif
      }

//---------------------------------------------------------
//   portalSettingChanged
//---------------------------------------------------------

#ifdef HAVE_QTDBUS
void SystemTheme::portalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value)
      {
      if (ns != QLatin1String("org.freedesktop.appearance") || key != QLatin1String("color-scheme"))
            return;
      const Scheme s = schemeFromPortalValue(value.variant());
      if (s == Scheme::UNKNOWN)
            refresh();          // "no preference", let the fallbacks decide
      else
            setScheme(s);
      }
#endif

//---------------------------------------------------------
//   portalScheme
//---------------------------------------------------------

SystemTheme::Scheme SystemTheme::portalScheme()
      {
#ifdef HAVE_QTDBUS
      if (_portalDead)
            return Scheme::UNKNOWN;
      bool ok = false;
      const QVariant value = portalRead(QStringLiteral("color-scheme"), &ok);
      if (!ok)
            return Scheme::UNKNOWN;
      return schemeFromPortalValue(value);
#else
      return Scheme::UNKNOWN;
#endif
      }

//---------------------------------------------------------
//   detect
//---------------------------------------------------------

SystemTheme::Scheme SystemTheme::detect()
      {
      Scheme s = Scheme::UNKNOWN;
#if defined(Q_OS_MAC)
      s = CocoaBridge::isSystemDarkTheme() ? Scheme::DARK : Scheme::LIGHT;
#elif defined(Q_OS_WIN)
      s = windowsScheme();
#elif defined(Q_OS_LINUX)
      s = portalScheme();
      if (s == Scheme::UNKNOWN)
            s = kdeScheme();
      if (s == Scheme::UNKNOWN)
            s = gtkScheme();
#endif
      if (s == Scheme::UNKNOWN)
            s = _initialWindowIsDark ? Scheme::DARK : Scheme::LIGHT;
      return s;
      }

//---------------------------------------------------------
//   refresh
//---------------------------------------------------------

void SystemTheme::refresh()
      {
      setScheme(detect());
      }

//---------------------------------------------------------
//   setScheme
//---------------------------------------------------------

void SystemTheme::setScheme(Scheme s)
      {
      if (s == Scheme::UNKNOWN || s == _scheme)
            return;
      _scheme = s;
      emit schemeChanged();
      }

//---------------------------------------------------------
//   watchConfigFiles
//    Live fallback for systems without a desktop portal.
//---------------------------------------------------------

void SystemTheme::watchConfigFiles()
      {
#if defined(Q_OS_LINUX)
      if (_watcher)
            return;
      const QStringList candidates {
            configFile(QStringLiteral("kdeglobals")),
            configFile(QStringLiteral("gtk-3.0/settings.ini")),
            configFile(QStringLiteral("gtk-4.0/settings.ini"))
            };
      _watcher = new QFileSystemWatcher(this);
      for (const QString& f : candidates) {
            if (QFileInfo::exists(f))
                  _watcher->addPath(f);
            }
      connect(_watcher, &QFileSystemWatcher::fileChanged, this, [this, candidates](const QString&) {
            // These files are usually replaced rather than modified, which makes
            // the watcher drop them, so re-add everything and re-check.
            QTimer::singleShot(50, this, [this, candidates]() {
                  for (const QString& f : candidates) {
                        if (QFileInfo::exists(f) && !_watcher->files().contains(f))
                              _watcher->addPath(f);
                        }
                  refresh();
                  });
            });
#endif
      }

} // namespace Ms
