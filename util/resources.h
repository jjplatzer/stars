#pragma once

#include <QByteArray>
#include <QString>

namespace stars::util {

QByteArray readCompressedResource(const QString& path,
                                  qsizetype maximumSize = 8 * 1024 * 1024);

}  // namespace stars::util
