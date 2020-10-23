/*
 * Copyright 2014 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.realm.internal;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.Serializable;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.util.Locale;

/**
 * Utility methods for Realm Core.
 */
public class RealmCore {

///*
    private static final String FILE_SEP = File.separator;
    private static final String PATH_SEP = File.pathSeparator;          // On Windows ";"
    private static final String BINARIES_PATH = "lib" + PATH_SEP + ".." + FILE_SEP + "lib";
    private static final String JAVA_LIBRARY_PATH = "java.library.path";
//*/

    private static volatile boolean libraryIsLoaded = false;

/*
    private static String getJniFileName()
    {
        String os = System.getProperty("os.name").toLowerCase();
        if (os.indexOf("win") >= 0)
            return "realm_jni32.dll or realm_jni64.dll";
        if (os.indexOf("mac") >= 0)
            return "librealm-jni.jnilib";
        if (os.indexOf("nix") >= 0 || os.indexOf("nux") >= 0 || os.indexOf("sunos") >= 0)
            return "librealm-jni.so";
        return "realm-jni";
    }
*/

    public static boolean osIsWindows()
    {
        String os = System.getProperty("os.name").toLowerCase(Locale.getDefault());
        return (os.contains("win"));
    }

    public static byte[] serialize(Serializable value) {
        try {
            ByteArrayOutputStream mem = new ByteArrayOutputStream();
            ObjectOutputStream output = new ObjectOutputStream(mem);
            output.writeObject(value);
            output.close();
            return mem.toByteArray();
        } catch (Exception e) {
            throw new RuntimeException("Cannot serialize the object!", e);
        }
    }

    public static Serializable deserialize(ByteBuffer buf) {
        return deserialize(buf.array());
    }

    public static Serializable deserialize(byte[] value) {
        try {
            ByteArrayInputStream mem = new ByteArrayInputStream(value);
            ObjectInputStream output = new ObjectInputStream(mem);
            Object obj = output.readObject();
            output.close();
            return (Serializable) obj;
        } catch (Exception e) {
            throw new RuntimeException("Cannot deserialize the object!", e);
        }
    }
/*
    public static void print(String caption, AbstractCursor<?> cursor) {
        System.out.println(caption + ": " + cursor);
    }
*/

    // Although loadLibrary is synchronized internally from AOSP 4.3, for the compatibility reason,
    // KEEP synchronized here for the old devices!
    public static synchronized void loadLibrary() {
        if (libraryIsLoaded) {
            // The java native should ensure only load the lib once, but we met some problems before.
            // So keep the flag.
            return;
        }

        if (osIsWindows()) {
            loadLibraryWindows();
        }
        else {
            String jnilib;
            String debug = System.getenv("REALM_JAVA_DEBUG");
            if (debug == null || debug.isEmpty()) {
                jnilib = "realm-jni";
            }
            else {
                jnilib = "realm-jni-dbg";
            }
            System.loadLibrary(jnilib);
        }
        libraryIsLoaded = true;

        Version.coreLibVersionCompatible(true);
    }

    private static String loadLibraryWindows() {
///*
        try {
            addNativeLibraryPath(BINARIES_PATH);
            resetLibraryPath();
        }
        catch (Throwable e) {
            // Above can't be used on Android.
        }
//*/
        // Load debug library first - if available
        String jnilib;
        jnilib = loadCorrectLibrary("realm_jni32d", "realm_jni64d");
        if (jnilib != null) {
            System.out.println("!!! Realm debug version loaded. !!!\n");
        }
        else {
            jnilib = loadCorrectLibrary("realm_jni32", "realm_jni64");
            if (jnilib == null) {
                System.err.println("Searched java.library.path=" + System.getProperty("java.library.path"));
                throw new RuntimeException("Couldn't load the Realm JNI library 'realm_jni32.dll or realm_jni64.dll" +
                                           "'. Please include the directory to the library in java.library.path.");
            }
        }
        return jnilib;
    }

    private static String loadCorrectLibrary(String... libraryCandidateNames) {
        for (String libraryCandidateName : libraryCandidateNames) {
            try {
                System.loadLibrary(libraryCandidateName);
                return libraryCandidateName;
            } catch (Throwable e) {
            }
        }
        return null;
    }

// /*
    public static void addNativeLibraryPath(String path) {
        try {
            String libraryPath = System.getProperty(JAVA_LIBRARY_PATH) + PATH_SEP + path + PATH_SEP;
            System.setProperty(JAVA_LIBRARY_PATH, libraryPath);
        } catch (Exception e) {
            throw new RuntimeException("Cannot set the library path!", e);
        }
    }

    // Hack for having a cross platform location for the lib:
    // The Classloader has a static field (sys_paths) that contains the paths.
    // If that field is set to null, it is initialized automatically.
    // Therefore forcing that field to null will result into the reevaluation of the library path
    // as soon as loadLibrary() is called

    private static void resetLibraryPath() {
        try {
            // reset the library path (a hack)
            Field fieldSysPath = ClassLoader.class.getDeclaredField("sys_paths");
            fieldSysPath.setAccessible(true);
            fieldSysPath.set(null, null);
        } catch (Exception e) {
            throw new RuntimeException("Cannot reset the library path!", e);
        }
    }
// */
}
