package org.hibernate.test.schemaupdate;

import java.io.File;
import java.nio.file.Files;

import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;

import org.hibernate.boot.MetadataSources;
import org.hibernate.boot.registry.StandardServiceRegistry;
import org.hibernate.boot.registry.StandardServiceRegistryBuilder;
import org.hibernate.boot.spi.MetadataImplementor;
import org.hibernate.cfg.Environment;
import org.hibernate.testing.TestForIssue;
import org.hibernate.tool.hbm2ddl.SchemaUpdate;
import org.hibernate.tool.hbm2ddl.Target;
import org.junit.Assert;
import org.junit.Test;

/**
 * @author Koen Aers
 */
@TestForIssue(jiraKey = "HHH-10158")
public class SchemaUpdateFormatterTest {
	
	private static final String AFTER_FORMAT = 
			System.lineSeparator() +
			"    create table test_entity (" + System.lineSeparator() +
			"        field varchar(255) not null," + System.lineSeparator() +
			"        primary key (field)" + System.lineSeparator() +
			"    );" + System.lineSeparator();
	private static final String DELIMITER = ";";

	@Test
	public void testSetFormat() throws Exception {
		StandardServiceRegistry ssr = new StandardServiceRegistryBuilder()
				.applySetting( Environment.HBM2DDL_AUTO, "none" )
				.build();
		try {
			File output = File.createTempFile( "update_script", ".sql" );
			output.deleteOnExit();

			final MetadataImplementor metadata = (MetadataImplementor) new MetadataSources( ssr )
					.addAnnotatedClass( TestEntity.class )
					.buildMetadata();
			metadata.validate();

			SchemaUpdate su = new SchemaUpdate( ssr, metadata );
			su.setHaltOnError( true );
			su.setOutputFile( output.getAbsolutePath() );
			su.setDelimiter( DELIMITER );
			su.setFormat( true );
			su.execute( Target.SCRIPT );

			Assert.assertEquals(
					AFTER_FORMAT, 
					new String(Files.readAllBytes(output.toPath())));
		}
		finally {
			StandardServiceRegistryBuilder.destroy( ssr );
		}
	}

	@Entity
	@Table(name = "test_entity")
	public static class TestEntity {
		@Id
		private String field;

		public String getField() {
			return field;
		}

		public void setField(String field) {
			this.field = field;
		}
	}

}
