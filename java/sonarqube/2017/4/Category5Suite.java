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
package it;

import it.organization.RootTest;
import it.serverSystem.ClusterTest;
import it.serverSystem.RestartTest;
import it.serverSystem.ServerSystemRestartingOrchestrator;
import it.settings.LicensesPageTest;
import it.settings.SettingsTestRestartingOrchestrator;
import it.updateCenter.UpdateCenterTest;
import it.user.RealmAuthenticationTest;
import it.user.SsoAuthenticationTest;
import org.junit.runner.RunWith;
import org.junit.runners.Suite;

/**
 * This suite is reserved to the tests that start their own instance of Orchestrator.
 * Indeed multiple instances of Orchestrator can't be started in parallel, so this
 * suite does not declare a shared Orchestrator.
 */
@RunWith(Suite.class)
@Suite.SuiteClasses({
  ClusterTest.class,
  ServerSystemRestartingOrchestrator.class,
  RestartTest.class,
  SettingsTestRestartingOrchestrator.class,
  LicensesPageTest.class,
  // update center
  UpdateCenterTest.class,
  RealmAuthenticationTest.class,
  SsoAuthenticationTest.class,
  RootTest.class
})
public class Category5Suite {

}
