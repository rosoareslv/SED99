/*
 * Copyright 2012-2015 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.springframework.boot.loader;

import java.io.IOException;
import java.net.URL;
import java.net.URLClassLoader;
import java.security.AccessController;
import java.security.PrivilegedExceptionAction;
import java.util.Arrays;
import java.util.Collections;
import java.util.Enumeration;

import org.springframework.boot.loader.jar.Handler;
import org.springframework.boot.loader.jar.JarFile;
import org.springframework.lang.UsesJava7;

/**
 * {@link ClassLoader} used by the {@link Launcher}.
 *
 * @author Phillip Webb
 * @author Dave Syer
 * @author Andy Wilkinson
 */
public class LaunchedURLClassLoader extends URLClassLoader {

	private static LockProvider LOCK_PROVIDER = setupLockProvider();

	private final ClassLoader rootClassLoader;

	/**
	 * Create a new {@link LaunchedURLClassLoader} instance.
	 * @param urls the URLs from which to load classes and resources
	 * @param parent the parent class loader for delegation
	 */
	public LaunchedURLClassLoader(URL[] urls, ClassLoader parent) {
		super(urls, parent);
		this.rootClassLoader = findRootClassLoader(parent);
	}

	private ClassLoader findRootClassLoader(ClassLoader classLoader) {
		while (classLoader != null) {
			if (classLoader.getParent() == null) {
				return classLoader;
			}
			classLoader = classLoader.getParent();
		}
		return null;
	}

	/**
	 * Gets the resource with the given {@code name}. Unlike a standard
	 * {@link ClassLoader}, this method will first search the root class loader. If the
	 * resource is not found, this method will call {@link #findResource(String)}.
	 */
	@Override
	public URL getResource(String name) {
		URL url = null;
		if (this.rootClassLoader != null) {
			url = this.rootClassLoader.getResource(name);
		}
		return (url == null ? findResource(name) : url);
	}

	@Override
	public URL findResource(String name) {
		try {
			if (name.equals("") && hasURLs()) {
				return getURLs()[0];
			}
			Handler.setUseFastConnectionExceptions(true);
			try {
				return super.findResource(name);
			}
			finally {
				Handler.setUseFastConnectionExceptions(false);
			}
		}
		catch (IllegalArgumentException ex) {
			return null;
		}
	}

	@Override
	public Enumeration<URL> findResources(String name) throws IOException {
		if (name.equals("") && hasURLs()) {
			return Collections.enumeration(Arrays.asList(getURLs()));
		}
		Handler.setUseFastConnectionExceptions(true);
		try {
			return super.findResources(name);
		}
		finally {
			Handler.setUseFastConnectionExceptions(false);
		}
	}

	private boolean hasURLs() {
		return getURLs().length > 0;
	}

	/**
	 * Gets the resources with the given {@code name}. Returns a combination of the
	 * resources found by {@link #findResources(String)} and from
	 * {@link ClassLoader#getResources(String) getResources(String)} on the root class
	 * loader, if any.
	 */
	@Override
	public Enumeration<URL> getResources(String name) throws IOException {
		if (this.rootClassLoader == null) {
			return findResources(name);
		}
		return new ResourceEnumeration(this.rootClassLoader.getResources(name),
				findResources(name));
	}

	/**
	 * Attempt to load classes from the URLs before delegating to the parent loader.
	 */
	@Override
	protected Class<?> loadClass(String name, boolean resolve)
			throws ClassNotFoundException {
		synchronized (LaunchedURLClassLoader.LOCK_PROVIDER.getLock(this, name)) {
			Class<?> loadedClass = findLoadedClass(name);
			if (loadedClass == null) {
				Handler.setUseFastConnectionExceptions(true);
				try {
					loadedClass = doLoadClass(name);
				}
				finally {
					Handler.setUseFastConnectionExceptions(false);
				}
			}
			if (resolve) {
				resolveClass(loadedClass);
			}
			return loadedClass;
		}
	}

	private Class<?> doLoadClass(String name) throws ClassNotFoundException {

		// 1) Try the root class loader
		try {
			if (this.rootClassLoader != null) {
				return this.rootClassLoader.loadClass(name);
			}
		}
		catch (Exception ex) {
			// Ignore and continue
		}

		// 2) Try to find locally
		try {
			findPackage(name);
			Class<?> cls = findClass(name);
			return cls;
		}
		catch (Exception ex) {
			// Ignore and continue
		}

		// 3) Use standard loading
		return super.loadClass(name, false);
	}

	private void findPackage(final String name) throws ClassNotFoundException {
		int lastDot = name.lastIndexOf('.');
		if (lastDot != -1) {
			String packageName = name.substring(0, lastDot);
			if (getPackage(packageName) == null) {
				try {
					definePackageForFindClass(name, packageName);
				}
				catch (Exception ex) {
					// Swallow and continue
				}
			}
		}
	}

	/**
	 * Define a package before a {@code findClass} call is made. This is necessary to
	 * ensure that the appropriate manifest for nested JARs associated with the package.
	 * @param name the class name being found
	 * @param packageName the package
	 */
	private void definePackageForFindClass(final String name, final String packageName) {
		try {
			AccessController.doPrivileged(new PrivilegedExceptionAction<Object>() {
				@Override
				public Object run() throws ClassNotFoundException {
					String path = name.replace('.', '/').concat(".class");
					for (URL url : getURLs()) {
						try {
							if (url.getContent() instanceof JarFile) {
								JarFile jarFile = (JarFile) url.getContent();
								// Check the jar entry data before needlessly creating the
								// manifest
								if (jarFile.getJarEntryData(path) != null
										&& jarFile.getManifest() != null) {
									definePackage(packageName, jarFile.getManifest(),
											url);
									return null;
								}

							}
						}
						catch (IOException ex) {
							// Ignore
						}
					}
					return null;
				}
			}, AccessController.getContext());
		}
		catch (java.security.PrivilegedActionException ex) {
			// Ignore
		}
	}

	@UsesJava7
	private static LockProvider setupLockProvider() {
		try {
			ClassLoader.registerAsParallelCapable();
			return new Java7LockProvider();
		}
		catch (NoSuchMethodError ex) {
			return new LockProvider();
		}
	}

	/**
	 * Strategy used to provide the synchronize lock object to use when loading classes.
	 */
	private static class LockProvider {

		public Object getLock(LaunchedURLClassLoader classLoader, String className) {
			return classLoader;
		}

	}

	/**
	 * Java 7 specific {@link LockProvider}.
	 */
	@UsesJava7
	private static class Java7LockProvider extends LockProvider {

		@Override
		public Object getLock(LaunchedURLClassLoader classLoader, String className) {
			return classLoader.getClassLoadingLock(className);
		}

	}

	/**
	 * {@link Enumeration} implementation used for {@code getResources()}.
	 */
	private static class ResourceEnumeration implements Enumeration<URL> {

		private final Enumeration<URL> rootResources;

		private final Enumeration<URL> localResources;

		ResourceEnumeration(Enumeration<URL> rootResources,
				Enumeration<URL> localResources) {
			this.rootResources = rootResources;
			this.localResources = localResources;
		}

		@Override
		public boolean hasMoreElements() {
			try {
				Handler.setUseFastConnectionExceptions(true);
				return this.rootResources.hasMoreElements()
						|| this.localResources.hasMoreElements();
			}
			finally {
				Handler.setUseFastConnectionExceptions(false);
			}
		}

		@Override
		public URL nextElement() {
			if (this.rootResources.hasMoreElements()) {
				return this.rootResources.nextElement();
			}
			return this.localResources.nextElement();
		}

	}

}
