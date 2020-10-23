/*
 * Copyright (C) 2016 The Dagger Authors.
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

import static com.google.auto.common.AnnotationMirrors.getAnnotationElementAndValue;
import static dagger.internal.codegen.ConfigurationAnnotations.getModuleAnnotation;
import static dagger.internal.codegen.ConfigurationAnnotations.getModuleSubcomponents;
import static dagger.internal.codegen.ConfigurationAnnotations.getSubcomponentBuilder;

import com.google.auto.common.MoreTypes;
import com.google.auto.value.AutoValue;
import com.google.common.collect.ImmutableSet;
import java.util.Optional;
import javax.lang.model.element.AnnotationMirror;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.TypeElement;

/**
 * A declaration for a subcomponent that is included in a module via {@link
 * dagger.Module#subcomponents()}.
 */
@AutoValue
abstract class SubcomponentDeclaration extends BindingDeclaration {
  /**
   * Key for the {@link dagger.Subcomponent.Builder} or {@link
   * dagger.producers.ProductionSubcomponent.Builder} of {@link #subcomponentType()}.
   */
  @Override
  public abstract Key key();

  @Override
  abstract Optional<? extends ExecutableElement> bindingElement();

  /**
   * The type element that defines the {@link dagger.Subcomponent} or {@link
   * dagger.producers.ProductionSubcomponent} for this declaration.
   */
  abstract TypeElement subcomponentType();

  abstract AnnotationMirror moduleAnnotation();

  static class Factory {
    private final Key.Factory keyFactory;

    public Factory(Key.Factory keyFactory) {
      this.keyFactory = keyFactory;
    }

    ImmutableSet<SubcomponentDeclaration> forModule(TypeElement module) {
      ImmutableSet.Builder<SubcomponentDeclaration> declarations = ImmutableSet.builder();
      AnnotationMirror moduleAnnotation = getModuleAnnotation(module).get();
      ExecutableElement subcomponentAttribute =
          getAnnotationElementAndValue(moduleAnnotation, "subcomponents").getKey();
      for (TypeElement subcomponent :
          MoreTypes.asTypeElements(getModuleSubcomponents(moduleAnnotation))) {
        declarations.add(
            new AutoValue_SubcomponentDeclaration(
                Optional.of(module),
                keyFactory.forSubcomponentBuilder(
                    getSubcomponentBuilder(subcomponent).get().asType()),
                Optional.of(subcomponentAttribute),
                subcomponent,
                moduleAnnotation));
      }
      return declarations.build();
    }
  }
}
