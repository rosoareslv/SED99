#include <IO/ReadBuffer.h>
#include <IO/WriteBuffer.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <IO/ReadBufferFromString.h>
#include <IO/WriteBufferFromString.h>
#include <Core/FieldVisitors.h>
#include <Common/SipHash.h>


namespace DB
{


template <typename T>
static inline String formatQuoted(T x)
{
    WriteBufferFromOwnString wb;
    writeQuoted(x, wb);
    return wb.str();
}

template <typename T>
static inline String formatQuotedWithPrefix(T x, const char * prefix)
{
    WriteBufferFromOwnString wb;
    wb.write(prefix, strlen(prefix));
    writeQuoted(x, wb);
    return wb.str();
}


String FieldVisitorDump::operator() (const Null & x) const { return "NULL"; }
String FieldVisitorDump::operator() (const UInt64 & x) const { return formatQuotedWithPrefix(x, "UInt64_"); }
String FieldVisitorDump::operator() (const Int64 & x) const { return formatQuotedWithPrefix(x, "Int64_"); }
String FieldVisitorDump::operator() (const Float64 & x) const { return formatQuotedWithPrefix(x, "Float64_"); }


String FieldVisitorDump::operator() (const String & x) const
{
    WriteBufferFromOwnString wb;
    writeQuoted(x, wb);
    return wb.str();
}

String FieldVisitorDump::operator() (const Array & x) const
{
    WriteBufferFromOwnString wb;

    wb.write("Array_[", 7);
    for (auto it = x.begin(); it != x.end(); ++it)
    {
        if (it != x.begin())
            wb.write(", ", 2);
        writeString(applyVisitor(*this, *it), wb);
    }
    writeChar(']', wb);

    return wb.str();
}

String FieldVisitorDump::operator() (const Tuple & x_def) const
{
    auto & x = x_def.t;
    WriteBufferFromOwnString wb;

    wb.write("Tuple_(", 7);
    for (auto it = x.begin(); it != x.end(); ++it)
    {
        if (it != x.begin())
            wb.write(", ", 2);
        writeString(applyVisitor(*this, *it), wb);
    }
    writeChar(')', wb);

    return wb.str();
}


/** In contrast to writeFloatText (and writeQuoted),
  *  even if number looks like integer after formatting, prints decimal point nevertheless (for example, Float64(1) is printed as 1.).
  * - because resulting text must be able to be parsed back as Float64 by query parser (otherwise it will be parsed as integer).
  *
  * Trailing zeros after decimal point are omitted.
  *
  * NOTE: Roundtrip may lead to loss of precision.
  */
static String formatFloat(const Float64 x)
{
    DoubleConverter<true>::BufferType buffer;
    double_conversion::StringBuilder builder{buffer, sizeof(buffer)};

    const auto result = DoubleConverter<true>::instance().ToShortest(x, &builder);

    if (!result)
        throw Exception("Cannot print float or double number", ErrorCodes::CANNOT_PRINT_FLOAT_OR_DOUBLE_NUMBER);

    return { buffer, buffer + builder.position() };
}


String FieldVisitorToString::operator() (const Null & x) const { return "NULL"; }
String FieldVisitorToString::operator() (const UInt64 & x) const { return formatQuoted(x); }
String FieldVisitorToString::operator() (const Int64 & x) const { return formatQuoted(x); }
String FieldVisitorToString::operator() (const Float64 & x) const { return formatFloat(x); }
String FieldVisitorToString::operator() (const String & x) const { return formatQuoted(x); }


String FieldVisitorToString::operator() (const Array & x) const
{
    WriteBufferFromOwnString wb;

    writeChar('[', wb);
    for (Array::const_iterator it = x.begin(); it != x.end(); ++it)
    {
        if (it != x.begin())
            wb.write(", ", 2);
        writeString(applyVisitor(*this, *it), wb);
    }
    writeChar(']', wb);

    return wb.str();
}

String FieldVisitorToString::operator() (const Tuple & x_def) const
{
    auto & x = x_def.t;
    WriteBufferFromOwnString wb;

    writeChar('(', wb);
    for (auto it = x.begin(); it != x.end(); ++it)
    {
        if (it != x.begin())
            wb.write(", ", 2);
        writeString(applyVisitor(*this, *it), wb);
    }
    writeChar(')', wb);

    return wb.str();
}


FieldVisitorHash::FieldVisitorHash(SipHash & hash) : hash(hash) {}

void FieldVisitorHash::operator() (const Null & x) const
{
    UInt8 type = Field::Types::Null;
    hash.update(reinterpret_cast<const char *>(&type), sizeof(type));
}

void FieldVisitorHash::operator() (const UInt64 & x) const
{
    UInt8 type = Field::Types::UInt64;
    hash.update(reinterpret_cast<const char *>(&type), sizeof(type));
    hash.update(reinterpret_cast<const char *>(&x), sizeof(x));
}

void FieldVisitorHash::operator() (const Int64 & x) const
{
    UInt8 type = Field::Types::Int64;
    hash.update(reinterpret_cast<const char *>(&type), sizeof(type));
    hash.update(reinterpret_cast<const char *>(&x), sizeof(x));
}

void FieldVisitorHash::operator() (const Float64 & x) const
{
    UInt8 type = Field::Types::Float64;
    hash.update(reinterpret_cast<const char *>(&type), sizeof(type));
    hash.update(reinterpret_cast<const char *>(&x), sizeof(x));
}

void FieldVisitorHash::operator() (const String & x) const
{
    UInt8 type = Field::Types::String;
    hash.update(reinterpret_cast<const char *>(&type), sizeof(type));
    size_t size = x.size();
    hash.update(reinterpret_cast<const char *>(&size), sizeof(size));
    hash.update(x.data(), x.size());
}

void FieldVisitorHash::operator() (const Array & x) const
{
    UInt8 type = Field::Types::Array;
    hash.update(reinterpret_cast<const char *>(&type), sizeof(type));
    size_t size = x.size();
    hash.update(reinterpret_cast<const char *>(&size), sizeof(size));

    for (const auto & elem : x)
        applyVisitor(*this, elem);
}

}
