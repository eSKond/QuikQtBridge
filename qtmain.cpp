#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include "quikqtbridge.h"
#include "bridgetcpserver.h"
#include "serverconfigreader.h"

int qtMain(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_PluginApplication, true);
    QCoreApplication app(argc, argv);
    QString scriptPath = app.arguments().at(0);
    ServerConfigReader cfgrdr(scriptPath);
    qDebug() << "Create server...";
    BridgeTCPServer server;
    server.setAllowedIPs(cfgrdr.getAllowedIPs());
    server.setLogPathPrefix(cfgrdr.getLogPathPrefix());
    server.setDebugLogPathPrefix(cfgrdr.getDebugLogPathPrefix());
    QString msg;
    QTextStream ts2m(&msg);
    ts2m << "start listening on " << cfgrdr.getHost().toString() << ":" << cfgrdr.getPort();
    server.sendStdoutLine(msg);
    msg.clear();
    if(server.listen(cfgrdr.getHost(), cfgrdr.getPort()))
    {
        ts2m << "Server started on:" << server.serverAddress().toString() << ":" << server.serverPort();
        server.sendStdoutLine(msg);
        msg.clear();
    }
    ts2m << "start app event loop...";
    server.sendStdoutLine(msg);
    msg.clear();
    int res = app.exec();
    ts2m << "app event loop finished.";
    server.sendStdoutLine(msg);
    msg.clear();
    server.close();
    return res;
}
