/*
 * Copyright (C) 2017 The Dagger Authors.
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

import static com.google.common.base.CaseFormat.LOWER_CAMEL;
import static com.google.common.base.CaseFormat.UPPER_CAMEL;
import static com.squareup.javapoet.MethodSpec.methodBuilder;
import static dagger.internal.codegen.Accessibility.isElementAccessibleFrom;
import static dagger.internal.codegen.Accessibility.isRawTypeAccessible;
import static dagger.internal.codegen.Accessibility.isRawTypePubliclyAccessible;
import static dagger.internal.codegen.Accessibility.isTypeAccessibleFrom;
import static dagger.internal.codegen.CodeBlocks.makeParametersCodeBlock;
import static dagger.internal.codegen.CodeBlocks.toConcatenatedCodeBlock;
import static dagger.internal.codegen.ConfigurationAnnotations.getNullableType;
import static dagger.internal.codegen.SourceFiles.generatedClassNameForBinding;
import static dagger.internal.codegen.SourceFiles.membersInjectorNameForType;
import static dagger.internal.codegen.TypeNames.rawTypeName;
import static dagger.internal.codegen.Util.toImmutableList;
import static java.util.stream.Collectors.toList;
import static javax.lang.model.element.Modifier.PUBLIC;
import static javax.lang.model.element.Modifier.STATIC;
import static javax.lang.model.type.TypeKind.VOID;

import com.google.auto.common.MoreElements;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.squareup.javapoet.ClassName;
import com.squareup.javapoet.CodeBlock;
import com.squareup.javapoet.MethodSpec;
import com.squareup.javapoet.ParameterSpec;
import com.squareup.javapoet.TypeName;
import com.squareup.javapoet.TypeVariableName;
import dagger.internal.codegen.DependencyRequest.Kind;
import dagger.internal.codegen.MembersInjectionBinding.InjectionSite;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.function.Function;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.Parameterizable;
import javax.lang.model.element.TypeElement;
import javax.lang.model.element.TypeParameterElement;
import javax.lang.model.element.VariableElement;
import javax.lang.model.type.TypeKind;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Types;

/**
 * Injection methods are static methods that implement provision and/or injection in one step:
 *
 * <ul>
 *   <li>methods that invoke {@code @Inject} constructors and do members injection if necessary
 *   <li>methods that call {@code @Provides} module methods
 *   <li>methods that perform members injection
 * </ul>
 */
// TODO(ronshapiro): add examples for each class of injection method
final class InjectionMethods {
  /**
   * A static method that returns an object from a {@code @Provides} method or an {@code @Inject}ed
   * constructor. Its parameters match the dependency requests for constructor and members
   * injection.
   *
   * <p>For {@code @Provides} methods named "foo", the method name is "proxyFoo". If the
   * {@code @Provides} method and its raw parameter types are publicly accessible, no method is
   * necessary and this method returns {@link Optional#empty()}.
   *
   * <p>TODO(ronshapiro): At the moment non-static {@code @Provides} methods are not supported.
   *
   * <p>Example:
   *
   * <pre><code>
   * abstract class FooModule {
   *   {@literal @Provides} static Foo provideFoo(Bar bar, Baz baz) { … }
   * }
   *
   * public static proxyProvideFoo(Bar bar, Baz baz) { … }
   * </code></pre>
   *
   * <p>For {@code @Inject}ed constructors, the method name is "newFoo". If the constructor and its
   * raw parameter types are publicly accessible, no method is necessary and this method returns
   * {@code Optional#empty()}.
   *
   * <p>Example:
   *
   * <pre><code>
   * class Foo {
   *   {@literal @Inject} Foo(Bar bar) {}
   * }
   *
   * public static Foo newFoo(Bar bar) { … }
   * </code></pre>
   */
  static final class ProvisionMethod {

    /**
     * Returns a method that invokes the binding's {@linkplain ProvisionBinding#bindingElement()
     * constructor} and injects the instance's members, if necessary. If {@link
     * #shouldCreateInjectionMethod(ProvisionBinding) no method is necessary}, then {@link
     * Optional#empty()} is returned.
     */
    static Optional<MethodSpec> create(ProvisionBinding binding) {
      if (!shouldCreateInjectionMethod(binding)) {
        return Optional.empty();
      }
      ExecutableElement element = MoreElements.asExecutable(binding.bindingElement().get());
      switch (element.getKind()) {
        case CONSTRUCTOR:
          return Optional.of(constructorProxy(element));
        case METHOD:
          return Optional.of(
              methodProxy(element, methodName(element), ReceiverAccessibility.IGNORE));
        default:
          throw new AssertionError(element);
      }
    }

    /**
     * Invokes the injection method for {@code binding}, with the dependencies transformed with the
     * {@code dependencyUsage} function.
     */
    static CodeBlock invoke(
        ProvisionBinding binding,
        Function<DependencyRequest, CodeBlock> dependencyUsage,
        ClassName requestingClass,
        Optional<CodeBlock> moduleReference) {
      ImmutableList.Builder<CodeBlock> arguments = ImmutableList.builder();
      moduleReference.ifPresent(arguments::add);
      arguments.addAll(
          injectionMethodArguments(
              binding.provisionDependencies(), dependencyUsage, requestingClass));
      return callInjectionMethod(
          create(binding).get().name,
          arguments.build(),
          generatedClassNameForBinding(binding),
          requestingClass);
    }

    private static MethodSpec constructorProxy(ExecutableElement constructor) {
      UniqueNameSet names = new UniqueNameSet();
      TypeElement enclosingType = MoreElements.asType(constructor.getEnclosingElement());
      MethodSpec.Builder method =
          methodBuilder(methodName(constructor))
              .returns(TypeName.get(enclosingType.asType()))
              .addModifiers(PUBLIC, STATIC);

      copyTypeParameters(enclosingType, method);
      copyThrows(constructor, method);

      return method
          .addStatement(
              "return new $T($L)", enclosingType, copyParameters(constructor, method, names))
          .build();
    }

    /**
     * Returns {@code true} if injecting an instance of {@code binding} from {@code callingPackage}
     * requires the use of an injection method.
     */
    static boolean requiresInjectionMethod(ProvisionBinding binding, String callingPackage) {
      ExecutableElement method = MoreElements.asExecutable(binding.bindingElement().get());
      return !binding.injectionSites().isEmpty()
          || !isElementAccessibleFrom(method, callingPackage)
          || method
              .getParameters()
              .stream()
              .map(VariableElement::asType)
              .anyMatch(type -> !isRawTypeAccessible(type, callingPackage));
    }

    private static boolean shouldCreateInjectionMethod(ProvisionBinding binding) {
      return requiresInjectionMethod(binding, "dagger.should.never.exist");
    }

    /**
     * Returns the name of the {@code static} method that wraps {@code method}. For methods that are
     * associated with {@code @Inject} constructors, the method will also inject all {@link
     * InjectionSite}s.
     */
    private static String methodName(ExecutableElement method) {
      switch (method.getKind()) {
        case CONSTRUCTOR:
          return "new" + method.getEnclosingElement().getSimpleName();
        case METHOD:
          return "proxy" + LOWER_CAMEL.to(UPPER_CAMEL, method.getSimpleName().toString());
        default:
          throw new AssertionError(method);
      }
    }
  }

  /**
   * A static method that injects one member of an instance of a type. Its first parameter is an
   * instance of the type to be injected. The remaining parameters match the dependency requests for
   * the injection site.
   *
   * <p>Example:
   *
   * <pre><code>
   * class Foo {
   *   {@literal @Inject} Bar bar;
   *   {@literal @Inject} void setThings(Baz baz, Qux qux) {}
   * }
   *
   * public static injectBar(Foo instance, Bar bar) { … }
   * public static injectSetThings(Foo instance, Baz baz, Qux qux) { … }
   * </code></pre>
   */
  static final class InjectionSiteMethod {
    /**
     * When a type has an inaccessible member from a supertype (e.g. an @Inject field in a parent
     * that's in a different package), a method in the supertype's package must be generated to give
     * the subclass's members injector a way to inject it. Each potentially inaccessible member
     * receives its own method, as the subclass may need to inject them in a different order from
     * the parent class.
     */
    static MethodSpec create(InjectionSite injectionSite) {
      String methodName = methodName(injectionSite);
      switch (injectionSite.kind()) {
        case METHOD:
          return methodProxy(
              MoreElements.asExecutable(injectionSite.element()),
              methodName,
              ReceiverAccessibility.CAST_IF_NOT_PUBLIC);
        case FIELD:
          return fieldProxy(MoreElements.asVariable(injectionSite.element()), methodName);
        default:
          throw new AssertionError(injectionSite);
      }
    }

    /**
     * Invokes each of the injection methods for {@code injectionSites}, with the dependencies
     * transformed using the {@code dependencyUsage} function.
     *
     * @param instanceType the type of the {@code instance} parameter
     */
    static CodeBlock invokeAll(
        ImmutableSet<InjectionSite> injectionSites,
        ClassName generatedTypeName,
        CodeBlock instanceCodeBlock,
        TypeMirror instanceType,
        Types types,
        Function<DependencyRequest, CodeBlock> dependencyUsage) {
      return injectionSites
          .stream()
          .map(
              injectionSite -> {
                TypeMirror injectSiteType =
                    types.erasure(injectionSite.element().getEnclosingElement().asType());

                // If instance has been declared as Object because it is not accessible from the
                // component, but the injectionSite is in a supertype of instanceType that is
                // publicly accessible, the InjectionSiteMethod will request the actual type and not
                // Object as the first parameter. If so, cast to the supertype which is accessible
                // from within generatedTypeName
                CodeBlock maybeCastedInstance =
                    !types.isSubtype(instanceType, injectSiteType)
                            && isTypeAccessibleFrom(injectSiteType, generatedTypeName.packageName())
                        ? CodeBlock.of("($T) $L", injectSiteType, instanceCodeBlock)
                        : instanceCodeBlock;
                return CodeBlock.of(
                    "$L;",
                    invoke(injectionSite, generatedTypeName, maybeCastedInstance, dependencyUsage));
              })
          .collect(toConcatenatedCodeBlock());
    }

    /**
     * Invokes the injection method for {@code injectionSite}, with the dependencies transformed
     * using the {@code dependencyUsage} function.
     */
    private static CodeBlock invoke(
        InjectionSite injectionSite,
        ClassName generatedTypeName,
        CodeBlock instanceCodeBlock,
        Function<DependencyRequest, CodeBlock> dependencyUsage) {
      List<CodeBlock> arguments = new ArrayList<>();
      arguments.add(instanceCodeBlock);
      if (!injectionSite.dependencies().isEmpty()) {
        arguments.addAll(
            injectionSite
                .dependencies()
                .stream()
                .map(dependencyUsage)
                .collect(toList()));
      }
      return callInjectionMethod(
          create(injectionSite).name,
          arguments,
          membersInjectorNameForType(
              MoreElements.asType(injectionSite.element().getEnclosingElement())),
          generatedTypeName);
    }

    /*
     * TODO(ronshapiro): this isn't perfect, as collisions could still exist. Some examples:
     *
     *  - @Inject void members() {} will generate a method that conflicts with the instance
     *    method `injectMembers(T)`
     *  - Adding the index could conflict with another member:
     *      @Inject void a(Object o) {}
     *      @Inject void a(String s) {}
     *      @Inject void a1(String s) {}
     *
     *    Here, Method a(String) will add the suffix "1", which will conflict with the method
     *    generated for a1(String)
     *  - Members named "members" or "methods" could also conflict with the {@code static} injection
     *    method.
     */
    private static String methodName(InjectionSite injectionSite) {
      int index = injectionSite.indexAmongAtInjectMembersWithSameSimpleName();
      String indexString = index == 0 ? "" : String.valueOf(index + 1);
      return "inject"
          + LOWER_CAMEL.to(UPPER_CAMEL, injectionSite.element().getSimpleName().toString())
          + indexString;
    }
  }

  /**
   * Returns an argument list suitable for calling an injection method. Down-casts any arguments
   * that are {@code Object} (or {@code Provider<Object>}) at the caller but not the method.
   *
   * @param dependencies the dependencies used by the method
   * @param dependencyUsage function to apply on each of {@code dependencies} before casting
   * @param requestingClass the class calling the injection method
   */
  private static ImmutableList<CodeBlock> injectionMethodArguments(
      ImmutableSet<DependencyRequest> dependencies,
      Function<DependencyRequest, CodeBlock> dependencyUsage,
      ClassName requestingClass) {
    return dependencies.stream()
        .map(dep -> injectionMethodArgument(dep, dependencyUsage.apply(dep), requestingClass))
        .collect(toImmutableList());
  }

  private static CodeBlock injectionMethodArgument(
      DependencyRequest dependency, CodeBlock argument, ClassName generatedTypeName) {
    TypeMirror keyType = dependency.key().type();
    CodeBlock.Builder codeBlock = CodeBlock.builder();
    if (!isRawTypeAccessible(keyType, generatedTypeName.packageName())
        && isTypeAccessibleFrom(keyType, generatedTypeName.packageName())) {
      if (!dependency.kind().equals(Kind.INSTANCE)) {
        TypeName usageTypeName = accessibleType(dependency);
        codeBlock.add("($T) ($T)", usageTypeName, rawTypeName(usageTypeName));
      } else if (dependency.requestElement().get().asType().getKind().equals(TypeKind.TYPEVAR)) {
        codeBlock.add("($T)", keyType);
      }
    }
    return codeBlock.add(argument).build();
  }

  /**
   * Returns the parameter type for {@code dependency}. If the raw type is not accessible, returns
   * {@link Object}.
   */
  private static TypeName accessibleType(DependencyRequest dependency) {
    TypeName typeName = dependency.kind().typeName(accessibleType(dependency.key().type()));
    return dependency.requestsPrimitiveType() ? typeName.unbox() : typeName;
  }

  /**
   * Returns the accessible type for {@code type}. If the raw type is not accessible, returns {@link
   * Object}.
   */
  private static TypeName accessibleType(TypeMirror type) {
    return isRawTypePubliclyAccessible(type) ? TypeName.get(type) : TypeName.OBJECT;
  }

  private static CodeBlock callInjectionMethod(
      String methodName,
      List<CodeBlock> arguments,
      ClassName enclosingClass,
      ClassName requestingClass) {
    CodeBlock.Builder invocation = CodeBlock.builder();
    if (!enclosingClass.equals(requestingClass)) {
      invocation.add("$T.", enclosingClass);
    }
    return invocation.add("$L($L)", methodName, makeParametersCodeBlock(arguments)).build();
  }

  private enum ReceiverAccessibility {
    CAST_IF_NOT_PUBLIC {
      @Override
      TypeName parameterType(TypeMirror type) {
        return accessibleType(type);
      }

      @Override
      CodeBlock potentiallyCast(CodeBlock instance, TypeMirror instanceType) {
        return instanceWithPotentialCast(instance, instanceType);
      }
    },
    IGNORE {
      @Override
      TypeName parameterType(TypeMirror type) {
        return TypeName.get(type);
      }

      @Override
      CodeBlock potentiallyCast(CodeBlock instance, TypeMirror instanceType) {
        return instance;
      }
    },
    ;

    abstract TypeName parameterType(TypeMirror type);
    abstract CodeBlock potentiallyCast(CodeBlock instance, TypeMirror instanceType);
  }

  private static CodeBlock instanceWithPotentialCast(CodeBlock instance, TypeMirror instanceType) {
    return isRawTypePubliclyAccessible(instanceType)
        ? instance
        : CodeBlock.of("(($T) $L)", instanceType, instance);
  }

  private static MethodSpec methodProxy(
      ExecutableElement method, String methodName, ReceiverAccessibility receiverAccessibility) {
    TypeElement enclosingType = MoreElements.asType(method.getEnclosingElement());
    MethodSpec.Builder methodBuilder = methodBuilder(methodName).addModifiers(PUBLIC, STATIC);

    UniqueNameSet nameSet = new UniqueNameSet();
    if (!method.getModifiers().contains(STATIC)) {
      methodBuilder.addParameter(
          receiverAccessibility.parameterType(enclosingType.asType()),
          nameSet.getUniqueName("instance"));
    }
    CodeBlock arguments = copyParameters(method, methodBuilder, nameSet);
    if (!method.getReturnType().getKind().equals(VOID)) {
      methodBuilder.returns(TypeName.get(method.getReturnType()));
      getNullableType(method)
          .ifPresent(nullableType -> CodeBlocks.addAnnotation(methodBuilder, nullableType));
      methodBuilder.addCode("return ");
    }
    if (method.getModifiers().contains(STATIC)) {
      methodBuilder.addCode("$T", rawTypeName(TypeName.get(enclosingType.asType())));
    } else {
      copyTypeParameters(enclosingType, methodBuilder);
      // "instance" is guaranteed b/c it was the first name into the UniqueNameSet
      methodBuilder.addCode(
          receiverAccessibility.potentiallyCast(CodeBlock.of("instance"), enclosingType.asType()));
    }
    copyTypeParameters(method, methodBuilder);
    copyThrows(method, methodBuilder);

    methodBuilder.addCode(".$N($L);", method.getSimpleName(), arguments);
    return methodBuilder.build();
  }

  private static MethodSpec fieldProxy(VariableElement field, String methodName) {
    TypeElement enclosingType = MoreElements.asType(field.getEnclosingElement());
    MethodSpec.Builder methodBuilder = methodBuilder(methodName).addModifiers(PUBLIC, STATIC);
    copyTypeParameters(enclosingType, methodBuilder);

    UniqueNameSet nameSet = new UniqueNameSet();
    String instanceName = nameSet.getUniqueName("instance");
    methodBuilder.addParameter(accessibleType(enclosingType.asType()), instanceName);
    return methodBuilder
        .addCode(
            "$L.$L = $L;",
            instanceWithPotentialCast(CodeBlock.of(instanceName), enclosingType.asType()),
            field.getSimpleName(),
            copyParameter(field, methodBuilder, nameSet))
        .build();
  }

  private static void copyThrows(ExecutableElement method, MethodSpec.Builder methodBuilder) {
    for (TypeMirror thrownType : method.getThrownTypes()) {
      methodBuilder.addException(TypeName.get(thrownType));
    }
  }

  private static CodeBlock copyParameters(
      ExecutableElement method, MethodSpec.Builder methodBuilder, UniqueNameSet nameSet) {
    ImmutableList.Builder<CodeBlock> argumentsBuilder = ImmutableList.builder();
    for (VariableElement parameter : method.getParameters()) {
      argumentsBuilder.add(copyParameter(parameter, methodBuilder, nameSet));
    }
    methodBuilder.varargs(method.isVarArgs());
    return makeParametersCodeBlock(argumentsBuilder.build());
  }

  private static CodeBlock copyParameter(
      VariableElement element, MethodSpec.Builder methodBuilder, UniqueNameSet nameSet) {
    TypeMirror elementType = element.asType();
    boolean useObject = !isRawTypePubliclyAccessible(elementType);
    TypeName typeName = useObject ? TypeName.OBJECT : TypeName.get(elementType);
    String name = nameSet.getUniqueName(element.getSimpleName().toString());
    ParameterSpec parameter =
        ParameterSpec.builder(typeName, name).build();
    methodBuilder.addParameter(parameter);
    return useObject
        ? CodeBlock.of("($T) $N", elementType, parameter)
        : CodeBlock.of("$N", parameter);
  }

  private static void copyTypeParameters(
      Parameterizable parameterizable, MethodSpec.Builder methodBuilder) {
    for (TypeParameterElement typeParameterElement : parameterizable.getTypeParameters()) {
      methodBuilder.addTypeVariable(TypeVariableName.get(typeParameterElement));
    }
  }
}
