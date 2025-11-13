#include "mediabase.h"

YUVStruct::~YUVStruct()
{
    if (data)
    {
        free(data);
    }
}

YUVStruct::YUVStruct(const char* data, const int32_t size, const int32_t width, const int32_t height)
    : size(size), width(width), height(height)
{
    this->data = static_cast<char*>(malloc(size));
    memcpy(this->data, data, size);
}

YUVStruct::YUVStruct(const int32_t size, const int32_t width, const int32_t height)
    : size(size), width(width), height(height)
{
    this->data = static_cast<char*>(malloc(size));
}

YUV420p::YUV420p(const int32_t size, const int32_t width, const int height) : YUVStruct(size, width, height)
{
    const int frame = width * height;
    Y = data;
    U = data + frame;
    V = data + frame * 5 / 4;
}

YUV420p::YUV420p(char* data, const int32_t size, const int32_t width, const int height) : YUVStruct(
    data, size, width, height)
{
    const int frame = width * height;
    Y = data;
    U = data + frame;
    V = data + frame * 5 / 4;
}

YUV420p::~YUV420p() = default;

NaluStruct::NaluStruct(const int size)
{
    this->size = size;
    type = 0;
    data = static_cast<unsigned char*>(malloc(size * sizeof(char)));
}

NaluStruct::NaluStruct(const unsigned char* buf, int bufLen) : pts(0)
{
    this->size = bufLen;
    type = buf[4] & 0x1f;
    data = static_cast<unsigned char*>(malloc(bufLen * sizeof(char)));
    memcpy(data, buf, bufLen);
}

NaluStruct::~NaluStruct()
{
    if (data)
    {
        free(data);
        data = nullptr;
    }
}
