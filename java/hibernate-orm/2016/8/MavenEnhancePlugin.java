/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.orm.tooling.maven;

import java.io.File;
import java.io.FileFilter;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;

import javassist.ClassPool;
import javassist.CtClass;
import javassist.CtField;

import org.apache.maven.artifact.Artifact;
import org.apache.maven.plugin.AbstractMojo;
import org.apache.maven.plugin.MojoExecutionException;
import org.apache.maven.plugin.MojoFailureException;
import org.apache.maven.plugins.annotations.Execute;
import org.apache.maven.plugins.annotations.LifecyclePhase;
import org.apache.maven.plugins.annotations.Mojo;
import org.apache.maven.plugins.annotations.Parameter;
import org.apache.maven.plugins.annotations.ResolutionScope;
import org.apache.maven.project.MavenProject;

import org.hibernate.bytecode.enhance.spi.DefaultEnhancementContext;
import org.hibernate.bytecode.enhance.spi.EnhancementContext;
import org.hibernate.bytecode.enhance.spi.Enhancer;

/**
 * This plugin will enhance Entity objects.
 *
 * @author Jeremy Whiting
 * @author Luis Barreiro
 */
@Mojo(name = "enhance", defaultPhase = LifecyclePhase.COMPILE, requiresDependencyResolution = ResolutionScope.COMPILE_PLUS_RUNTIME)
@Execute(goal = "enhance", phase = LifecyclePhase.COMPILE)
public class MavenEnhancePlugin extends AbstractMojo {

	/**
	 * The contexts to use during enhancement.
	 */
	private List<File> sourceSet = new ArrayList<File>();

	@Parameter(property = "dir", defaultValue = "${project.build.outputDirectory}")
	private String dir;

	@Parameter(property = "failOnError", defaultValue = "true")
	private boolean failOnError = true;

	@Parameter(property = "enableLazyInitialization", defaultValue = "false")
	private boolean enableLazyInitialization;

	@Parameter(property = "enableDirtyTracking", defaultValue = "false")
	private boolean enableDirtyTracking;

	@Parameter(property = "enableAssociationManagement", defaultValue = "false")
	private boolean enableAssociationManagement;

	@Parameter(property = "enableExtendedEnhancement", defaultValue = "false")
	private boolean enableExtendedEnhancement;

	private boolean shouldApply() {
		return enableLazyInitialization || enableDirtyTracking || enableAssociationManagement || enableExtendedEnhancement;
	}

	public void execute() throws MojoExecutionException, MojoFailureException {
		if ( !shouldApply() ) {
			getLog().warn( "Skipping Hibernate bytecode enhancement plugin execution since no feature is enabled" );
			return;
		}

		// Perform a depth first search for sourceSet
		File root = new File( this.dir );
		if ( !root.exists() ) {
			getLog().info( "Skipping Hibernate enhancement plugin execution since there is no classes dir " + dir );
			return;
		}
		walkDir( root );
		if ( sourceSet.isEmpty() ) {
			getLog().info( "Skipping Hibernate enhancement plugin execution since there are no classes to enhance on " + dir );
			return;
		}

		getLog().info( "Starting Hibernate enhancement for classes on " + dir );
		final ClassLoader classLoader = toClassLoader( Collections.singletonList( root ) );

		EnhancementContext enhancementContext = new DefaultEnhancementContext() {
			@Override
			public ClassLoader getLoadingClassLoader() {
				return classLoader;
			}

			@Override
			public boolean doBiDirectionalAssociationManagement(CtField field) {
				return enableAssociationManagement;
			}

			@Override
			public boolean doDirtyCheckingInline(CtClass classDescriptor) {
				return enableDirtyTracking;
			}

			@Override
			public boolean hasLazyLoadableAttributes(CtClass classDescriptor) {
				return enableLazyInitialization;
			}

			@Override
			public boolean isLazyLoadable(CtField field) {
				return enableLazyInitialization;
			}

			@Override
			public boolean doExtendedEnhancement(CtClass classDescriptor) {
				return enableExtendedEnhancement;
			}
		};

		if ( enableExtendedEnhancement ) {
			getLog().warn( "Extended enhancement is enabled. Classes other than entities may be modified. You should consider access the entities using getter/setter methods and disable this property. Use at your own risk." );
		}

		final Enhancer enhancer = new Enhancer( enhancementContext );
		final ClassPool classPool = new ClassPool( false );

		for ( File file : sourceSet ) {
			final CtClass ctClass = toCtClass( file, classPool );
			if ( ctClass == null ) {
				continue;
			}

			if ( !enableLazyInitialization ) {
				if ( !enhancementContext.isEntityClass( ctClass )
						&& !enhancementContext.isCompositeClass( ctClass )
						&& !enhancementContext.isMappedSuperclassClass( ctClass ) ) {
					getLog().info( "Skipping class file [" + file.getAbsolutePath() + "], not an entity nor embeddable" );
					continue;
				}
			}

			final byte[] enhancedBytecode = doEnhancement( ctClass, enhancer );
			writeOutEnhancedClass( enhancedBytecode, ctClass, file );

			getLog().info( "Successfully enhanced class [" + ctClass.getName() + "]" );
		}
	}

	private ClassLoader toClassLoader(List<File> runtimeClasspath) throws MojoExecutionException {
		List<URL> urls = new ArrayList<URL>();
		for ( File file : runtimeClasspath ) {
			try {
				urls.add( file.toURI().toURL() );
				getLog().debug( "Adding classpath entry for classes root " + file.getAbsolutePath() );
			}
			catch (MalformedURLException e) {
				String msg = "Unable to resolve classpath entry to URL: " + file.getAbsolutePath();
				if ( failOnError ) {
					throw new MojoExecutionException( msg, e );
				}
				getLog().warn( msg );
			}
		}

		// HHH-10145 Add dependencies to classpath as well - all but the ones used for testing purposes
		Set<Artifact> artifacts = null;
		MavenProject project = ( (MavenProject) getPluginContext().get( "project" ) );
		if ( project != null ) {
			// Prefer execution project when available (it includes transient dependencies)
			MavenProject executionProject = project.getExecutionProject();
			artifacts = ( executionProject != null ? executionProject.getArtifacts() : project.getArtifacts() );
		}
		if ( artifacts != null) {
			for ( Artifact a : artifacts ) {
				if ( !Artifact.SCOPE_TEST.equals( a.getScope() ) ) {
					try {
						urls.add( a.getFile().toURI().toURL() );
						getLog().debug( "Adding classpath entry for dependency " + a.getId() );
					}
					catch (MalformedURLException e) {
						String msg = "Unable to resolve URL for dependency " + a.getId() + " at " + a.getFile().getAbsolutePath();
						if ( failOnError ) {
							throw new MojoExecutionException( msg, e );
						}
						getLog().warn( msg );
					}
				}
			}
		}

		return new URLClassLoader( urls.toArray( new URL[urls.size()] ), Enhancer.class.getClassLoader() );
	}

	private CtClass toCtClass(File file, ClassPool classPool) throws MojoExecutionException {
		try {
			final InputStream is = new FileInputStream( file.getAbsolutePath() );

			try {
				return classPool.makeClass( is );
			}
			catch (IOException e) {
				String msg = "Javassist unable to load class in preparation for enhancing: " + file.getAbsolutePath();
				if ( failOnError ) {
					throw new MojoExecutionException( msg, e );
				}
				getLog().warn( msg );
				return null;
			}
			finally {
				try {
					is.close();
				}
				catch (IOException e) {
					getLog().info( "Was unable to close InputStream : " + file.getAbsolutePath(), e );
				}
			}
		}
		catch (FileNotFoundException e) {
			// should never happen, but...
			String msg = "Unable to locate class file for InputStream: " + file.getAbsolutePath();
			if ( failOnError ) {
				throw new MojoExecutionException( msg, e );
			}
			getLog().warn( msg );
			return null;
		}
	}

	private byte[] doEnhancement(CtClass ctClass, Enhancer enhancer) throws MojoExecutionException {
		try {
			return enhancer.enhance( ctClass.getName(), ctClass.toBytecode() );
		}
		catch (Exception e) {
			String msg = "Unable to enhance class: " + ctClass.getName();
			if ( failOnError ) {
				throw new MojoExecutionException( msg, e );
			}
			getLog().warn( msg );
			return null;
		}
	}

	/**
	 * Expects a directory.
	 */
	private void walkDir(File dir) {
		walkDir(
				dir,
				new FileFilter() {
					@Override
					public boolean accept(File pathname) {
						return ( pathname.isFile() && pathname.getName().endsWith( ".class" ) );
					}
				},
				new FileFilter() {
					@Override
					public boolean accept(File pathname) {
						return ( pathname.isDirectory() );
					}
				}
		);
	}

	private void walkDir(File dir, FileFilter classesFilter, FileFilter dirFilter) {
		File[] dirs = dir.listFiles( dirFilter );
		for ( File dir1 : dirs ) {
			walkDir( dir1, classesFilter, dirFilter );
		}
		File[] files = dir.listFiles( classesFilter );
		Collections.addAll( this.sourceSet, files );
	}

	private void writeOutEnhancedClass(byte[] enhancedBytecode, CtClass ctClass, File file) throws MojoExecutionException{
		if ( enhancedBytecode == null ) {
			return;
		}
		try {
			if ( file.delete() ) {
				if ( !file.createNewFile() ) {
					getLog().error( "Unable to recreate class file [" + ctClass.getName() + "]" );
				}
			}
			else {
				getLog().error( "Unable to delete class file [" + ctClass.getName() + "]" );
			}
		}
		catch (IOException e) {
			getLog().warn( "Problem preparing class file for writing out enhancements [" + ctClass.getName() + "]" );
		}

		try {
			FileOutputStream outputStream = new FileOutputStream( file, false );
			try {
				outputStream.write( enhancedBytecode );
				outputStream.flush();
			}
			catch (IOException e) {
				String msg = String.format( "Error writing to enhanced class [%s] to file [%s]", ctClass.getName(), file.getAbsolutePath() );
				if ( failOnError ) {
					throw new MojoExecutionException( msg, e );
				}
				getLog().warn( msg );
			}
			finally {
				try {
					outputStream.close();
					ctClass.detach();
				}
				catch (IOException ignore) {
				}
			}
		}
		catch (FileNotFoundException e) {
			String msg = "Error opening class file for writing: " + file.getAbsolutePath();
			if ( failOnError ) {
				throw new MojoExecutionException( msg, e );
			}
			getLog().warn( msg );
		}
	}
}
