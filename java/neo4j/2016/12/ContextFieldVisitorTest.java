/*
 * Copyright (c) 2002-2016 "Neo Technology,"
 * Network Engine for Objects in Lund AB [http://neotechnology.com]
 *
 * This file is part of Neo4j.
 *
 * Neo4j is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package org.neo4j.tooling.procedure.visitors;

import com.google.testing.compile.CompilationRule;
import org.neo4j.tooling.procedure.messages.CompilationMessage;
import org.neo4j.tooling.procedure.testutils.ElementTestUtils;
import org.neo4j.tooling.procedure.visitors.examples.FinalContextMisuse;
import org.neo4j.tooling.procedure.visitors.examples.NonPublicContextMisuse;
import org.neo4j.tooling.procedure.visitors.examples.StaticContextMisuse;
import org.neo4j.tooling.procedure.visitors.examples.UnsupportedInjectedContextTypes;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;

import java.util.stream.Stream;
import javax.lang.model.element.ElementVisitor;
import javax.lang.model.element.VariableElement;
import javax.tools.Diagnostic;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.tuple;

public class ContextFieldVisitorTest
{

    @Rule
    public CompilationRule compilationRule = new CompilationRule();
    private ElementTestUtils elementTestUtils;
    private ElementVisitor<Stream<CompilationMessage>,Void> contextFieldVisitor;

    @Before
    public void prepare()
    {
        elementTestUtils = new ElementTestUtils( compilationRule );
        contextFieldVisitor =
                new ContextFieldVisitor( compilationRule.getTypes(), compilationRule.getElements(), false );
    }

    @Test
    public void rejects_non_public_context_fields()
    {
        Stream<VariableElement> fields = elementTestUtils.getFields( NonPublicContextMisuse.class );

        Stream<CompilationMessage> result = fields.flatMap( contextFieldVisitor::visit );

        assertThat( result ).extracting( CompilationMessage::getCategory, CompilationMessage::getContents )
                .containsExactly( tuple( Diagnostic.Kind.ERROR,
                        "@org.neo4j.procedure.Context usage error: field NonPublicContextMisuse#arithm should be public, " +
                                "non-static and non-final" ) );
    }

    @Test
    public void rejects_static_context_fields()
    {
        Stream<VariableElement> fields = elementTestUtils.getFields( StaticContextMisuse.class );

        Stream<CompilationMessage> result = fields.flatMap( contextFieldVisitor::visit );

        assertThat( result ).extracting( CompilationMessage::getCategory, CompilationMessage::getContents )
                .containsExactly( tuple( Diagnostic.Kind.ERROR,
                        "@org.neo4j.procedure.Context usage error: field StaticContextMisuse#db should be public, non-static " +
                                "and non-final" ) );
    }

    @Test
    public void rejects_final_context_fields()
    {
        Stream<VariableElement> fields = elementTestUtils.getFields( FinalContextMisuse.class );

        Stream<CompilationMessage> result = fields.flatMap( contextFieldVisitor::visit );

        assertThat( result ).extracting( CompilationMessage::getCategory, CompilationMessage::getContents )
                .containsExactly( tuple( Diagnostic.Kind.ERROR,
                        "@org.neo4j.procedure.Context usage error: field FinalContextMisuse#graphDatabaseService should be " +
                                "public, non-static and non-final" ) );
    }

    @Test
    public void warns_against_unsupported_injected_types_when_warnings_enabled()
    {
        Stream<VariableElement> fields = elementTestUtils.getFields( UnsupportedInjectedContextTypes.class );

        Stream<CompilationMessage> result = fields.flatMap( contextFieldVisitor::visit );

        assertThat( result ).extracting( CompilationMessage::getCategory, CompilationMessage::getContents )
                .containsExactlyInAnyOrder( tuple( Diagnostic.Kind.WARNING,
                        "@org.neo4j.procedure.Context usage warning: found type: <java.lang.String>, expected one of: <org.neo4j.graphdb.GraphDatabaseService>, <org.neo4j.logging.Log>" ),
                        tuple( Diagnostic.Kind.WARNING,
                                "@org.neo4j.procedure.Context usage warning: found type: <org.neo4j.kernel.internal.GraphDatabaseAPI>, expected one of: <org.neo4j.graphdb.GraphDatabaseService>, <org.neo4j.logging.Log>" ) );
    }

    @Test
    public void does_not_warn_against_unsupported_injected_types_when_warnings_disabled()
    {
        ContextFieldVisitor contextFieldVisitor =
                new ContextFieldVisitor( compilationRule.getTypes(), compilationRule.getElements(), true );
        Stream<VariableElement> fields = elementTestUtils.getFields( UnsupportedInjectedContextTypes.class );

        Stream<CompilationMessage> result = fields.flatMap( contextFieldVisitor::visit );

        assertThat( result ).isEmpty();
    }

}
