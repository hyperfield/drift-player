#include <QApplication>
#include <QDebug>

#include <clocale>

#include "MainWindow.h"

namespace
{
void enforceCLocale()
{
    if (!std::setlocale(LC_NUMERIC, "C")) {
        qWarning() << "Unable to set LC_NUMERIC to C";
    }
}
} // namespace

int main(int argc, char *argv[])
{
    enforceCLocale(); // ensure mpv sees C locale before Qt starts

    QApplication app(argc, argv);
    QApplication::setApplicationName("BVPlayer");
    QApplication::setOrganizationName("evoid");

    enforceCLocale(); // Qt may change it back; reapply

    MainWindow window;
    window.show();

    return app.exec();
}
