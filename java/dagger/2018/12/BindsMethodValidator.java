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

import static dagger.internal.codegen.BindingMethodValidator.Abstractness.MUST_BE_ABSTRACT;
import static dagger.internal.codegen.BindingMethodValidator.AllowsMultibindings.ALLOWS_MULTIBINDINGS;
import static dagger.internal.codegen.BindingMethodValidator.ExceptionSuperclass.RUNTIME_EXCEPTION;

import com.google.auto.common.MoreTypes;
import com.google.common.collect.ImmutableSet;
import dagger.Binds;
import dagger.Module;
import dagger.producers.ProducerModule;
import javax.inject.Inject;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.VariableElement;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Types;

/**
 * A validator for {@link Binds} methods.
 */
final class BindsMethodValidator extends BindingMethodValidator {
  private final Types types;
  private final BindsTypeChecker bindsTypeChecker;

  @Inject
  BindsMethodValidator(
      DaggerElements elements, Types types, DependencyRequestValidator dependencyRequestValidator) {
    super(
        elements,
        types,
        Binds.class,
        ImmutableSet.of(Module.class, ProducerModule.class),
        dependencyRequestValidator,
        MUST_BE_ABSTRACT,
        RUNTIME_EXCEPTION,
        ALLOWS_MULTIBINDINGS);
    this.types = types;
    this.bindsTypeChecker = new BindsTypeChecker(types, elements);
  }

  @Override
  protected void checkMethod(ValidationReport.Builder<ExecutableElement> builder) {
    super.checkMethod(builder);
    checkParameters(builder);
  }

  @Override
  protected void checkParameters(ValidationReport.Builder<ExecutableElement> builder) {
    ExecutableElement method = builder.getSubject();
    if (method.getParameters().size() != 1) {
      builder.addError(
          bindingMethods(
              "must have exactly one parameter, whose type is assignable to the return type"));
    } else {
      super.checkParameters(builder);
    }
  }

  @Override
  protected void checkParameter(
      ValidationReport.Builder<ExecutableElement> builder, VariableElement parameter) {
    super.checkParameter(builder, parameter);
    ExecutableElement method = builder.getSubject();
    TypeMirror leftHandSide = boxIfNecessary(method.getReturnType());
    TypeMirror rightHandSide = parameter.asType();
    ContributionType contributionType = ContributionType.fromBindingMethod(method);
    if (contributionType.equals(ContributionType.SET_VALUES) && !SetType.isSet(leftHandSide)) {
      builder.addError(
          "@Binds @ElementsIntoSet methods must return a Set and take a Set parameter");
    }

    if (!bindsTypeChecker.isAssignable(rightHandSide, leftHandSide, contributionType)) {
      // TODO(ronshapiro): clarify this error message for @ElementsIntoSet cases, where the
      // right-hand-side might not be assignable to the left-hand-side, but still compatible with
      // Set.addAll(Collection<? extends E>)
      builder.addError("@Binds methods' parameter type must be assignable to the return type");
    }
  }

  private TypeMirror boxIfNecessary(TypeMirror maybePrimitive) {
    if (maybePrimitive.getKind().isPrimitive()) {
      return types.boxedClass(MoreTypes.asPrimitiveType(maybePrimitive)).asType();
    }
    return maybePrimitive;
  }
}
