/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package jdk.internal.jimage;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.concurrent.ConcurrentHashMap;
import java.util.Map;

/**
 * Factory to get ImageReader
 */
public class ImageReaderFactory {
    private ImageReaderFactory() {}

    private static final String JAVA_HOME = System.getProperty("java.home");
    private static final Path BOOT_MODULES_JIMAGE =
        Paths.get(JAVA_HOME, "lib", "modules", "bootmodules.jimage");

    private static final Map<Path, ImageReader> readers = new ConcurrentHashMap<>();

    /**
     * Returns an {@code ImageReader} to read from the given image file
     */
    public static ImageReader get(Path jimage) throws IOException {
        ImageReader reader = readers.get(jimage);
        if (reader != null) {
            return reader;
        }
        reader = ImageReader.open(jimage.toString());
        // potential race with other threads opening the same URL
        ImageReader r = readers.putIfAbsent(jimage, reader);
        if (r == null) {
            return reader;
        } else {
            reader.close();
            return r;
        }
    }

    /**
     * Returns the {@code ImageReader} to read the image file in this
     * run-time image.
     *
     * @throws UncheckedIOException if an I/O error occurs
     */
    public static ImageReader getImageReader() {
        try {
            return get(BOOT_MODULES_JIMAGE);
        } catch (IOException ioe) {
            throw new UncheckedIOException(ioe);
        }
    }
}
