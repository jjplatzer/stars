#pragma once
#include <QByteArray>
#include <QJsonObject>

namespace stars {
QJsonObject parseJson(const QByteArray& bytes);
}
