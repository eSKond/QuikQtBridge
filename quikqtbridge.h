#ifndef QUIKQTBRIDGE_H
#define QUIKQTBRIDGE_H

#include <QObject>
#include <QMap>
#include <QString>
#include <lua.hpp>

class QuikCallbackHandler
{
public:
    virtual void callbackRequest(QString name, const QVariantList &args, QVariant &vres) = 0;
    virtual void fastCallbackRequest(void *data, const QVariantList &args, QVariant &res) = 0;
    virtual void clearFastCallbackData(void *data) = 0;
    virtual void sendStdoutLine(QString line) = 0;
    virtual void sendStderrLine(QString line) = 0;
};

struct BridgeCallableObject
{
    QuikCallbackHandler *handler;
    void *data;
    BridgeCallableObject() : handler(nullptr), data(nullptr){}
};
Q_DECLARE_METATYPE(BridgeCallableObject)

struct QuikCallableObject
{
    int objid;
    QuikCallableObject():objid(-1){}
};
Q_DECLARE_METATYPE(QuikCallableObject)

#define qqBridge    QuikQtBridge::initQuikQtBridge()

class QuikQtBridge : public QObject
{
public:
    static QuikQtBridge *initQuikQtBridge();
    static void deinitQuikQtBridge();

    void invokeMethod(QString method, const QVariantList &args, QVariantList &res, QuikCallbackHandler *errOut);
    void invokeObjectMethod(int objid, QString method, const QVariantList &args, QVariantList &res, QuikCallbackHandler *errOut);
    void deleteObject(int objid);
    bool registerCallback(QuikCallbackHandler *handler, QString name);
    void getVariable(QString varname, QVariant &res);

    lua_State *getRecentStackForThreadId(Qt::HANDLE ctid);
    void setRecentStack(Qt::HANDLE ctid, lua_State *l);
    void callbackRequest(QString name, const QVariantList &args, QVariant &vres);
private:
    static QuikQtBridge *global_bridge;
    QMap<QString, QuikCallbackHandler *> m_handlers;
    QMap<Qt::HANDLE, lua_State *> recentStackMap;

    explicit QuikQtBridge();
    ~QuikQtBridge();
};

#endif // QUIKQTBRIDGE_H
