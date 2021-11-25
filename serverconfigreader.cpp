#include "serverconfigreader.h"
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>

ServerConfigReader::ServerConfigReader(QString scriptPath)
{
    QFileInfo fi(scriptPath);
    QString ext = fi.completeSuffix();
    QString pathPart = fi.path();
    if(!pathPart.endsWith(QDir::separator()))
        pathPart += QDir::separator();
    QString jsonPath = scriptPath.replace(scriptPath.length() - ext.length(), ext.length(), QString("json"));
    qDebug() << "Json config path is" << jsonPath;
    QFile jfile(jsonPath);
    if(jfile.open(QIODevice::ReadOnly))
    {
        QByteArray cfgData = jfile.readAll();
        QJsonDocument jdoc(QJsonDocument::fromJson(cfgData));
        if(jdoc.object().contains("allowedIPs"))
        {
            QVariantList vlist = jdoc.object().value("allowedIPs").toArray().toVariantList();
            foreach (QVariant v, vlist)
            {
                allowedIPs.append(v.toString());
                qDebug() << v.toString();
            }
        }
        if(jdoc.object().contains("exchangeLogPrefix"))
        {
            QString prefix = jdoc.object().value("exchangeLogPrefix").toString();
            logPathPrefix = pathPart+prefix;
        }
        if(jdoc.object().contains("host"))
        {
            QString hname = jdoc.object().value("host").toString().toLower();
            if(hname == "local" || hname == "localhost")
                host = QHostAddress(QHostAddress::LocalHost);
            else if(hname == "any")
                host = QHostAddress(QHostAddress::Any);
            else if(hname == "anyipv4")
                host = QHostAddress(QHostAddress::AnyIPv4);
            else if(hname == "anyipv6")
                host = QHostAddress(QHostAddress::AnyIPv6);
            else
                host = QHostAddress(hname);
        }
        else
            host = QHostAddress(QHostAddress::Any);
        if(jdoc.object().contains("port"))
            port = jdoc.object().value("port").toInt(0);
        else
            port = 0;
    }
}
