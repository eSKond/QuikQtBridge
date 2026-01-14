#include "bridgetcpserver.h"
#include <QRegularExpression>

#define ALLOW_LOCAL_IP

BridgeTCPServer * BridgeTCPServer::g_server = nullptr;
struct FastCallbackFunctionData
{
    ConnectionData *cd;
    int objId;
    QString funName;
    FastCallbackFunctionData():cd(nullptr),objId(-1){}
    FastCallbackFunctionData(QString fn):cd(nullptr),objId(-1),funName(fn){}
};

BridgeTCPServer::BridgeTCPServer(QObject *parent)
    : QTcpServer(parent), logf(nullptr), logts(nullptr)
{
    g_server = this;
    connect(this, SIGNAL(acceptError(QAbstractSocket::SocketError)), this, SLOT(serverError(QAbstractSocket::SocketError)));
    qqBridge->registerCallback(this, "OnStop");
    activeCallbacks.append("OnStop");
    qqBridge->registerCallback(this, "OnParam");
    activeCallbacks.append("OnParam");
    qqBridge->registerCallback(this, "OnQuote");
    activeCallbacks.append("OnQuote");
}

BridgeTCPServer::~BridgeTCPServer()
{
    while(!m_connections.isEmpty())
    {
        ConnectionData *cd = m_connections.takeLast();
        paramSubscriptions.clearAllSubscriptions(cd);
        delete cd;
    }
}

void BridgeTCPServer::setAllowedIPs(const QStringList &aips)
{
    m_allowedIps = aips;
}

void BridgeTCPServer::setLogPathPrefix(QString lpp)
{
    logPathPrefix = lpp;
}

void BridgeTCPServer::setDebugLogPathPrefix(QString lpp)
{
    if(!lpp.isEmpty())
    {
        QString logPath = lpp + ".log";
        QFile *tmpf=new QFile(logPath);
        if(tmpf->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            logf=tmpf;
            logts=new QTextStream(logf);
        }
        else
            delete tmpf;
    }
}

void BridgeTCPServer::callbackRequest(QString name, const QVariantList &args, QVariant &vres)
{
    if(!activeCallbacks.contains(name))
    {
        sendStderrLine(QString("Called callback %1 was not registered").arg(name));
        return;
    }
    QJsonObject cbCall
    {
        {"method", "callback"},
        {"name", name},
        {"arguments", QJsonArray::fromVariantList(args)}
    };
    ConnectionData *cd;
    foreach (cd, m_connections)
    {
        if(cd->callbackSubscriptions.contains(name))
        {
            int id = cd->callbackSubscriptions.value(name);
            // qDebug() << "Сall safeSendReq from BridgeTCPServer::callbackRequest";
            safeSendReq(cd, id, cbCall, false);
        }
    }
    if(name == "OnStop")
    {
        qApp->quit();
        vres = (int)100;
    }
    if(name == "OnParam")
    {
        QString cls = args[0].toString();
        QString sec = args[1].toString();
        QMetaObject::invokeMethod(this, "secParamsUpdate", Qt::QueuedConnection,
                                  Q_ARG(QString, cls),
                                  Q_ARG(QString, sec));
        /*
        SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
        if(s)
        {
            QMetaObject::invokeMethod(this, "secParamsUpdate", Qt::QueuedConnection,
                                      Q_ARG(QString, cls),
                                      Q_ARG(QString, sec));
        }
        */
    }
    if(name == "OnQuote")
    {
        QString cls = args[0].toString();
        QString sec = args[1].toString();
        QMetaObject::invokeMethod(this, "secQuotesUpdate", Qt::QueuedConnection,
                                  Q_ARG(QString, cls),
                                  Q_ARG(QString, sec));
        /*
        SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
        if(s)
        {
            QMetaObject::invokeMethod(this, "secQuotesUpdate", Qt::QueuedConnection,
                                      Q_ARG(QString, cls),
                                      Q_ARG(QString, sec));
        }
        */
    }
}

void BridgeTCPServer::fastCallbackRequest(void *data, const QVariantList &args, QVariant &res)
{
    FastCallbackFunctionData *fcfdata = reinterpret_cast<FastCallbackFunctionData *>(data);
    if(fcfdata->cd)
    {
        if(m_connections.contains(fcfdata->cd))
        {
            FastCallbackRequestEventLoop el(fcfdata->cd, fcfdata->objId, fcfdata->funName, this);
            res = el.sendAndWaitResult(this, args);
        }
    }
}

void BridgeTCPServer::clearFastCallbackData(void *data)
{
    FastCallbackFunctionData *fcfdata = reinterpret_cast<FastCallbackFunctionData *>(data);
    if(fcfdata)
        delete fcfdata;
}

void BridgeTCPServer::sendStdoutLine(QString line)
{
#ifdef QT_DEBUG
    qDebug() << line;
#endif
    if(logts)
    {
        *logts << Qt::endl << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "OUT: " << line;
        logts->flush();
    }
}

void BridgeTCPServer::sendStderrLine(QString line)
{
#ifdef QT_DEBUG
    qDebug() << line;
#endif
    if(logts)
    {
        *logts << Qt::endl << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "ERR: " << line;
        logts->flush();
    }
}

bool BridgeTCPServer::ipAllowed(QString ip)
{
    sendStdoutLine(QString("Checking ip: ") + ip);
    foreach(QString mask, m_allowedIps)
    {
        QString rexpstr = QString("%1").arg(mask);
        sendStdoutLine(QString("rexp: ") + rexpstr);
        QRegularExpression rexp(rexpstr);
        if(rexp.match(ip).hasMatch())
        {
            sendStdoutLine(QString("matched"));
            return true;
        }
    }
    sendStdoutLine(QString("IP disabled"));
    return false;
}

void BridgeTCPServer::cacheSecClasses()
{
    if(secClasses.isEmpty())
    {
        QVariantList args, res;
        qqBridge->invokeMethod("getClassesList", args, res, this);
        secClasses = res[0].toString().split(",", Qt::SkipEmptyParts);
    }
}

void BridgeTCPServer::safeSendReq(ConnectionData *cd, int id, QJsonValue data, bool showInLog)
{
    if(cd->threadId == QThread::currentThreadId())
    {
        // qDebug() << "Direct call sendReq from BridgeTCPServer::safeSendReq";
        cd->proto->sendReq(id, data, showInLog);
    }
    else
    {
        // qDebug() << "Queued call sendReq from BridgeTCPServer::safeSendReq";
        QMetaObject::invokeMethod(cd->proto, "sendReq", Qt::QueuedConnection,
                                  Q_ARG(int, id),
                                  Q_ARG(QJsonValue, data),
                                  Q_ARG(bool, showInLog));
    }
}

void BridgeTCPServer::safeSendAns(ConnectionData *cd, int id, QJsonValue data, bool showInLog)
{
    if(cd->threadId == QThread::currentThreadId())
    {
        // qDebug() << "Direct call sendAns from BridgeTCPServer::safeSendAns";
        cd->proto->sendAns(id, data, showInLog);
    }
    else
    {
        // qDebug() << "Queued call sendAns from BridgeTCPServer::safeSendAns";
        QMetaObject::invokeMethod(cd->proto, "sendAns", Qt::QueuedConnection,
                                  Q_ARG(int, id),
                                  Q_ARG(QJsonValue, data),
                                  Q_ARG(bool, showInLog));
    }
}

ConnectionData *BridgeTCPServer::getCDByProtoPtr(JsonProtocolHandler *p)
{
    int i;
    ConnectionData *cd;
    for(i=0; i<m_connections.count(); i++)
    {
        cd = m_connections.at(i);
        if(cd->proto == p)
            return cd;
    }
    return nullptr;
}

void BridgeTCPServer::sendError(ConnectionData *cd, int id, int errcode, QString errmsg, bool log)
{
    QJsonObject errObj
    {
        {"method", "error"},
        {"code", errcode},
        {"text", errmsg}
    };
    if(log)
        sendStderrLine(errmsg);
    if(cd)
    {
        if(cd->threadId == QThread::currentThreadId())
        {
            // qDebug() << "Direct call sendAns from BridgeTCPServer::sendError";
            cd->proto->sendAns(id, errObj, log);
        }
        else
        {
            // qDebug() << "Queued call sendAns from BridgeTCPServer::sendError";
            QMetaObject::invokeMethod(cd->proto, "sendAns", Qt::QueuedConnection,
                                      Q_ARG(int, id),
                                      Q_ARG(QJsonValue, errObj),
                                      Q_ARG(bool, log));
        }
    }
}

void BridgeTCPServer::processExtendedRequests(ConnectionData *cd, int id, QString method, QJsonObject &jobj)
{
    sendStdoutLine(QString("BridgeTCPServer::processExtendedRequests(\"%1\")").arg(method));
    method = method.toLower();
    if(method == "loadaccounts")
        processLoadAccountsRequest(cd, id, jobj);
    else if(method == "loadclasses")
        processLoadClassesRequest(cd, id, jobj);
    else if(method == "loadclasssecurities")
        processLoadClassSecuritiesRequest(cd, id, jobj);
    else if(method == "subscribeparamchanges")
        processSubscribeParamChangesRequest(cd, id, jobj);
    else if(method == "unsubscribeparamchanges")
        processUnsubscribeParamChangesRequest(cd, id, jobj);
    else if(method == "subscribequotes")
        processSubscribeQuotesRequest(cd, id, jobj);
    else if(method == "unsubscribequotes")
        processUnsubscribeQuotesRequest(cd, id, jobj);
}

void BridgeTCPServer::processLoadAccountsRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    sendStdoutLine(QString("BridgeTCPServer::processLoadAccountsRequest(%1)").arg(id));
    QJsonArray filters = jobj.value("filters").toArray();
    int j, fcnt = filters.count();
    QVariantList args, res;
    QVariantMap vmrow;
    QJsonObject row;
    QJsonArray table;
    args << "trade_accounts";
    qqBridge->invokeMethod("getNumberOf", args, res, this);
    int i, n = res[0].toInt();
    bool get;
    args << 0;
    for(i=0;i<n;i++)
    {
        args[1] = i;
        qqBridge->invokeMethod("getItem", args, res, this);
        vmrow = res[0].toMap();
        get = true;
        for(j = 0; j < fcnt; j++)
        {
            QJsonObject flt = filters.at(j).toObject();
            if(!flt.contains("key"))
                continue;
            QString key = flt.value("key").toString();
            if(!vmrow.contains(key))
            {
                get = false;
                break;
            }
            if(flt.contains("regexp"))
            {
                QRegularExpression rexp(flt.value("regexp").toString());
                QRegularExpressionMatch match = rexp.match(vmrow.value(key).toString());
                if(!match.hasMatch())
                {
                    get = false;
                    break;
                }
            }
        }
        if(get)
        {
            row = QJsonObject::fromVariantMap(vmrow);
            table.append(row);
        }
    }
    QJsonObject invRes
    {
        {"method", "return"},
        {"result", table}
    };
    cd->proto->sendAns(id, invRes, false);
}

void BridgeTCPServer::processLoadClassesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    sendStdoutLine(QString("BridgeTCPServer::processLoadClassesRequest(%1)").arg(id));
    QJsonArray clist;
    cacheSecClasses();
    clist = QJsonArray::fromStringList(secClasses);
    QJsonObject invRes
    {
        {"method", "return"},
        {"result", clist}
    };
    cd->proto->sendAns(id, invRes, false);
}

void BridgeTCPServer::processLoadClassSecuritiesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 6, "'class' must be specified in loadClassSecurities", true);
        return;
    }
    sendStdoutLine(QString("BridgeTCPServer::processLoadClassSecuritiesRequest(%1)").arg(id));
    QString cls = jobj.value("class").toString();
    cacheSecClasses();
    if(!secClasses.contains(cls, Qt::CaseInsensitive))
    {
        sendError(cd, id, 7, QString("Unknown securities class %1").arg(cls), true);
        return;
    }
    QJsonArray filters = jobj.value("filters").toArray();
    int j, fcnt = filters.count();
    QVariantList args, res;
    QVariantMap vmrow;
    QJsonObject row;
    QJsonArray table;
    args << cls;
    qqBridge->invokeMethod("getClassSecurities", args, res, this);
    QStringList allSecs = res[0].toString().split(",", Qt::SkipEmptyParts);
    int i, n = allSecs.count();
    bool get;
    args << QString();
    for(i=0;i<n;i++)
    {
        args[1] = allSecs[i];
        qqBridge->invokeMethod("getSecurityInfo", args, res, this);
        vmrow = res[0].toMap();
        get = true;
        for(j = 0; j < fcnt; j++)
        {
            QJsonObject flt = filters.at(j).toObject();
            if(!flt.contains("key"))
                continue;
            QString key = flt.value("key").toString();
            if(!vmrow.contains(key))
            {
                get = false;
                break;
            }
            if(flt.contains("regexp"))
            {
                QRegularExpression rexp(flt.value("regexp").toString());
                QRegularExpressionMatch match = rexp.match(vmrow.value(key).toString());
                if(!match.hasMatch())
                {
                    get = false;
                    break;
                }
            }
        }
        if(get)
        {
            row = QJsonObject::fromVariantMap(vmrow);
            table.append(row);
        }
    }
    QJsonObject invRes
    {
        {"method", "return"},
        {"result", table}
    };
    cd->proto->sendAns(id, invRes, false);
}

void BridgeTCPServer::processSubscribeParamChangesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 8, "'class' must be specified in subscribeParamChanges", true);
        return;
    }
    sendStdoutLine(QString("BridgeTCPServer::processSubscribeParamChangesRequest(%1)").arg(id));
    QString cls = jobj.value("class").toString();
    cacheSecClasses();
    if(!secClasses.contains(cls, Qt::CaseInsensitive))
    {
        sendError(cd, id, 7, QString("Unknown securities class %1").arg(cls), true);
        return;
    }
    if(!jobj.contains("security"))
    {
        sendError(cd, id, 9, "'security' must be specified in subscribeParamChanges", true);
        return;
    }
    QString sec = jobj.value("security").toString();
    if(!jobj.contains("param"))
    {
        sendError(cd, id, 10, "'param' must be specified in subscribeParamChanges", true);
        return;
    }
    QString par = jobj.value("param").toString();
    ParamSubs *p=paramSubscriptions.findParamSubscriptions(cls, sec, par);
    if(p)
    {
        if(p->hasConsumer(cd)) //p->consumers.contains(cd))
        {
            sendError(cd, id, 11, QString("You already subscribed %1/%2/%3").arg(cls, sec, par), true);
            return;
        }
    }
    else
    {
        QVariantList args, res;
        args << cls << sec << par;
        qqBridge->invokeMethod("ParamRequest", args, res, this);
        if(res[0].toBool())
            sendStderrLine("ParamRequest succesfully finished");
        else
            sendStderrLine("ParamRequest failed");
    }
    sendStderrLine("We are ready to add subscription");
    paramSubscriptions.addConsumer(cd, cls, sec, par, id);
    QJsonObject subsRes
    {
        {"method", "return"},
        {"result", true}
    };
    cd->proto->sendAns(id, subsRes, false);
    secParamsUpdate(cls, sec);
}

void BridgeTCPServer::processUnsubscribeParamChangesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 12, "'class' must be specified in unsubscribeParamChanges", true);
        return;
    }
    sendStdoutLine(QString("BridgeTCPServer::processUnsubscribeParamChangesRequest(%1)").arg(id));
    QString cls = jobj.value("class").toString();
    cacheSecClasses();
    if(!secClasses.contains(cls, Qt::CaseInsensitive))
    {
        sendError(cd, id, 7, QString("Unknown securities class %1").arg(cls), true);
        return;
    }
    if(!jobj.contains("security"))
    {
        sendError(cd, id, 13, "'security' must be specified in unsubscribeParamChanges", true);
        return;
    }
    QString sec = jobj.value("security").toString();
    if(!jobj.contains("param"))
    {
        sendError(cd, id, 14, "'param' must be specified in unsubscribeParamChanges", true);
        return;
    }
    QString par = jobj.value("param").toString();
    sendStdoutLine(QString("Try delete subscription to %1/%2/%3").arg(cls, sec, par));
    paramSubscriptions.delConsumer(cd, cls, sec, par);
    if(paramSubscriptions.findParamSubscriptions(cls, sec, par))
        sendStdoutLine(QString("There are some consumers subscribed to %1/%2/%3. Param left in DB").arg(cls, sec, par));
    else
    {
        QVariantList args, res;
        args << cls << sec << par;
        qqBridge->invokeMethod("CancelParamRequest", args, res, this);
        if(res[0].toBool())
            sendStdoutLine("CancelParamRequest returned true");
        else
            sendStderrLine("CancelParamRequest returned false");
    }
    QJsonObject usubsRes
    {
        {"method", "return"},
        {"result", true}
    };
    cd->proto->sendAns(id, usubsRes, false);
}

void BridgeTCPServer::processExtendedAnswers(ConnectionData *cd, int id, QString method, QJsonObject &jobj)
{

}

void BridgeTCPServer::processSubscribeQuotesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 15, "'class' must be specified in subscribeQuotes", true);
        return;
    }
    sendStdoutLine(QString("BridgeTCPServer::processSubscribeQuotesRequest(%1)").arg(id));
    QString cls = jobj.value("class").toString();
    cacheSecClasses();
    if(!secClasses.contains(cls, Qt::CaseInsensitive))
    {
        sendError(cd, id, 16, QString("Unknown securities class %1").arg(cls), true);
        return;
    }
    if(!jobj.contains("security"))
    {
        sendError(cd, id, 17, "'security' must be specified in subscribeQuotes", true);
        return;
    }
    QString sec = jobj.value("security").toString();
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(s && s->hasQuotesConsumer(cd)) //s->quoteConsumers.contains(cd))
    {
        sendError(cd, id, 18, QString("You already subscriped %1/%2 quotes").arg(cls, sec), true);
        return;
    }
    else
    {
        QVariantList args, res;
        args << cls << sec;
        qqBridge->invokeMethod("Subscribe_Level_II_Quotes", args, res, this);
        if(!res[0].toBool())
            sendStderrLine("Subscribe_Level_II_Quotes returned false");
    }
    paramSubscriptions.addQuotesConsumer(cd, cls, sec, id);
    QJsonObject subsRes
    {
        {"method", "return"},
        {"result", true}
    };
    cd->proto->sendAns(id, subsRes, false);
    secQuotesUpdate(cls, sec);
}

void BridgeTCPServer::processUnsubscribeQuotesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    sendStdoutLine(QString("BridgeTCPServer::processUnsubscribeQuotesRequest(%1)").arg(id));
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 19, "'class' must be specified in unsubscribeQuotes", true);
        return;
    }
    QString cls = jobj.value("class").toString();
    cacheSecClasses();
    if(!secClasses.contains(cls, Qt::CaseInsensitive))
    {
        sendError(cd, id, 20, QString("Unknown securities class %1").arg(cls), true);
        return;
    }
    if(!jobj.contains("security"))
    {
        sendError(cd, id, 21, "'security' must be specified in unsubscribeQuotes", true);
        return;
    }
    QString sec = jobj.value("security").toString();
    paramSubscriptions.delQuotesConsumer(cd, cls, sec);
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(!s || !s->hasQuotesConsumer()) //s->quoteConsumers.isEmpty())
    {
        QVariantList args, res;
        args << cls << sec;
        qqBridge->invokeMethod("Unsubscribe_Level_II_Quotes", args, res, this);
        if(!res[0].toBool())
            sendStderrLine("Unsubscribe_Level_II_Quotes returned false");
    }
    QJsonObject usubsRes
    {
        {"method", "return"},
        {"result", true}
    };
    cd->proto->sendAns(id, usubsRes, false);
}

void BridgeTCPServer::incomingConnection(qintptr handle)
{
    Qt::HANDLE thh = QThread::currentThreadId();
    QString hstr = QString("0x%1").arg((quintptr)thh, QT_POINTER_SIZE * 2, 16, QChar('0'));
    sendStdoutLine(QString("incomingConnection thread: %1").arg(hstr));
    QTcpSocket * sock = new QTcpSocket();
    sock->setSocketDescriptor(handle);
    sock->setSocketOption(QAbstractSocket::LowDelayOption, true);
    bool allowed = false;
    if(ipAllowed(sock->peerAddress().toString()))
        allowed = true;
    if(!allowed)
    {
        QString msg = QString("Connection from %1 refused").arg(sock->peerAddress().toString());
        sock->close();
        sock->deleteLater();
        sendStderrLine(msg);
        return;
    }
    QString logPath;
    if(!logPathPrefix.isEmpty())
    {
        logPath = logPathPrefix+sock->peerAddress().toString()+".log";
    }
    ConnectionData *cd = new ConnectionData();
    cd->srv = this;
    cd->threadId = thh;
    cd->peerIp = sock->peerAddress().toString();
    cd->proto = new JsonProtocolHandler(sock, logPath, this);
    connect(cd->proto, SIGNAL(reqArrived(int,QJsonValue)), this, SLOT(protoReqArrived(int,QJsonValue)));
    connect(cd->proto, SIGNAL(ansArrived(int,QJsonValue)), this, SLOT(protoAnsArrived(int,QJsonValue)));
    connect(cd->proto, SIGNAL(verArrived(int)), this, SLOT(protoVerArrived(int)));
    connect(cd->proto, SIGNAL(endArrived()), this, SLOT(protoEndArrived()));
    connect(cd->proto, SIGNAL(finished()), this, SLOT(protoFinished()));
    connect(cd->proto, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(protoError(QAbstractSocket::SocketError)));
    //connect(cd->proto, SIGNAL(debugLog(QString)), this, SLOT(debugLog(QString)));

    QString msg = QString("New connection from %1 established").arg(cd->peerIp);
    sendStdoutLine(msg);

    QMetaObject::invokeMethod(this, "connectionEstablished", Qt::QueuedConnection,
                              Q_ARG(ConnectionData*, cd));
}

void BridgeTCPServer::connectionEstablished(ConnectionData *cd)
{
    m_connections.append(cd);
    cd->proto->sendVer(BRIDGE_SERVER_PROTOCOL_VERSION);
    cd->versionSent = true;
}

void BridgeTCPServer::protoReqArrived(int id, QJsonValue data)
{
    Qt::HANDLE thh = QThread::currentThreadId();
    QString hstr = QString("0x%1").arg((quintptr)thh, QT_POINTER_SIZE * 2, 16, QChar('0'));
    sendStdoutLine(QString("protoReqArrived thread: %1").arg(hstr));
    ConnectionData *cd = getCDByProtoPtr(qobject_cast<JsonProtocolHandler *>(sender()));
    if(!cd)
        return;
    QJsonObject defaultObj
    {
        {"method", "nop"}
    };
    QJsonObject reqObj = data.toObject(defaultObj);
    if(!reqObj.contains("method"))
    {
        sendError(cd, id, 0, "Wrong format", true);
        return;
    }
    QString method = reqObj.value("method").toString("nop").toLower();
    if(method == "nop")
    {
        sendError(cd, id, 0, "Wrong request method", true);
        return;
    }
    if(method == "register")
    {
        if(!reqObj.contains("callback"))
        {
            sendError(cd, id, 1, "Method 'register' must have 'callback' argument", true);
            return;
        }
        QString callbackName = reqObj.value("callback").toString("__unknown__");
        if(callbackName == "__unknown__")
        {
            sendError(cd, id, 2, "Unknown callback name", true);
            return;
        }
        if(cd->callbackSubscriptions.contains(callbackName))
        {
            sendError(cd, id, 3, QString("Callback %1 already registered").arg(callbackName), true);
            return;
        }
        if(!activeCallbacks.contains(callbackName))
        {
            qqBridge->registerCallback(this, callbackName);
            activeCallbacks.append(callbackName);
        }
        cd->callbackSubscriptions.insert(callbackName, id);
        QJsonObject regRes
        {
            {"method", "registered"},
            {"callback", callbackName}
        };
        // qDebug() << "Сall safeSendAns from BridgeTCPServer::protoReqArrived 1";
        safeSendAns(cd, id, regRes, false);
        return;
    }
    if(method == "invoke")
    {
        int objId=-1;
        if(reqObj.contains("object"))
            objId = reqObj.value("object").toInt(-1);
        QString funName = "__unknown__";
        if(reqObj.contains("function"))
                funName = reqObj.value("function").toString("__unknown__");
        if(funName == "__unknown__")
        {
            sendError(cd, id, 4, "Unknown function name", true);
            return;
        }
        QVariantList oargs, args;
        if(reqObj.contains("arguments"))
        {
            oargs = reqObj.value("arguments").toArray().toVariantList();
            int k;
            for(k=0; k<oargs.count(); k++)
            {
                QVariant carg = oargs[k];
                bool isCallable = false;
                if(carg.type() == QVariant::Map)
                {
                    QVariantMap pcabl = carg.toMap();
                    if(pcabl.contains("type") && pcabl.value("type").toString()=="callable")
                    {
                        if(pcabl.contains("function"))
                        {
                            QString fname = pcabl.value("function").toString();
                            if(!fname.isEmpty())
                            {
                                isCallable = true;
                                BridgeCallableObject cobj;
                                FastCallbackFunctionData *fcfdata = new FastCallbackFunctionData();
                                fcfdata->cd = cd;
                                fcfdata->objId = objId;
                                fcfdata->funName = fname;
                                cobj.data = reinterpret_cast<void *>(fcfdata);
                                cobj.handler = this;
                                args.append(QVariant::fromValue(cobj));
                            }
                        }
                    }
                }
                if(!isCallable)
                {
                    args.append(carg);
                }
            }
        }
        QVariantList res;
        if(objId > 0)
            qqBridge->invokeObjectMethod(objId, funName, args, res, this);
        else
            qqBridge->invokeMethod(funName, args, res, this);
        int k;
        for(k=0;k<res.count();k++)
        {
            QVariant val = res[k];
            if(val.canConvert<QuikCallableObject>())
            {
                QuikCallableObject qco = val.value<QuikCallableObject>();
                res[k] = QVariant(qco.objid);
                cd->objRefs.append(qco.objid);
            }
        }
        QJsonObject invRes
        {
            {"method", "return"},
            {"result", QJsonArray::fromVariantList(res)}
        };
        // qDebug() << "Сall safeSendAns from BridgeTCPServer::protoReqArrived 2";
        safeSendAns(cd, id, invRes, false);
        return;
    }
    if(method == "delete")
    {
        int objId=-1;
        if(reqObj.contains("object"))
            objId = reqObj.value("object").toInt(-1);
        if(objId > 0)
        {
            qqBridge->deleteObject(objId);
            QJsonObject delRes
            {
                {"method", "deleted"},
                {"object", objId}
            };
            cd->objRefs.removeAll(objId);
            // qDebug() << "Сall safeSendAns from BridgeTCPServer::protoReqArrived 3";
            safeSendAns(cd, id, delRes, false);
            return;
        }
        else
        {
            sendError(cd, id, 5, QString("Object %1 is unknown").arg(objId), true);
            return;
        }
    }
    processExtendedRequests(cd, id, method, reqObj);
}

void BridgeTCPServer::protoAnsArrived(int id, QJsonValue data)
{
    Qt::HANDLE thh = QThread::currentThreadId();
    QString hstr = QString("0x%1").arg((quintptr)thh, QT_POINTER_SIZE * 2, 16, QChar('0'));
    sendStdoutLine(QString("protoAnsArrived thread: %1").arg(hstr));
    ConnectionData *cd = getCDByProtoPtr(qobject_cast<JsonProtocolHandler *>(sender()));
    if(!cd)
        return;
    QJsonObject defaultObj
    {
        {"method", "nop"}
    };
    QJsonObject reqObj = data.toObject(defaultObj);
    if(!reqObj.contains("method"))
    {
        sendError(cd, id, 0, "Wrong format", true);
        return;
    }
    QString method = reqObj.value("method").toString("nop").toLower();
    if(method == "nop")
    {
        sendError(cd, id, 0, "Wrong request method", true);
        return;
    }
    if(method == "return")
    {
        QVariant res = reqObj.value("result").toVariant();
        if(cd->fcbWaitResult)
            cd->fcbWaitResult->fastCallbackReturnArrived(cd, id, res);
        return;
    }
    processExtendedAnswers(cd, id, method, reqObj);
}

void BridgeTCPServer::protoVerArrived(int ver)
{
    if(ver<1)
        return;
    ConnectionData *cd = getCDByProtoPtr(qobject_cast<JsonProtocolHandler *>(sender()));
    if(cd)
    {
        cd->peerProtocolVersion = ver;
        if(!cd->versionSent)
        {
            if(cd->threadId == QThread::currentThreadId())
            {
                // qDebug() << "Direct call sendVer from BridgeTCPServer::protoVerArrived";
                cd->proto->sendVer(BRIDGE_SERVER_PROTOCOL_VERSION);
            }
            else
            {
                // qDebug() << "Queued call sendVer from BridgeTCPServer::protoVerArrived";
                QMetaObject::invokeMethod(cd->proto, "sendVer", Qt::QueuedConnection,
                                          Q_ARG(int, BRIDGE_SERVER_PROTOCOL_VERSION));
            }
            cd->versionSent = true;
        }
    }
}

void BridgeTCPServer::protoEndArrived()
{
    ConnectionData *cd = getCDByProtoPtr(qobject_cast<JsonProtocolHandler *>(sender()));
    if(cd)
    {
        QString msg = QString("Close connection request from %1").arg(cd->peerIp);
        sendStdoutLine(msg);
    }
}

void BridgeTCPServer::protoFinished()
{
    ConnectionData *cd = getCDByProtoPtr(qobject_cast<JsonProtocolHandler *>(sender()));
    if(cd)
    {
        QString msg = QString("Connection %1 closed").arg(cd->peerIp);
        sendStdoutLine(msg);
        m_connections.removeAll(cd);
        paramSubscriptions.clearAllSubscriptions(cd);
        delete cd;
    }
}

void BridgeTCPServer::protoError(QAbstractSocket::SocketError err)
{
    ConnectionData *cd = getCDByProtoPtr(qobject_cast<JsonProtocolHandler *>(sender()));
    if(cd)
    {
        sendStderrLine(QString("Socket error %1: ").arg((int)err)+cd->proto->lastErrorString());
        m_connections.removeAll(cd);
        paramSubscriptions.clearAllSubscriptions(cd);
        delete cd;
    }
}

void BridgeTCPServer::serverError(QAbstractSocket::SocketError err)
{
    sendStderrLine(QString("Server accepting error: ") + errorString());
}

void BridgeTCPServer::fastCallbackRequestHandler(ConnectionData *cd, int oid, QString fname, QVariantList args)
{
    if(m_connections.contains(cd))
    {
        QJsonObject invReq
        {
            {"method", "invoke"},
            {"function", fname},
            {"arguments", QJsonArray::fromVariantList(args)}
        };
        if(oid > 0)
            invReq["object"] = oid;
        int id = ++(cd->outMsgId);
        cd->fcbWaitResult->fastCallbackRequestSent(cd, oid, fname, id);
        // qDebug() << "Сall safeSendReq from BridgeTCPServer::fastCallbackRequestHandler";
        safeSendReq(cd, id, invReq, false);
    }
}

void BridgeTCPServer::secParamsUpdate(QString cls, QString sec)
{
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(s)
    {
        sendStdoutLine(QString("BridgeTCPServer::secParamsUpdate(%1, %2) -> subscription found").arg(cls, sec));
        QStringList allParams = s->getParamsList(); //s->params.keys();
        QString par;
        ParamSubs *p;
        int i,j;
        for(i=0; i<allParams.count();i++)
        {
            par = allParams[i];
            sendStdoutLine(QString("BridgeTCPServer::secParamsUpdate(%1, %2) -> check param %3").arg(cls, sec, par));
            QVariantList args, res;
            args << cls << sec << par;
            qqBridge->invokeMethod("getParamEx2", args, res, this);
            QVariantMap mres = res[0].toMap();
            QVariant pval = mres["param_value"];
            sendStdoutLine(QString("Search subscription for %1").arg(par));
            p = s->findParamSubscriptions(par);
            if(!p)
            {
                sendStdoutLine(QString("Parameter %1 subscription was canceled").arg(par));
                continue;
            }
            if(pval == p->value)
            {
                sendStdoutLine(QString("Value of %1 wasn't changed").arg(par));
                continue;
            }
            sendStdoutLine(QString("Value of %1 was changed. Send it to consumers").arg(par));
            p->value = pval;
            QJsonObject subsAns
            {
                {"method", "paramChange"},
                {"class", cls},
                {"security", sec},
                {"param", par},
                {"value", QJsonValue::fromVariant(pval)}
            };
            QList<ConnectionData *> consList = p->consumersList(); //p->consumers.keys();
            for(j=0; j<consList.count(); j++)
            {
                ConnectionData *cd = consList.at(j);
                int id = p->getSubscriptionId(cd); //p->consumers.value(cd);
                if(id >= 0)
                    cd->proto->sendReq(id, subsAns, false);
            }
        }
    }
    //else
    //    sendStderrLine(QString("BridgeTCPServer::secParamsUpdate(%1, %2) -> subscription not found").arg(cls, sec));
}

void BridgeTCPServer::secQuotesUpdate(QString cls, QString sec)
{
    //bool needStop = true;
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(s)
    {
        sendStdoutLine(QString("BridgeTCPServer::secQuotesUpdate(%1, %2)").arg(cls, sec));
        if(s->hasQuotesConsumer()) //!s->quoteConsumers.isEmpty())
        {
            //needStop = false;
            QVariantList args, res;
            args << cls << sec;
            qqBridge->invokeMethod("getQuoteLevel2", args, res, this);
            QVariantMap mres = res[0].toMap();
            QJsonObject subsQAns
            {
                {"method", "quotesChange"},
                {"class", cls},
                {"security", sec},
                {"quotes", QJsonValue::fromVariant(mres)}
            };
            int i;
            QList<ConnectionData *> consList = s->getQuotesConsumersList(); //s->quoteConsumers.keys();
            for(i=0; i<consList.count(); i++)
            {
                ConnectionData *cd = consList.at(i);
                int id = s->getQuotesSubscriptionId(cd); // s->quoteConsumers.value(cd);
                cd->proto->sendReq(id, subsQAns, false);
            }
        }
        else
        {
            sendStderrLine("Quote consumer not found");
        }
    }
    /*
    if(needStop)
    {
        QVariantList args, res;
        args << cls << sec;
        qqBridge->invokeMethod("Unsubscribe_Level_II_Quotes", args, res, this);
        if(!res[0].toBool())
            sendStderrLine("Unsubscribe_Level_II_Quotes returned false");
    }
    */
}

FastCallbackRequestEventLoop::FastCallbackRequestEventLoop(ConnectionData *rcd, int oid, QString rfname, BridgeTCPServer *s)
    : cd(rcd), funName(rfname), objId(oid), waitMux(nullptr), srv(s)
{
}

QVariant FastCallbackRequestEventLoop::sendAndWaitResult(BridgeTCPServer *server, const QVariantList &args)
{
    QMutex waitMutex;
    waitMux = &waitMutex;
    cd->fcbWaitResult = this;
    sendStdoutLine(QString("sendAndWaitResult: invoke fastCallbackRequestHandler..."));
    QMetaObject::invokeMethod(server, "fastCallbackRequestHandler", Qt::QueuedConnection,
                              Q_ARG(ConnectionData*, cd),
                              Q_ARG(int, objId),
                              Q_ARG(QString, funName),
                              Q_ARG(QVariantList, args));
    sendStdoutLine(QString("sendAndWaitResult: wait result..."));
    waitMutex.lock();
    waitMutex.tryLock(FASTCALLBACK_TIMEOUT_SEC * 1000);
    sendStdoutLine(QString("sendAndWaitResult: Event loop finished"));
    waitMux = nullptr;
    waitMutex.unlock();
    if(cd)
        cd->fcbWaitResult = nullptr;
    return result;
}

void FastCallbackRequestEventLoop::fastCallbackRequestSent(ConnectionData *acd, int oid, QString afname, int aid)
{
    if(cd==acd && funName==afname && objId==oid)
    {
        sendStdoutLine(QString("Fast callback request sent"));
        id = aid;
    }
}

void FastCallbackRequestEventLoop::fastCallbackReturnArrived(ConnectionData *acd, int aid, QVariant res)
{
    if(cd==acd && id==aid)
    {
        result = res;
        sendStdoutLine(QString("Wake fast callback waiter"));
        if(waitMux)
            waitMux->unlock();
        else
        {
            sendStderrLine(QString("Fast callback return arrived without locking?"));
        }
    }
}

void FastCallbackRequestEventLoop::connectionDataDeleted(ConnectionData *dcd)
{
    if(cd == dcd)
    {
        cd = nullptr;
        if(waitMux)
        {
            waitMux->unlock();
            sendStdoutLine(QString("Wake fast callback waiter(connectionDataDeleted)"));
        }
        else
        {
            sendStderrLine(QString("Connection data deleted unexpected call"));
        }
    }
}

ConnectionData::~ConnectionData()
{
    while(!objRefs.isEmpty())
    {
        int objid = objRefs.takeLast();
        qqBridge->deleteObject(objid);
        sendStdoutLine(QString("Object %1 deleted").arg(objid));
    }
    if(fcbWaitResult)
        fcbWaitResult->connectionDataDeleted(this);
    if(proto)
        delete proto;
}

ParamSubscriptionsDb::ParamSubscriptionsDb()
{

}

ParamSubscriptionsDb::~ParamSubscriptionsDb()
{
    //Здесь нельзя использовать локер использующий стек,
    //потому-что он удаляется вместе с мьютексом на выходе из деструктора
    // sendStdoutLine("ParamSubscriptionsDb::~ParamSubscriptionsDb lock");
    mutex.lock();
    QStringList keys = classes.keys();
    foreach (QString cls, keys)
    {
        delete classes.take(cls);
    }
    mutex.unlock();
    // sendStdoutLine("ParamSubscriptionsDb::~ParamSubscriptionsDb unlocked");
}

void ParamSubscriptionsDb::addConsumer(ConnectionData *cd, QString cls, QString sec, QString param, int id)
{
    sendStdoutLine(QString("ParamSubscriptionsDb::addConsumer(%1, %2, %3, %4)").arg(cls, sec, param).arg(id));
    ClsSubs *c;
    mutex.lock();
    {
        if(classes.contains(cls))
        {
            sendStdoutLine("ParamSubscriptionsDb::addConsumer: class already exists");
            c = classes.value(cls);
        }
        else
        {
            sendStdoutLine("ParamSubscriptionsDb::addConsumer: create new class");
            c = new ClsSubs(cls);
            classes.insert(cls, c);
        }
    }
    mutex.unlock();
    // sendStdoutLine(QString("ParamSubscriptionsDb::addConsumer(%1, %2, %3, %4): addConsumer to class").arg(cls, sec, param).arg(id));
    c->addConsumer(cd, sec, param, id);
}

bool ParamSubscriptionsDb::delConsumer(ConnectionData *cd, QString cls, QString sec, QString param)
{
    sendStdoutLine(QString("ParamSubscriptionsDb::delConsumer(%1, %2, %3)").arg(cls, sec, param));
    mutex.lock();
    if(classes.contains(cls))
    {
        sendStdoutLine(QString("ParamSubscriptionsDb::delConsumer found class %1 in subscriptions").arg(cls));
        if(classes.value(cls)->delConsumer(cd, sec, param))
            delete classes.take(cls);
    }
    bool res = classes.isEmpty();
    mutex.unlock();
    // sendStdoutLine(QString("ParamSubscriptionsDb::delConsumer(%1, %2, %3) unlocked").arg(cls, sec, param));
    return res;
}

bool ParamSubscriptionsDb::clearAllSubscriptions(ConnectionData *cd)
{
    // sendStdoutLine(QString("ParamSubscriptionsDb::clearAllSubscriptions() lock"));
    mutex.lock();
    QStringList toDel = classes.keys();
    foreach (QString cname, toDel)
    {
        ClsSubs *c = classes[cname];
        if(c->clearAllSubscriptions(cd))
        {
            classes.remove(cname);
            delete c;
        }
    }
    bool res = classes.isEmpty();
    mutex.unlock();
    // sendStdoutLine(QString("ParamSubscriptionsDb::clearAllSubscriptions() unlocked"));
    return res;
}

ParamSubs *ParamSubscriptionsDb::findParamSubscriptions(QString cls, QString sec, QString param)
{
    ParamSubs *res = nullptr;
    // sendStdoutLine("ParamSubscriptionsDb::findParamSubscriptions lock");
    mutex.lock();
    if(classes.contains(cls))
        res = classes.value(cls)->findParamSubscriptions(sec, param);
    mutex.unlock();
    // sendStdoutLine("ParamSubscriptionsDb::findParamSubscriptions unlocked");
    return res;
}

SecSubs *ParamSubscriptionsDb::findSecuritySubscriptions(QString cls, QString sec)
{
    SecSubs *res = nullptr;
    // sendStdoutLine("ParamSubscriptionsDb::findSecuritySubscriptions lock");
    mutex.lock();
    if(classes.contains(cls))
        res = classes.value(cls)->findSecuritySubscriptions(sec);
    mutex.unlock();
    // sendStdoutLine("ParamSubscriptionsDb::findSecuritySubscriptions unlocked");
    return res;
}

void ParamSubscriptionsDb::addQuotesConsumer(ConnectionData *cd, QString cls, QString sec, int id)
{
    sendStdoutLine(QString("ParamSubscriptionsDb::addQuotesConsumer(%1, %2, %3)").arg(cls, sec).arg(id));
    ClsSubs *c;
    mutex.lock();
    {
        if(classes.contains(cls))
        {
            sendStdoutLine("ParamSubscriptionsDb::addQuotesConsumer: class already exists");
            c = classes.value(cls);
        }
        else
        {
            sendStdoutLine("ParamSubscriptionsDb::addQuotesConsumer: create new class");
            c = new ClsSubs(cls);
            classes.insert(cls, c);
        }
    }
    mutex.unlock();
    // sendStdoutLine("ParamSubscriptionsDb::addQuotesConsumer unlocked");
    c->addQuotesConsumer(cd, sec, id);
}

bool ParamSubscriptionsDb::delQuotesConsumer(ConnectionData *cd, QString cls, QString sec)
{
    // sendStdoutLine("ParamSubscriptionsDb::delQuotesConsumer lock");
    mutex.lock();
    if(classes.contains(cls))
    {
        if(classes.value(cls)->delQuotesConsumer(cd, sec))
            delete classes.take(cls);
    }
    bool res = classes.isEmpty();
    mutex.unlock();
    // sendStdoutLine("ParamSubscriptionsDb::delQuotesConsumer unlocked");
    return res;
}

ClsSubs::~ClsSubs()
{
    //Здесь нельзя использовать локер использующий стек,
    //потому-что он удаляется вместе с мьютексом на выходе из деструктора
    // sendStdoutLine("ClsSubs::~ClsSubs lock");
    mutex.lock();
    QStringList keys = securities.keys();
    foreach (QString sec, keys)
    {
        delete securities.take(sec);
    }
    mutex.unlock();
    // sendStdoutLine("ClsSubs::~ClsSubs unlocked");
}

void ClsSubs::addConsumer(ConnectionData *cd, QString sec, QString param, int id)
{
    sendStdoutLine(QString("ClsSubs#%1::addConsumer(%2, %3, %4)").arg(this->className, sec, param).arg(id));
    SecSubs *s;
    mutex.lock();
    {
        if(securities.contains(sec))
        {
            sendStdoutLine("ParamSubscriptionsDb::addConsumer: security already exists");
            s = securities.value(sec);
        }
        else
        {
            sendStdoutLine("ParamSubscriptionsDb::addConsumer: create new security");
            s = new SecSubs(sec);
            securities.insert(sec, s);
        }
    }
    mutex.unlock();
    // sendStdoutLine("ClsSubs::addConsumer unlocked");
    s->addConsumer(cd, param, id);
}

bool ClsSubs::delConsumer(ConnectionData *cd, QString sec, QString param)
{
    // sendStdoutLine("ClsSubs::delConsumer lock");
    mutex.lock();
    if(securities.contains(sec))
    {
        sendStdoutLine(QString("ClsSubs::delConsumer found security %1 in subscriptions").arg(sec));
        if(securities.value(sec)->delConsumer(cd, param))
            delete securities.take(sec);
    }
    bool res = securities.isEmpty();
    mutex.unlock();
    // sendStdoutLine("ClsSubs::delConsumer unlocked");
    return res;
}

bool ClsSubs::clearAllSubscriptions(ConnectionData *cd)
{
    // sendStdoutLine("ClsSubs::clearAllSubscriptions lock");
    mutex.lock();
    QStringList toDel = securities.keys();
    foreach (QString sname, toDel)
    {
        SecSubs *s = securities[sname];
        if(s->clearAllSubscriptions(cd))
        {
            securities.remove(sname);
            delete s;
        }
    }
    bool res = securities.isEmpty();
    mutex.unlock();
    // sendStdoutLine("ClsSubs::clearAllSubscriptions unlocked");
    return res;
}

ParamSubs *ClsSubs::findParamSubscriptions(QString sec, QString param)
{
    ParamSubs *res = nullptr;
    // sendStdoutLine("ClsSubs::findParamSubscriptions lock");
    mutex.lock();
    if(securities.contains(sec))
        res = securities.value(sec)->findParamSubscriptions(param);
    mutex.unlock();
    // sendStdoutLine("ClsSubs::findParamSubscriptions unlocked");
    return res;
}

SecSubs *ClsSubs::findSecuritySubscriptions(QString sec)
{
    SecSubs *res = nullptr;
    // sendStdoutLine("ClsSubs::findSecuritySubscriptions lock");
    mutex.lock();
    if(securities.contains(sec))
        res = securities.value(sec);
    mutex.unlock();
    // sendStdoutLine("ClsSubs::findSecuritySubscriptions unlocked");
    return res;
}

void ClsSubs::addQuotesConsumer(ConnectionData *cd, QString sec, int id)
{
    sendStdoutLine(QString("ClsSubs#%1::addQuotesConsumer(%2, %3)").arg(this->className, sec).arg(id));
    SecSubs *s;
    mutex.lock();
    {
        if(securities.contains(sec))
        {
            sendStdoutLine("ClsSubs::addQuotesConsumer: security already exists");
            s = securities.value(sec);
        }
        else
        {
            sendStdoutLine("ClsSubs::addQuotesConsumer: create new security");
            s = new SecSubs(sec);
            securities.insert(sec, s);
        }
    }
    mutex.unlock();
    // sendStdoutLine("ClsSubs::addQuotesConsumer unlocked");
    s->addQuotesConsumer(cd, id);
}

bool ClsSubs::delQuotesConsumer(ConnectionData *cd, QString sec)
{
    // sendStdoutLine("ClsSubs::delQuotesConsumer lock");
    mutex.lock();
    if(securities.contains(sec))
    {
        if(securities.value(sec)->delQuotesConsumer(cd))
            delete securities.take(sec);
    }
    bool res = securities.isEmpty();
    mutex.unlock();
    // sendStdoutLine("ClsSubs::delQuotesConsumer unlocked");
    return res;
}

SecSubs::~SecSubs()
{
    //Здесь нельзя использовать локер использующий стек,
    //потому-что он удаляется вместе с мьютексом на выходе из деструктора
    // sendStdoutLine("SecSubs::~SecSubs lock");
    pmutex.lock();
    QStringList keys = params.keys();
    foreach (QString par, keys)
    {
        delete params.take(par);
    }
    pmutex.unlock();
    // sendStdoutLine("SecSubs::~SecSubs unlocked");
}

void SecSubs::addConsumer(ConnectionData *cd, QString param, int id)
{
    sendStdoutLine(QString("SecSubs#%1::addConsumer(%2, %3)").arg(this->secName, param).arg(id));
    ParamSubs *p;
    pmutex.lock();
    {
        if(params.contains(param))
        {
            sendStdoutLine("ParamSubscriptionsDb::addConsumer: param already exists");
            p = params.value(param);
        }
        else
        {
            sendStdoutLine("ParamSubscriptionsDb::addConsumer: create new param");
            p = new ParamSubs(param);
            params.insert(param, p);
        }
    }
    pmutex.unlock();
    // sendStdoutLine("SecSubs::addConsumer unlocked");
    p->addConsumer(cd, id);
}

bool SecSubs::delConsumer(ConnectionData *cd, QString param)
{
    // sendStdoutLine("SecSubs::delConsumer lock");
    pmutex.lock();
    if(params.contains(param))
    {
        sendStdoutLine(QString("SecSubs::delConsumer found param %1 in subscriptions").arg(param));
        if(params.value(param)->delConsumer(cd))
            delete params.take(param);
    }
    qmutex.lock();
    bool res = (params.isEmpty() && quoteConsumers.isEmpty());
    qmutex.unlock();
    pmutex.unlock();
    // sendStdoutLine("SecSubs::delConsumer unlocked");
    return res;
}

bool SecSubs::clearAllSubscriptions(ConnectionData *cd)
{
    // sendStdoutLine("SecSubs::clearAllSubscriptions lock");
    pmutex.lock();
    QStringList toDel;
    foreach (ParamSubs *p, params)
    {
        if(p->delConsumer(cd))
            toDel.append(p->param);
    }
    while (!toDel.isEmpty())
    {
        QString pname = toDel.takeFirst();
        delete params.take(pname);
    }
    qmutex.lock();
    quoteConsumers.remove(cd);
    bool res = (params.isEmpty() && quoteConsumers.isEmpty());
    qmutex.unlock();
    pmutex.unlock();
    // sendStdoutLine("SecSubs::clearAllSubscriptions unlocked");
    return res;
}

ParamSubs *SecSubs::findParamSubscriptions(QString param)
{
    ParamSubs *res = nullptr;
    // sendStdoutLine("SecSubs::findParamSubscriptions lock");
    pmutex.lock();
    if(params.contains(param))
        res = params.value(param);
    pmutex.unlock();
    // sendStdoutLine("SecSubs::findParamSubscriptions unlocked");
    return res;
}

void SecSubs::addQuotesConsumer(ConnectionData *cd, int id)
{
    sendStdoutLine(QString("SecSubs#%1::addQuotesConsumer(%2)").arg(this->secName).arg(id));
    qmutex.lock();
    if(!quoteConsumers.contains(cd))
        quoteConsumers.insert(cd, id);
    qmutex.unlock();
    // sendStdoutLine("SecSubs::addQuotesConsumer unlocked");
}

bool SecSubs::delQuotesConsumer(ConnectionData *cd)
{
    sendStdoutLine(QString("SecSubs#%1::delQuotesConsumer()").arg(this->secName));
    qmutex.lock();
    quoteConsumers.remove(cd);
    pmutex.lock();
    bool res = (params.isEmpty() && quoteConsumers.isEmpty());
    pmutex.unlock();
    qmutex.unlock();
    // sendStdoutLine("SecSubs::delQuotesConsumer unlocked");
    return res;
}

bool SecSubs::hasQuotesConsumer(ConnectionData *cd)
{
    // sendStdoutLine("SecSubs::hasQuotesConsumer lock");
    qmutex.lock();
    bool res;
    if(cd)
        res = quoteConsumers.contains(cd);
    else
        res = !quoteConsumers.isEmpty();
    qmutex.unlock();
    // sendStdoutLine("SecSubs::hasQuotesConsumer unlocked");
    return res;
}

QStringList SecSubs::getParamsList()
{
    QStringList res;
    // sendStdoutLine("SecSubs::getParamsList lock");
    pmutex.lock();
    res = params.keys();
    pmutex.unlock();
    // sendStdoutLine("SecSubs::getParamsList unlocked");
    return res;
}

QList<ConnectionData *> SecSubs::getQuotesConsumersList()
{
    QList<ConnectionData *> res;
    // sendStdoutLine("SecSubs::getQuotesConsumersList lock");
    qmutex.lock();
    res = quoteConsumers.keys();
    qmutex.unlock();
    // sendStdoutLine("SecSubs::getQuotesConsumersList unlocked");
    return res;
}

int SecSubs::getQuotesSubscriptionId(ConnectionData *cd)
{
    int res = -1;
    // sendStdoutLine("SecSubs::getQuotesSubscriptionId lock");
    qmutex.lock();
    if(quoteConsumers.contains(cd))
        res = quoteConsumers.value(cd);
    qmutex.unlock();
    // sendStdoutLine("SecSubs::getQuotesSubscriptionId unlocked");
    return res;
}

void ParamSubs::addConsumer(ConnectionData *cd, int id)
{
    // sendStdoutLine(QString("ParamSubs#%1::addConsumer(%2)").arg(this->param).arg(id));
    mutex.lock();
    if(!consumers.contains(cd))
        consumers.insert(cd, id);
    mutex.unlock();
    // sendStdoutLine("ParamSubs::addConsumer unlocked");
}

bool ParamSubs::delConsumer(ConnectionData *cd)
{
    if(!cd)
        return true;
    sendStdoutLine(QString("ParamSubs#%1::delConsumer()").arg(this->param));
    mutex.lock();
    consumers.remove(cd);
    bool res = consumers.isEmpty();
    mutex.unlock();
    // sendStdoutLine("ParamSubs::delConsumer unlocked");
    return res;
}

bool ParamSubs::hasConsumer(ConnectionData *cd)
{
    // sendStdoutLine("ParamSubs::hasConsumer lock");
    mutex.lock();
    bool res = consumers.contains(cd);
    mutex.unlock();
    // sendStdoutLine("ParamSubs::hasConsumer unlocked");
    return res;
}

QList<ConnectionData *> ParamSubs::consumersList()
{
    QList<ConnectionData *> res;
    // sendStdoutLine("ParamSubs::consumersList lock");
    mutex.lock();
    res = consumers.keys();
    mutex.unlock();
    // sendStdoutLine("ParamSubs::consumersList unlocked");
    return res;
}

int ParamSubs::getSubscriptionId(ConnectionData *cd)
{
    int res = -1;
    // sendStdoutLine("ParamSubs::getSubscriptionId lock");
    mutex.lock();
    if(consumers.contains(cd))
        res = consumers.value(cd);
    mutex.unlock();
    // sendStdoutLine("ParamSubs::getSubscriptionId unlocked");
    return res;
}


void sendStdoutLine(QString line)
{
    BridgeTCPServer *gsrv = BridgeTCPServer::getGlobalServer();
    if(gsrv)
        gsrv->sendStdoutLine(line);
}

void sendStderrLine(QString line)
{
    BridgeTCPServer *gsrv = BridgeTCPServer::getGlobalServer();
    if(gsrv)
        gsrv->sendStderrLine(line);
}
