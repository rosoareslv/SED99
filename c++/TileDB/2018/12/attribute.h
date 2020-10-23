/**
 * @file   tiledb_cpp_api_attribute.h
 *
 * @author Ravi Gaddipati
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2017-2018 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file declares the C++ API for the TileDB Attribute object.
 */

#ifndef TILEDB_CPP_API_ATTRIBUTE_H
#define TILEDB_CPP_API_ATTRIBUTE_H

#include "compressor.h"
#include "context.h"
#include "deleter.h"
#include "exception.h"
#include "filter_list.h"
#include "object.h"
#include "tiledb.h"
#include "type.h"

#include <array>
#include <functional>
#include <memory>
#include <type_traits>

namespace tiledb {

/**
 * Describes an attribute of an Array cell.
 *
 * @details
 * An attribute specifies a name and datatype for a particular value in each
 * array cell. There are 3 supported attribute types:
 *
 * - Fundamental types, such as `char`, `int`, `double`, `uint64_t`, etc..
 * - Fixed sized arrays: `T[N]` or `std::array<T, N>`, where T is a fundamental
 * type
 * - Variable length data: `std::string`, `std::vector<T>` where T is a
 * fundamental type
 *
 * Fixed-size array types using POD types like `std::array<T, N>` are internally
 * converted to byte-array attributes. E.g. an attribute of type
 * `std::array<float, 3>` will be created as an attribute of type `TILEDB_CHAR`
 * with cell_val_num `sizeof(std::array<float, 3>)`.
 *
 * Therefore, for fixed-length attributes it is recommended to use C-style
 * arrays instead, e.g. `float[3]` instead of `std::array<float, 3>`.
 *
 * **Example:**
 *
 * @code{.cpp}
 * tiledb::Context ctx;
 * auto a1 = tiledb::Attribute::create<int>(ctx, "a1");
 * auto a2 = tiledb::Attribute::create<std::string>(ctx, "a2");
 * auto a3 = tiledb::Attribute::create<float[3]>(ctx, "a3");
 *
 * // Change compression scheme
 * tiledb::FilterList filters(ctx);
 * filters.add_filter({ctx, TILEDB_FILTER_BZIP2});
 * a1.set_filter_list(filters);
 *
 * // Add attributes to a schema
 * tiledb::ArraySchema schema(ctx, TILEDB_DENSE);
 * schema.add_attributes(a1, a2, a3);
 * @endcode
 */
class Attribute {
 public:
  /* ********************************* */
  /*     CONSTRUCTORS & DESTRUCTORS    */
  /* ********************************* */

  Attribute(const Context& ctx, tiledb_attribute_t* attr)
      : ctx_(ctx) {
    attr_ = std::shared_ptr<tiledb_attribute_t>(attr, deleter_);
  }

  /**
   * Construct an attribute with a name and enumerated type. `cell_val_num` will
   * be set to 1.
   *
   * @param ctx TileDB context
   * @param name Name of attribute
   * @param type Enumerated type of attribute
   */
  Attribute(const Context& ctx, const std::string& name, tiledb_datatype_t type)
      : ctx_(ctx) {
    init_from_type(name, type);
  }

  /**
   * Construct an attribute with an enumerated type and given compressor.
   *
   * @note This constructor is deprecated and will be removed in a future
   *    version. The filter API should be used instead.
   */
  TILEDB_DEPRECATED Attribute(
      const Context& ctx,
      const std::string& name,
      tiledb_datatype_t type,
      const Compressor& compressor)
      : ctx_(ctx) {
    init_from_type(name, type);

    FilterList filter_list(ctx);
    Filter filter(ctx, Compressor::to_filter(compressor.compressor()));
    int32_t level = compressor.level();
    filter.set_option(TILEDB_COMPRESSION_LEVEL, &level);
    filter_list.add_filter(filter);
    set_filter_list(filter_list);
  }

  /** Construct an attribute with an enumerated type and given filter list. */
  Attribute(
      const Context& ctx,
      const std::string& name,
      tiledb_datatype_t type,
      const FilterList& filter_list)
      : ctx_(ctx) {
    init_from_type(name, type);
    set_filter_list(filter_list);
  }

  Attribute(const Attribute&) = default;
  Attribute(Attribute&&) = default;
  Attribute& operator=(const Attribute&) = default;
  Attribute& operator=(Attribute&&) = default;

  /* ********************************* */
  /*                API                */
  /* ********************************* */

  /** Returns the name of the attribute. */
  std::string name() const {
    auto& ctx = ctx_.get();
    const char* name;
    ctx.handle_error(tiledb_attribute_get_name(ctx, attr_.get(), &name));
    return name;
  }

  /** Returns the attribute datatype. */
  tiledb_datatype_t type() const {
    auto& ctx = ctx_.get();
    tiledb_datatype_t type;
    ctx.handle_error(tiledb_attribute_get_type(ctx, attr_.get(), &type));
    return type;
  }

  /**
   * Returns the size (in bytes) of one cell on this attribute. For
   * variable-sized attributes returns TILEDB_VAR_NUM.
   *
   * **Example:**
   * @code{.cpp}
   * tiledb::Context ctx;
   * auto a1 = tiledb::Attribute::create<int>(ctx, "a1");
   * auto a2 = tiledb::Attribute::create<std::string>(ctx, "a2");
   * auto a3 = tiledb::Attribute::create<float[3]>(ctx, "a3");
   * auto a4 = tiledb::Attribute::create<std::array<float, 3>>(ctx, "a4");
   * a1.cell_size();    // Returns sizeof(int)
   * a2.cell_size();    // Variable sized attribute, returns TILEDB_VAR_NUM
   * a3.cell_size();    // Returns 3 * sizeof(float)
   * a4.cell_size();    // Stored as byte array, returns sizeof(char).
   * @endcode
   */
  uint64_t cell_size() const {
    auto& ctx = ctx_.get();
    uint64_t cell_size;
    ctx.handle_error(
        tiledb_attribute_get_cell_size(ctx, attr_.get(), &cell_size));
    return cell_size;
  }

  /**
   * Returns number of values of one cell on this attribute. For variable-sized
   * attributes returns TILEDB_VAR_NUM.
   *
   * **Example:**
   * @code{.cpp}
   * tiledb::Context ctx;
   * auto a1 = tiledb::Attribute::create<int>(ctx, "a1");
   * auto a2 = tiledb::Attribute::create<std::string>(ctx, "a2");
   * auto a3 = tiledb::Attribute::create<float[3]>(ctx, "a3");
   * auto a4 = tiledb::Attribute::create<std::array<float, 3>>(ctx, "a4");
   * a1.cell_val_num();   // Returns 1
   * a2.cell_val_num();   // Variable sized attribute, returns TILEDB_VAR_NUM
   * a3.cell_val_num();   // Returns 3
   * a4.cell_val_num();   // Stored as byte array, returns
   *                         sizeof(std::array<float, 3>).
   * @endcode
   */
  unsigned cell_val_num() const {
    auto& ctx = ctx_.get();
    unsigned num;
    ctx.handle_error(tiledb_attribute_get_cell_val_num(ctx, attr_.get(), &num));
    return num;
  }

  /**
   * Sets the number of attribute values per cell. This is inferred from
   * the type parameter of the `Attribute::create<T>()` function, but can also
   * be set manually.
   *
   * **Example:**
   * @code{.cpp}
   * // a1 and a2 are equivalent:
   * auto a1 = Attribute::create<std::vector<int>>(...);
   * auto a2 = Attribute::create<int>(...);
   * a2.set_cell_val_num(TILEDB_VAR_NUM);
   * @endcode
   *
   * @param num Cell val number to set.
   * @return Reference to this Attribute
   */
  Attribute& set_cell_val_num(unsigned num) {
    auto& ctx = ctx_.get();
    ctx.handle_error(tiledb_attribute_set_cell_val_num(ctx, attr_.get(), num));
    return *this;
  }

  /** Check if attribute is variable sized. **/
  bool variable_sized() const {
    return cell_val_num() == TILEDB_VAR_NUM;
  }

  /**
   * Returns a copy of the attribute compressor. To change the
   * attribute compressor, use `Attribute::set_compressor()`.
   *
   * @note This function is deprecated and will be removed in a future version.
   *       The filter API should be used instead.
   */
  TILEDB_DEPRECATED Compressor compressor() const {
    FilterList filters = filter_list();
    for (uint32_t i = 0; i < filters.nfilters(); i++) {
      auto f = filters.filter(i);
      int32_t level;
      switch (f.filter_type()) {
        case TILEDB_FILTER_GZIP:
          f.get_option(TILEDB_COMPRESSION_LEVEL, &level);
          return {TILEDB_GZIP, level};
        case TILEDB_FILTER_ZSTD:
          f.get_option(TILEDB_COMPRESSION_LEVEL, &level);
          return {TILEDB_ZSTD, level};
        case TILEDB_FILTER_LZ4:
          f.get_option(TILEDB_COMPRESSION_LEVEL, &level);
          return {TILEDB_LZ4, level};
        case TILEDB_FILTER_RLE:
          f.get_option(TILEDB_COMPRESSION_LEVEL, &level);
          return {TILEDB_RLE, level};
        case TILEDB_FILTER_BZIP2:
          f.get_option(TILEDB_COMPRESSION_LEVEL, &level);
          return {TILEDB_BZIP2, level};
        case TILEDB_FILTER_DOUBLE_DELTA:
          f.get_option(TILEDB_COMPRESSION_LEVEL, &level);
          return {TILEDB_DOUBLE_DELTA, level};
        default:
          continue;
      }
    }
    return {TILEDB_NO_COMPRESSION, -1};
  }

  /**
   * Sets the attribute compressor.
   *
   * @param c Compressor to set
   * @return Reference to this Attribute
   *
   * @note This function is deprecated and will be removed in a future version.
   *       The filter API should be used instead.
   */
  TILEDB_DEPRECATED Attribute& set_compressor(Compressor c) {
    if (filter_list().nfilters() > 0)
      throw TileDBError(
          "[TileDB::C++API] Error: Cannot add second filter with "
          "deprecated API.");

    auto& ctx = ctx_.get();
    FilterList filter_list(ctx);
    Filter filter(ctx, Compressor::to_filter(c.compressor()));
    int32_t level = c.level();
    filter.set_option(TILEDB_COMPRESSION_LEVEL, &level);
    filter_list.add_filter(filter);
    set_filter_list(filter_list);
    return *this;
  }

  /**
   * Returns a copy of the FilterList of the attribute.
   * To change the filter list, use `set_filter_list()`.
   *
   * @return Copy of the attribute FilterList.
   */
  FilterList filter_list() const {
    auto& ctx = ctx_.get();
    tiledb_filter_list_t* filter_list;
    ctx.handle_error(
        tiledb_attribute_get_filter_list(ctx, attr_.get(), &filter_list));
    return FilterList(ctx, filter_list);
  }

  /**
   * Sets the attribute filter list, which is an ordered list of filters that
   * will be used to process and/or transform the attribute data (such as
   * compression).
   *
   * @param filter_list Filter list to set
   * @return Reference to this Attribute
   */
  Attribute& set_filter_list(const FilterList& filter_list) {
    auto& ctx = ctx_.get();
    ctx.handle_error(
        tiledb_attribute_set_filter_list(ctx, attr_.get(), filter_list));
    return *this;
  }

  /** Returns the C TileDB attribute object pointer. */
  std::shared_ptr<tiledb_attribute_t> ptr() const {
    return attr_;
  }

  /** Auxiliary operator for getting the underlying C TileDB object. */
  operator tiledb_attribute_t*() const {
    return attr_.get();
  }

  /**
   * Dumps information about the attribute in an ASCII representation to an
   * output.
   *
   * @param out (Optional) File to dump output to. Defaults to `stdout`.
   */
  void dump(FILE* out = stdout) const {
    ctx_.get().handle_error(
        tiledb_attribute_dump(ctx_.get(), attr_.get(), out));
  }

  /* ********************************* */
  /*          STATIC FUNCTIONS         */
  /* ********************************* */

  /**
   * Factory function for creating a new attribute with datatype T.
   *
   * **Example:**
   * @code{.cpp}
   * tiledb::Context ctx;
   * auto a1 = tiledb::Attribute::create<int>(ctx, "a1");
   * auto a2 = tiledb::Attribute::create<std::string>(ctx, "a2");
   * auto a3 = tiledb::Attribute::create<std::array<float, 3>>(ctx, "a3");
   * auto a4 = tiledb::Attribute::create<std::vector<double>>(ctx, "a4");
   * auto a5 = tiledb::Attribute::create<char[8]>(ctx, "a5");
   * @endcode
   *
   * @tparam T Datatype of the attribute. Can either be arithmetic type,
   *         C-style array, std::string, std::vector, or any trivially
   *         copyable classes (defined by std::is_trivially_copyable).
   * @param ctx The TileDB context.
   * @param name The attribute name.
   * @return A new Attribute object.
   */
  template <typename T>
  static Attribute create(const Context& ctx, const std::string& name) {
    using DataT = typename impl::TypeHandler<T>;
    Attribute a(ctx, name, DataT::tiledb_type);
    a.set_cell_val_num(DataT::tiledb_num);
    return a;
  }

  /**
   * Factory function for creating a new attribute with datatype T and
   * a Compressor.
   *
   * **Example:**
   * @code{.cpp}
   * tiledb::Context ctx;
   * auto a1 = tiledb::Attribute::create<int>(ctx, "a1", {TILEDB_BZIP2, -1});
   * @endcode
   *
   * @tparam T Datatype of the attribute. Can either be arithmetic type,
   *         C-style array, `std::string`, `std::vector`, or any trivially
   *         copyable classes (defined by `std::is_trivially_copyable`).
   * @param ctx The TileDB context.
   * @param name The attribute name.
   * @param compressor Compressor to use for attribute
   * @return A new Attribute object.
   *
   * @note This function is deprecated and will be removed in a future version.
   *       The filter API should be used instead.
   */
  template <typename T>
  TILEDB_DEPRECATED static Attribute create(
      const Context& ctx,
      const std::string& name,
      const Compressor& compressor) {
    FilterList filter_list(ctx);
    Filter filter(ctx, Compressor::to_filter(compressor.compressor()));
    int32_t level = compressor.level();
    filter.set_option(TILEDB_COMPRESSION_LEVEL, &level);
    filter_list.add_filter(filter);

    auto a = create<T>(ctx, name);
    a.set_filter_list(filter_list);
    return a;
  }

  /**
   * Factory function for creating a new attribute with datatype T and
   * a FilterList.
   *
   * **Example:**
   * @code{.cpp}
   * tiledb::Context ctx;
   * tiledb::FilterList filter_list(ctx);
   * filter_list.add_filter({ctx, TILEDB_FILTER_BYTESHUFFLE})
   *     .add_filter({ctx, TILEDB_FILTER_BZIP2});
   * auto a1 = tiledb::Attribute::create<int>(ctx, "a1", filter_list);
   * @endcode
   *
   * @tparam T Datatype of the attribute. Can either be arithmetic type,
   *         C-style array, `std::string`, `std::vector`, or any trivially
   *         copyable classes (defined by `std::is_trivially_copyable`).
   * @param ctx The TileDB context.
   * @param name The attribute name.
   * @param filter_list FilterList to use for attribute
   * @return A new Attribute object.
   */
  template <typename T>
  static Attribute create(
      const Context& ctx,
      const std::string& name,
      const FilterList& filter_list) {
    auto a = create<T>(ctx, name);
    a.set_filter_list(filter_list);
    return a;
  }

 private:
  /* ********************************* */
  /*         PRIVATE ATTRIBUTES        */
  /* ********************************* */

  /** The TileDB context. */
  std::reference_wrapper<const Context> ctx_;

  /** An auxiliary deleter. */
  impl::Deleter deleter_;

  /** The pointer to the C TileDB attribute object. */
  std::shared_ptr<tiledb_attribute_t> attr_;

  /* ********************************* */
  /*         PRIVATE FUNCTIONS         */
  /* ********************************* */

  void init_from_type(const std::string& name, tiledb_datatype_t type) {
    tiledb_attribute_t* attr;
    auto& ctx = ctx_.get();
    ctx.handle_error(tiledb_attribute_alloc(ctx, name.c_str(), type, &attr));
    attr_ = std::shared_ptr<tiledb_attribute_t>(attr, deleter_);
  }
};

/* ********************************* */
/*               MISC                */
/* ********************************* */

/** Gets a string representation of an attribute for an output stream. */
inline std::ostream& operator<<(std::ostream& os, const Attribute& a) {
  os << "Attr<" << a.name() << ',' << tiledb::impl::to_str(a.type()) << ','
     << (a.cell_val_num() == TILEDB_VAR_NUM ? "VAR" :
                                              std::to_string(a.cell_val_num()))
     << '>';
  return os;
}

}  // namespace tiledb

#endif  // TILEDB_CPP_API_ATTRIBUTE_H
