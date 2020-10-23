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
package org.sonarqube.pageobjects;

import com.codeborne.selenide.Condition;

import static com.codeborne.selenide.Condition.text;
import static com.codeborne.selenide.Condition.visible;
import static com.codeborne.selenide.Selenide.$;
import static com.codeborne.selenide.Selenide.page;

public class QualityProfilePage {
  public QualityProfilePage() {
    $("#quality-profile").shouldBe(Condition.visible);
  }

  public QualityProfilePage shouldHaveMissingSonarWayRules(Integer nbRules) {
    $(".quality-profile-rules-sonarway-missing")
      .shouldBe(Condition.visible)
      .$("a").shouldHave(text(nbRules.toString()));
    return this;
  }

  public RulesPage showMissingSonarWayRules() {
    $(".quality-profile-rules-sonarway-missing")
      .shouldBe(Condition.visible).$("a").click();
    $(".coding-rules").shouldBe(Condition.visible);
    return page(RulesPage.class);
  }

  public QualityProfilePage shouldHaveAssociatedProject(String projectName) {
    $(".js-profile-project").shouldHave(text(projectName));
    return this;
  }

  public QualityProfilePage shouldAllowToChangeProjects() {
    $(".js-change-projects").shouldBe(visible).click();
    $("#profile-projects .select-list-list").shouldBe(visible);
    return this;
  }
}
