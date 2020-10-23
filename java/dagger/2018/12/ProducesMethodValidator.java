/*
 * Copyright (C) 2014 The Dagger Authors.
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

import static com.google.common.collect.Iterables.getOnlyElement;
import static dagger.internal.codegen.BindingMethodValidator.Abstractness.MUST_BE_CONCRETE;
import static dagger.internal.codegen.BindingMethodValidator.AllowsMultibindings.ALLOWS_MULTIBINDINGS;
import static dagger.internal.codegen.BindingMethodValidator.ExceptionSuperclass.EXCEPTION;
import static dagger.internal.codegen.Scopes.scopesOf;

import com.google.auto.common.MoreTypes;
import com.google.common.util.concurrent.ListenableFuture;
import dagger.multibindings.ElementsIntoSet;
import dagger.producers.ProducerModule;
import dagger.producers.Produces;
import java.util.Optional;
import java.util.Set;
import javax.inject.Inject;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.type.DeclaredType;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Types;

/**
 * A validator for {@link Produces} methods.
 */
final class ProducesMethodValidator extends BindingMethodValidator {

  @Inject
  ProducesMethodValidator(
      DaggerElements elements, Types types, DependencyRequestValidator dependencyRequestValidator) {
    super(
        elements,
        types,
        dependencyRequestValidator,
        Produces.class,
        ProducerModule.class,
        MUST_BE_CONCRETE,
        EXCEPTION,
        ALLOWS_MULTIBINDINGS);
  }
  
  @Override
  protected void checkMethod(ValidationReport.Builder<ExecutableElement> builder) {
    super.checkMethod(builder);
    checkNullable(builder);
  }

  /** Adds a warning if a {@link Produces @Produces} method is declared nullable. */
  // TODO(beder): Properly handle nullable with producer methods.
  private void checkNullable(ValidationReport.Builder<ExecutableElement> builder) {
    if (ConfigurationAnnotations.getNullableType(builder.getSubject()).isPresent()) {
      builder.addWarning("@Nullable on @Produces methods does not do anything");
    }
  }

  /** Adds an error if a {@link Produces @Produces} method has a scope annotation. */
  @Override
  protected void checkScopes(ValidationReport.Builder<ExecutableElement> builder) {
    if (!scopesOf(builder.getSubject()).isEmpty()) {
      builder.addError("@Produces methods may not have scope annotations");
    }
  }

  @Override
  protected String badReturnTypeMessage() {
    return "@Produces methods can return only a primitive, an array, a type variable, "
        + "a declared type, or a ListenableFuture of one of those types";
  }

  /**
   * {@inheritDoc}
   *
   * <p>Allows {@code keyType} to be a {@link ListenableFuture} of an otherwise-valid key type.
   */
  @Override
  protected void checkKeyType(
      ValidationReport.Builder<ExecutableElement> reportBuilder, TypeMirror keyType) {
    Optional<TypeMirror> typeToCheck = unwrapListenableFuture(reportBuilder, keyType);
    if (typeToCheck.isPresent()) {
      super.checkKeyType(reportBuilder, typeToCheck.get());
    }
  }

  /**
   * {@inheritDoc}
   *
   * <p>Allows an {@link ElementsIntoSet @ElementsIntoSet} or {@code SET_VALUES} method to return a
   * {@link ListenableFuture} of a {@link Set} as well.
   */
  @Override
  protected void checkSetValuesType(ValidationReport.Builder<ExecutableElement> builder) {
    Optional<TypeMirror> typeToCheck =
        unwrapListenableFuture(builder, builder.getSubject().getReturnType());
    if (typeToCheck.isPresent()) {
      checkSetValuesType(builder, typeToCheck.get());
    }
  }

  @Override
  protected String badSetValuesTypeMessage() {
    return "@Produces methods of type set values must return a Set or ListenableFuture of Set";
  }

  private static Optional<TypeMirror> unwrapListenableFuture(
      ValidationReport.Builder<ExecutableElement> reportBuilder, TypeMirror type) {
    if (MoreTypes.isType(type) && MoreTypes.isTypeOf(ListenableFuture.class, type)) {
      DeclaredType declaredType = MoreTypes.asDeclared(type);
      if (declaredType.getTypeArguments().isEmpty()) {
        reportBuilder.addError("@Produces methods cannot return a raw ListenableFuture");
        return Optional.empty();
      } else {
        return Optional.of((TypeMirror) getOnlyElement(declaredType.getTypeArguments()));
      }
    }
    return Optional.of(type);
  }
}
