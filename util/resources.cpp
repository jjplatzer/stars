#include "resources.h"

#include <zstd.h>

#include <QFile>
#include <stdexcept>

// Keep the resource object linked when this utility is used from a static
// library, including standalone STARS tests without the profile UI.
static void initializeResources() { Q_INIT_RESOURCE(common_resources); }

namespace stars::util {

QByteArray readCompressedResource(const QString& path, qsizetype maximumSize) {
  static const bool initialized = [] {
    initializeResources();
    return true;
  }();
  (void)initialized;
  const auto fail = [&path](const char* message) -> void {
    throw std::runtime_error(QStringLiteral("%1: %2")
                                 .arg(path, QString::fromUtf8(message))
                                 .toStdString());
  };
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    fail("Cannot open resource");
  }
  const auto compressed = file.readAll();
  const auto size =
      ZSTD_getFrameContentSize(compressed.constData(), compressed.size());
  if (maximumSize <= 0 || size == ZSTD_CONTENTSIZE_ERROR ||
      size == ZSTD_CONTENTSIZE_UNKNOWN ||
      size > static_cast<unsigned long long>(maximumSize)) {
    fail("Invalid compressed resource size");
  }
  QByteArray data(static_cast<qsizetype>(size), Qt::Uninitialized);
  const auto decoded = ZSTD_decompress(
      data.data(), data.size(), compressed.constData(), compressed.size());
  if (ZSTD_isError(decoded) || decoded != size) {
    fail("Cannot decompress resource");
  }
  return data;
}

}  // namespace stars::util
