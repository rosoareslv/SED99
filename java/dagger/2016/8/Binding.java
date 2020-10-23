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

import static javax.lang.model.element.Modifier.PUBLIC;

import com.google.auto.common.MoreElements;
import com.google.common.base.Optional;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Lists;
import com.google.common.collect.Sets;
import dagger.internal.codegen.BindingType.HasBindingType;
import java.util.List;
import java.util.Set;
import javax.lang.model.element.Element;
import javax.lang.model.element.Name;
import javax.lang.model.element.PackageElement;
import javax.lang.model.element.TypeElement;
import javax.lang.model.element.TypeParameterElement;
import javax.lang.model.type.ArrayType;
import javax.lang.model.type.DeclaredType;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.type.WildcardType;
import javax.lang.model.util.SimpleTypeVisitor6;
import javax.lang.model.util.Types;

/**
 * An abstract type for classes representing a Dagger binding.  Particularly, contains the
 * {@link Element} that generated the binding and the {@link DependencyRequest} instances that are
 * required to satisfy the binding, but leaves the specifics of the <i>mechanism</i> of the binding
 * to the subtypes.
 *
 * @author Gregory Kick
 * @since 2.0
 */
abstract class Binding extends BindingDeclaration implements HasBindingType {

  /** The {@link Key} that is provided by this binding. */
  @Override
  public abstract Key key();

  /**
   * The explicit set of {@link DependencyRequest dependencies} required to satisfy this binding as
   * defined by the user-defined injection sites.
   */
  abstract ImmutableSet<DependencyRequest> dependencies();

  /**
   * The set of {@link DependencyRequest dependencies} that are added by the framework rather than a
   * user-defined injection site. This returns an unmodifiable set.
   */
  // TODO(gak): this will eventually get migrated to FrameworkDependency
  Set<DependencyRequest> frameworkDependencies() {
    return ImmutableSet.of();
  }

  /**
   * The set of {@link DependencyRequest dependencies} required to satisfy this binding. This is the
   * union of {@link #dependencies()} and {@link #frameworkDependencies()}. This returns an
   * unmodifiable set.
   */
  final Set<DependencyRequest> implicitDependencies() {
    Set<DependencyRequest> frameworkDependencies = frameworkDependencies();
    return frameworkDependencies.isEmpty()
        ? dependencies()
        : Sets.union(frameworkDependencies, dependencies());
  }

  /**
   * Returns the name of the package in which this binding must be managed. E.g.: a binding
   * may reference non-public types.
   */
  final Optional<String> bindingPackage() {
    Set<String> packages = nonPublicPackageUse(key().type());
    switch (packages.size()) {
      case 0:
        return Optional.absent();
      case 1:
        return Optional.of(packages.iterator().next());
      default:
        throw new IllegalStateException();
    }
  }

  private static Set<String> nonPublicPackageUse(TypeMirror typeMirror) {
    ImmutableSet.Builder<String> packages = ImmutableSet.builder();
    typeMirror.accept(new SimpleTypeVisitor6<Void, ImmutableSet.Builder<String>>() {
      @Override
      public Void visitArray(ArrayType t, ImmutableSet.Builder<String> p) {
        return t.getComponentType().accept(this, p);
      }

      @Override
      public Void visitDeclared(DeclaredType t, ImmutableSet.Builder<String> p) {
        for (TypeMirror typeArgument : t.getTypeArguments()) {
          typeArgument.accept(this, p);
        }
        // TODO(gak): address public nested types in non-public types
        TypeElement typeElement = MoreElements.asType(t.asElement());
        if (!typeElement.getModifiers().contains(PUBLIC)) {
          PackageElement elementPackage = MoreElements.getPackage(typeElement);
          Name qualifiedName = elementPackage.getQualifiedName();
          p.add(qualifiedName.toString());
        }
        // Also make sure enclosing types are visible, otherwise we're fooled by
        // class Foo { public class Bar }
        // (Note: we can't use t.getEnclosingType() because it doesn't work!)
        typeElement.getEnclosingElement().asType().accept(this, p);
        return null;
      }

      @Override
      public Void visitWildcard(WildcardType t, ImmutableSet.Builder<String> p) {
        if (t.getExtendsBound() != null) {
          t.getExtendsBound().accept(this, p);
        }
        if (t.getSuperBound() != null) {
          t.getSuperBound().accept(this, p);
        }
        return null;
      }
    }, packages);
    return packages.build();
  }

  /**
   * If this binding's key's type parameters are different from those of the
   * {@link #bindingTypeElement()}, this is the binding for the {@link #bindingTypeElement()}'s
   * unresolved type.
   */
  abstract Optional<? extends Binding> unresolved();

  /**
   * The scope of this binding.
   */
  Optional<Scope> scope() {
    return Optional.absent();
  }

  // TODO(sameb): Remove the TypeElement parameter and pull it from the TypeMirror.
  static boolean hasNonDefaultTypeParameters(TypeElement element, TypeMirror type, Types types) {
    // If the element has no type parameters, nothing can be wrong.
    if (element.getTypeParameters().isEmpty()) {
      return false;
    }

    List<TypeMirror> defaultTypes = Lists.newArrayList();
    for (TypeParameterElement parameter : element.getTypeParameters()) {
      defaultTypes.add(parameter.asType());
    }

    List<TypeMirror> actualTypes =
        type.accept(
            new SimpleTypeVisitor6<List<TypeMirror>, Void>() {
              @Override
              protected List<TypeMirror> defaultAction(TypeMirror e, Void p) {
                return ImmutableList.of();
              }

              @Override
              public List<TypeMirror> visitDeclared(DeclaredType t, Void p) {
                return ImmutableList.<TypeMirror>copyOf(t.getTypeArguments());
              }
            },
            null);

    // The actual type parameter size can be different if the user is using a raw type.
    if (defaultTypes.size() != actualTypes.size()) {
      return true;
    }

    for (int i = 0; i < defaultTypes.size(); i++) {
      if (!types.isSameType(defaultTypes.get(i), actualTypes.get(i))) {
        return true;
      }
    }
    return false;
  }
}
