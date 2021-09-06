#ifndef JSONPROTOCOLHANDLER_H
#define JSONPROTOCOLHANDLER_H

#include <QtNetwork>
#include <QAbstractSocket>
#include <QTcpSocket>
#include <QMutex>
#include <QObject>
#include <QTimerEvent>
//#include <QTextCodec>
#include <QJsonDocument>

class JsonProtocolHandler : public QObject
{
    Q_OBJECT
public:
    JsonProtocolHandler(QTcpSocket * sock, QObject *parent=0);
    ~JsonProtocolHandler();
    int getSocketDescriptor();
    QTcpSocket * getTcpSocket(){return socket;}
    QAbstractSocket::SocketState getTcpSocketState(){return socket?socket->state():QAbstractSocket::UnconnectedState;}
    QString peerAddressPort();
    QHostAddress peerAddress();
    quint16 peerPort();
    QString lastErrorString();

    void forceDisconnect();
    void safeAbort();
public slots:
    void sendReq(int id, QJsonValue data, bool showInLog=true);
    void sendAns(int id, QJsonValue data, bool showInLog=true);
    void sendVer(int ver);
    void end(bool force=false);
private:
    QTcpSocket * socket;
    bool peerEnded;
    bool weEnded;
    QByteArray incommingBuf;
    //QTextCodec *win1251;
    void processBuffer();
    bool socketValid();
signals:
    void reqArrived(int id, QJsonValue data);
    void ansArrived(int id, QJsonValue data);
    void verArrived(int ver);
    void endArrived();
    void finished();
    void error(QAbstractSocket::SocketError err);
    void parseError(QByteArray trash);
    void debugLog(QString msg);
private slots:
    void readyRead();
    void disconnected();
    void errorThunk(QAbstractSocket::SocketError err);
};

#endif // JSONPROTOCOLHANDLER_H
