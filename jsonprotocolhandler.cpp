#include "jsonprotocolhandler.h"
#include <QTimer>

JsonProtocolHandler::JsonProtocolHandler(QTcpSocket *sock,  QString logFileName, QObject *parent)
    : QObject(parent), logf(0), logts(0)//, win1251(QTextCodec::codecForName("Windows-1251"))
{
    if(!logFileName.isEmpty())
    {
        QFile *tmpf=new QFile(logFileName);
        if(tmpf->open(QIODevice::WriteOnly | QIODevice::Text))
        {
            logf=tmpf;
            logts=new QTextStream(logf);
        }
        else
            delete tmpf;
    }
    socket=sock;
    //socket->setParent(this);
    peerEnded=false;
    weEnded=false;
    connect(socket, SIGNAL(errorOccurred(QAbstractSocket::SocketError)), this, SLOT(errorThunk(QAbstractSocket::SocketError)));
    connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
}

void JsonProtocolHandler::forceDisconnect()
{
    qDebug() << ("JsonProtocolHandler::forceDisconnect");
    end(true);
    disconnected();
}

void JsonProtocolHandler::safeAbort()
{
    /*
    try
    {
        socket->abort();
    }
    catch(...)
    {
        //do nothing
    }
    */
}

void JsonProtocolHandler::disconnected()
{
    emit finished();
}

int JsonProtocolHandler::getSocketDescriptor()
{
    if(socket)
        return socket->socketDescriptor();
    return 0;
}

QString JsonProtocolHandler::peerAddressPort()
{
    if(!socket)
        return QString();
    return QString("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
}

JsonProtocolHandler::~JsonProtocolHandler()
{
    qDebug() << "Socket deleted";
    socket->deleteLater();
    socket=0;
}

void JsonProtocolHandler::sendReq(int id, QJsonValue data, bool showInLog)
{
    if(weEnded)
        return;
    if(!socketValid())
    {
        weEnded = true;
        return;
    }
    QJsonObject jobj
    {
        {"id", id},
        {"type", QString("req")},
        {"data", data}
    };
    QJsonDocument jdoc(jobj);
    QByteArray msg = jdoc.toJson(QJsonDocument::Compact);
    if(showInLog)
    {
        qDebug() << (QString("Send req[%1]:").arg(id) + QString::fromLocal8Bit(msg));
    }
    logOutgoing(msg);

    socket->write(msg);
    socket->flush();
    //qDebug() << "Sent";
}

void JsonProtocolHandler::sendAns(int id, QJsonValue data, bool showInLog)
{
    if(weEnded)
        return;
    if(!socketValid())
    {
        weEnded = true;
        return;
    }
    QJsonObject jobj
    {
        {"id", id},
        {"type", QString("ans")},
        {"data", data}
    };
    QJsonDocument jdoc(jobj);
    QByteArray msg = jdoc.toJson(QJsonDocument::Compact);
    if(showInLog)
    {
        qDebug() << (QString("Send ans[%1]:").arg(id) + QString::fromLocal8Bit(msg));
    }
    logOutgoing(msg);

    socket->write(msg);
    socket->flush();
    //qDebug() << ("Sent");
}

void JsonProtocolHandler::sendVer(int ver)
{
    if(weEnded)
        return;
    if(!socketValid())
    {
        weEnded = true;
        return;
    }
    QJsonObject jobj
    {
        {"id", 0},
        {"type", QString("ver")},
        {"version", ver}
    };
    QJsonDocument jdoc(jobj);
    QByteArray msg = jdoc.toJson(QJsonDocument::Compact);

    qDebug() << (QString("Send ver[%1]:").arg(ver) + QString::fromLocal8Bit(msg));
    logOutgoing(msg);

    socket->write(msg);
    socket->flush();
    //qDebug() << ("Sent");
}

void JsonProtocolHandler::end(bool force)
{
    if(weEnded && !force)
        return;
    if(!socketValid())
    {
        weEnded = true;
        return;
    }
    QJsonObject jobj
    {
        {"id", 0},
        {"type", QString("end")}
    };
    QJsonDocument jdoc(jobj);
    QByteArray msg = jdoc.toJson(QJsonDocument::Compact);

    logOutgoing(msg);
    qDebug() << ("send END");
    socket->write(msg);
    socket->flush();
    //qDebug() << ("Sent");
    weEnded=true;
    if(peerEnded)
        socket->disconnectFromHost();
    else
        if(force)
            socket->abort();
}

void JsonProtocolHandler::processBuffer()
{
    int i;
    for(i=0; i<incommingBuf.length(); i++)
    {
        if(incommingBuf[i] == '{')
        {
            if(i>0)
            {
                QByteArray trash = incommingBuf.left(i);
                incommingBuf = incommingBuf.mid(i);
                qDebug() << ("Malformed request");
                emit parseError(trash);
                i=0;
            }
            break;
        }
    }
    if(i)
        return;

    bool in_string = false;
    bool in_esc = false;
    int brace_nesting_level = 0;

    for(i=0; i<incommingBuf.length(); i++)
    {
        const char curr_ch = incommingBuf[i];

        if(curr_ch == '"' && !in_esc)
        {
            in_string = !in_string;
            continue;
        }

        if(!in_string)
        {
            if(curr_ch == '{')
                brace_nesting_level++;
            else if(curr_ch == '}')
            {
                brace_nesting_level--;
                if(brace_nesting_level == 0)
                {
                    QByteArray pdoc = incommingBuf.left(i+1);
                    if(incommingBuf.length() == i+1)
                        incommingBuf.clear();
                    else
                        incommingBuf = incommingBuf.mid(i+1);
                    i=-1;
                    in_string = false;
                    in_esc = false;
                    brace_nesting_level = 0;
                    QJsonDocument jdoc = QJsonDocument::fromJson(pdoc);
                    if(jdoc.isNull() || !jdoc.isObject())
                    {
                        qDebug() << ("Malformed request");
                        emit parseError(pdoc);
                    }
                    else
                    {
                        //qDebug() << (QString("Received:") + QString::fromLocal8Bit(pdoc));
                        QJsonObject jobj = jdoc.object();
                        if(jobj.contains("id") && jobj.contains("type"))
                        {
                            int id = jobj.value("id").toInt(-1);
                            if(id>=0)
                            {
                                QString mtype = jobj.value("type").toString("nul").toLower();
                                if(mtype == "end")
                                {
                                    peerEnded=true;
                                    if(weEnded)
                                    {
                                        socket->disconnectFromHost();
                                    }
                                    else
                                    {
                                        emit endArrived();
                                        end();
                                    }
                                    return;
                                }
                                if(mtype == "ver")
                                {
                                    int ver = 0;
                                    if(jobj.contains("version"))
                                        ver = jobj.value("version").toInt(0);
                                    emit verArrived(ver);
                                    return;
                                }
                                if(mtype == "ans" || mtype == "req")
                                {
                                    QJsonValue data;
                                    if(jobj.contains("data"))
                                    {
                                        data = jobj.value("data");
                                    }
                                    if(mtype == "ans")
                                        emit ansArrived(id, data);
                                    else
                                        emit reqArrived(id, data);
                                    return;
                                }
                            }
                        }
                    }
                    continue;
                }
            }
        }
        else
        {
            if(curr_ch == '\\' && !in_esc)
                in_esc = true;
            else
                in_esc = false;
        }
    }
}

bool JsonProtocolHandler::socketValid()
{
    if(weEnded)
        return false;
    if(!socket || !socket->isOpen())
        return false;
    if(socket->state() != QAbstractSocket::ConnectedState )
        return false;
    return true;
}

void JsonProtocolHandler::logIncoming(const QByteArray &msg)
{
    if(!logts)
        return;
    *logts << Qt::endl << "<--" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << Qt::endl;
    *logts << QString::fromLocal8Bit(msg);
    logts->flush();
}

void JsonProtocolHandler::logOutgoing(const QByteArray &msg)
{
    if(!logts)
        return;
    *logts << Qt::endl << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "-->" << Qt::endl;
    *logts << QString::fromLocal8Bit(msg);
    logts->flush();
}

void JsonProtocolHandler::readyRead()
{
    if(!socketValid() || !socket->bytesAvailable())
        return;
    if(peerEnded)
    {
        char buf[200];
        int ibav;
        qint64 bav;
        while((bav=socket->bytesAvailable())>0)
        {
            ibav=(bav>200)?200:(int)bav;
            socket->read(buf,ibav);
        }
        return;
    }
    QByteArray chunk = socket->readAll();
    logIncoming(chunk);
    incommingBuf.append(chunk);
    processBuffer();
}

void JsonProtocolHandler::errorThunk(QAbstractSocket::SocketError err)
{
    safeAbort();
    emit error(err);
}

QHostAddress JsonProtocolHandler::peerAddress()
{
    if(!socket)
        return QHostAddress();
    return socket->peerAddress();
}

quint16 JsonProtocolHandler::peerPort()
{
    if(!socket)
        return 0;
    return socket->peerPort();
}

QString JsonProtocolHandler::lastErrorString()
{
    if(!socket)
        return "There is no socket";
    return socket->errorString();
}
