#include "bridgetcpserver.h"
#include <QRegularExpression>

#define ALLOW_LOCAL_IP

struct FastCallbackFunctionData
{
    ConnectionData *cd;
    QString funName;
    FastCallbackFunctionData():cd(nullptr){}
    FastCallbackFunctionData(QString fn):cd(nullptr),funName(fn){}
};

BridgeTCPServer::BridgeTCPServer(QObject *parent)
    : QTcpServer(parent)
{
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
            cd->proto->sendReq(id, cbCall, false);
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
            FastCallbackRequestEventLoop el(fcfdata->cd, fcfdata->funName);
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
    //qDebug() << line;
}

void BridgeTCPServer::sendStderrLine(QString line)
{
    qDebug() << line;
}

bool BridgeTCPServer::ipAllowed(QString ip)
{
    qDebug() << "Checking ip:" << ip;
    foreach(QString mask, m_allowedIps)
    {
        QString rexpstr = QString("%1").arg(mask);
        qDebug() << "rexp:" << rexpstr;
        QRegularExpression rexp(rexpstr);
        if(rexp.match(ip).hasMatch())
        {
            qDebug() << "matched";
            return true;
        }
    }
    qDebug() << "IP disabled";
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
        cd->proto->sendAns(id, errObj, log);
}

void BridgeTCPServer::processExtendedRequests(ConnectionData *cd, int id, QString method, QJsonObject &jobj)
{
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
    QString cls = jobj.value("class").toString().toUpper();
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
    QString cls = jobj.value("class").toString().toUpper();
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
    QString sec = jobj.value("security").toString().toUpper();
    if(!jobj.contains("param"))
    {
        sendError(cd, id, 10, "'param' must be specified in subscribeParamChanges", true);
        return;
    }
    QString par = jobj.value("param").toString().toUpper();
    ParamSubs *p=paramSubscriptions.findParamSubscriptions(cls, sec, par);
    if(p)
    {
        if(p->consumers.contains(cd))
        {
            sendError(cd, id, 11, QString("You already subscriped %1/%2/%3").arg(cls, sec, par), true);
            return;
        }
    }
    else
    {
        QVariantList args, res;
        args << cls << sec << par;
        qqBridge->invokeMethod("ParamRequest", args, res, this);
        if(!res[0].toBool())
            sendStderrLine("ParamRequest returned false");
    }
    paramSubscriptions.addConsumer(cd, cls, sec, par, id);
    QJsonObject subsRes
    {
        {"method", "return"},
        {"result", true}
    };
    cd->proto->sendAns(id, subsRes, false);
}

void BridgeTCPServer::processUnsubscribeParamChangesRequest(ConnectionData *cd, int id, QJsonObject &jobj)
{
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 12, "'class' must be specified in unsubscribeParamChanges", true);
        return;
    }
    QString cls = jobj.value("class").toString().toUpper();
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
    QString sec = jobj.value("security").toString().toUpper();
    if(!jobj.contains("param"))
    {
        sendError(cd, id, 14, "'param' must be specified in unsubscribeParamChanges", true);
        return;
    }
    QString par = jobj.value("param").toString().toUpper();
    paramSubscriptions.delConsumer(cd, cls, sec, par);
    if(!paramSubscriptions.findParamSubscriptions(cls, sec, par))
    {
        QVariantList args, res;
        args << cls << sec << par;
        qqBridge->invokeMethod("CancelParamRequest", args, res, this);
        if(!res[0].toBool())
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
    QString cls = jobj.value("class").toString().toUpper();
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
    QString sec = jobj.value("security").toString().toUpper();
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(s)
    {
        if(s->quoteConsumers.contains(cd))
        {
            sendError(cd, id, 18, QString("You already subscriped %1/%2 quotes").arg(cls, sec), true);
            return;
        }
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
    if(!jobj.contains("class"))
    {
        sendError(cd, id, 19, "'class' must be specified in unsubscribeQuotes", true);
        return;
    }
    QString cls = jobj.value("class").toString().toUpper();
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
    QString sec = jobj.value("security").toString().toUpper();
    paramSubscriptions.delQuotesConsumer(cd, cls, sec);
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(!s || s->quoteConsumers.isEmpty())
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
    qDebug() << "incomingConnection thread: " << hstr;
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
    //qDebug() << "protoReqArrived thread: " << hstr;
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
    QString method = reqObj.value("method").toString("nop");
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
        cd->proto->sendAns(id, regRes, false);
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
        cd->proto->sendAns(id, invRes, false);
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
            cd->proto->sendAns(id, delRes, false);
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
    QString method = reqObj.value("method").toString("nop");
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
            cd->proto->sendVer(BRIDGE_SERVER_PROTOCOL_VERSION);
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

void BridgeTCPServer::fastCallbackRequest(ConnectionData *cd, QString fname, QVariantList args)
{
    if(m_connections.contains(cd))
    {
        QJsonObject invReq
        {
            {"method", "invoke"},
            {"function", fname},
            {"arguments", QJsonArray::fromVariantList(args)}
        };
        int id = ++(cd->outMsgId);
        cd->proto->sendReq(id, invReq, false);
        cd->fcbWaitResult->fastCallbackRequestSent(cd, fname, id);
    }
}

void BridgeTCPServer::secParamsUpdate(QString cls, QString sec)
{
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(s)
    {
        QStringList allParams = s->params.keys();
        QString par;
        ParamSubs *p;
        int i,j;
        for(i=0; i<allParams.count();i++)
        {
            par = allParams[i];
            QVariantList args, res;
            args << cls << sec << par;
            qqBridge->invokeMethod("getParamEx2", args, res, this);
            QVariantMap mres = res[0].toMap();
            QVariant pval = mres["param_value"];
            p = s->findParamSubscriptions(par);
            if(pval == p->value)
                continue;
            p->value = pval;
            QJsonObject subsAns
            {
                {"method", "paramChange"},
                {"class", cls},
                {"security", sec},
                {"param", par},
                {"value", QJsonValue::fromVariant(pval)}
            };
            QList<ConnectionData *> consList = p->consumers.keys();
            for(j=0; j<consList.count(); j++)
            {
                ConnectionData *cd = consList.at(j);
                int id = p->consumers.value(cd);
                cd->proto->sendReq(id, subsAns, false);
            }
        }
    }
}

void BridgeTCPServer::secQuotesUpdate(QString cls, QString sec)
{
    bool needStop = true;
    SecSubs *s = paramSubscriptions.findSecuritySubscriptions(cls, sec);
    if(s)
    {
        if(!s->quoteConsumers.isEmpty())
        {
            needStop = false;
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
            QList<ConnectionData *> consList = s->quoteConsumers.keys();
            for(i=0; i<consList.count(); i++)
            {
                ConnectionData *cd = consList.at(i);
                int id = s->quoteConsumers.value(cd);
                cd->proto->sendReq(id, subsQAns, false);
            }
        }
    }
    if(needStop)
    {
        QVariantList args, res;
        args << cls << sec;
        qqBridge->invokeMethod("Unsubscribe_Level_II_Quotes", args, res, this);
        if(!res[0].toBool())
            sendStderrLine("Unsubscribe_Level_II_Quotes returned false");
    }
}

FastCallbackRequestEventLoop::FastCallbackRequestEventLoop(ConnectionData *rcd, QString rfname)
    : cd(rcd), funName(rfname), waitMux(nullptr)
{
}

QVariant FastCallbackRequestEventLoop::sendAndWaitResult(BridgeTCPServer *server, const QVariantList &args)
{
    QMutex waitMutex;
    waitMux = &waitMutex;
    cd->fcbWaitResult = this;
    QMetaObject::invokeMethod(server, "fastCallbackRequest", Qt::QueuedConnection,
                              Q_ARG(ConnectionData*, cd),
                              Q_ARG(QString, funName),
                              Q_ARG(QVariantList, args));
    waitMutex.lock();
    waitMutex.tryLock(FASTCALLBACK_TIMEOUT_SEC * 1000);
    qDebug() << "sendAndWaitResult: Event loop finished";
    waitMutex.unlock();
    if(cd)
        cd->fcbWaitResult = nullptr;
    return result;
}

void FastCallbackRequestEventLoop::fastCallbackRequestSent(ConnectionData *acd, QString afname, int aid)
{
    if(cd==acd && funName==afname)
        id = aid;
}

void FastCallbackRequestEventLoop::fastCallbackReturnArrived(ConnectionData *acd, int aid, QVariant res)
{
    if(cd==acd && id==aid)
    {
        result = res;
        qDebug() << "Wake fast callback waiter";
        waitMux->unlock();
    }
}

void FastCallbackRequestEventLoop::connectionDataDeleted(ConnectionData *dcd)
{
    if(cd == dcd)
    {
        cd = nullptr;
        waitMux->unlock();
        qDebug() << "Wake fast callback waiter(connectionDataDeleted)";
    }
}

ConnectionData::~ConnectionData()
{
    while(!objRefs.isEmpty())
    {
        int objid = objRefs.takeLast();
        qqBridge->deleteObject(objid);
        qDebug() << "Object" << objid << "deleted";
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
    mutex.lock();
    QStringList keys = classes.keys();
    foreach (QString cls, keys)
    {
        delete classes.take(cls);
    }
    mutex.unlock();
}

void ParamSubscriptionsDb::addConsumer(ConnectionData *cd, QString cls, QString sec, QString param, int id)
{
    QMutexLocker locker(&mutex);
    ClsSubs *c;
    if(classes.contains(cls))
        c = classes.value(cls);
    else
    {
        c = new ClsSubs(cls);
        classes.insert(cls, c);
    }
    c->addConsumer(cd, sec, param, id);
}

bool ParamSubscriptionsDb::delConsumer(ConnectionData *cd, QString cls, QString sec, QString param)
{
    QMutexLocker locker(&mutex);
    if(classes.contains(cls))
    {
        if(classes.value(cls)->delConsumer(cd, sec, param))
            delete classes.take(cls);
    }
    return classes.isEmpty();
}

bool ParamSubscriptionsDb::clearAllSubscriptions(ConnectionData *cd)
{
    QMutexLocker locker(&mutex);
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
    return classes.isEmpty();
}

ParamSubs *ParamSubscriptionsDb::findParamSubscriptions(QString cls, QString sec, QString param)
{
    QMutexLocker locker(&mutex);
    if(classes.contains(cls))
        return classes.value(cls)->findParamSubscriptions(sec, param);
    return nullptr;
}

SecSubs *ParamSubscriptionsDb::findSecuritySubscriptions(QString cls, QString sec)
{
    QMutexLocker locker(&mutex);
    if(classes.contains(cls))
        return classes.value(cls)->findSecuritySubscriptions(sec);
    return nullptr;
}

void ParamSubscriptionsDb::addQuotesConsumer(ConnectionData *cd, QString cls, QString sec, int id)
{
    QMutexLocker locker(&mutex);
    ClsSubs *c;
    if(classes.contains(cls))
        c = classes.value(cls);
    else
    {
        c = new ClsSubs(cls);
        classes.insert(cls, c);
    }
    c->addQuotesConsumer(cd, sec, id);
}

bool ParamSubscriptionsDb::delQuotesConsumer(ConnectionData *cd, QString cls, QString sec)
{
    QMutexLocker locker(&mutex);
    if(classes.contains(cls))
    {
        if(classes.value(cls)->delQuotesConsumer(cd, sec))
            delete classes.take(cls);
    }
    return classes.isEmpty();
}

ClsSubs::~ClsSubs()
{
    //Здесь нельзя использовать локер использующий стек,
    //потому-что он удаляется вместе с мьютексом на выходе из деструктора
    mutex.lock();
    QStringList keys = securities.keys();
    foreach (QString sec, keys)
    {
        delete securities.take(sec);
    }
    mutex.unlock();
}

void ClsSubs::addConsumer(ConnectionData *cd, QString sec, QString param, int id)
{
    QMutexLocker locker(&mutex);
    SecSubs *s;
    if(securities.contains(sec))
        s = securities.value(sec);
    else
    {
        s = new SecSubs(sec);
        securities.insert(sec, s);
    }
    s->addConsumer(cd, param, id);
}

bool ClsSubs::delConsumer(ConnectionData *cd, QString sec, QString param)
{
    QMutexLocker locker(&mutex);
    if(securities.contains(sec))
    {
        if(securities.value(sec)->delConsumer(cd, param))
            delete securities.take(sec);
    }
    return securities.isEmpty();
}

bool ClsSubs::clearAllSubscriptions(ConnectionData *cd)
{
    QMutexLocker locker(&mutex);
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
    return securities.isEmpty();
}

ParamSubs *ClsSubs::findParamSubscriptions(QString sec, QString param)
{
    QMutexLocker locker(&mutex);
    if(securities.contains(sec))
        return securities.value(sec)->findParamSubscriptions(param);
    return nullptr;
}

SecSubs *ClsSubs::findSecuritySubscriptions(QString sec)
{
    QMutexLocker locker(&mutex);
    if(securities.contains(sec))
        return securities.value(sec);
    return nullptr;
}

void ClsSubs::addQuotesConsumer(ConnectionData *cd, QString sec, int id)
{
    QMutexLocker locker(&mutex);
    SecSubs *s;
    if(securities.contains(sec))
        s = securities.value(sec);
    else
    {
        s = new SecSubs(sec);
        securities.insert(sec, s);
    }
    s->addQuotesConsumer(cd, id);
}

bool ClsSubs::delQuotesConsumer(ConnectionData *cd, QString sec)
{
    QMutexLocker locker(&mutex);
    if(securities.contains(sec))
    {
        if(securities.value(sec)->delQuotesConsumer(cd))
            delete securities.take(sec);
    }
    return securities.isEmpty();
}

SecSubs::~SecSubs()
{
    //Здесь нельзя использовать локер использующий стек,
    //потому-что он удаляется вместе с мьютексом на выходе из деструктора
    mutex.lock();
    QStringList keys = params.keys();
    foreach (QString par, keys)
    {
        delete params.take(par);
    }
    mutex.unlock();
}

void SecSubs::addConsumer(ConnectionData *cd, QString param, int id)
{
    QMutexLocker locker(&mutex);
    ParamSubs *p;
    if(params.contains(param))
        p = params.value(param);
    else
    {
        p = new ParamSubs(param);
        params.insert(param, p);
    }
    p->addConsumer(cd, id);
}

bool SecSubs::delConsumer(ConnectionData *cd, QString param)
{
    QMutexLocker locker(&mutex);
    if(params.contains(param))
    {
        if(params.value(param)->delConsumer(cd))
            delete params.take(param);
    }
    return (params.isEmpty() && quoteConsumers.isEmpty());
}

bool SecSubs::clearAllSubscriptions(ConnectionData *cd)
{
    QMutexLocker locker(&mutex);
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
    quoteConsumers.remove(cd);
    return (params.isEmpty() && quoteConsumers.isEmpty());
}

ParamSubs *SecSubs::findParamSubscriptions(QString param)
{
    QMutexLocker locker(&mutex);
    if(params.contains(param))
        return params.value(param);
    return nullptr;
}

void SecSubs::addQuotesConsumer(ConnectionData *cd, int id)
{
    QMutexLocker locker(&mutex);
    if(!quoteConsumers.contains(cd))
        quoteConsumers.insert(cd, id);
}

bool SecSubs::delQuotesConsumer(ConnectionData *cd)
{
    QMutexLocker locker(&mutex);
    quoteConsumers.remove(cd);
    return (params.isEmpty() && quoteConsumers.isEmpty());
}

void ParamSubs::addConsumer(ConnectionData *cd, int id)
{
    QMutexLocker locker(&mutex);
    if(!consumers.contains(cd))
        consumers.insert(cd, id);
}

bool ParamSubs::delConsumer(ConnectionData *cd)
{
    QMutexLocker locker(&mutex);
    consumers.remove(cd);
    return consumers.isEmpty();
}

