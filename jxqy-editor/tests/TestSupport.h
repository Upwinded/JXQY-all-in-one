#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QString>

#include <cstdint>
#include <vector>

struct MapTileData;

namespace TestSupport
{
template <typename T>
void appendValue(std::vector<uint8_t>& buffer, const T& value)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

bool check(bool condition, const char* message);
bool check(bool condition, const QString& message);

bool writeUtf8TextFile(const QString& filePath, const QString& content);
bool writeRawFile(const QString& filePath, const QByteArray& content);
QByteArray readRawFile(const QString& filePath);
QString readUtf8TextFile(const QString& filePath);

MapTileData makeMarkerTile(uint8_t marker);
std::vector<uint8_t> makeSinglePixelMpc(const QColor& color);
std::vector<uint8_t> makeEmptyMapBuffer(int width, int height);

std::vector<uint8_t> buildMpcFileFromImages(const std::vector<QImage>& frames,
    int directions, int interval, int yMove);
bool writeMpcFileFromImages(const QString& path, const std::vector<QImage>& frames,
    int yMove);
}
