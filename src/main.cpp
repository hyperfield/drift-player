#include <QApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>

#include <clocale>

#include <spdlog/spdlog.h>

#include "MainWindow.h"
#include "app_version.hpp"

namespace
{
void enforceCLocale()
{
    if (!std::setlocale(LC_NUMERIC, "C")) {
        spdlog::warn("Unable to set LC_NUMERIC to C");
    }
}
} // namespace

int main(int argc, char *argv[])
{
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::info("Drift Player starting up");

    enforceCLocale(); // ensure mpv sees C locale before Qt starts

    QApplication app(argc, argv);
    QApplication::setApplicationName("Drift Player");
    QApplication::setApplicationVersion(QStringLiteral(DRIFT_APP_VERSION_STRING));
    QApplication::setOrganizationName("hyperfield");
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QGuiApplication::setDesktopFileName(QStringLiteral("drift-player.desktop"));
#endif
    const QIcon appIcon =
        QIcon::fromTheme(QStringLiteral("drift-player"),
                         QIcon(QStringLiteral(":/images/logo.png")));
    app.setWindowIcon(appIcon);

    enforceCLocale(); // Qt may change it back; reapply

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    const int result = app.exec();
    spdlog::info("Drift Player shutting down with code {}", result);
    return result;
}
