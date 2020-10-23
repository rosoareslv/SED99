/*
 * SonarQube
 * Copyright (C) 2009-2017 SonarSource SA
 * mailto:info AT sonarsource DOT com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
package org.sonar.server.edition;

import org.sonar.core.platform.Module;
import org.sonar.server.edition.ws.ApplyLicenseAction;
import org.sonar.server.edition.ws.ClearErrorMessageAction;
import org.sonar.server.edition.ws.EditionsWs;
import org.sonar.server.edition.ws.FormDataAction;
import org.sonar.server.edition.ws.PreviewAction;
import org.sonar.server.edition.ws.StatusAction;
import org.sonar.server.edition.ws.UninstallAction;

public class EditionsWsModule extends Module {
  @Override
  protected void configureModule() {
    add(
      StandaloneEditionManagementStateImpl.class,
      StatusAction.class,
      ApplyLicenseAction.class,
      PreviewAction.class,
      FormDataAction.class,
      ClearErrorMessageAction.class,
      UninstallAction.class,
      EditionsWs.class);
  }
}
