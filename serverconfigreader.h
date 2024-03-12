#ifndef SERVERCONFIGREADER_H
#define SERVERCONFIGREADER_H

#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QHostAddress>

class ServerConfigReader
{
public:
    ServerConfigReader(QString scriptPath);
    QStringList getAllowedIPs(){return allowedIPs;}
    QHostAddress getHost(){return host;}
    int getPort(){return port;}
    QString getLogPathPrefix(){return logPathPrefix;}
    QString getDebugLogPathPrefix(){return debugLogPathPrefix;}
private:
    QStringList allowedIPs;
    QHostAddress host;
    int port;
    QString logPathPrefix;
    QString debugLogPathPrefix;
};

#endif // SERVERCONFIGREADER_H
