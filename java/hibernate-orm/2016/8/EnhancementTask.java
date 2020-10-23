/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.tool.enhance;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import javassist.ClassPool;
import javassist.CtClass;

import org.hibernate.bytecode.enhance.spi.DefaultEnhancementContext;
import org.hibernate.bytecode.enhance.spi.Enhancer;

import org.apache.tools.ant.BuildException;
import org.apache.tools.ant.DirectoryScanner;
import org.apache.tools.ant.Project;
import org.apache.tools.ant.Task;
import org.apache.tools.ant.types.FileSet;

/**
 * Ant task for performing build-time enhancement of entities and component/embeddable classes.
 * <p/>
 * IMPL NOTE : currently makes numerous assumptions, the most "horrific" being that all entities are
 * annotated @Entity which precludes {@code hbm.xml} mappings as well as complete {@code orm.xml} mappings.  This is
 * just a PoC though...
 *
 * @author Steve Ebersole
 * @see org.hibernate.engine.spi.Managed
 */
public class EnhancementTask extends Task {
	private List<FileSet> filesets = new ArrayList<FileSet>();

	// Enhancer also builds CtClass instances.  Might make sense to share these (ClassPool).
	private final ClassPool classPool = new ClassPool( false );
	private final Enhancer enhancer = new Enhancer( new DefaultEnhancementContext() );

	public void addFileset(FileSet set) {
		this.filesets.add( set );
	}

	@Override
	public void execute() throws BuildException {
		log( "Starting Hibernate EnhancementTask execution", Project.MSG_INFO );

		// we use the CtClass stuff here just as a simple vehicle for obtaining low level information about
		// the class(es) contained in a file while still maintaining easy access to the underlying byte[]
		final Project project = getProject();

		for ( FileSet fileSet : filesets ) {
			final File fileSetBaseDir = fileSet.getDir( project );
			final DirectoryScanner directoryScanner = fileSet.getDirectoryScanner( project );
			for ( String relativeIncludedFileName : directoryScanner.getIncludedFiles() ) {
				final File javaClassFile = new File( fileSetBaseDir, relativeIncludedFileName );
				if ( !javaClassFile.exists() ) {
					continue;
				}

				processClassFile( javaClassFile );
			}
		}

	}

	private void processClassFile(File javaClassFile) {
		try {
			final CtClass ctClass = classPool.makeClass( new FileInputStream( javaClassFile ) );
			byte[] result = enhancer.enhance( ctClass.getName(), ctClass.toBytecode() );
			if ( result != null ) {
				writeEnhancedClass( javaClassFile, result );
			}
		}
		catch (Exception e) {
			log( "Unable to enhance class file [" + javaClassFile.getAbsolutePath() + "]", e, Project.MSG_WARN );
		}
	}

	private void writeEnhancedClass(File javaClassFile, byte[] result) {
		try {
			if ( javaClassFile.delete() ) {
				if ( !javaClassFile.createNewFile() ) {
					log( "Unable to recreate class file [" + javaClassFile.getName() + "]", Project.MSG_INFO );
				}
			}
			else {
				log( "Unable to delete class file [" + javaClassFile.getName() + "]", Project.MSG_INFO );
			}

			FileOutputStream outputStream = new FileOutputStream( javaClassFile, false );
			try {
				outputStream.write( result );
				outputStream.flush();
			}
			finally {
				try {
					outputStream.close();
				}
				catch (IOException ignore) {
				}
			}
		}
		catch (FileNotFoundException ignore) {
			// should not ever happen because of explicit checks
		}
		catch (IOException e) {
			throw new BuildException(
					String.format( "Error processing included file [%s]", javaClassFile.getAbsolutePath() ), e
			);
		}
	}

}
