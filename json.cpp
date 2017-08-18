#include "json.h"
#include <QDebug>
#include <QFile>

#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
#include <QJsonDocument>
#include <QMetaProperty>
#include <QVariantHash>
#else
#include <qjson/parser.h>
#include <qjson/qobjecthelper.h>
#include <qjson/serializer.h>
#endif

/* Json helper class
 *
 * Outsources the hard work to QJson
 *
 * Initial author: Floris Bos
 * Maintained by Raspberry Pi
 *
 * See LICENSE.txt for license details
 *
 */

QVariant Json::parse(const QByteArray &json)
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson( json, &error );
    return doc.toVariant();
#else
    QJson::Parser parser;
    bool ok;
    QVariant result = parser.parse (json, &ok);

    if (!ok)
    {
        qDebug() << "Error parsing json";
        qDebug() << "Json input:" << json;
    }

    return result;
#endif
}

QVariant Json::loadFromFile(const QString &filename)
{
    QFile f(filename);
    if (!f.open(f.ReadOnly))
    {
        qDebug() << "Error opening file:" << filename;
        return QVariant();
    }

#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson( f.readAll(), &error );
    f.close();
    return doc.toVariant();
#else
    QJson::Parser parser;
    bool ok;
    QVariant result = parser.parse (&f, &ok);
    f.close();

    if (!ok)
    {
        qDebug() << "Error parsing json file:" << filename;
    }

    return result;
#endif
}

QByteArray Json::serialize(const QVariant &json)
{
#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
    QJsonDocument doc = QJsonDocument::fromVariant( json );
    return doc.toJson();
#else
    QJson::Serializer serializer;
    bool ok;

    serializer.setIndentMode(QJson::IndentFull);
    QByteArray result = serializer.serialize(json, &ok);

    if (!ok)
    {
        qDebug() << "Error serializing json";
    }

    return result;
#endif
}

void Json::saveToFile(const QString &filename, const QVariant &json)
{
    QFile f(filename);

    if (!f.open(f.WriteOnly))
    {
        qDebug() << "Error opening file for writing: " << filename;
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
    f.write(serialize(json));
#else
    QJson::Serializer serializer;
    bool ok;

    serializer.setIndentMode(QJson::IndentFull);
    serializer.serialize(json, &f, &ok);

    if (!ok)
    {
        qDebug() << "Error serializing json to file:" << filename;
    }
#endif

    f.close();
}
