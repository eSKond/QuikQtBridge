#include "quikcoast.h"
#include "quikqtbridge.h"

#include <QDebug>
#include <QThread>
#include <string.h>
//#include <processthreadsapi.h>

#define JUMP_TABLE_SIZE 100

static void stackDump(lua_State *l)
{
    int i;
    int top = lua_gettop(l);
    qDebug() << "-- Stack Dump, Size=" << top << " --";
    for(i = 1; i <= top; i++)
    {
        int t = lua_type(l, i);
        switch(t)
        {
        case LUA_TSTRING:
            qDebug() << "String";
            break;
        case LUA_TBOOLEAN:
            qDebug() << "Bool";
            break;
        case LUA_TNUMBER:
            qDebug() << "Number";
            break;
        default:
            qDebug() << QString::fromLocal8Bit(lua_typename(l, t));
            break;
        }
    }
    qDebug() << "-----------";
}

lua_State *getRecentStackForThreadId(Qt::HANDLE ctid)
{
    return qqBridge->getRecentStackForThreadId(ctid);
}

lua_State *getRecentStack(Qt::HANDLE *p_ctid=nullptr)
{
    Qt::HANDLE ctid = reinterpret_cast<Qt::HANDLE>(Concurrency::details::platform::GetCurrentThreadId());
    //Qt::HANDLE ctid = QThread::currentThreadId();
    if(p_ctid)
        *p_ctid = ctid;
    return getRecentStackForThreadId(ctid);
}

void setRecentStack(lua_State *l)
{
    Qt::HANDLE ctid = QThread::currentThreadId();
    qqBridge->setRecentStack(ctid, l);
}

struct JumpTableItem
{
    QuikCallbackHandler *owner;
    Qt::HANDLE threadId;
    QString fName;
    void *customData;
    lua_CFunction callback;
    QString callerName;
    JumpTableItem():
        owner(nullptr),
        threadId(nullptr),
        fName(QString()),
        customData(nullptr),
        callback(nullptr),
        callerName(QString())
    {}
};

JumpTableItem jumpTable[JUMP_TABLE_SIZE];

int findFreeJumpTableSlot()
{
    int i;
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(!jumpTable[i].owner && jumpTable[i].fName.isEmpty())
            return i;
    }
    return -1;
}

int findJumpTableSlotForNamedCallback(QString cbName)
{
    int i;
    //search slot for replacement
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(!jumpTable[i].owner && jumpTable[i].fName==cbName)
            return i;
    }
    return findFreeJumpTableSlot();
}

int findFreeOrExpiredJumpTableSlot(QString caller)
{
    //find expired
    Qt::HANDLE ctid = QThread::currentThreadId();
    int i;
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(jumpTable[i].owner && jumpTable[i].threadId==ctid && jumpTable[i].callerName==caller)
            return i;
    }
    //else find free
    return findFreeJumpTableSlot();
}

int findNamedJumpTableSlot(QString cbName)
{
    int i;
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(jumpTable[i].fName==cbName)
            return i;
    }
    return -1;
}

bool registerNamedCallback(QString cbName)
{
    Qt::HANDLE ctid;
    lua_State *recentStack = getRecentStack(&ctid);
    //qDebug() << "register named callback:" << cbName;
    if(recentStack)
    {
        int i=findJumpTableSlotForNamedCallback(cbName);
        //qDebug() << "jumptable slot found:" << i;
        if(i<0)
        {
            qDebug() << "No more free callback slots. Can't register" << cbName;
            return false;
        }
        jumpTable[i].owner = nullptr;
        jumpTable[i].threadId = ctid;
        jumpTable[i].fName = cbName;
        lua_register(recentStack, cbName.toLocal8Bit().data(), jumpTable[i].callback);
    }
    return true;
}

bool registerPredefinedNamedCallback(lua_State *l, QString cbName)
{
    Qt::HANDLE ctid = reinterpret_cast<Qt::HANDLE>(Concurrency::details::platform::GetCurrentThreadId());
    int i=findJumpTableSlotForNamedCallback(cbName);
    if(i<0)
    {
        return false;
    }
    jumpTable[i].owner = nullptr;
    jumpTable[i].threadId = ctid;
    jumpTable[i].fName = cbName;
    lua_register(l, cbName.toLocal8Bit().data(), jumpTable[i].callback);
    return true;
}

int registerFastCallback(QuikCallbackHandler *qcbh, QString caller, void *data)
{
    Qt::HANDLE ctid = QThread::currentThreadId();
    int i=findFreeOrExpiredJumpTableSlot(caller);
    if(i<0)
    {
        qDebug() << "No more free callback slots. Can't register fast callback";
        return -1;
    }
    jumpTable[i].owner = qcbh;
    jumpTable[i].threadId = ctid;
    jumpTable[i].callerName = caller;
    jumpTable[i].customData = data;
    return i;
}

void unregisterAllNamedCallbacks()
{
    int i;
    lua_State *recentStack = getRecentStack();
    if(!recentStack)
        return;
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(!jumpTable[i].owner && !jumpTable[i].fName.isEmpty())
        {
            lua_pushnil(recentStack);
            lua_setglobal(recentStack, jumpTable[i].fName.toLocal8Bit().data());
            jumpTable[i].owner = nullptr;
            jumpTable[i].threadId = nullptr;
            jumpTable[i].customData = nullptr;
            jumpTable[i].fName = QString();
            jumpTable[i].callerName = QString();
        }
    }
}

void unregisterAllCallbacksForCaller(QString caller)
{
    int i;
    Qt::HANDLE ctid = QThread::currentThreadId();
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(jumpTable[i].owner && jumpTable[i].customData && jumpTable[i].threadId==ctid && jumpTable[i].callerName==caller)
        {
            jumpTable[i].owner = nullptr;
            jumpTable[i].threadId = nullptr;
            jumpTable[i].customData = nullptr;
            jumpTable[i].fName = QString();
            jumpTable[i].callerName = QString();
        }
    }
}

void unregisterAllObjectCallbacks(int objid)
{
    QString prefix = QString("obj%1.").arg(objid);
    int i;
    Qt::HANDLE ctid = QThread::currentThreadId();
    for(i=0; i<JUMP_TABLE_SIZE; i++)
    {
        if(jumpTable[i].owner && jumpTable[i].customData && jumpTable[i].threadId==ctid && jumpTable[i].callerName.startsWith(prefix))
        {
            jumpTable[i].owner->clearFastCallbackData(jumpTable[i].customData);
            jumpTable[i].owner = nullptr;
            jumpTable[i].threadId = nullptr;
            jumpTable[i].customData = nullptr;
            jumpTable[i].fName = QString();
            jumpTable[i].callerName = QString();
        }
    }
}

static int extractValueFromLuaStack(lua_State *l, int sid, QVariant &sVal, QVariantList &lVal, QVariantMap &mVal, int *dtype=nullptr)
{
    int resType; //0 - simple value, 1 - list, 2 - map
    sid = lua_absindex(l, sid);
    int t = lua_type(l, sid);
    sVal.clear();
    lVal.clear();
    mVal.clear();
    if(dtype)
        *dtype = t;
    switch(t)
    {
    case LUA_TBOOLEAN:
    {
        //qDebug() << "bool";
        bool v = (bool)lua_toboolean(l, sid);
        resType = 0;
        sVal = QVariant(v);
        break;
    }
    case LUA_TSTRING:
    {
        //qDebug() << "string";
        QString v = QString::fromLocal8Bit(lua_tostring(l, sid));
        resType = 0;
        sVal = QVariant(v);
        break;
    }
    case LUA_TNUMBER:
    {
        double v = lua_tonumber(l, sid);
        QString sv = QString::fromLocal8Bit(lua_tostring(l, sid));
        //qDebug() << QString("read number: double=%1, string=%2").arg(v, 0, 'f').arg(sv);
        resType = 0;
        if(sv.indexOf('.') < 0)
        {
            qint64 bigint = (qint64)v;
            if(QString("%1").arg(bigint) == sv)
                sVal = QVariant(bigint);
            else
                sVal = sv;
            //qDebug() << "double" << v << "is recognized as int" << sVal;
        }
        else
        {
            sVal = QVariant(v);
            //qDebug() << "double" << v << "is kept as double" << sVal;
        }
        break;
    }
    case LUA_TTABLE:
    {
        lua_pushnil(l);
        int lidx=1;
        bool islist=true;
        int dtype;
        bool hasFunction=false;
        while(lua_next(l, sid) != 0)
        {
            int kt=lua_type(l, -2);
            QString k;
            int ridx=0;
            if(kt == LUA_TSTRING)
            {
                k = QString::fromLocal8Bit(lua_tostring(l, -2));
                islist=false;
            }
            else
            {
                ridx = (int)lua_tonumber(l, -2);
                k = QString("[%1]").arg(ridx);
                if(ridx != lidx)
                    islist=false;
            }
            QVariant sv;
            QVariantList lv;
            QVariantMap mv;
            int vtp = extractValueFromLuaStack(l, lua_gettop(l), sv, lv, mv, &dtype);
            if(!vtp)
            {
                lVal.append(sv);
                mVal.insert(k, sv);
                if(dtype == LUA_TFUNCTION)
                    hasFunction = true;
            }
            else
            {
                if(vtp==1)
                {
                    lVal.insert(lVal.length(), lv);
                    mVal.insert(k, lv);
                }
                else
                {
                    lVal.insert(lVal.length(), mv);
                    mVal.insert(k, mv);
                }
            }
            lidx++;
            lua_pop(l, 1);
        }
        if(islist)
        {
            //qDebug() << "list";
            mVal.clear();
            resType = 1;
        }
        else
        {
            if(hasFunction)
            {
                //qDebug() << "object";
                lua_pushvalue(l, sid); //копируем таблицу
                int objid = luaL_ref(l, LUA_REGISTRYINDEX); //сохраняем в реестр и возвращаем индекс в реестре
                lVal.clear();
                mVal.clear();
                QuikCallableObject qcobj;
                qcobj.objid = objid;
                sVal = QVariant::fromValue(qcobj);
                resType = 0;
            }
            else
            {
                //qDebug() << "dict";
                lVal.clear();
                resType = 2;
            }
        }
        break;
    }
    case LUA_TNIL:
    case LUA_TNONE:
        //qDebug() << "none";
        resType = 0;
        sVal = QVariant();
        break;
    case LUA_TFUNCTION:
        //qDebug() << "function";
        resType = 0;
        sVal = QVariant();
        break;
    default:
        //qDebug() << "Unknown type:" << QString::fromLocal8Bit(lua_typename(l, t));
        resType = 0;
        sVal = QVariant();
        break;
    }
    return resType;
}

//void invokePlugin(QString name, const QVariantList &args, QVariant &vres)
//{
//    qqBridge->callbackRequest(name, args, vres);
//}

//void fastInvokePlugin(BridgeCallableObject cobj, const QVariantList &args, QVariant &vres)
//{
//    cobj.handler->fastCallbackRequest(cobj.data, args, vres);
//}

void pushVariantToLuaStack(lua_State *l, QVariant val, QString caller)
{
    switch(val.type())
    {
    case QVariant::Bool:
    {
        bool v = val.toBool();
        lua_pushboolean(l, v);
        break;
    }
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
    {
        qlonglong v = val.toLongLong();
        lua_pushinteger(l, v);
        break;
    }
    case QVariant::String:
    {        
        QString v = val.toString();
        //qDebug() << "push string arg:" << v;
        lua_pushstring(l, v.toLocal8Bit().data());
        break;
    }
    case QVariant::Double:
    {
        double v = val.toDouble();
        lua_pushnumber(l, v);
        break;
    }
    case QVariant::Map:
    {
        QVariantMap tbl = val.toMap();
        lua_newtable(l);
        QMapIterator<QString, QVariant> ti(tbl);
        while(ti.hasNext())
        {
            ti.next();
            QString k;
            QVariant v;
            k=ti.key();
            v=ti.value();
            //QString logstr = QString("Field %1 type is %2 = %3").arg(k, QString::fromLocal8Bit(v.typeName()), v.toString());
            //qDebug() << logstr;
            lua_pushstring(l, k.toLocal8Bit().data());
            pushVariantToLuaStack(l, v, caller);
            lua_settable(l, -3);
        }
        break;
    }
    case QVariant::List:
    {
        QVariantList lst = val.toList();
        lua_newtable(l);
        for(int li=0;li<lst.count();li++)
        {
            QVariant v = lst.at(li);
            lua_pushinteger(l, li+1);
            pushVariantToLuaStack(l, v, caller);
            lua_settable(l, -3);
        }
        break;
    }
    default:
        if(val.canConvert<BridgeCallableObject>())
        {
            if(!caller.isEmpty())
            {
                BridgeCallableObject fcb = val.value<BridgeCallableObject>();
                int cbi=registerFastCallback(fcb.handler, caller, fcb.data);
                if(cbi<0)
                    lua_pushnil(l);
                else
                {
                    lua_pushcfunction(l, jumpTable[cbi].callback);
                }
            }
            else
                qDebug() << "Callable object sent from lua?!";
        }
        else
            lua_pushnil(l);
        break;
    }
}

QVariant popVariantFromLuaStack(lua_State *l)
{
    QVariant sv;
    QVariantList lv;
    QVariantMap mv;
    int vtp = extractValueFromLuaStack(l, -1, sv, lv, mv);
    lua_pop(l, 1);
    if(vtp == 1)
        return QVariant(lv);
    if(vtp == 2)
        return QVariant(mv);
    return sv;
}

bool getQuikVariable(QString varname, QVariant &res)
{
    lua_State *recentStack = getRecentStack();
    if(!recentStack)
    {
        qDebug() << "No stack?!";
        return false;
    }
    lua_getglobal(recentStack, varname.toLocal8Bit().data());
    res = popVariantFromLuaStack(recentStack);
    return true;
}

bool invokeQuik(QString method, const QVariantList &args, QVariantList &res, QString &errMsg)
{
    //qDebug() << "invokeQuik:" << method;
    lua_State *recentStack = getRecentStack();
    int top = lua_gettop(recentStack);
    lua_getglobal(recentStack, method.toLocal8Bit().data());
    res.clear();
    errMsg.clear();
    int li;
    for(li=0;li<args.count();li++)
    {
        QVariant v = args.at(li);
        pushVariantToLuaStack(recentStack, v, method);
    }
    //qDebug() << "lua_pcall...";
    int pcres=lua_pcall(recentStack, li, LUA_MULTRET, 0);
    if(pcres)
    {
        errMsg = QString::fromLocal8Bit(lua_tostring(recentStack, -1));
        //qDebug() << "..." << errMsg;
        lua_pop(recentStack, 1);
        return false;
    }
    //qDebug() << "...finished";
    while(lua_gettop(recentStack) != top)
    {
        QVariant resItem = popVariantFromLuaStack(recentStack);
        //qDebug() << "return" << resItem.toString();
        //мы забираем результаты с конца, поэтому и складывать нужно в обратном порядке
        res.prepend(resItem);
    }
    //qDebug() << "return from invokeQuik";
    return true;
}

bool invokeQuikObject(int objid, QString method, const QVariantList &args, QVariantList &res, QString &errMsg)
{
    QString caller = QString("obj%1.%2").arg(objid).arg(method);
    lua_State *recentStack = getRecentStack();
    int top = lua_gettop(recentStack);
    lua_rawgeti(recentStack, LUA_REGISTRYINDEX, objid);
    lua_getfield(recentStack, -1,  method.toLocal8Bit().data());
    lua_pushvalue(recentStack, -2);
    res.clear();
    errMsg.clear();
    int li;
    for(li=0;li<args.count();li++)
    {
        QVariant v = args.at(li);
        pushVariantToLuaStack(recentStack, v, caller);
    }
    int pcres=lua_pcall(recentStack, li+1, LUA_MULTRET, 0);
    if(pcres)
    {
        errMsg = QString::fromLocal8Bit(lua_tostring(recentStack, -1));
        lua_pop(recentStack, 1);
        return false;
    }
    while(lua_gettop(recentStack) != top+1)
    {
        QVariant resItem = popVariantFromLuaStack(recentStack);
        //мы забираем результаты с конца, поэтому и складывать нужно в обратном порядке
        res.prepend(resItem);
    }
    lua_pop(recentStack, 1);
    return true;
}

void deleteQuikObject(int objid)
{
    lua_State *recentStack = getRecentStack();
    if(!recentStack)
        return;
    unregisterAllObjectCallbacks(objid);
    luaL_unref(recentStack, LUA_REGISTRYINDEX, objid);
}

char savedScriptPath[_MAX_PATH + 1] = "";

static int onInitHandler(lua_State *l)
{
    const char *path = luaL_checkstring(l, 1);
    strncpy_s(savedScriptPath, path, _MAX_PATH);
    return 0;
}

int qtMain(int argc, char *argv[]);

static int mainTrampoline(lua_State *l)
{
    int argc = 1;
    char * argv[] = {savedScriptPath, NULL};
    setRecentStack(l);
    qtMain(argc, argv);
    QuikQtBridge::deinitQuikQtBridge();
    return 0;
}

//Оставим этот код в комментах на всякий случай. Это жёсткая привязка предопределенного колбека
//который нужно регать заранее. Мы использовали динамическую привязку через таблицу переходов
//но вдруг там какие то проблемы обнаружатся и придётся каждому колбеку такую обёртку писать
//Но вообще всё должно работать (и тьфу-тьфу работает)
/*
static int onStopHandler(lua_State *l)
{
    QVariantList args;
    QVariant sv, vres;
    QVariantList lv;
    QVariantMap mv;
    int i;
    int top = lua_gettop(l);
    for(i = 1; i <= top; i++)
    {
        int vtp = extractValueFromLuaStack(l, i, sv, lv, mv);
        if(!vtp)
        {
            args.append(sv);
        }
        else
        {
            if(vtp==1)
            {
                args.insert(args.length(), lv);
            }
            else
            {
                args.insert(args.length(), mv);
            }
        }
    }
    setRecentStack(l);
    qqBridge->callbackRequest("OnStop", args, vres);
    int rescnt = 0;
    if(!vres.isNull())
    {
        if(vres.type() == QVariant::List)
        {
            //special case: multiple results
            QVariantList lst = vres.toList();
            for(int li=0;li<lst.count();li++)
            {
                QVariant v = lst.at(li);
                pushVariantToLuaStack(l, v, QString());
                rescnt++;
            }
        }
        else
        {
            pushVariantToLuaStack(l, vres, QString());
            rescnt++;
        }
    }
    return rescnt;
}
*/

static struct luaL_Reg ls_lib[] = {
    //{"OnInit", onInitHandler},
    //{"main", mainTrampoline},
    {nullptr, nullptr}
};

int luaopenImp(lua_State *l)
{
    luaL_newlib(l, ls_lib);
    lua_register(l, "OnInit", onInitHandler);
    registerPredefinedNamedCallback(l, "OnFirm");
    registerPredefinedNamedCallback(l, "OnAllTrade");
    registerPredefinedNamedCallback(l, "OnTrade");
    registerPredefinedNamedCallback(l, "OnOrder");
    registerPredefinedNamedCallback(l, "OnAccountBalance");
    registerPredefinedNamedCallback(l, "OnFuturesLimitChange");
    registerPredefinedNamedCallback(l, "OnFuturesLimitDelete");
    registerPredefinedNamedCallback(l, "OnFuturesClientHolding");
    registerPredefinedNamedCallback(l, "OnMoneyLimit");
    registerPredefinedNamedCallback(l, "OnMoneyLimitDelete");
    registerPredefinedNamedCallback(l, "OnDepoLimit");
    registerPredefinedNamedCallback(l, "OnDepoLimitDelete");
    registerPredefinedNamedCallback(l, "OnAccountPosition");
    registerPredefinedNamedCallback(l, "OnNegDeal");
    registerPredefinedNamedCallback(l, "OnNegTrade");
    registerPredefinedNamedCallback(l, "OnStopOrder");
    registerPredefinedNamedCallback(l, "OnTransReply");
    registerPredefinedNamedCallback(l, "OnParam");
    registerPredefinedNamedCallback(l, "OnQuote");
    registerPredefinedNamedCallback(l, "OnDisconnected");
    registerPredefinedNamedCallback(l, "OnConnected");
    registerPredefinedNamedCallback(l, "OnCleanUp");
    registerPredefinedNamedCallback(l, "OnClose");
    registerPredefinedNamedCallback(l, "OnStop");
    lua_register(l, "main", mainTrampoline);
    return 0;
}

static int universalCallbackHandler(JumpTableItem *jitem, lua_State *l)
{
    QVariantList args;
    QVariant sv, vres;
    QVariantList lv;
    QVariantMap mv;
    int i;
    //qDebug() << "universalCallbackHandler: start";
    int top = lua_gettop(l);
    for(i = 1; i <= top; i++)
    {
        int vtp = extractValueFromLuaStack(l, i, sv, lv, mv);
        if(!vtp)
        {
            args.append(sv);
        }
        else
        {
            if(vtp==1)
            {
                args.insert(args.length(), QVariant(lv));
            }
            else
            {
                args.insert(args.length(), QVariant(mv));
            }
        }
    }
    setRecentStack(l);
    if(jitem->fName.isEmpty())
    {
        if(jitem->owner)
        {
            jitem->owner->fastCallbackRequest(jitem->customData, args, vres);
        }
    }
    else
    {
        qqBridge->callbackRequest(jitem->fName, args, vres);
    }
    int rescnt = 0;
    if(!vres.isNull())
    {
        if(vres.type() == QVariant::List)
        {
            //special case: multiple results
            QVariantList lst = vres.toList();
            for(int li=0;li<lst.count();li++)
            {
                QVariant v = lst.at(li);
                pushVariantToLuaStack(l, v, QString());
                rescnt++;
            }
        }
        else
        {
            pushVariantToLuaStack(l, vres, QString());
            rescnt++;
        }
    }
    return rescnt;
}

template<int i> int cbHandler(lua_State *l)
{
    return universalCallbackHandler(&jumpTable[i], l);
}

template<int i> bool JumpTable_init()
{
  jumpTable[i-1].callback = &cbHandler<i-1>;
  return JumpTable_init<i-1>();
}

template<> bool JumpTable_init<-1>(){ return true; }

const bool jt_initialized = JumpTable_init<JUMP_TABLE_SIZE>();
