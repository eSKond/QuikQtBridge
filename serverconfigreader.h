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
private:
    QStringList allowedIPs;
    QHostAddress host;
    int port;
    QString logPathPrefix;
};

#endif // SERVERCONFIGREADER_H
