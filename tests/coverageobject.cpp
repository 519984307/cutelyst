#include "coverageobject.h"

#include "enginerequest.h"

#include <Cutelyst/context.h>

#include <QDebug>
#include <QLibrary>
#include <QMetaObject>
#include <QString>
#include <QTest>

using namespace Cutelyst;

class TestEngineConnection : public EngineRequest
{
public:
    TestEngineConnection() {}

protected:
    virtual qint64 doWrite(const char *data, qint64 len) final;
    virtual bool writeHeaders(quint16 status, const Headers &headers) final;

public:
    QByteArray m_responseData;
    QByteArray m_status;
    Headers m_headers;
    quint16 m_statusCode;
};

void CoverageObject::init()
{
    initTest();
}

QString CoverageObject::generateTestName() const
{
    QString test_name;
    test_name += QString::fromLatin1(metaObject()->className());
    test_name += QLatin1Char('/');
    test_name += QString::fromLatin1(QTest::currentTestFunction());
    if (QTest::currentDataTag()) {
        test_name += QLatin1Char('/');
        test_name += QString::fromLatin1(QTest::currentDataTag());
    }
    return test_name;
}

void CoverageObject::saveCoverageData()
{
#ifdef __COVERAGESCANNER__
    QString test_name;
    test_name += generateTestName();

    __coveragescanner_testname(test_name.toStdString().c_str());
    if (QTest::currentTestFailed())
        __coveragescanner_teststate("FAILED");
    else
        __coveragescanner_teststate("PASSED");
    __coveragescanner_save();
    __coveragescanner_testname("");
    __coveragescanner_clear();
#endif
}

void CoverageObject::cleanup()
{
    cleanupTest();
    saveCoverageData();
}

TestEngine::TestEngine(Application *app, const QVariantMap &opts)
    : Engine(app, 0, opts)
{
}

int TestEngine::workerId() const
{
    return 0;
}

QVariantMap TestEngine::createRequest(const QByteArray &method,
                                      const QString &path,
                                      const QByteArray &query,
                                      const Headers &headers,
                                      QByteArray *body)
{
    QIODevice *bodyDevice = nullptr;
    if (headers.header("Sequential"_qba).isEmpty()) {
        bodyDevice = new QBuffer(body);
    } else {
        bodyDevice = new SequentialBuffer(body);
    }
    bodyDevice->open(QIODevice::ReadOnly);

    Headers headersCL = headers;
    if (bodyDevice->size()) {
        headersCL.setContentLength(bodyDevice->size());
    }

    QVariantMap ret;

    TestEngineConnection req;
    req.method = method;
    req.setPath(path);
    req.query         = query;
    req.protocol      = "HTTP/1.1"_qba;
    req.isSecure      = false;
    req.serverAddress = "127.0.0.1"_qba;
    req.remoteAddress = QHostAddress(u"127.0.0.1"_qs);
    req.remotePort    = 3000;
    req.remoteUser    = QString{};
    req.headers       = headersCL;
    req.elapsed.start();
    req.body = bodyDevice;

    processRequest(&req);

    ret = {{u"body"_qs, req.m_responseData},
           {u"status"_qs, req.m_status},
           {u"statusCode"_qs, req.m_statusCode},
           {u"headers"_qs, QVariant::fromValue(req.m_headers)}};

    return ret;
}

bool TestEngine::init()
{
    return initApplication() && postForkApplication();
}

SequentialBuffer::SequentialBuffer(QByteArray *buffer)
    : buf(buffer)
{
}

bool SequentialBuffer::isSequential() const
{
    return true;
}

qint64 SequentialBuffer::bytesAvailable() const
{
    return buf->size() + QIODevice::bytesAvailable();
}

qint64 SequentialBuffer::readData(char *data, qint64 maxlen)
{
    QByteArray mid = buf->mid(pos(), maxlen);
    memcpy(data, mid.data(), mid.size());
    // Sequential devices consume the body
    buf->remove(0, mid.size());
    return mid.size();
}

qint64 SequentialBuffer::writeData(const char *data, qint64 len)
{
    return -1;
}

#include "moc_coverageobject.cpp"

qint64 TestEngineConnection::doWrite(const char *data, qint64 len)
{
    m_responseData.append(data, len);
    return len;
}

bool TestEngineConnection::writeHeaders(quint16 status, const Headers &headers)
{
    qDebug() << "---------= " << status;
    int len;
    const auto *statusChar = TestEngine::httpStatusMessage(status, &len);
    m_statusCode           = status;
    m_status               = QByteArray(statusChar + 9, len - 9);
    m_headers              = headers;

    return true;
}
