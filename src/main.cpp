#include <QApplication>
#include <QDebug>
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
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/driftplayer_256.png")));

    enforceCLocale(); // Qt may change it back; reapply

    MainWindow window;
    window.show();

    const int result = app.exec();
    spdlog::info("Drift Player shutting down with code {}", result);
    return result;
}
