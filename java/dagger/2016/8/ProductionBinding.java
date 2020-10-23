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

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Preconditions.checkNotNull;
import static dagger.internal.codegen.MapKeys.getMapKey;
import static dagger.internal.codegen.MoreAnnotationMirrors.wrapOptionalInEquivalence;
import static javax.lang.model.element.ElementKind.METHOD;

import com.google.auto.common.MoreTypes;
import com.google.auto.value.AutoValue;
import com.google.common.base.Optional;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import dagger.producers.Producer;
import java.util.Set;
import javax.annotation.CheckReturnValue;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.TypeElement;
import javax.lang.model.type.ExecutableType;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Types;

/**
 * A value object representing the mechanism by which a {@link Key} can be produced. New instances
 * should be created using an instance of the {@link Factory}.
 *
 * @author Jesse Beder
 * @since 2.0
 */
@AutoValue
abstract class ProductionBinding extends ContributionBinding {

  @Override
  public BindingType bindingType() {
    return BindingType.PRODUCTION;
  }

  @Override
  Optional<ProductionBinding> unresolved() {
    return Optional.absent();
  }

  @Override
  Set<DependencyRequest> frameworkDependencies() {
    return new ImmutableSet.Builder<DependencyRequest>()
        .addAll(executorRequest().asSet())
        .addAll(monitorRequest().asSet())
        .build();
  }

  /** Returns the list of types in the throws clause of the method. */
  abstract ImmutableList<? extends TypeMirror> thrownTypes();

  /**
   * If this production requires an executor, this will be the corresponding request.  All
   * production bindings from {@code @Produces} methods will have an executor request, but
   * synthetic production bindings may not.
   */
  abstract Optional<DependencyRequest> executorRequest();

  /** If this production requires a monitor, this will be the corresponding request.  All
   * production bindings from {@code @Produces} methods will have a monitor request, but synthetic
   * production bindings may not.
   */
  abstract Optional<DependencyRequest> monitorRequest();

  private static Builder builder() {
    return new AutoValue_ProductionBinding.Builder()
        .dependencies(ImmutableList.<DependencyRequest>of())
        .thrownTypes(ImmutableList.<TypeMirror>of());
  }

  @AutoValue.Builder
  @CanIgnoreReturnValue
  abstract static class Builder extends ContributionBinding.Builder<Builder> {

    abstract Builder thrownTypes(Iterable<? extends TypeMirror> thrownTypes);

    abstract Builder executorRequest(DependencyRequest executorRequest);

    abstract Builder monitorRequest(DependencyRequest monitorRequest);

    @CheckReturnValue
    abstract ProductionBinding build();
  }

  static final class Factory {
    private final Types types;
    private final Key.Factory keyFactory;
    private final DependencyRequest.Factory dependencyRequestFactory;

    Factory(
        Types types, Key.Factory keyFactory, DependencyRequest.Factory dependencyRequestFactory) {
      this.types = types;
      this.keyFactory = keyFactory;
      this.dependencyRequestFactory = dependencyRequestFactory;
    }

    ProductionBinding forProducesMethod(
        ExecutableElement producesMethod, TypeElement contributedBy) {
      checkArgument(producesMethod.getKind().equals(METHOD));
      Key key = keyFactory.forProducesMethod(producesMethod, contributedBy);
      ExecutableType resolvedMethod =
          MoreTypes.asExecutable(
              types.asMemberOf(MoreTypes.asDeclared(contributedBy.asType()), producesMethod));
      ImmutableSet<DependencyRequest> dependencies =
          dependencyRequestFactory.forRequiredResolvedVariables(
              producesMethod.getParameters(),
              resolvedMethod.getParameterTypes());
      DependencyRequest executorRequest =
          dependencyRequestFactory.forProductionImplementationExecutor();
      DependencyRequest monitorRequest =
          dependencyRequestFactory.forProductionComponentMonitor();
      Kind kind = MoreTypes.isTypeOf(ListenableFuture.class, producesMethod.getReturnType())
          ? Kind.FUTURE_PRODUCTION
          : Kind.IMMEDIATE;
      // TODO(beder): Add nullability checking with Java 8.
      return ProductionBinding.builder()
          .contributionType(ContributionType.fromBindingMethod(producesMethod))
          .bindingElement(producesMethod)
          .contributingModule(contributedBy)
          .key(key)
          .dependencies(dependencies)
          .wrappedMapKey(wrapOptionalInEquivalence(getMapKey(producesMethod)))
          .bindingKind(kind)
          .thrownTypes(producesMethod.getThrownTypes())
          .executorRequest(executorRequest)
          .monitorRequest(monitorRequest)
          .build();
    }

    /**
     * A synthetic binding of {@code Map<K, V>} or {@code Map<K, Produced<V>>} that depends on
     * {@code Map<K, Producer<V>>}.
     */
    ProductionBinding syntheticMapOfValuesOrProducedBinding(Key mapOfValuesOrProducedKey) {
      checkNotNull(mapOfValuesOrProducedKey);
      Optional<Key> mapOfProducersKey =
          keyFactory.implicitMapProducerKeyFrom(mapOfValuesOrProducedKey);
      checkArgument(
          mapOfProducersKey.isPresent(),
          "%s is not a key for of Map<K, V> or Map<K, Produced<V>>",
          mapOfValuesOrProducedKey);
      DependencyRequest requestForMapOfProducers =
          dependencyRequestFactory.forImplicitMapBinding(mapOfProducersKey.get());
      return ProductionBinding.builder()
          .contributionType(ContributionType.UNIQUE)
          .key(mapOfValuesOrProducedKey)
          .dependencies(requestForMapOfProducers)
          .bindingKind(Kind.SYNTHETIC_MAP)
          .build();
    }

    /**
     * A synthetic binding that depends explicitly on a set of individual provision or production
     * multibinding contribution methods.
     *
     * <p>Note that these could be set multibindings or map multibindings.
     */
    ProductionBinding syntheticMultibinding(
        Key key, Iterable<ContributionBinding> multibindingContributions) {
      return ProductionBinding.builder()
          .contributionType(ContributionType.UNIQUE)
          .key(key)
          .dependencies(
              dependencyRequestFactory.forMultibindingContributions(multibindingContributions))
          .bindingKind(Kind.forMultibindingKey(key))
          .build();
    }

    ProductionBinding forComponentMethod(ExecutableElement componentMethod) {
      checkNotNull(componentMethod);
      checkArgument(componentMethod.getKind().equals(METHOD));
      checkArgument(componentMethod.getParameters().isEmpty());
      checkArgument(MoreTypes.isTypeOf(ListenableFuture.class, componentMethod.getReturnType()));
      return ProductionBinding.builder()
          .contributionType(ContributionType.UNIQUE)
          .bindingElement(componentMethod)
          .key(keyFactory.forProductionComponentMethod(componentMethod))
          .bindingKind(Kind.COMPONENT_PRODUCTION)
          .thrownTypes(componentMethod.getThrownTypes())
          .build();
    }

    ProductionBinding delegate(
        DelegateDeclaration delegateDeclaration, ProductionBinding delegateBinding) {
      return ProductionBinding.builder()
          .contributionType(delegateDeclaration.contributionType())
          .bindingElement(delegateDeclaration.bindingElement().get())
          .contributingModule(delegateDeclaration.contributingModule().get())
          .key(keyFactory.forDelegateBinding(delegateDeclaration, Producer.class))
          .dependencies(delegateDeclaration.delegateRequest())
          .nullableType(delegateBinding.nullableType())
          .wrappedMapKey(delegateDeclaration.wrappedMapKey())
          .bindingKind(Kind.SYNTHETIC_DELEGATE_BINDING)
          .build();
    }
  }
}
