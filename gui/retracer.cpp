#include "retracer.h"

#include "apitracecall.h"
#include "thumbnail.h"

#include "image.hpp"

#include <QDebug>
#include <QVariant>
#include <QList>
#include <QImage>

#include <qjson/parser.h>

Retracer::Retracer(QObject *parent)
    : QThread(parent),
      m_benchmarking(false),
      m_doubleBuffered(true),
      m_captureState(false),
      m_captureCall(0)
{
#ifdef Q_OS_WIN
    QString format = QLatin1String("%1;");
#else
    QString format = QLatin1String("%1:");
#endif
    QString buildPath = format.arg(APITRACE_BINARY_DIR);
    m_processEnvironment = QProcessEnvironment::systemEnvironment();
    m_processEnvironment.insert("PATH", buildPath +
                                m_processEnvironment.value("PATH"));

    qputenv("PATH",
            m_processEnvironment.value("PATH").toLatin1());
}

QString Retracer::fileName() const
{
    return m_fileName;
}

void Retracer::setFileName(const QString &name)
{
    m_fileName = name;
}

void Retracer::setAPI(trace::API api)
{
    m_api = api;
}

bool Retracer::isBenchmarking() const
{
    return m_benchmarking;
}

void Retracer::setBenchmarking(bool bench)
{
    m_benchmarking = bench;
}

bool Retracer::isDoubleBuffered() const
{
    return m_doubleBuffered;
}

void Retracer::setDoubleBuffered(bool db)
{
    m_doubleBuffered = db;
}

void Retracer::setCaptureAtCallNumber(qlonglong num)
{
    m_captureCall = num;
}

qlonglong Retracer::captureAtCallNumber() const
{
    return m_captureCall;
}

bool Retracer::captureState() const
{
    return m_captureState;
}

void Retracer::setCaptureState(bool enable)
{
    m_captureState = enable;
}

bool Retracer::captureThumbnails() const
{
    return m_captureThumbnails;
}

void Retracer::setCaptureThumbnails(bool enable)
{
    m_captureThumbnails = enable;
}


void Retracer::run()
{
    RetraceProcess *retrace = new RetraceProcess();
    retrace->process()->setProcessEnvironment(m_processEnvironment);

    retrace->setFileName(m_fileName);
    retrace->setAPI(m_api);
    retrace->setBenchmarking(m_benchmarking);
    retrace->setDoubleBuffered(m_doubleBuffered);
    retrace->setCaptureState(m_captureState);
    retrace->setCaptureThumbnails(m_captureThumbnails);
    retrace->setCaptureAtCallNumber(m_captureCall);

    connect(retrace, SIGNAL(finished(const QString&)),
            this, SLOT(cleanup()));
    connect(retrace, SIGNAL(error(const QString&)),
            this, SLOT(cleanup()));
    connect(retrace, SIGNAL(finished(const QString&)),
            this, SIGNAL(finished(const QString&)));
    connect(retrace, SIGNAL(error(const QString&)),
            this, SIGNAL(error(const QString&)));
    connect(retrace, SIGNAL(foundState(ApiTraceState*)),
            this, SIGNAL(foundState(ApiTraceState*)));
    connect(retrace, SIGNAL(foundThumbnails(const QList<QImage>&)),
            this, SIGNAL(foundThumbnails(const QList<QImage>&)));
    connect(retrace, SIGNAL(retraceErrors(const QList<ApiTraceError>&)),
            this, SIGNAL(retraceErrors(const QList<ApiTraceError>&)));

    retrace->start();

    exec();

    /* means we need to kill the process */
    if (retrace->process()->state() != QProcess::NotRunning) {
        retrace->terminate();
    }

    delete retrace;
}


void RetraceProcess::start()
{
    QString prog;
    QStringList arguments;

    if (m_api == trace::API_GL) {
        prog = QLatin1String("glretrace");
    } else if (m_api == trace::API_EGL) {
        prog = QLatin1String("eglretrace");
    } else {
        assert(0);
        return;
    }

    if (m_doubleBuffered) {
        arguments << QLatin1String("-db");
    } else {
        arguments << QLatin1String("-sb");
    }

    if (m_captureState || m_captureThumbnails) {
        if (m_captureState) {
            arguments << QLatin1String("-D");
            arguments << QString::number(m_captureCall);
        }
        if (m_captureThumbnails) {
            arguments << QLatin1String("-s"); // emit snapshots
            arguments << QLatin1String("-"); // emit to stdout
        }
    } else {
        if (m_benchmarking) {
            arguments << QLatin1String("-b");
        }
    }

    arguments << m_fileName;

    m_process->start(prog, arguments);
}


void RetraceProcess::replayFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QString msg;

    if (exitStatus != QProcess::NormalExit) {
        msg = QLatin1String("Process crashed");
    } else if (exitCode != 0) {
        msg = QLatin1String("Process exited with non zero exit code");
    } else {
        if (m_captureState || m_captureThumbnails) {
            if (m_captureState) {
                bool ok = false;
                m_process->setReadChannel(QProcess::StandardOutput);
                QVariantMap parsedJson = m_jsonParser->parse(m_process, &ok).toMap();
                ApiTraceState *state = new ApiTraceState(parsedJson);
                emit foundState(state);
                msg = tr("State fetched.");
            }
            if (m_captureThumbnails) {
                m_process->setReadChannel(QProcess::StandardOutput);

                QList<QImage> thumbnails;

                while (!m_process->atEnd()) {
                    unsigned channels = 0;
                    unsigned width = 0;
                    unsigned height = 0;

                    char header[512];
                    qint64 headerSize = 0;
                    int headerLines = 3; // assume no optional comment line

                    for (int headerLine = 0; headerLine < headerLines; ++headerLine) {
                        qint64 headerRead = m_process->readLine(&header[headerSize], sizeof(header) - headerSize);

                        // if header actually contains optional comment line, ...
                        if (headerLine == 1 && header[headerSize] == '#') {
                            ++headerLines;
                        }

                        headerSize += headerRead;
                    }

                    const char *headerEnd = image::readPNMHeader(header, headerSize, &channels, &width, &height);

                    // if invalid PNM header was encountered, ...
                    if (header == headerEnd) {
                        qDebug() << "error: invalid snapshot stream encountered\n";
                        break;
                    }

                    //qDebug() << "channels: " << channels << ", width: " << width << ", height: " << height << "\n";

                    QImage snapshot = QImage(width, height, channels == 1 ? QImage::Format_Mono : QImage::Format_RGB888);

                    int rowBytes = channels * width;
                    for (int y = 0; y < height; ++y) {
                        unsigned char *scanLine = snapshot.scanLine(y);
                        m_process->read((char *) scanLine, rowBytes);
                    }

                    QImage thumb = thumbnail(snapshot);
                    thumbnails.append(thumb);
                }

                emit foundThumbnails(thumbnails);
                msg = tr("Thumbnails fetched.");
            }
        } else {
            QByteArray output;
            output = m_process->readAllStandardOutput();
            msg = QString::fromUtf8(output);
        }
    }

    m_process->setReadChannel(QProcess::StandardError);
    QList<ApiTraceError> errors;
    QRegExp regexp("(^\\d+): +(\\b\\w+\\b): ([^\\r\\n]+)[\\r\\n]*$");
    while (!m_process->atEnd()) {
        QString line = m_process->readLine();
        if (regexp.indexIn(line) != -1) {
            ApiTraceError error;
            error.callIndex = regexp.cap(1).toInt();
            error.type = regexp.cap(2);
            error.message = regexp.cap(3);
            errors.append(error);
        }
    }
    if (!errors.isEmpty()) {
        emit retraceErrors(errors);
    }
    emit finished(msg);
}

void RetraceProcess::replayError(QProcess::ProcessError err)
{
    /*
     * XXX: this function is likely unnecessary and should be eliminated given
     * that replayFinished is always called, even on errors.
     */

#if 0
    qDebug()<<"Process error = "<<err;
    qDebug()<<"\terr = "<<m_process->readAllStandardError();
    qDebug()<<"\tout = "<<m_process->readAllStandardOutput();
#endif

    emit error(
        tr("Couldn't execute the replay file '%1'").arg(m_fileName));
}

Q_DECLARE_METATYPE(QList<ApiTraceError>);
RetraceProcess::RetraceProcess(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_jsonParser = new QJson::Parser();

    qRegisterMetaType<QList<ApiTraceError> >();

    connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(replayFinished(int, QProcess::ExitStatus)));
    connect(m_process, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(replayError(QProcess::ProcessError)));
}

QProcess * RetraceProcess::process() const
{
    return m_process;
}

QString RetraceProcess::fileName() const
{
    return m_fileName;
}

void RetraceProcess::setFileName(const QString &name)
{
    m_fileName = name;
}

void RetraceProcess::setAPI(trace::API api)
{
    m_api = api;
}

bool RetraceProcess::isBenchmarking() const
{
    return m_benchmarking;
}

void RetraceProcess::setBenchmarking(bool bench)
{
    m_benchmarking = bench;
}

bool RetraceProcess::isDoubleBuffered() const
{
    return m_doubleBuffered;
}

void RetraceProcess::setDoubleBuffered(bool db)
{
    m_doubleBuffered = db;
}

void RetraceProcess::setCaptureAtCallNumber(qlonglong num)
{
    m_captureCall = num;
}

qlonglong RetraceProcess::captureAtCallNumber() const
{
    return m_captureCall;
}

bool RetraceProcess::captureState() const
{
    return m_captureState;
}

void RetraceProcess::setCaptureState(bool enable)
{
    m_captureState = enable;
}

bool RetraceProcess::captureThumbnails() const
{
    return m_captureThumbnails;
}

void RetraceProcess::setCaptureThumbnails(bool enable)
{
    m_captureThumbnails = enable;
}

void RetraceProcess::terminate()
{
    if (m_process) {
        m_process->terminate();
        emit finished(tr("Process terminated."));
    }
}

void Retracer::cleanup()
{
    quit();
}

RetraceProcess::~RetraceProcess()
{
    delete m_jsonParser;
}

#include "retracer.moc"
