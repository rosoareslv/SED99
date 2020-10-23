/*
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

package com.google.devtools.j2objc.ast;

import java.util.List;

/**
 * Node type for an enum declaration.
 */
public class EnumDeclaration extends AbstractTypeDeclaration {

  private ChildList<Type> superInterfaceTypes = ChildList.create(Type.class, this);
  private ChildList<EnumConstantDeclaration> enumConstants =
      ChildList.create(EnumConstantDeclaration.class, this);
  
  public EnumDeclaration() {}

  public EnumDeclaration(EnumDeclaration other) {
    super(other);
    superInterfaceTypes.copyFrom(other.getSuperInterfaceTypes());
    enumConstants.copyFrom(other.getEnumConstants());
  }

  @Override
  public Kind getKind() {
    return Kind.ENUM_DECLARATION;
  }

  public List<Type> getSuperInterfaceTypes() {
    return superInterfaceTypes;
  }
  
  public EnumDeclaration addSuperInterfaceType(Type type) {
    superInterfaceTypes.add(type);
    return this;
  }

  public List<EnumConstantDeclaration> getEnumConstants() {
    return enumConstants;
  }
  
  public EnumDeclaration addEnumConstant(EnumConstantDeclaration constant) {
    enumConstants.add(constant);
    return this;
  }

  @Override
  protected void acceptInner(TreeVisitor visitor) {
    if (visitor.visit(this)) {
      javadoc.accept(visitor);
      annotations.accept(visitor);
      name.accept(visitor);
      superInterfaceTypes.accept(visitor);
      enumConstants.accept(visitor);
      bodyDeclarations.accept(visitor);
      classInitStatements.accept(visitor);
    }
    visitor.endVisit(this);
  }

  @Override
  public EnumDeclaration copy() {
    return new EnumDeclaration(this);
  }
}
