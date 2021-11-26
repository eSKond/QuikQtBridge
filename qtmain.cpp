#include <QCoreApplication>
#include <QDebug>
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
    qDebug() << "start listening on " << cfgrdr.getHost() << ":" << cfgrdr.getPort();
    if(server.listen(cfgrdr.getHost(), cfgrdr.getPort()))
        qDebug() << "Server started on:" << server.serverAddress().toString() << ":" << server.serverPort();
    qDebug() << "start app event loop...";
    int res = app.exec();
    qDebug() << "app event loop finished.";
    server.close();
    return res;
}
