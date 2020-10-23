/*
 * Copyright (C) 2015 The Dagger Authors.
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

package dagger.internal.codegen;

import static com.google.common.truth.Truth.assertAbout;
import static com.google.testing.compile.JavaSourcesSubject.assertThat;
import static com.google.testing.compile.JavaSourcesSubjectFactory.javaSources;
import static dagger.internal.codegen.GeneratedLines.GENERATED_ANNOTATION;

import com.google.common.collect.ImmutableList;
import com.google.testing.compile.JavaFileObjects;
import javax.tools.JavaFileObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class SubcomponentValidationTest {
  @Test public void factoryMethod_missingModulesWithParameters() {
    JavaFileObject componentFile = JavaFileObjects.forSourceLines("test.TestComponent",
        "package test;",
        "",
        "import dagger.Component;",
        "",
        "@Component",
        "interface TestComponent {",
        "  ChildComponent newChildComponent();",
        "}");
    JavaFileObject childComponentFile = JavaFileObjects.forSourceLines("test.ChildComponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent(modules = ModuleWithParameters.class)",
        "interface ChildComponent {",
        "  Object object();",
        "}");
    JavaFileObject moduleFile = JavaFileObjects.forSourceLines("test.ModuleWithParameters",
        "package test;",
        "",
        "import dagger.Module;",
        "import dagger.Provides;",
        "",
        "@Module",
        "final class ModuleWithParameters {",
        "  private final Object object;",
        "",
        "  ModuleWithParameters(Object object) {",
        "    this.object = object;",
        "  }",
        "",
        "  @Provides Object object() {",
        "    return object;",
        "  }",
        "}");
    assertAbout(javaSources()).that(ImmutableList.of(componentFile, childComponentFile, moduleFile))
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining(
            "test.ChildComponent requires modules which have no visible default constructors. "
                + "Add the following modules as parameters to this method: "
                + "test.ModuleWithParameters")
        .in(componentFile).onLine(7);
  }

  @Test public void factoryMethod_nonModuleParameter() {
    JavaFileObject componentFile = JavaFileObjects.forSourceLines("test.TestComponent",
        "package test;",
        "",
        "import dagger.Component;",
        "",
        "@Component",
        "interface TestComponent {",
        "  ChildComponent newChildComponent(String someRandomString);",
        "}");
    JavaFileObject childComponentFile = JavaFileObjects.forSourceLines("test.ChildComponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent",
        "interface ChildComponent {}");
    assertAbout(javaSources()).that(ImmutableList.of(componentFile, childComponentFile))
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining(
            "Subcomponent factory methods may only accept modules, but java.lang.String is not.")
        .in(componentFile).onLine(7).atColumn(43);
  }

  @Test public void factoryMethod_duplicateParameter() {
    JavaFileObject moduleFile = JavaFileObjects.forSourceLines("test.TestModule",
        "package test;",
        "",
        "import dagger.Module;",
        "",
        "@Module",
        "final class TestModule {}");
    JavaFileObject componentFile = JavaFileObjects.forSourceLines("test.TestComponent",
        "package test;",
        "",
        "import dagger.Component;",
        "",
        "@Component",
        "interface TestComponent {",
        "  ChildComponent newChildComponent(TestModule testModule1, TestModule testModule2);",
        "}");
    JavaFileObject childComponentFile = JavaFileObjects.forSourceLines("test.ChildComponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent(modules = TestModule.class)",
        "interface ChildComponent {}");
    assertAbout(javaSources()).that(ImmutableList.of(moduleFile, componentFile, childComponentFile))
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining(
            "A module may only occur once an an argument in a Subcomponent factory method, "
                + "but test.TestModule was already passed.")
        .in(componentFile).onLine(7).atColumn(71);
  }

  @Test public void factoryMethod_superflouousModule() {
    JavaFileObject moduleFile = JavaFileObjects.forSourceLines("test.TestModule",
        "package test;",
        "",
        "import dagger.Module;",
        "",
        "@Module",
        "final class TestModule {}");
    JavaFileObject componentFile = JavaFileObjects.forSourceLines("test.TestComponent",
        "package test;",
        "",
        "import dagger.Component;",
        "",
        "@Component",
        "interface TestComponent {",
        "  ChildComponent newChildComponent(TestModule testModule);",
        "}");
    JavaFileObject childComponentFile = JavaFileObjects.forSourceLines("test.ChildComponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent",
        "interface ChildComponent {}");
    assertAbout(javaSources()).that(ImmutableList.of(moduleFile, componentFile, childComponentFile))
    .processedWith(new ComponentProcessor())
    .failsToCompile()
    .withErrorContaining(
        "test.TestModule is present as an argument to the test.ChildComponent factory method, but "
            + "is not one of the modules used to implement the subcomponent.")
                .in(componentFile).onLine(7);
  }

  @Test public void missingBinding() {
    JavaFileObject moduleFile = JavaFileObjects.forSourceLines("test.TestModule",
        "package test;",
        "",
        "import dagger.Module;",
        "import dagger.Provides;",
        "",
        "@Module",
        "final class TestModule {",
        "  @Provides String provideString(int i) {",
        "    return Integer.toString(i);",
        "  }",
        "}");
    JavaFileObject componentFile = JavaFileObjects.forSourceLines("test.TestComponent",
        "package test;",
        "",
        "import dagger.Component;",
        "",
        "@Component",
        "interface TestComponent {",
        "  ChildComponent newChildComponent();",
        "}");
    JavaFileObject childComponentFile = JavaFileObjects.forSourceLines("test.ChildComponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent(modules = TestModule.class)",
        "interface ChildComponent {",
        "  String getString();",
        "}");
    assertAbout(javaSources())
        .that(ImmutableList.of(moduleFile, componentFile, childComponentFile))
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining(
            "[test.ChildComponent.getString()] "
                + "java.lang.Integer cannot be provided without an @Inject constructor or from an "
                + "@Provides-annotated method")
        .in(componentFile)
        .onLine(6);
  }

  @Test public void subcomponentOnConcreteType() {
    JavaFileObject subcomponentFile = JavaFileObjects.forSourceLines("test.NotASubcomponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent",
        "final class NotASubcomponent {}");
    assertAbout(javaSources()).that(ImmutableList.of(subcomponentFile))
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining("interface");
  }

  @Test public void scopeMismatch() {
    JavaFileObject componentFile = JavaFileObjects.forSourceLines("test.ParentComponent",
        "package test;",
        "",
        "import dagger.Component;",
        "import javax.inject.Singleton;",
        "",
        "@Component",
        "@Singleton",
        "interface ParentComponent {",
        "  ChildComponent childComponent();",
        "}");
    JavaFileObject subcomponentFile = JavaFileObjects.forSourceLines("test.ChildComponent",
        "package test;",
        "",
        "import dagger.Subcomponent;",
        "",
        "@Subcomponent(modules = ChildModule.class)",
        "interface ChildComponent {",
        "  Object getObject();",
        "}");
    JavaFileObject moduleFile = JavaFileObjects.forSourceLines("test.ChildModule",
        "package test;",
        "",
        "import dagger.Module;",
        "import dagger.Provides;",
        "import javax.inject.Singleton;",
        "",
        "@Module",
        "final class ChildModule {",
        "  @Provides @Singleton Object provideObject() { return null; }",
        "}");
    assertAbout(javaSources()).that(ImmutableList.of(componentFile, subcomponentFile, moduleFile))
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining("@Singleton");
  }

  @Test
  public void delegateFactoryNotCreatedForSubcomponentWhenProviderExistsInParent() {
    JavaFileObject parentComponentFile =
        JavaFileObjects.forSourceLines(
            "test.ParentComponent",
            "package test;",
            "",
            "import dagger.Component;",
            "",
            "@Component",
            "interface ParentComponent {",
            "  ChildComponent childComponent();",
            "  Dep1 getDep1();",
            "  Dep2 getDep2();",
            "}");
    JavaFileObject childComponentFile =
        JavaFileObjects.forSourceLines(
            "test.ChildComponent",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "@Subcomponent(modules = ChildModule.class)",
            "interface ChildComponent {",
            "  Object getObject();",
            "}");
    JavaFileObject childModuleFile =
        JavaFileObjects.forSourceLines(
            "test.ChildModule",
            "package test;",
            "",
            "import dagger.Module;",
            "import dagger.Provides;",
            "",
            "@Module",
            "final class ChildModule {",
            "  @Provides Object provideObject(A a) { return null; }",
            "}");
    JavaFileObject aFile =
        JavaFileObjects.forSourceLines(
            "test.A",
            "package test;",
            "",
            "import javax.inject.Inject;",
            "",
            "final class A {",
            "  @Inject public A(NeedsDep1 a, Dep1 b, Dep2 c) { }",
            "  @Inject public void methodA() { }",
            "}");
    JavaFileObject needsDep1File =
        JavaFileObjects.forSourceLines(
            "test.NeedsDep1",
            "package test;",
            "",
            "import javax.inject.Inject;",
            "",
            "final class NeedsDep1 {",
            "  @Inject public NeedsDep1(Dep1 d) { }",
            "}");
    JavaFileObject dep1File =
        JavaFileObjects.forSourceLines(
            "test.Dep1",
            "package test;",
            "",
            "import javax.inject.Inject;",
            "",
            "final class Dep1 {",
            "  @Inject public Dep1() { }",
            "  @Inject public void dep1Method() { }",
            "}");
    JavaFileObject dep2File =
        JavaFileObjects.forSourceLines(
            "test.Dep2",
            "package test;",
            "",
            "import javax.inject.Inject;",
            "",
            "final class Dep2 {",
            "  @Inject public Dep2() { }",
            "  @Inject public void dep2Method() { }",
            "}");

    JavaFileObject componentGeneratedFile =
        JavaFileObjects.forSourceLines(
            "test.DaggerParentComponent",
            "package test;",
            "",
            "import dagger.MembersInjector;",
            "import javax.annotation.Generated;",
            "import javax.inject.Provider;",
            "",
            GENERATED_ANNOTATION,
            "public final class DaggerParentComponent implements ParentComponent {",
            "  private MembersInjector<Dep1> dep1MembersInjector;",
            "  private Provider<Dep1> dep1Provider;",
            "  private MembersInjector<Dep2> dep2MembersInjector;",
            "  private Provider<Dep2> dep2Provider;",
            "",
            "  private DaggerParentComponent(Builder builder) {  ",
            "    assert builder != null;",
            "    initialize(builder);",
            "  }",
            "",
            "  public static Builder builder() {  ",
            "    return new Builder();",
            "  }",
            "",
            "  public static ParentComponent create() {  ",
            "    return new Builder().build();",
            "  }",
            "",
            "  @SuppressWarnings(\"unchecked\")",
            "  private void initialize(final Builder builder) {  ",
            "    this.dep1MembersInjector = Dep1_MembersInjector.create();",
            "    this.dep1Provider = Dep1_Factory.create(dep1MembersInjector);",
            "    this.dep2MembersInjector = Dep2_MembersInjector.create();",
            "    this.dep2Provider = Dep2_Factory.create(dep2MembersInjector);",
            "  }",
            "",
            "  @Override",
            "  public Dep1 getDep1() {  ",
            "    return dep1Provider.get();",
            "  }",
            "",
            "  @Override",
            "  public Dep2 getDep2() {  ",
            "    return dep2Provider.get();",
            "  }",
            "",
            "  @Override",
            "  public ChildComponent childComponent() {  ",
            "    return new ChildComponentImpl();",
            "  }",
            "",
            "  public static final class Builder {",
            "    private Builder() {  ",
            "    }",
            "  ",
            "    public ParentComponent build() {  ",
            "      return new DaggerParentComponent(this);",
            "    }",
            "  }",
            "",
            "  private final class ChildComponentImpl implements ChildComponent {",
            "    private final ChildModule childModule;",
            "    private MembersInjector<A> aMembersInjector;",
            "    private Provider<NeedsDep1> needsDep1Provider;",
            "    private Provider<A> aProvider;",
            "    private Provider<Object> provideObjectProvider;",
            "  ",
            "    private ChildComponentImpl() {  ",
            "      this.childModule = new ChildModule();",
            "      initialize();",
            "    }",
            "",
            "    @SuppressWarnings(\"unchecked\")",
            "    private void initialize() {  ",
            "      this.aMembersInjector = A_MembersInjector.create();",
            "      this.needsDep1Provider = NeedsDep1_Factory.create(",
            "          DaggerParentComponent.this.dep1Provider);",
            "      this.aProvider = A_Factory.create(",
            "          aMembersInjector,",
            "          needsDep1Provider,",
            "          DaggerParentComponent.this.dep1Provider,",
            "          DaggerParentComponent.this.dep2Provider);",
            "      this.provideObjectProvider = ChildModule_ProvideObjectFactory.create(",
            "          childModule, aProvider);",
            "    }",
            "  ",
            "    @Override",
            "    public Object getObject() {  ",
            "      return provideObjectProvider.get();",
            "    }",
            "  }",
            "}");
    assertAbout(javaSources())
        .that(
            ImmutableList.of(
                parentComponentFile,
                childComponentFile,
                childModuleFile,
                aFile,
                needsDep1File,
                dep1File,
                dep2File))
        .processedWith(new ComponentProcessor())
        .compilesWithoutError()
        .and()
        .generatesSources(componentGeneratedFile);
  }

  @Test
  public void multipleSubcomponentsWithSameSimpleNamesCanExistInSameComponent() {
    JavaFileObject parent =
        JavaFileObjects.forSourceLines(
            "test.ParentComponent",
            "package test;",
            "",
            "import dagger.Component;",
            "",
            "@Component",
            "interface ParentComponent {",
            "  Foo.Sub newFooSubcomponent();",
            "  NoConflict newNoConflictSubcomponent();",
            "}");
    JavaFileObject foo =
        JavaFileObjects.forSourceLines(
            "test.Foo",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "interface Foo {",
            "  @Subcomponent interface Sub {",
            "    Bar.Sub newBarSubcomponent();",
            "  }",
            "}");
    JavaFileObject bar =
        JavaFileObjects.forSourceLines(
            "test.Bar",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "interface Bar {",
            "  @Subcomponent interface Sub {",
            "    test.subpackage.Sub newSubcomponentInSubpackage();",
            "  }",
            "}");
    JavaFileObject baz =
        JavaFileObjects.forSourceLines(
            "test.subpackage.Sub",
            "package test.subpackage;",
            "",
            "import dagger.Subcomponent;",
            "",
            "@Subcomponent public interface Sub {}");
    JavaFileObject noConflict =
        JavaFileObjects.forSourceLines(
            "test.NoConflict",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "@Subcomponent interface NoConflict {}");

    JavaFileObject componentGeneratedFile =
        JavaFileObjects.forSourceLines(
            "test.DaggerParentComponent",
            "package test;",
            "",
            "import javax.annotation.Generated;",
            "import test.subpackage.Sub;",
            "",
            GENERATED_ANNOTATION,
            "public final class DaggerParentComponent implements ParentComponent {",
            "  private DaggerParentComponent(Builder builder) {",
            "    assert builder != null;",
            "  }",
            "",
            "  public static Builder builder() {",
            "    return new Builder();",
            "  }",
            "",
            "  public static ParentComponent create() {",
            "    return new Builder().build();",
            "  }",
            "",
            "  @Override",
            "  public Foo.Sub newFooSubcomponent() {",
            "    return new Foo_SubImpl();",
            "  }",
            "",
            "  @Override",
            "  public NoConflict newNoConflictSubcomponent() {",
            "    return new NoConflictImpl();",
            "  }",
            "",
            "  public static final class Builder {",
            "    private Builder() {}",
            "",
            "    public ParentComponent build() {",
            "      return new DaggerParentComponent(this);",
            "    }",
            "  }",
            "",
            "  private final class Foo_SubImpl implements Foo.Sub {",
            "",
            "    private Foo_SubImpl() {}",
            "",
            "    @Override",
            "    public Bar.Sub newBarSubcomponent() {",
            "      return new Bar_SubImpl();",
            "    }",
            "",
            "    private final class Bar_SubImpl implements Bar.Sub {",
            "",
            "      private Bar_SubImpl() {}",
            "",
            "      @Override",
            "      public Sub newSubcomponentInSubpackage() {",
            "        return new subpackage_SubImpl();",
            "      }",
            "",
            "      private final class subpackage_SubImpl implements Sub {",
            "        private subpackage_SubImpl() {}",
            "      }",
            "    }",
            "  }",
            "  private final class NoConflictImpl implements NoConflict {",
            "    private NoConflictImpl() {}",
            "  }",
            "}");

    assertAbout(javaSources())
        .that(ImmutableList.of(parent, foo, bar, baz, noConflict))
        .processedWith(new ComponentProcessor())
        .compilesWithoutError()
        .and()
        .generatesSources(componentGeneratedFile);
  }

  @Test
  public void subcomponentSimpleNamesDisambiguated() {
    JavaFileObject parent =
        JavaFileObjects.forSourceLines(
            "test.ParentComponent",
            "package test;",
            "",
            "import dagger.Component;",
            "",
            "@Component",
            "interface ParentComponent {",
            "  Sub newSubcomponent();",
            "}");
    JavaFileObject sub =
        JavaFileObjects.forSourceLines(
            "test.Sub",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "@Subcomponent interface Sub {",
            "  test.deep.many.levels.that.match.test.Sub newDeepSubcomponent();",
            "}");
    JavaFileObject deepSub =
        JavaFileObjects.forSourceLines(
            "test.deep.many.levels.that.match.test.Sub",
            "package test.deep.many.levels.that.match.test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "@Subcomponent public interface Sub {}");

    JavaFileObject componentGeneratedFile =
        JavaFileObjects.forSourceLines(
            "test.DaggerParentComponent",
            "package test;",
            "",
            "import javax.annotation.Generated;",
            "",
            GENERATED_ANNOTATION,
            "public final class DaggerParentComponent implements ParentComponent {",
            "  private DaggerParentComponent(Builder builder) {",
            "    assert builder != null;",
            "  }",
            "",
            "  public static Builder builder() {",
            "    return new Builder();",
            "  }",
            "",
            "  public static ParentComponent create() {",
            "    return new Builder().build();",
            "  }",
            "",
            "  @Override",
            "  public Sub newSubcomponent() {",
            "    return new test_SubImpl();",
            "  }",
            "",
            "  public static final class Builder {",
            "    private Builder() {}",
            "",
            "    public ParentComponent build() {",
            "      return new DaggerParentComponent(this);",
            "    }",
            "  }",
            "",
            "  private final class test_SubImpl implements Sub {",
            "",
            "    private test_SubImpl() {}",
            "",
            "    @Override",
            "    public test.deep.many.levels.that.match.test.Sub newDeepSubcomponent() {",
            "      return new match_test_SubImpl();",
            "    }",
            "",
            "    private final class match_test_SubImpl implements ",
            "        test.deep.many.levels.that.match.test.Sub {",
            "      private match_test_SubImpl() {}",
            "    }",
            "  }",
            "}");

    assertAbout(javaSources())
        .that(ImmutableList.of(parent, sub, deepSub))
        .processedWith(new ComponentProcessor())
        .compilesWithoutError()
        .and()
        .generatesSources(componentGeneratedFile);
  }

  @Test
  public void subcomponentImplNameUsesFullyQualifiedClassNameIfNecessary() {
    JavaFileObject parent =
        JavaFileObjects.forSourceLines(
            "test.ParentComponent",
            "package test;",
            "",
            "import dagger.Component;",
            "",
            "@Component",
            "interface ParentComponent {",
            "  top1.a.b.c.d.E.F.Sub top1();",
            "  top2.a.b.c.d.E.F.Sub top2();",
            "}");
    JavaFileObject top1 =
        JavaFileObjects.forSourceLines(
            "top1.a.b.c.d.E",
            "package top1.a.b.c.d;",
            "",
            "import dagger.Subcomponent;",
            "",
            "public interface E {",
            "  interface F {",
            "    @Subcomponent interface Sub {}",
            "  }",
            "}");
    JavaFileObject top2 =
        JavaFileObjects.forSourceLines(
            "top2.a.b.c.d.E",
            "package top2.a.b.c.d;",
            "",
            "import dagger.Subcomponent;",
            "",
            "public interface E {",
            "  interface F {",
            "    @Subcomponent interface Sub {}",
            "  }",
            "}");

    JavaFileObject componentGeneratedFile =
        JavaFileObjects.forSourceLines(
            "test.DaggerParentComponent",
            "package test;",
            "",
            "import javax.annotation.Generated;",
            "import top1.a.b.c.d.E;",
            "",
            GENERATED_ANNOTATION,
            "public final class DaggerParentComponent implements ParentComponent {",
            "  private DaggerParentComponent(Builder builder) {",
            "    assert builder != null;",
            "  }",
            "",
            "  public static Builder builder() {",
            "    return new Builder();",
            "  }",
            "",
            "  public static ParentComponent create() {",
            "    return new Builder().build();",
            "  }",
            "",
            "  @Override",
            "  public E.F.Sub top1() {",
            "    return new top1_a_b_c_d_E_F_SubImpl();",
            "  }",
            "",
            "  @Override",
            "  public top2.a.b.c.d.E.F.Sub top2() {",
            "    return new top2_a_b_c_d_E_F_SubImpl();",
            "  }",
            "",
            "  public static final class Builder {",
            "    private Builder() {}",
            "",
            "    public ParentComponent build() {",
            "      return new DaggerParentComponent(this);",
            "    }",
            "  }",
            "",
            "  private final class top1_a_b_c_d_E_F_SubImpl implements E.F.Sub {",
            "    private top1_a_b_c_d_E_F_SubImpl() {}",
            "  }",
            "  private final class top2_a_b_c_d_E_F_SubImpl implements top2.a.b.c.d.E.F.Sub {",
            "    private top2_a_b_c_d_E_F_SubImpl() {}",
            "  }",
            "}");

    assertAbout(javaSources())
        .that(ImmutableList.of(parent, top1, top2))
        .processedWith(new ComponentProcessor())
        .compilesWithoutError()
        .and()
        .generatesSources(componentGeneratedFile);
  }

  @Test
  public void parentComponentNameShouldNotBeDisambiguatedWhenItConflictsWithASubcomponent() {
    JavaFileObject parent =
        JavaFileObjects.forSourceLines(
            "test.C",
            "package test;",
            "",
            "import dagger.Component;",
            "",
            "@Component",
            "interface C {",
            "  test.Foo.C newFooC();",
            "}");
    JavaFileObject subcomponentWithSameSimpleNameAsParent =
        JavaFileObjects.forSourceLines(
            "test.Foo",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "interface Foo {",
            "  @Subcomponent interface C {}",
            "}");

    JavaFileObject componentGeneratedFile =
        JavaFileObjects.forSourceLines(
            "test.DaggerC",
            "package test;",
            "",
            "import javax.annotation.Generated;",
            "",
            GENERATED_ANNOTATION,
            "public final class DaggerC implements C {",
            "  private DaggerC(Builder builder) {",
            "    assert builder != null;",
            "  }",
            "",
            "  public static Builder builder() {",
            "    return new Builder();",
            "  }",
            "",
            "  public static C create() {",
            "    return new Builder().build();",
            "  }",
            "",
            "  @Override",
            "  public Foo.C newFooC() {",
            "    return new Foo_CImpl();",
            "  }",
            "",
            "  public static final class Builder {",
            "    private Builder() {}",
            "",
            "    public C build() {",
            "      return new DaggerC(this);",
            "    }",
            "  }",
            "",
            "  private final class Foo_CImpl implements Foo.C {",
            "    private Foo_CImpl() {}",
            "  }",
            "}");

    assertAbout(javaSources())
        .that(ImmutableList.of(parent, subcomponentWithSameSimpleNameAsParent))
        .processedWith(new ComponentProcessor())
        .compilesWithoutError()
        .and()
        .generatesSources(componentGeneratedFile);
  }

  @Test
  public void subcomponentBuilderNamesShouldNotConflict() {
    JavaFileObject parent =
        JavaFileObjects.forSourceLines(
            "test.C",
            "package test;",
            "",
            "import dagger.Component;",
            "import dagger.Subcomponent;",
            "",
            "@Component",
            "interface C {",
            "  Foo.Sub.Builder fooBuilder();",
            "  Bar.Sub.Builder barBuilder();",
            "",
            "  interface Foo {",
            "    @Subcomponent",
            "    interface Sub {",
            "      @Subcomponent.Builder",
            "      interface Builder {",
            "        Sub build();",
            "      }",
            "    }",
            "  }",
            "",
            "  interface Bar {",
            "    @Subcomponent",
            "    interface Sub {",
            "      @Subcomponent.Builder",
            "      interface Builder {",
            "        Sub build();",
            "      }",
            "    }",
            "  }",
            "}");
    JavaFileObject componentGeneratedFile =
        JavaFileObjects.forSourceLines(
            "test.DaggerC",
            "package test;",
            "",
            "import javax.annotation.Generated;",
            "import javax.inject.Provider;",
            "",
            GENERATED_ANNOTATION,
            "public final class DaggerC implements C {",
            "",
            "  private Provider<C.Foo.Sub.Builder> fooBuilderProvider;",
            "  private Provider<C.Bar.Sub.Builder> barBuilderProvider;",
            "",
            "  private DaggerC(Builder builder) {",
            "    assert builder != null;",
            "    initialize(builder);",
            "  }",
            "",
            "  public static Builder builder() {",
            "    return new Builder();",
            "  }",
            "",
            "  public static C create() {",
            "    return new Builder().build();",
            "  }",
            "",
            "  @SuppressWarnings(\"unchecked\")",
            "  private void initialize(final Builder builder) {",
            "    this.fooBuilderProvider = ",
            "        new dagger.internal.Factory<C.Foo.Sub.Builder>() {",
            "          @Override",
            "          public C.Foo.Sub.Builder get() {",
            "            return new Foo_SubBuilder();",
            "          }",
            "        };",
            "",
            "    this.barBuilderProvider = ",
            "        new dagger.internal.Factory<C.Bar.Sub.Builder>() {",
            "          @Override",
            "          public C.Bar.Sub.Builder get() {",
            "            return new Bar_SubBuilder();",
            "          }",
            "        };",
            "  }",
            "",
            "  @Override",
            "  public C.Foo.Sub.Builder fooBuilder() {",
            "    return fooBuilderProvider.get();",
            "  }",
            "",
            "  @Override",
            "  public C.Bar.Sub.Builder barBuilder() {",
            "    return barBuilderProvider.get();",
            "  }",
            "",
            "  public static final class Builder {",
            "    private Builder() {}",
            "",
            "    public C build() {",
            "      return new DaggerC(this);",
            "    }",
            "  }",
            "",
            "  private final class Foo_SubBuilder implements C.Foo.Sub.Builder {",
            "    @Override",
            "    public C.Foo.Sub build() {",
            "      return new Foo_SubImpl(this);",
            "    }",
            "  }",
            "",
            "  private final class Foo_SubImpl implements C.Foo.Sub {",
            "    private Foo_SubImpl(Foo_SubBuilder builder) {",
            "      assert builder != null;",
            "    }",
            "  }",
            "",
            "  private final class Bar_SubBuilder implements C.Bar.Sub.Builder {",
            "    @Override",
            "    public C.Bar.Sub build() {",
            "      return new Bar_SubImpl(this);",
            "    }",
            "  }",
            "",
            "  private final class Bar_SubImpl implements C.Bar.Sub {",
            "    private Bar_SubImpl(Bar_SubBuilder builder) {",
            "      assert builder != null;",
            "    }",
            "  }",
            "}");

    assertAbout(javaSources())
        .that(ImmutableList.of(parent))
        .processedWith(new ComponentProcessor())
        .compilesWithoutError()
        .and()
        .generatesSources(componentGeneratedFile);
  }

  @Test
  public void duplicateBindingWithSubcomponentDeclaration() {
    JavaFileObject module =
        JavaFileObjects.forSourceLines(
            "test.TestModule",
            "package test;",
            "",
            "import dagger.Module;",
            "import dagger.Provides;",
            "",
            "@Module(subcomponents = Sub.class)",
            "class TestModule {",
            "  @Provides Sub.Builder providesConflictsWithModuleSubcomponents() { return null; }",
            "  @Provides Object usesSubcomponentBuilder(Sub.Builder builder) {",
            "    return new Builder().toString();",
            "  }",
            "}");

    JavaFileObject subcomponent =
        JavaFileObjects.forSourceLines(
            "test.Sub",
            "package test;",
            "",
            "import dagger.Subcomponent;",
            "",
            "@Subcomponent",
            "interface Sub {",
            "  @Subcomponent.Builder",
            "  interface Builder {",
            "    Sub build();",
            "  }",
            "}");

    JavaFileObject component =
        JavaFileObjects.forSourceLines(
            "test.Sub",
            "package test;",
            "",
            "import dagger.Component;",
            "",
            "@Component(modules = TestModule.class)",
            "interface C {",
            "  Object dependsOnBuilder();",
            "}");

    assertThat(module, component, subcomponent)
        .processedWith(new ComponentProcessor())
        .failsToCompile()
        .withErrorContaining("test.Sub.Builder is bound multiple times:")
        .and()
        .withErrorContaining(
            "@Provides test.Sub.Builder test.TestModule.providesConflictsWithModuleSubcomponents()")
        .and()
        .withErrorContaining("@Module(subcomponents = test.Sub.class) for test.TestModule");
  }
}
