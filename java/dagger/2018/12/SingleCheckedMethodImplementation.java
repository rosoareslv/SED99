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

import static com.google.common.base.Preconditions.checkNotNull;
import static dagger.internal.codegen.ComponentImplementation.FieldSpecKind.PRIVATE_METHOD_SCOPED_FIELD;
import static javax.lang.model.element.Modifier.PRIVATE;
import static javax.lang.model.element.Modifier.VOLATILE;

import com.google.common.base.Supplier;
import com.google.common.base.Suppliers;
import com.squareup.javapoet.CodeBlock;
import com.squareup.javapoet.FieldSpec;
import com.squareup.javapoet.TypeName;
import dagger.internal.MemoizedSentinel;
import dagger.model.RequestKind;
import java.util.Optional;

/**
 * Defines a method body and return type for single checked caching of the given {@link
 * BindingExpression}.
 */
final class SingleCheckedMethodImplementation extends BindingMethodImplementation {

  private final ComponentImplementation componentImplementation;
  private final ResolvedBindings resolvedBindings;
  private final ContributionBinding binding;
  private final BindingRequest request;
  private final Supplier<FieldSpec> field = Suppliers.memoize(this::createField);

  SingleCheckedMethodImplementation(
      ComponentImplementation component,
      ResolvedBindings resolvedBindings,
      BindingRequest request,
      BindingExpression bindingExpression,
      DaggerTypes types) {
    super(component, resolvedBindings.contributionBinding(), request, bindingExpression, types);
    this.componentImplementation = checkNotNull(component);
    this.resolvedBindings = resolvedBindings;
    this.binding = resolvedBindings.contributionBinding();
    this.request = checkNotNull(request);
  }

  @Override
  CodeBlock implementation(Supplier<CodeBlock> simpleBindingExpression) {
    String fieldExpression = field.get().name.equals("local") ? "this.local" : field.get().name;

    CodeBlock.Builder builder = CodeBlock.builder()
        .addStatement("Object local = $N", fieldExpression);

    if (isNullable()) {
      builder.beginControlFlow("if (local instanceof $T)", MemoizedSentinel.class);
    } else {
      builder.beginControlFlow("if (local == null)");
    }

    return builder
        .addStatement("local = $L", simpleBindingExpression.get())
        .addStatement("$N = ($T) local", fieldExpression, returnType())
        .endControlFlow()
        .addStatement("return ($T) local", returnType())
        .build();
  }

  private FieldSpec createField() {
    String name =
        componentImplementation.getUniqueFieldName(
            request.isRequestKind(RequestKind.INSTANCE)
                ? BindingVariableNamer.name(binding)
                : FrameworkField.forResolvedBindings(resolvedBindings, Optional.empty()).name());

    FieldSpec.Builder builder = FieldSpec.builder(fieldType(), name, PRIVATE, VOLATILE);
    if (isNullable()) {
      builder.initializer("new $T()", MemoizedSentinel.class);
    }

    FieldSpec field = builder.build();
    componentImplementation.addField(PRIVATE_METHOD_SCOPED_FIELD, field);
    return field;
  }

  private TypeName fieldType() {
    if (isNullable()) {
      // Nullable instances use `MemoizedSentinel` instead of `null` as the initialization value, so
      // the field type must accept that and the return type
      return TypeName.OBJECT;
    }
    TypeName returnType = TypeName.get(returnType());
    return returnType.isPrimitive() ? returnType.box() : returnType;
  }

  private boolean isNullable() {
    return request.isRequestKind(RequestKind.INSTANCE) && binding.isNullable();
  }
}
