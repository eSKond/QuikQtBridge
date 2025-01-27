#ifndef BRIDGETCPSERVER_H
#define BRIDGETCPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QObject>
#include <QStringList>
#include <QMap>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QWaitCondition>
#include "jsonprotocolhandler.h"
#include "quikqtbridge.h"

#define BRIDGE_SERVER_PROTOCOL_VERSION  1
#define FASTCALLBACK_TIMEOUT_SEC    5

class FastCallbackRequestEventLoop;
class BridgeTCPServer;
struct ConnectionData
{
    int outMsgId;
    QString peerIp;
    JsonProtocolHandler *proto;
    QMap<QString, int> callbackSubscriptions;
    int peerProtocolVersion;
    bool versionSent;
    QList<int> objRefs;
    FastCallbackRequestEventLoop *fcbWaitResult;
    BridgeTCPServer *srv;
    ConnectionData()
        : outMsgId(0),
          proto(nullptr),
          peerProtocolVersion(0),
          versionSent(false),
          fcbWaitResult(nullptr),
          srv(nullptr)
    {}
    ~ConnectionData();
};
Q_DECLARE_METATYPE(ConnectionData*)

struct ParamSubs
{
    QString param;
    QVariant value;
    QMap<ConnectionData *, int> consumers;
    QMutex mutex;
    ParamSubs(QString pname) : param(pname){}
    void addConsumer(ConnectionData *cd, int id);
    bool delConsumer(ConnectionData *cd);
    bool hasConsumer(ConnectionData *cd);
    QList<ConnectionData *> consumersList();
    int getSubscriptionId(ConnectionData *cd);
};

struct SecSubs
{
    QString secName;
    QMap<QString, ParamSubs *> params;
    QMap<ConnectionData *, int> quoteConsumers;
    QMutex pmutex;
    QMutex qmutex;
    SecSubs(QString sec) : secName(sec){}
    ~SecSubs();
    void addConsumer(ConnectionData *cd, QString param, int id);
    bool delConsumer(ConnectionData *cd, QString param);
    bool clearAllSubscriptions(ConnectionData *cd);
    ParamSubs *findParamSubscriptions(QString param);
    void addQuotesConsumer(ConnectionData *cd, int id);
    bool delQuotesConsumer(ConnectionData *cd);
    bool hasQuotesConsumer(ConnectionData *cd = nullptr);
    QStringList getParamsList();
    QList<ConnectionData *> getQuotesConsumersList();
    int getQuotesSubscriptionId(ConnectionData *cd);
};

struct ClsSubs
{
    QString className;
    QMap<QString, SecSubs *> securities;
    QMutex mutex;
    ClsSubs(QString cls) : className(cls){}
    ~ClsSubs();
    void addConsumer(ConnectionData *cd, QString sec, QString param, int id);
    bool delConsumer(ConnectionData *cd, QString sec, QString param);
    bool clearAllSubscriptions(ConnectionData *cd);
    ParamSubs *findParamSubscriptions(QString sec, QString param);
    SecSubs *findSecuritySubscriptions(QString sec);
    void addQuotesConsumer(ConnectionData *cd, QString sec, int id);
    bool delQuotesConsumer(ConnectionData *cd, QString sec);
};

class ParamSubscriptionsDb
{
public:
    ParamSubscriptionsDb();
    ~ParamSubscriptionsDb();
    void addConsumer(ConnectionData *cd, QString cls, QString sec, QString param, int id);
    bool delConsumer(ConnectionData *cd, QString cls, QString sec, QString param);
    bool clearAllSubscriptions(ConnectionData *cd);
    ParamSubs *findParamSubscriptions(QString cls, QString sec, QString param);
    SecSubs *findSecuritySubscriptions(QString cls, QString sec);
    void addQuotesConsumer(ConnectionData *cd, QString cls, QString sec, int id);
    bool delQuotesConsumer(ConnectionData *cd, QString cls, QString sec);
private:
    QMutex mutex;
    QMap<QString, ClsSubs *> classes;
};

void sendStdoutLine(QString line);
void sendStderrLine(QString line);

class BridgeTCPServer : public QTcpServer, public QuikCallbackHandler
{
    Q_OBJECT
public:
    BridgeTCPServer(QObject *parent = nullptr);
    ~BridgeTCPServer();
    static BridgeTCPServer *getGlobalServer(){return g_server;}
    void setAllowedIPs(const QStringList &aips);
    void setLogPathPrefix(QString lpp);
    void setDebugLogPathPrefix(QString lpp);

    virtual void callbackRequest(QString name, const QVariantList &args, QVariant &vres);
    virtual void fastCallbackRequest(void *data, const QVariantList &args, QVariant &res);
    virtual void clearFastCallbackData(void *data);
    virtual void sendStdoutLine(QString line);
    virtual void sendStderrLine(QString line);
private:
    static BridgeTCPServer * g_server;
    QStringList m_allowedIps;
    bool ipAllowed(QString ip);
    QList<ConnectionData *> m_connections;
    QStringList activeCallbacks;
    QString logPathPrefix;
    QFile *logf;
    QTextStream *logts;

    //cache
    QStringList secClasses;
    void cacheSecClasses();

    ParamSubscriptionsDb paramSubscriptions;

    ConnectionData *getCDByProtoPtr(JsonProtocolHandler *p);
    void sendError(ConnectionData *cd, int id, int errcode, QString errmsg, bool log=false);

    void processExtendedRequests(ConnectionData *cd, int id, QString method, QJsonObject &jobj);
    void processLoadAccountsRequest(ConnectionData *cd, int id, QJsonObject &jobj);
    void processLoadClassesRequest(ConnectionData *cd, int id, QJsonObject &jobj);
    void processLoadClassSecuritiesRequest(ConnectionData *cd, int id, QJsonObject &jobj);
    void processSubscribeParamChangesRequest(ConnectionData *cd, int id, QJsonObject &jobj);
    void processUnsubscribeParamChangesRequest(ConnectionData *cd, int id, QJsonObject &jobj);
    void processExtendedAnswers(ConnectionData *cd, int id, QString method, QJsonObject &jobj);
    void processSubscribeQuotesRequest(ConnectionData *cd, int id, QJsonObject &jobj);
    void processUnsubscribeQuotesRequest(ConnectionData *cd, int id, QJsonObject &jobj);
protected:
    virtual void incomingConnection(qintptr handle);
private slots:
    void connectionEstablished(ConnectionData *cd);
    void protoReqArrived(int id, QJsonValue data);
    void protoAnsArrived(int id, QJsonValue data);
    void protoVerArrived(int ver);
    void protoEndArrived();
    void protoFinished();
    void protoError(QAbstractSocket::SocketError err);
    void serverError(QAbstractSocket::SocketError err);
    //void debugLog(QString msg);

    void fastCallbackRequestHandler(ConnectionData *cd, int oid, QString fname, QVariantList args);

    void secParamsUpdate(QString cls, QString sec);
    void secQuotesUpdate(QString cls, QString sec);
signals:
    void fastCallbackRequestSent(ConnectionData *cd, QString fname, int id);
    void fastCallbackReturnArrived(ConnectionData *cd, int id, QVariant res);
};

class FastCallbackRequestEventLoop
{
public:
    FastCallbackRequestEventLoop(ConnectionData *rcd, int oid, QString rfname, BridgeTCPServer *s);
    QVariant sendAndWaitResult(BridgeTCPServer *server, const QVariantList &args);

    void fastCallbackRequestSent(ConnectionData *acd, int oid, QString afname, int aid);
    void fastCallbackReturnArrived(ConnectionData *acd, int aid, QVariant res);
    void connectionDataDeleted(ConnectionData *dcd);
private:
    ConnectionData *cd;
    QString funName;
    int objId;
    int id;
    QVariant result;
    QMutex *waitMux;
    BridgeTCPServer *srv;
};

#endif // BRIDGETCPSERVER_H
