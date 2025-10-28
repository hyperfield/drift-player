#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

namespace
{
QString mediaFilter()
{
    return QObject::tr("Media files (*.mp4 *.mkv *.mov *.mp3 *.flac *.wav);;All files (*.*)");
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2) {
        return 2;
    }

    const QString outputPath = arguments.at(1);

    const QStringList files = QFileDialog::getOpenFileNames(nullptr,
                                                            QObject::tr("Add Media"),
                                                            QString(),
                                                            mediaFilter());

    QFile output(outputPath);
    if (output.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream stream(&output);
        for (const QString &file : files) {
            stream << file << '\n';
        }
    }

    return files.isEmpty() ? 1 : 0;
}
