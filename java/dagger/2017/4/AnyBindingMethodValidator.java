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

import static com.google.auto.common.MoreElements.isAnnotationPresent;
import static com.google.common.collect.Iterables.getOnlyElement;
import static com.google.common.collect.Maps.uniqueIndex;
import static dagger.internal.codegen.DaggerElements.isAnyAnnotationPresent;
import static dagger.internal.codegen.ErrorMessages.tooManyBindingMethodAnnotations;
import static dagger.internal.codegen.Util.toImmutableSet;
import static java.util.Arrays.asList;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import java.lang.annotation.Annotation;
import java.util.HashMap;
import java.util.Map;
import javax.lang.model.element.ExecutableElement;

/** Validates any binding method. */
final class AnyBindingMethodValidator {

  private final ImmutableMap<Class<? extends Annotation>, BindingMethodValidator> validators;
  private final Map<ExecutableElement, ValidationReport<ExecutableElement>> reports =
      new HashMap<>();

  AnyBindingMethodValidator(BindingMethodValidator... validators) {
    this.validators = uniqueIndex(asList(validators), BindingMethodValidator::methodAnnotation);
  }

  /** Returns the binding method annotations considered by this validator. */
  ImmutableSet<Class<? extends Annotation>> methodAnnotations() {
    return validators.keySet();
  }

  /**
   * Returns {@code true} if {@code method} is annotated with at least one of {@link
   * #methodAnnotations()}.
   */
  boolean isBindingMethod(ExecutableElement method) {
    return isAnyAnnotationPresent(method, methodAnnotations());
  }

  /**
   * Returns a validation report for a method.
   *
   * <ul>
   *   <li>Reports an error if {@code method} is annotated with more than one {@linkplain
   *       #methodAnnotations() binding method annotation}.
   *   <li>Validates {@code method} with the {@link BindingMethodValidator} for the single
   *       {@linkplain #methodAnnotations() binding method annotation}.
   * </ul>
   *
   * @throws IllegalArgumentException if {@code method} is not annotated by any {@linkplain
   *     #methodAnnotations() binding method annotation}
   */
  ValidationReport<ExecutableElement> validate(ExecutableElement method) {
    return reports.computeIfAbsent(method, this::validateUncached);
  }

  /**
   * Returns {@code true} if {@code method} was already {@linkplain #validate(ExecutableElement)
   * validated}.
   */
  boolean wasAlreadyValidated(ExecutableElement method) {
    return reports.containsKey(method);
  }

  private ValidationReport<ExecutableElement> validateUncached(ExecutableElement method) {
    ValidationReport.Builder<ExecutableElement> report = ValidationReport.about(method);
    ImmutableSet<? extends Class<? extends Annotation>> bindingMethodAnnotations =
        methodAnnotations()
            .stream()
            .filter(annotation -> isAnnotationPresent(method, annotation))
            .collect(toImmutableSet());
    switch (bindingMethodAnnotations.size()) {
      case 0:
        throw new IllegalArgumentException(
            String.format("%s has no binding method annotation", method));

      case 1:
        report.addSubreport(
            validators.get(getOnlyElement(bindingMethodAnnotations)).validate(method));
        break;

      default:
        report.addError(tooManyBindingMethodAnnotations(method, methodAnnotations()), method);
        break;
    }
    return report.build();
  }
}
