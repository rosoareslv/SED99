/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.test.bytecode.enhancement.lazy.HHH_10708;

import org.hibernate.Session;
import org.hibernate.cfg.Configuration;
import org.hibernate.cfg.Environment;
import org.hibernate.test.bytecode.enhancement.AbstractEnhancerTestTask;
import org.junit.Assert;

import javax.persistence.Column;
import javax.persistence.ElementCollection;
import javax.persistence.Entity;
import javax.persistence.FetchType;
import javax.persistence.Id;
import javax.persistence.ManyToMany;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

public class UnexpectedDeleteThreeTestTask extends AbstractEnhancerTestTask {

	@Override
	public Class<?>[] getAnnotatedClasses() {
		return new Class<?>[] { Parent.class, Child.class };
	}

	@Override
	public void prepare() {
		Configuration cfg = new Configuration();
		cfg.setProperty( Environment.ENABLE_LAZY_LOAD_NO_TRANS, "true" );
		cfg.setProperty( Environment.USE_SECOND_LEVEL_CACHE, "false" );
		prepare( cfg );

		Session s = getFactory().openSession();
		s.beginTransaction();

		Child child = new Child();
		child.setId( 2L );
		s.save(child);

		Parent parent = new Parent();
		parent.setId( 1L );
		parent.setNames( Collections.singleton( "name" ) );

		Set<Child> children = new HashSet<Child>();
		children.add(child);
		parent.setChildren( children );

		s.save( parent );

		s.getTransaction().commit();
		s.clear();
		s.close();
	}

	@Override
	public void execute() {
		Session s = getFactory().openSession();
		s.beginTransaction();

		Parent parent = s.get( Parent.class, 1L );
		Set<Child> children = parent.getChildren();
		if (children == null) {
			children = new HashSet<Child>();
			parent.setChildren( children );
		}
		Child child = new Child();
		child.setId( 1L );
		s.save(child);
		children.add(child);

		// We need to leave at least one attribute unfetchd
		//parent.getNames().size();
		s.save(parent);

		s.getTransaction().commit();
		s.close();

		s = getFactory().openSession();
		s.beginTransaction();

		Parent application = s.get( Parent.class, 1L );
		Assert.assertEquals( "Loaded Children collection has unexpected size", 2, application.getChildren().size() );

		s.getTransaction().commit();
		s.close();
	}

	@Override
	protected void cleanup() {
	}

	// --- //

	@Entity public static class Child {

		private Long id;

		@Id
		@Column(name = "id", unique = true, nullable = false)
		public Long getId() {
			return id;
		}

		public void setId(Long id)
		{
			this.id = id;
		}

	}

	@Entity public static class Parent {

		private Long id;
		private Set<String> names;
		private Set<Child> children;

		@Id @Column(name = "id", unique = true, nullable = false)
		public Long getId() {
			return id;
		}

		public void setId(Long id) {
			this.id = id;
		}

		@ElementCollection
		public Set<String> getNames() {
			return names;
		}

		public void setNames(Set<String> secrets) {
			this.names = secrets;
		}

		@ManyToMany(fetch = FetchType.LAZY, targetEntity = Child.class)
		public Set<Child> getChildren() {
			return children;
		}

		public void setChildren(Set<Child> children) {
			this.children = children;
		}

	}

}