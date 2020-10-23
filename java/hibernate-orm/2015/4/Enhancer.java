/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * Copyright (c) 2012, Red Hat Inc. or third-party contributors as
 * indicated by the @author tags or express copyright attribution
 * statements applied by the authors.  All third-party contributions are
 * distributed under license by Red Hat Inc.
 *
 * This copyrighted material is made available to anyone wishing to use, modify,
 * copy, or redistribute it subject to the terms and conditions of the GNU
 * Lesser General Public License, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this distribution; if not, write to:
 * Free Software Foundation, Inc.
 * 51 Franklin Street, Fifth Floor
 * Boston, MA  02110-1301  USA
 */
package org.hibernate.bytecode.enhance.spi;

import javassist.ClassPool;
import javassist.CtClass;
import javassist.LoaderClassPath;
import org.hibernate.HibernateException;
import org.hibernate.bytecode.enhance.internal.CompositeEnhancer;
import org.hibernate.bytecode.enhance.internal.EntityEnhancer;
import org.hibernate.bytecode.enhance.internal.FieldWriter;
import org.hibernate.engine.spi.EntityEntry;
import org.hibernate.engine.spi.ManagedComposite;
import org.hibernate.engine.spi.ManagedEntity;
import org.hibernate.engine.spi.PersistentAttributeInterceptable;
import org.hibernate.engine.spi.PersistentAttributeInterceptor;
import org.hibernate.internal.CoreLogging;
import org.hibernate.internal.CoreMessageLogger;

import javax.tools.JavaFileObject;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;

/**
 * Class responsible for performing enhancement.
 *
 * @author Steve Ebersole
 * @author Jason Greene
 */
public class Enhancer {
	private static final CoreMessageLogger log = CoreLogging.messageLogger( Enhancer.class );

	protected final EnhancementContext enhancementContext;

	protected final ClassPool classPool;
	protected final CtClass managedEntityCtClass;
	protected final CtClass attributeInterceptorCtClass;
	protected final CtClass attributeInterceptableCtClass;
	protected final CtClass entityEntryCtClass;

	/**
	 * Constructs the Enhancer, using the given context.
	 *
	 * @param enhancementContext Describes the context in which enhancement will occur so as to give access
	 * to contextual/environmental information.
	 */
	public Enhancer(EnhancementContext enhancementContext) {
		try {
			this.enhancementContext = enhancementContext;
			this.classPool = buildClassPool( enhancementContext );

			// add ManagedEntity contract
			this.managedEntityCtClass = loadCtClassFromClass( classPool, ManagedEntity.class );

			// add PersistentAttributeInterceptable contract
			this.attributeInterceptableCtClass = loadCtClassFromClass( classPool, PersistentAttributeInterceptable.class );

			// add PersistentAttributeInterceptor contract
			this.attributeInterceptorCtClass = loadCtClassFromClass( classPool, PersistentAttributeInterceptor.class );

			// add PersistentAttributeInterceptor contract
			this.entityEntryCtClass = loadCtClassFromClass( classPool, EntityEntry.class );
		}
		catch (IOException e) {
			throw new EnhancementException( "Could not prepare Javassist ClassPool", e );
		}
	}

	/**
	 * Performs the enhancement.
	 *
	 * @param className The name of the class whose bytecode is being enhanced.
	 * @param originalBytes The class's original (pre-enhancement) byte code
	 *
	 * @return The enhanced bytecode. Could be the same as the original bytecode if the original was
	 *         already enhanced or we could not enhance it for some reason.
	 *
	 * @throws EnhancementException Indicates a problem performing the enhancement
	 */
	public byte[] enhance(String className, byte[] originalBytes) throws EnhancementException {
		try {
			final CtClass managedCtClass = classPool.makeClassIfNew( new ByteArrayInputStream( originalBytes ) );
			enhance( managedCtClass );
			return getByteCode( managedCtClass );
		}
		catch (IOException e) {
			log.unableToBuildEnhancementMetamodel( className );
			return originalBytes;
		}
	}

	/**
	 * @deprecated Should use enhance(String, byte[]) and a proper EnhancementContext
	 */
	@Deprecated( )
	public byte[] enhanceComposite(String className, byte[] originalBytes) throws EnhancementException {
		return enhance( className, originalBytes );
	}

	/* --- */

	private ClassPool buildClassPool(EnhancementContext enhancementContext) {
		final ClassPool classPool = new ClassPool( false );
		final ClassLoader loadingClassLoader = enhancementContext.getLoadingClassLoader();
		if ( loadingClassLoader != null ) {
			classPool.appendClassPath( new LoaderClassPath( loadingClassLoader ) );
		}
		return classPool;
	}

	private CtClass loadCtClassFromClass(ClassPool cp, Class<?> aClass) throws IOException {
		return cp.makeClass( aClass.getClassLoader().getResourceAsStream( getFilenameForClass( aClass ) ) );
	}

	private String getFilenameForClass(Class<?> aClass) {
		return aClass.getName().replace( '.', File.separatorChar ) + JavaFileObject.Kind.CLASS.extension;
	}

	/* --- */

	private void enhance(CtClass managedCtClass) {
		// can't effectively enhance interfaces
		if ( managedCtClass.isInterface() ) {
			log.debugf( "Skipping enhancement of [%s]: it's an interface", managedCtClass );
			return;
		}
		// skip already enhanced classes
		for ( String interfaceName : managedCtClass.getClassFile2().getInterfaces() ) {
			if ( ManagedEntity.class.getName().equals( interfaceName ) || ManagedComposite.class.getName().equals( interfaceName ) ) {
				log.debugf( "Skipping enhancement of [%s]: already enhanced", managedCtClass.getName() );
				return;
			}
		}

		if ( enhancementContext.isEntityClass( managedCtClass ) ) {
			log.debugf( "Enhancing [%s] as Entity", managedCtClass.getName() );
			new EntityEnhancer( enhancementContext ).enhance( managedCtClass );
		}
		else if ( enhancementContext.isCompositeClass( managedCtClass ) ) {
			log.debugf( "Enhancing [%s] as Composite", managedCtClass.getName() );
			new CompositeEnhancer( enhancementContext ).enhance( managedCtClass );
		}
		else {
			log.debug( "Skipping enhancement: not entity or composite" );
		}
	}

	private byte[] getByteCode(CtClass managedCtClass) {
		final ByteArrayOutputStream byteStream = new ByteArrayOutputStream();
		final DataOutputStream out = new DataOutputStream( byteStream );
		try {
			managedCtClass.toBytecode( out );
			return byteStream.toByteArray();
		}
		catch (Exception e) {
			log.unableToTransformClass( e.getMessage() );
			throw new HibernateException( "Unable to transform class: " + e.getMessage() );
		}
		finally {
			try {
				out.close();
			}
			catch (IOException ignored) {
			}
		}
	}

	/* --- */

	protected void addInterceptorHandling(CtClass managedCtClass) {
		// interceptor handling is only needed if either:
		//		a) in-line dirty checking has *not* been requested
		//		b) class has lazy-loadable attributes
		if ( enhancementContext.doDirtyCheckingInline( managedCtClass ) && !enhancementContext.hasLazyLoadableAttributes( managedCtClass ) ) {
			return;
		}
		log.debugf( "Weaving in PersistentAttributeInterceptable implementation on [%s]", managedCtClass.getName() );

		managedCtClass.addInterface( attributeInterceptableCtClass );

		FieldWriter.addFieldWithGetterAndSetter( managedCtClass, attributeInterceptorCtClass,
				EnhancerConstants.INTERCEPTOR_FIELD_NAME,
				EnhancerConstants.INTERCEPTOR_GETTER_NAME,
				EnhancerConstants.INTERCEPTOR_SETTER_NAME );
	}

}
