/*
 * Copyright (C) 2015 Google, Inc.
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

import com.google.auto.common.MoreTypes;
import com.google.auto.value.AutoValue;
import com.google.common.collect.ImmutableSet;
import dagger.Module;
import dagger.Multibindings;
import dagger.internal.codegen.BindingType.HasBindingType;
import dagger.internal.codegen.ContributionType.HasContributionType;
import dagger.internal.codegen.Key.HasKey;
import dagger.internal.codegen.SourceElement.HasSourceElement;
import dagger.producers.Producer;
import dagger.producers.ProducerModule;
import java.util.Map;
import java.util.Set;
import javax.inject.Provider;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.TypeElement;
import javax.lang.model.type.DeclaredType;
import javax.lang.model.type.ExecutableType;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Elements;
import javax.lang.model.util.Types;

import static com.google.auto.common.MoreElements.getLocalAndInheritedMethods;
import static com.google.auto.common.MoreElements.isAnnotationPresent;
import static com.google.common.base.Preconditions.checkArgument;
import static javax.lang.model.element.ElementKind.INTERFACE;

/**
 * A declaration that a multibinding with a certain key is available to be injected in a component
 * even if the component has no multibindings for that key. Identified by a map- or set-returning
 * method in a {@link Multibindings @Multibindings}-annotated interface nested within a module.
 */
@AutoValue
abstract class MultibindingDeclaration
    implements HasBindingType, HasKey, HasSourceElement, HasContributionType {

  /**
   * The method in a {@link Multibindings @Multibindings} interface that declares that this map or
   * set is available to be injected.
   */
  @Override
  public abstract SourceElement sourceElement();

  /**
   * The map or set key whose availability is declared. For maps, this will be {@code Map<K, F<V>>},
   * where {@code F} is either {@link Provider} or {@link Producer}. For sets, this will be
   * {@code Set<T>}.
   */
  @Override
  public abstract Key key();

  /**
   * {@link ContributionType#SET} if the declared type is a {@link Set}, or
   * {@link ContributionType#MAP} if it is a {@link Map}.
   */
  @Override
  public abstract ContributionType contributionType();

  /**
   * {@link BindingType#PROVISION} if the {@link Multibindings @Multibindings}-annotated interface
   * is nested in a {@link Module @Module}, or {@link BindingType#PROVISION} if it is nested in a
   * {@link ProducerModule @ProducerModule}.
   */
  @Override
  public abstract BindingType bindingType();

  /**
   * A factory for {@link MultibindingDeclaration}s.
   */
  static final class Factory {
    private final Elements elements;
    private final Types types;
    private final Key.Factory keyFactory;
    private final TypeElement objectElement;

    Factory(Elements elements, Types types, Key.Factory keyFactory) {
      this.elements = elements;
      this.types = types;
      this.keyFactory = keyFactory;
      this.objectElement = elements.getTypeElement(Object.class.getCanonicalName());
    }

    /**
     * Creates multibinding declarations for each method in a
     * {@link Multibindings @Multibindings}-annotated interface.
     */
    ImmutableSet<MultibindingDeclaration> forDeclaredInterface(TypeElement interfaceElement) {
      checkArgument(interfaceElement.getKind().equals(INTERFACE));
      checkArgument(isAnnotationPresent(interfaceElement, Multibindings.class));
      BindingType bindingType = bindingType(interfaceElement);
      DeclaredType interfaceType = MoreTypes.asDeclared(interfaceElement.asType());

      ImmutableSet.Builder<MultibindingDeclaration> declarations = ImmutableSet.builder();
      for (ExecutableElement method : getLocalAndInheritedMethods(interfaceElement, elements)) {
        if (!method.getEnclosingElement().equals(objectElement)) {
          ExecutableType methodType =
              MoreTypes.asExecutable(types.asMemberOf(interfaceType, method));
          declarations.add(forDeclaredMethod(bindingType, method, methodType, interfaceElement));
        }
      }
      return declarations.build();
    }

    private BindingType bindingType(TypeElement interfaceElement) {
      if (isAnnotationPresent(interfaceElement.getEnclosingElement(), Module.class)) {
        return BindingType.PROVISION;
      } else if (isAnnotationPresent(
          interfaceElement.getEnclosingElement(), ProducerModule.class)) {
        return BindingType.PRODUCTION;
      } else {
        throw new IllegalArgumentException(
            "Expected " + interfaceElement + " to be nested in a @Module or @ProducerModule");
      }
    }

    private MultibindingDeclaration forDeclaredMethod(
        BindingType bindingType,
        ExecutableElement method,
        ExecutableType methodType,
        TypeElement interfaceElement) {
      TypeMirror returnType = methodType.getReturnType();
      checkArgument(
          SetType.isSet(returnType) || MapType.isMap(returnType),
          "%s must return a set or map",
          method);
      return new AutoValue_MultibindingDeclaration(
          SourceElement.forElement(method, interfaceElement),
          keyFactory.forMultibindingsMethod(bindingType, methodType, method),
          contributionType(returnType),
          bindingType);
    }

    private ContributionType contributionType(TypeMirror returnType) {
      if (MapType.isMap(returnType)) {
        return ContributionType.MAP;
      } else if (SetType.isSet(returnType)) {
        return ContributionType.SET;
      } else {
        throw new IllegalArgumentException("Must be Map or Set: " + returnType);
      }
    }
  }
}
