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

import static com.google.common.base.Preconditions.checkNotNull;
import static dagger.internal.codegen.MapKeys.getMapKeyExpression;
import static dagger.internal.codegen.SourceFiles.mapFactoryClassName;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.squareup.javapoet.CodeBlock;
import dagger.internal.codegen.FrameworkFieldInitializer.FrameworkInstanceCreationExpression;
import dagger.producers.Produced;
import dagger.producers.Producer;
import javax.inject.Provider;
import javax.lang.model.type.TypeMirror;

/** A factory creation expression for a multibound map. */
// TODO(dpb): Resolve with SetFactoryCreationExpression.
final class MapFactoryCreationExpression implements FrameworkInstanceCreationExpression {

  private final GeneratedComponentModel generatedComponentModel;
  private final ComponentBindingExpressions componentBindingExpressions;
  private final BindingGraph graph;
  private final ContributionBinding binding;
  private final DaggerElements elements;

  MapFactoryCreationExpression(
      ContributionBinding binding,
      GeneratedComponentModel generatedComponentModel,
      ComponentBindingExpressions componentBindingExpressions,
      BindingGraph graph,
      DaggerElements elements) {
    this.binding = checkNotNull(binding);
    this.generatedComponentModel = checkNotNull(generatedComponentModel);
    this.componentBindingExpressions = checkNotNull(componentBindingExpressions);
    this.graph = checkNotNull(graph);
    this.elements = checkNotNull(elements);
  }

  @Override
  public CodeBlock creationExpression() {
    CodeBlock.Builder builder = CodeBlock.builder().add("$T.", mapFactoryClassName(binding));
    boolean useRawType = !generatedComponentModel.isTypeAccessible(binding.key().type());
    if (!useRawType) {
      MapType mapType = MapType.from(binding.key().type());
      // TODO(ronshapiro): either inline this into mapFactoryClassName, or add a
      // mapType.unwrappedValueType() method that doesn't require a framework type
      TypeMirror valueType = mapType.valueType();
      for (Class<?> frameworkClass :
          ImmutableSet.of(Provider.class, Producer.class, Produced.class)) {
        if (mapType.valuesAreTypeOf(frameworkClass)) {
          valueType = mapType.unwrappedValueType(frameworkClass);
          break;
        }
      }
      builder.add("<$T, $T>", mapType.keyType(), valueType);
    }

    ImmutableList<FrameworkDependency> frameworkDependencies = binding.frameworkDependencies();
    if (binding.bindingType().equals(BindingType.PROVISION)) {
      builder.add("builder($L)", frameworkDependencies.size());
    } else {
      builder.add("builder()");
    }

    for (FrameworkDependency frameworkDependency : frameworkDependencies) {
      ContributionBinding contributionBinding =
          graph.contributionBindings().get(frameworkDependency.key()).contributionBinding();
      CodeBlock value =
          componentBindingExpressions
              .getDependencyExpression(frameworkDependency, generatedComponentModel.name())
              .codeBlock();
      builder.add(
          ".put($L, $L)",
          getMapKeyExpression(contributionBinding, generatedComponentModel.name(), elements),
          useRawType ? CodeBlocks.cast(value, frameworkDependency.frameworkClass()) : value);
    }
    builder.add(".build()");

    return builder.build();
  }

  @Override
  public boolean useInnerSwitchingProvider() {
    return !binding.dependencies().isEmpty();
  }
}
