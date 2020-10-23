/*
 * Copyright (C) 2014 Google, Inc.
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
import com.google.common.base.Function;
import com.google.common.base.Optional;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.collect.FluentIterable;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.ImmutableSetMultimap;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.google.common.collect.TreeTraverser;
import dagger.Component;
import dagger.Subcomponent;
import dagger.internal.codegen.ComponentDescriptor.ComponentMethodDescriptor;
import dagger.producers.ProductionComponent;
import java.util.ArrayDeque;
import java.util.Collection;
import java.util.Deque;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import javax.inject.Inject;
import javax.lang.model.element.AnnotationMirror;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.TypeElement;
import javax.lang.model.util.ElementFilter;
import javax.lang.model.util.Elements;

import static com.google.auto.common.MoreElements.getAnnotationMirror;
import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Predicates.in;
import static com.google.common.base.Verify.verify;
import static dagger.internal.codegen.ComponentDescriptor.isComponentContributionMethod;
import static dagger.internal.codegen.ComponentDescriptor.isComponentProductionMethod;
import static dagger.internal.codegen.ComponentDescriptor.ComponentMethodDescriptor.isOfKind;
import static dagger.internal.codegen.ComponentDescriptor.ComponentMethodKind.SUBCOMPONENT_BUILDER;
import static dagger.internal.codegen.ComponentDescriptor.Kind.PRODUCTION_COMPONENT;
import static dagger.internal.codegen.ConfigurationAnnotations.getComponentDependencies;
import static dagger.internal.codegen.Key.indexByKey;
import static javax.lang.model.element.Modifier.STATIC;

/**
 * The canonical representation of a full-resolved graph.
 *
 * @author Gregory Kick
 */
@AutoValue
abstract class BindingGraph {
  abstract ComponentDescriptor componentDescriptor();
  abstract ImmutableMap<BindingKey, ResolvedBindings> resolvedBindings();
  abstract ImmutableMap<ExecutableElement, BindingGraph> subgraphs();

  /**
   * Returns the set of modules that are owned by this graph regardless of whether or not any of
   * their bindings are used in this graph. For graphs representing top-level {@link Component
   * components}, this set will be the same as
   * {@linkplain ComponentDescriptor#transitiveModules the component's transitive modules}. For
   * {@linkplain Subcomponent subcomponents}, this set will be the transitive modules that are not
   * owned by any of their ancestors.
   */
  abstract ImmutableSet<ModuleDescriptor> ownedModules();

  ImmutableSet<TypeElement> ownedModuleTypes() {
    return FluentIterable.from(ownedModules())
        .transform(ModuleDescriptor.getModuleElement())
        .toSet();
  }

  private static final TreeTraverser<BindingGraph> SUBGRAPH_TRAVERSER =
      new TreeTraverser<BindingGraph>() {
        @Override
        public Iterable<BindingGraph> children(BindingGraph node) {
          return node.subgraphs().values();
        }
      };

  /**
   * Returns the set of types necessary to implement the component, but are not part of the injected
   * graph.  This includes modules, component dependencies and an {@link Executor} in the case of
   * {@link ProductionComponent}.
   */
  ImmutableSet<TypeElement> componentRequirements() {
    return SUBGRAPH_TRAVERSER
        .preOrderTraversal(this)
        .transformAndConcat(
            new Function<BindingGraph, Iterable<ResolvedBindings>>() {
              @Override
              public Iterable<ResolvedBindings> apply(BindingGraph input) {
                return input.resolvedBindings().values();
              }
            })
        .transformAndConcat(
            new Function<ResolvedBindings, Set<ContributionBinding>>() {
              @Override
              public Set<ContributionBinding> apply(ResolvedBindings input) {
                return input.contributionBindings();
              }
            })
        .transformAndConcat(
            new Function<ContributionBinding, Set<TypeElement>>() {
              @Override
              public Set<TypeElement> apply(ContributionBinding input) {
                return input.bindingElement().getModifiers().contains(STATIC)
                    ? ImmutableSet.<TypeElement>of()
                    : input.contributedBy().asSet();
              }
            })
        .filter(in(ownedModuleTypes()))
        .append(componentDescriptor().dependencies())
        .append(componentDescriptor().executorDependency().asSet())
        .toSet();
  }

  /**
   * Returns the {@link ComponentDescriptor}s for this component and its subcomponents.
   */
  ImmutableSet<ComponentDescriptor> componentDescriptors() {
    return SUBGRAPH_TRAVERSER
        .preOrderTraversal(this)
        .transform(
            new Function<BindingGraph, ComponentDescriptor>() {
              @Override
              public ComponentDescriptor apply(BindingGraph graph) {
                return graph.componentDescriptor();
              }
            })
        .toSet();
  }

  ImmutableSet<TypeElement> availableDependencies() {
    return new ImmutableSet.Builder<TypeElement>()
        .addAll(componentDescriptor().transitiveModuleTypes())
        .addAll(componentDescriptor().dependencies())
        .addAll(componentDescriptor().executorDependency().asSet())
        .build();
  }

  static final class Factory {
    private final Elements elements;
    private final InjectBindingRegistry injectBindingRegistry;
    private final Key.Factory keyFactory;
    private final ProvisionBinding.Factory provisionBindingFactory;
    private final ProductionBinding.Factory productionBindingFactory;

    Factory(Elements elements,
        InjectBindingRegistry injectBindingRegistry,
        Key.Factory keyFactory,
        ProvisionBinding.Factory provisionBindingFactory,
        ProductionBinding.Factory productionBindingFactory) {
      this.elements = elements;
      this.injectBindingRegistry = injectBindingRegistry;
      this.keyFactory = keyFactory;
      this.provisionBindingFactory = provisionBindingFactory;
      this.productionBindingFactory = productionBindingFactory;
    }

    BindingGraph create(ComponentDescriptor componentDescriptor) {
      return create(Optional.<Resolver>absent(), componentDescriptor);
    }

    private BindingGraph create(
        Optional<Resolver> parentResolver, ComponentDescriptor componentDescriptor) {
      ImmutableSet.Builder<ContributionBinding> explicitBindingsBuilder = ImmutableSet.builder();

      // binding for the component itself
      TypeElement componentDefinitionType = componentDescriptor.componentDefinitionType();
      explicitBindingsBuilder.add(provisionBindingFactory.forComponent(componentDefinitionType));

      // Collect Component dependencies.
      Optional<AnnotationMirror> componentMirror =
          getAnnotationMirror(componentDefinitionType, Component.class)
              .or(getAnnotationMirror(componentDefinitionType, ProductionComponent.class));
      ImmutableSet<TypeElement> componentDependencyTypes = componentMirror.isPresent()
          ? MoreTypes.asTypeElements(getComponentDependencies(componentMirror.get()))
          : ImmutableSet.<TypeElement>of();
      for (TypeElement componentDependency : componentDependencyTypes) {
        explicitBindingsBuilder.add(provisionBindingFactory.forComponent(componentDependency));
        List<ExecutableElement> dependencyMethods =
            ElementFilter.methodsIn(elements.getAllMembers(componentDependency));
        for (ExecutableElement method : dependencyMethods) {
          // MembersInjection methods aren't "provided" explicitly, so ignore them.
          if (isComponentContributionMethod(elements, method)) {
            explicitBindingsBuilder.add(
                componentDescriptor.kind().equals(PRODUCTION_COMPONENT)
                        && isComponentProductionMethod(elements, method)
                    ? productionBindingFactory.forComponentMethod(method)
                    : provisionBindingFactory.forComponentMethod(method));
          }
        }
      }

      // Bindings for subcomponent builders.
      for (ComponentMethodDescriptor subcomponentMethodDescriptor :
          Iterables.filter(
              componentDescriptor.subcomponents().keySet(), isOfKind(SUBCOMPONENT_BUILDER))) {
        explicitBindingsBuilder.add(
            provisionBindingFactory.forSubcomponentBuilderMethod(
                subcomponentMethodDescriptor.methodElement(),
                componentDescriptor.componentDefinitionType()));
      }

      ImmutableSet.Builder<MultibindingDeclaration> multibindingDeclarations =
          ImmutableSet.builder();

      // Collect transitive module bindings and multibinding declarations.
      for (ModuleDescriptor moduleDescriptor : componentDescriptor.transitiveModules()) {
        explicitBindingsBuilder.addAll(moduleDescriptor.bindings());
        multibindingDeclarations.addAll(moduleDescriptor.multibindingDeclarations());
      }

      Resolver requestResolver =
          new Resolver(
              parentResolver,
              componentDescriptor,
              indexByKey(explicitBindingsBuilder.build()),
              indexByKey(multibindingDeclarations.build()));
      for (ComponentMethodDescriptor componentMethod : componentDescriptor.componentMethods()) {
        Optional<DependencyRequest> componentMethodRequest = componentMethod.dependencyRequest();
        if (componentMethodRequest.isPresent()) {
          requestResolver.resolve(componentMethodRequest.get());
        }
      }

      ImmutableMap.Builder<ExecutableElement, BindingGraph> subgraphsBuilder =
          ImmutableMap.builder();
      for (Entry<ComponentMethodDescriptor, ComponentDescriptor> subcomponentEntry :
          componentDescriptor.subcomponents().entrySet()) {
        subgraphsBuilder.put(
            subcomponentEntry.getKey().methodElement(),
            create(Optional.of(requestResolver), subcomponentEntry.getValue()));
      }

      for (ResolvedBindings resolvedBindings : requestResolver.getResolvedBindings().values()) {
        verify(
            resolvedBindings.owningComponent().equals(componentDescriptor),
            "%s is not owned by %s",
            resolvedBindings,
            componentDescriptor);
      }

      return new AutoValue_BindingGraph(
          componentDescriptor,
          requestResolver.getResolvedBindings(),
          subgraphsBuilder.build(),
          requestResolver.getOwnedModules());
    }

    private final class Resolver {
      final Optional<Resolver> parentResolver;
      final ComponentDescriptor componentDescriptor;
      final ImmutableSetMultimap<Key, ContributionBinding> explicitBindings;
      final ImmutableSet<ContributionBinding> explicitBindingsSet;
      final ImmutableSetMultimap<Key, MultibindingDeclaration> multibindingDeclarations;
      final Map<BindingKey, ResolvedBindings> resolvedBindings;
      final Deque<BindingKey> cycleStack = new ArrayDeque<>();
      final Cache<BindingKey, Boolean> dependsOnLocalMultibindingsCache =
          CacheBuilder.newBuilder().<BindingKey, Boolean>build();
      final Cache<Binding, Boolean> bindingDependsOnLocalMultibindingsCache =
          CacheBuilder.newBuilder().<Binding, Boolean>build();

      Resolver(
          Optional<Resolver> parentResolver,
          ComponentDescriptor componentDescriptor,
          ImmutableSetMultimap<Key, ContributionBinding> explicitBindings,
          ImmutableSetMultimap<Key, MultibindingDeclaration> multibindingDeclarations) {
        assert parentResolver != null;
        this.parentResolver = parentResolver;
        assert componentDescriptor != null;
        this.componentDescriptor = componentDescriptor;
        assert explicitBindings != null;
        this.explicitBindings = explicitBindings;
        this.explicitBindingsSet = ImmutableSet.copyOf(explicitBindings.values());
        assert multibindingDeclarations != null;
        this.multibindingDeclarations = multibindingDeclarations;
        this.resolvedBindings = Maps.newLinkedHashMap();
      }

      /**
       * Returns the bindings that satisfy a given dependency request.
       *
       * <p>For {@link BindingKey.Kind#CONTRIBUTION} requests, returns all of:
       * <ul>
       * <li>All explicit bindings for the requested key.
       * <li>All explicit bindings for {@code Set<T>} if the requested key's type is
       *     {@code Set<Produced<T>>}.
       * <li>A synthetic binding that depends on {@code Map<K, Producer<V>>} if the requested key's
       *     type is {@code Map<K, V>} and there are some explicit bindings for
       *     {@code Map<K, Producer<V>>}.
       * <li>A synthetic binding that depends on {@code Map<K, Provider<V>>} if the requested key's
       *     type is {@code Map<K, V>} and there are some explicit bindings for
       *     {@code Map<K, Provider<V>>} but no explicit bindings for {@code Map<K, Producer<V>>}.
       * <li>An implicit {@link Inject @Inject}-annotated constructor binding if there is one and
       *     there are no explicit bindings or synthetic bindings.
       * </ul>
       *
       * <p>For {@link BindingKey.Kind#MEMBERS_INJECTION} requests, returns the
       * {@link MembersInjectionBinding} for the type.
       */
      ResolvedBindings lookUpBindings(DependencyRequest request) {
        BindingKey bindingKey = request.bindingKey();
        switch (bindingKey.kind()) {
          case CONTRIBUTION:
            Set<ContributionBinding> contributionBindings = new LinkedHashSet<>();
            ImmutableSet.Builder<MultibindingDeclaration> multibindingDeclarationsBuilder =
                ImmutableSet.builder();

            // Add explicit bindings and declarations (those from modules and components).
            contributionBindings.addAll(getExplicitBindings(bindingKey.key()));
            multibindingDeclarationsBuilder.addAll(getMultibindingDeclarations(bindingKey.key()));

            // If the key is Set<Produced<T>>, then add explicit bindings and declarations for
            // Set<T>.
            Optional<Key> implicitSetKey = keyFactory.implicitSetKeyFromProduced(bindingKey.key());
            contributionBindings.addAll(getExplicitBindings(implicitSetKey));
            multibindingDeclarationsBuilder.addAll(getMultibindingDeclarations(implicitSetKey));

            ImmutableSet<MultibindingDeclaration> multibindingDeclarations =
                multibindingDeclarationsBuilder.build();

            // If the key is Map<K, V>, get its map-of-framework-type binding keys, which are either
            // Map<K, Provider<V>> or Map<K, Producer<V>>, and grab their explicit bindings and
            // declarations.
            Optional<Key> implicitMapProviderKey =
                keyFactory.implicitMapProviderKeyFrom(bindingKey.key());
            ImmutableSet<ContributionBinding> explicitProviderMapBindings =
                getExplicitBindings(implicitMapProviderKey);
            ImmutableSet<MultibindingDeclaration> explicitProviderMultibindingDeclarations =
                getMultibindingDeclarations(implicitMapProviderKey);

            Optional<Key> implicitMapProducerKey =
                keyFactory.implicitMapProducerKeyFrom(bindingKey.key());
            ImmutableSet<ContributionBinding> explicitProducerMapBindings =
                getExplicitBindings(implicitMapProducerKey);
            ImmutableSet<MultibindingDeclaration> explicitProducerMultibindingDeclarations =
                getMultibindingDeclarations(implicitMapProducerKey);

            if (!explicitProducerMapBindings.isEmpty()
                || !explicitProducerMultibindingDeclarations.isEmpty()) {
              /* If the binding key is Map<K, V>, and there are some explicit Map<K, Producer<V>>
               * bindings or multibinding declarations, then add the synthetic binding that depends
               * on Map<K, Producer<V>>. */
              contributionBindings.add(
                  productionBindingFactory.implicitMapOfProducerBinding(request));
            } else if (!explicitProviderMapBindings.isEmpty()
                || !explicitProviderMultibindingDeclarations.isEmpty()) {
              /* If the binding key is Map<K, V>, and there are some explicit Map<K, Provider<V>>
               * bindings or multibinding declarations but no explicit Map<K, Producer<V>> bindings
               * or multibinding declarations, then add the synthetic binding that depends on
               * Map<K, Provider<V>>. */
              contributionBindings.add(
                  provisionBindingFactory.implicitMapOfProviderBinding(request));
            }

            /* If there are no explicit or synthetic bindings or multibinding declarations, use an
             * implicit @Inject- constructed binding if there is one. */
            if (contributionBindings.isEmpty() && multibindingDeclarations.isEmpty()) {
              contributionBindings.addAll(
                  injectBindingRegistry.getOrFindProvisionBinding(bindingKey.key()).asSet());
            }

            return ResolvedBindings.forContributionBindings(
                bindingKey,
                componentDescriptor,
                indexBindingsByOwningComponent(request, ImmutableSet.copyOf(contributionBindings)),
                multibindingDeclarations);

          case MEMBERS_INJECTION:
            // no explicit deps for members injection, so just look it up
            Optional<MembersInjectionBinding> binding =
                injectBindingRegistry.getOrFindMembersInjectionBinding(bindingKey.key());
            return binding.isPresent()
                ? ResolvedBindings.forMembersInjectionBinding(
                    bindingKey, componentDescriptor, binding.get())
                : ResolvedBindings.noBindings(bindingKey, componentDescriptor);
          default:
            throw new AssertionError();
        }
      }

      private ImmutableSetMultimap<ComponentDescriptor, ContributionBinding>
          indexBindingsByOwningComponent(
              DependencyRequest request, Iterable<? extends ContributionBinding> bindings) {
        ImmutableSetMultimap.Builder<ComponentDescriptor, ContributionBinding> index =
            ImmutableSetMultimap.builder();
        for (ContributionBinding binding : bindings) {
          index.put(getOwningComponent(request, binding), binding);
        }
        return index.build();
      }

      /**
       * Returns the component that "owns" {@code binding}.
       *
       * <p>If {@code binding} is bound in an ancestor component, resolves {@code request} in this
       * component's parent. Returns the ancestor component in which it is bound, unless
       * {@code binding} depends on local multibindings, in which case returns this component.
       *
       * <p>If {@code binding} is not bound in an ancestor component, simply returns this component.
       */
      private ComponentDescriptor getOwningComponent(
          DependencyRequest request, ContributionBinding binding) {
        return isResolvedInParent(request, binding)
                && !new MultibindingDependencies().dependsOnLocalMultibindings(binding)
            ? getOwningResolver(binding).get().componentDescriptor
            : componentDescriptor;
      }

      /**
       * Returns {@code true} if {@code binding} is owned by an ancestor. If so,
       * {@linkplain #resolve(DependencyRequest) resolves} the request in this component's parent.
       * Don't resolve directly in the owning component in case it depends on multibindings in any
       * of its descendants.
       */
      private boolean isResolvedInParent(DependencyRequest request, ContributionBinding binding) {
        Optional<Resolver> owningResolver = getOwningResolver(binding);
        if (owningResolver.isPresent() && !owningResolver.get().equals(this)) {
          parentResolver.get().resolve(request);
          return true;
        } else {
          return false;
        }
      }

      private Optional<Resolver> getOwningResolver(ContributionBinding binding) {
        for (Resolver requestResolver : getResolverLineage().reverse()) {
          if (requestResolver.explicitBindingsSet.contains(binding)) {
            return Optional.of(requestResolver);
          }
        }

        // look for scope separately.  we do this for the case where @Singleton can appear twice
        // in the † compatibility mode
        Scope bindingScope = binding.scope();
        if (bindingScope.isPresent()) {
          for (Resolver requestResolver : getResolverLineage().reverse()) {
            if (bindingScope.equals(requestResolver.componentDescriptor.scope())) {
              return Optional.of(requestResolver);
            }
          }
        }
        return Optional.absent();
      }

      /** Returns the resolver lineage from parent to child. */
      private ImmutableList<Resolver> getResolverLineage() {
        List<Resolver> resolverList = Lists.newArrayList();
        for (Optional<Resolver> currentResolver = Optional.of(this);
            currentResolver.isPresent();
            currentResolver = currentResolver.get().parentResolver) {
          resolverList.add(currentResolver.get());
        }
        return ImmutableList.copyOf(Lists.reverse(resolverList));
      }

      /**
       * Returns the explicit {@link ContributionBinding}s that match the {@code requestKey} from
       * this and all ancestor resolvers.
       */
      private ImmutableSet<ContributionBinding> getExplicitBindings(Key requestKey) {
        ImmutableSet.Builder<ContributionBinding> explicitBindingsForKey = ImmutableSet.builder();
        for (Resolver resolver : getResolverLineage()) {
          explicitBindingsForKey.addAll(resolver.explicitBindings.get(requestKey));
        }
        return explicitBindingsForKey.build();
      }

      private ImmutableSet<ContributionBinding> getExplicitBindings(Optional<Key> optionalKey) {
        return optionalKey.isPresent()
            ? getExplicitBindings(optionalKey.get())
            : ImmutableSet.<ContributionBinding>of();
      }

      /**
       * Returns the {@link MultibindingDeclaration}s that match the {@code key} from this and all
       * ancestor resolvers.
       */
      private ImmutableSet<MultibindingDeclaration> getMultibindingDeclarations(Key key) {
        ImmutableSet.Builder<MultibindingDeclaration> multibindingDeclarations =
            ImmutableSet.builder();
        for (Resolver resolver : getResolverLineage()) {
          multibindingDeclarations.addAll(resolver.multibindingDeclarations.get(key));
        }
        return multibindingDeclarations.build();
      }

      private ImmutableSet<MultibindingDeclaration> getMultibindingDeclarations(
          Optional<Key> optionalKey) {
        return optionalKey.isPresent()
            ? getMultibindingDeclarations(optionalKey.get())
            : ImmutableSet.<MultibindingDeclaration>of();
      }

      private Optional<ResolvedBindings> getPreviouslyResolvedBindings(
          final BindingKey bindingKey) {
        Optional<ResolvedBindings> result = Optional.fromNullable(resolvedBindings.get(bindingKey));
        if (result.isPresent()) {
          return result;
        } else if (parentResolver.isPresent()) {
          return parentResolver.get().getPreviouslyResolvedBindings(bindingKey);
        } else {
          return Optional.absent();
        }
      }

      void resolve(DependencyRequest request) {
        BindingKey bindingKey = request.bindingKey();

        // If we find a cycle, stop resolving. The original request will add it with all of the
        // other resolved deps.
        if (cycleStack.contains(bindingKey)) {
          return;
        }

        // If the binding was previously resolved in this (sub)component, don't resolve it again.
        if (resolvedBindings.containsKey(bindingKey)) {
          return;
        }

        // If the binding was previously resolved in a supercomponent, then test to see if it
        // depends on multibindings with contributions from this subcomponent. If it does, then we
        // have to resolve it in this subcomponent so that it sees the local contributions. If it
        // does not, then we can stop resolving it in this subcomponent and rely on the
        // supercomponent resolution.
        if (getPreviouslyResolvedBindings(bindingKey).isPresent()
            && !new MultibindingDependencies().dependsOnLocalMultibindings(bindingKey)) {
          return;
        }

        cycleStack.push(bindingKey);
        try {
          ResolvedBindings bindings = lookUpBindings(request);
          for (Binding binding : bindings.ownedBindings()) {
            for (DependencyRequest dependency : binding.implicitDependencies()) {
              resolve(dependency);
            }
          }
          resolvedBindings.put(bindingKey, bindings);
        } finally {
          cycleStack.pop();
        }
      }

      ImmutableMap<BindingKey, ResolvedBindings> getResolvedBindings() {
        ImmutableMap.Builder<BindingKey, ResolvedBindings> resolvedBindingsBuilder =
            ImmutableMap.builder();
        resolvedBindingsBuilder.putAll(resolvedBindings);
        if (parentResolver.isPresent()) {
          Collection<ResolvedBindings> bindingsResolvedInParent =
              Maps.difference(parentResolver.get().getResolvedBindings(), resolvedBindings)
                  .entriesOnlyOnLeft()
                  .values();
          for (ResolvedBindings resolvedInParent : bindingsResolvedInParent) {
            resolvedBindingsBuilder.put(
                resolvedInParent.bindingKey(),
                resolvedInParent.asInheritedIn(componentDescriptor));
          }
        }
        return resolvedBindingsBuilder.build();
      }

      ImmutableSet<ModuleDescriptor> getInheritedModules() {
        return parentResolver.isPresent()
            ? Sets.union(
                    parentResolver.get().getInheritedModules(),
                    parentResolver.get().componentDescriptor.transitiveModules())
                .immutableCopy()
            : ImmutableSet.<ModuleDescriptor>of();
      }

      ImmutableSet<ModuleDescriptor> getOwnedModules() {
        return Sets.difference(componentDescriptor.transitiveModules(), getInheritedModules())
            .immutableCopy();
      }

      private final class MultibindingDependencies {
        private final Set<BindingKey> cycleChecker = new HashSet<>();

        /**
         * Returns {@code true} if {@code bindingKey} previously resolved to multibindings with
         * contributions declared within this component's modules, or if any of its unscoped
         * dependencies depend on such local multibindings.
         *
         * <p>We don't care about scoped dependencies because they will never depend on
         * multibindings with contributions from subcomponents.
         *
         * @throws IllegalArgumentException if {@link #getPreviouslyResolvedBindings(BindingKey)} is
         *     absent
         */
        boolean dependsOnLocalMultibindings(final BindingKey bindingKey) {
          checkArgument(
              getPreviouslyResolvedBindings(bindingKey).isPresent(),
              "no previously resolved bindings in %s for %s",
              Resolver.this,
              bindingKey);
          // Don't recur infinitely if there are valid cycles in the dependency graph.
          if (!cycleChecker.add(bindingKey)) {
            return false;
          }
          try {
            return dependsOnLocalMultibindingsCache.get(
                bindingKey,
                new Callable<Boolean>() {
                  @Override
                  public Boolean call() {
                    ResolvedBindings previouslyResolvedBindings =
                        getPreviouslyResolvedBindings(bindingKey).get();
                    if (isMultibindingsWithLocalContributions(previouslyResolvedBindings)) {
                      return true;
                    }

                    for (Binding binding : previouslyResolvedBindings.bindings()) {
                      if (dependsOnLocalMultibindings(binding)) {
                        return true;
                      }
                    }
                    return false;
                  }
                });
          } catch (ExecutionException e) {
            throw new AssertionError(e);
          }
        }

        /**
         * Returns {@code true} if {@code binding} is unscoped and depends on multibindings with
         * contributions declared within this component's modules, or if any of its unscoped
         * dependencies depend on such local multibindings.
         *
         * <p>We don't care about scoped dependencies because they will never depend on
         * multibindings with contributions from subcomponents.
         */
        boolean dependsOnLocalMultibindings(final Binding binding) {
          try {
            return bindingDependsOnLocalMultibindingsCache.get(
                binding,
                new Callable<Boolean>() {
                  @Override
                  public Boolean call() {
                    if (!binding.scope().isPresent()
                        // TODO(beder): Figure out what happens with production subcomponents.
                        && !binding.bindingType().equals(BindingType.PRODUCTION)) {
                      for (DependencyRequest dependency : binding.implicitDependencies()) {
                        if (dependsOnLocalMultibindings(dependency.bindingKey())) {
                          return true;
                        }
                      }
                    }
                    return false;
                  }
                });
          } catch (ExecutionException e) {
            throw new AssertionError(e);
          }
        }

        private boolean isMultibindingsWithLocalContributions(ResolvedBindings resolvedBindings) {
          return resolvedBindings.isMultibindings()
              && explicitBindings.containsKey(resolvedBindings.key());
        }
      }
    }
  }
}
