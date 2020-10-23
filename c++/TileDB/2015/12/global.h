/**
 * @file   global.h
 * @author Stavros Papadopoulos <stavrosp@csail.mit.edu>
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2015 Stavros Papadopoulos <stavrosp@csail.mit.edu>
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
 * This file contains global definitions. 
 */

#ifndef __GLOBAL_H__
#define __GLOBAL_H__

/** Version */
#define TILEDB_VERSION "0.1"

/* Return codes. */
#define TILEDB_OK                     0
#define TILEDB_ERR                   -1

/* Array modes. */
#define TILEDB_READ                   1
#define TILEDB_READ_REVERSE           2
#define TILEDB_WRITE                  3
#define TILEDB_WRITE_UNSORTED         4

/** Name of the coordinates attribute. */
#define TILEDB_COORDS_NAME "__coords"

/** 
 * The segment size, which is used in some cases as the atomic unit of I/O. 
 */
#define TILEDB_SEGMENT_SIZE 10000000 // ~ 10MB

/** 
 * The segment size used in zlib (compression) operations, takining inot account
 * zlib's maximum expansion factor. 
 */
#define TILEDB_Z_SEGMENT_SIZE \
    TILEDB_SEGMENT_SIZE + 6 + 5*(ceil(TILEDB_SEGMENT_SIZE/16834.0)) 

/** Suffix of a TileDB file. */
#define TILEDB_FILE_SUFFIX ".tdb"

/** The TileDB data types. */ 
enum DataType {
    TILEDB_CHAR, 
    TILEDB_INT32, 
    TILEDB_INT64, 
    TILEDB_FLOAT32, 
    TILEDB_FLOAT64
};

#endif
