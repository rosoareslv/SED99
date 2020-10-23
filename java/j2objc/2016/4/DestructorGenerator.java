/*
 * Copyright 2011 Google Inc. All Rights Reserved.
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

package com.google.devtools.j2objc.translate;

import com.google.common.collect.Lists;
import com.google.devtools.j2objc.Options;
import com.google.devtools.j2objc.ast.AbstractTypeDeclaration;
import com.google.devtools.j2objc.ast.AnnotationTypeDeclaration;
import com.google.devtools.j2objc.ast.Block;
import com.google.devtools.j2objc.ast.Expression;
import com.google.devtools.j2objc.ast.ExpressionStatement;
import com.google.devtools.j2objc.ast.FunctionInvocation;
import com.google.devtools.j2objc.ast.MethodDeclaration;
import com.google.devtools.j2objc.ast.NativeStatement;
import com.google.devtools.j2objc.ast.PrefixExpression;
import com.google.devtools.j2objc.ast.SimpleName;
import com.google.devtools.j2objc.ast.Statement;
import com.google.devtools.j2objc.ast.SuperMethodInvocation;
import com.google.devtools.j2objc.ast.TreeUtil;
import com.google.devtools.j2objc.ast.TreeVisitor;
import com.google.devtools.j2objc.ast.TypeDeclaration;
import com.google.devtools.j2objc.ast.VariableDeclarationFragment;
import com.google.devtools.j2objc.types.FunctionBinding;
import com.google.devtools.j2objc.types.GeneratedMethodBinding;
import com.google.devtools.j2objc.util.BindingUtil;
import com.google.devtools.j2objc.util.NameTable;

import org.eclipse.jdt.core.dom.IMethodBinding;
import org.eclipse.jdt.core.dom.ITypeBinding;
import org.eclipse.jdt.core.dom.IVariableBinding;
import org.eclipse.jdt.core.dom.Modifier;

import java.util.List;

/**
 * Adds release methods to Java classes, in preparation for translation
 * to iOS.  Because Objective-C allows messages to be sent to nil, all
 * fields can be released regardless of whether they currently reference
 * data.
 *
 * @author Tom Ball
 */
public class DestructorGenerator extends TreeVisitor {

  @Override
  public void endVisit(AnnotationTypeDeclaration node) {
    addDeallocMethod(node);
  }

  @Override
  public void endVisit(TypeDeclaration node) {
    if (!node.isInterface()) {
      addDeallocMethod(node);
    }
  }

  private void addDeallocMethod(AbstractTypeDeclaration node) {
    ITypeBinding type = node.getTypeBinding();
    boolean hasFinalize = hasFinalizeMethod(type);
    List<Statement> releaseStatements = createReleaseStatements(node);
    if (releaseStatements.isEmpty() && !hasFinalize) {
      return;
    }

    ITypeBinding voidType = typeEnv.resolveJavaType("void");
    int modifiers = Modifier.PUBLIC | BindingUtil.ACC_SYNTHETIC;
    GeneratedMethodBinding deallocBinding = GeneratedMethodBinding.newMethod(
        NameTable.DEALLOC_METHOD, modifiers, voidType, type);
    MethodDeclaration deallocDecl = new MethodDeclaration(deallocBinding);
    deallocDecl.setHasDeclaration(false);
    Block block = new Block();
    deallocDecl.setBody(block);
    List<Statement> stmts = block.getStatements();
    if (hasFinalize) {
      String clsName = nameTable.getFullName(type);
      stmts.add(new NativeStatement("JreCheckFinalize(self, [" + clsName + " class]);"));
    }
    stmts.addAll(releaseStatements);
    if (Options.useReferenceCounting()) {
      stmts.add(new ExpressionStatement(new SuperMethodInvocation(typeEnv.getDeallocMethod())));
    }

    node.getBodyDeclarations().add(deallocDecl);
  }

  private boolean hasFinalizeMethod(ITypeBinding type) {
    if (type == null || typeEnv.isJavaObjectType(type)) {
      return false;
    }
    for (IMethodBinding method : type.getDeclaredMethods()) {
      if (method.getName().equals(NameTable.FINALIZE_METHOD)
          && method.getParameterTypes().length == 0) {
        return true;
      }
    }
    return hasFinalizeMethod(type.getSuperclass());
  }

  private List<Statement> createReleaseStatements(AbstractTypeDeclaration node) {
    List<Statement> statements = Lists.newArrayList();
    for (VariableDeclarationFragment fragment : TreeUtil.getAllFields(node)) {
      IVariableBinding var = fragment.getVariableBinding();
      ITypeBinding type = var.getType();
      if (BindingUtil.isStatic(var) || type.isPrimitive() || BindingUtil.isWeakReference(var)
          || (Options.useARC() && !BindingUtil.isVolatile(var))) {
        continue;
      }
      statements.add(createRelease(var));
    }
    return statements;
  }

  private Statement createRelease(IVariableBinding var) {
    ITypeBinding voidType = typeEnv.resolveJavaType("void");
    ITypeBinding idType = typeEnv.resolveIOSType("id");
    boolean isVolatile = BindingUtil.isVolatile(var);
    FunctionBinding binding = new FunctionBinding(
        isVolatile ? "JreReleaseVolatile" : "RELEASE_", voidType, null);
    binding.addParameter(isVolatile ? typeEnv.getPointerType(idType) : idType);
    FunctionInvocation releaseInvocation = new FunctionInvocation(binding, voidType);
    Expression arg = new SimpleName(var);
    if (isVolatile) {
      arg = new PrefixExpression(
          typeEnv.getPointerType(var.getType()), PrefixExpression.Operator.ADDRESS_OF, arg);
    }
    releaseInvocation.getArguments().add(arg);
    return new ExpressionStatement(releaseInvocation);
  }
}
