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

import com.squareup.javapoet.ClassName;

/**
 * A binding expression for instances bound with {@link dagger.BindsInstance} and instances of
 * {@linkplain dagger.Component#dependencies() component} and {@linkplain
 * dagger.producers.ProductionComponent#dependencies() production component dependencies}.
 */
final class ComponentRequirementBindingExpression extends SimpleInvocationBindingExpression {
  private final ComponentRequirement componentRequirement;
  private final ComponentRequirementFields componentRequirementFields;

  ComponentRequirementBindingExpression(
      ResolvedBindings resolvedBindings,
      ComponentRequirement componentRequirement,
      ComponentRequirementFields componentRequirementFields) {
    super(resolvedBindings);
    this.componentRequirement = componentRequirement;
    this.componentRequirementFields = componentRequirementFields;
  }

  @Override
  Expression getDependencyExpression(ClassName requestingClass) {
    return Expression.create(
        componentRequirement.type(),
        componentRequirementFields.getExpression(componentRequirement, requestingClass));
  }
}
