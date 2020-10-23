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
package org.sonar.server.qualityprofile;

import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.util.Comparator;
import java.util.Date;
import org.sonar.api.notifications.Notification;
import org.sonar.api.platform.Server;
import org.sonar.plugins.emailnotifications.api.EmailMessage;
import org.sonar.plugins.emailnotifications.api.EmailTemplate;

import static java.nio.charset.StandardCharsets.UTF_8;
import static org.sonar.api.utils.DateUtils.formatDate;
import static org.sonar.server.qualityprofile.BuiltInQualityProfilesNotification.Profile;
import static org.sonar.server.qualityprofile.BuiltInQualityProfilesNotification.parse;
import static org.sonar.server.qualityprofile.BuiltInQualityProfilesUpdateListener.BUILT_IN_QUALITY_PROFILES;

public class BuiltInQualityProfilesNotificationTemplate extends EmailTemplate {

  private final Server server;

  public BuiltInQualityProfilesNotificationTemplate(Server server) {
    this.server = server;
  }

  @Override
  public EmailMessage format(Notification notification) {
    if (!BUILT_IN_QUALITY_PROFILES.equals(notification.getType())) {
      return null;
    }

    BuiltInQualityProfilesNotification profilesNotification = parse(notification);
    StringBuilder message = new StringBuilder("The following built-in profiles have been updated:\n\n");
    profilesNotification.getProfiles().stream()
      .sorted(Comparator.comparing(Profile::getLanguageName).thenComparing(Profile::getProfileName))
      .forEach(profile -> {
        message.append("\"")
          .append(profile.getProfileName())
          .append("\" - ")
          .append(profile.getLanguageName())
          .append(": ")
          .append(server.getPublicRootUrl()).append("/profiles/changelog?language=")
          .append(profile.getLanguageKey())
          .append("&name=")
          .append(encode(profile.getProfileName()))
          .append("&since=")
          .append(formatDate(new Date(profile.getStartDate())))
          .append("&to=")
          .append(formatDate(new Date(profile.getEndDate())))
          .append("\n");
        int newRules = profile.getNewRules();
        if (newRules > 0) {
          message.append(" ").append(newRules).append(" new rules\n");
        }
        int updatedRules = profile.getUpdatedRules();
        if (updatedRules > 0) {
          message.append(" ").append(updatedRules).append(" rules have been updated\n");
        }
        int removedRules = profile.getRemovedRules();
        if (removedRules > 0) {
          message.append(" ").append(removedRules).append(" rules removed\n");
        }
        message.append("\n");
      });

    message.append("This is a good time to review your quality profiles and update them to benefit from the latest evolutions: ");
    message.append(server.getPublicRootUrl()).append("/profiles");

    // And finally return the email that will be sent
    return new EmailMessage()
      .setMessageId(BUILT_IN_QUALITY_PROFILES)
      .setSubject("Built-in quality profiles have been updated")
      .setMessage(message.toString());
  }

  public String encode(String text) {
    try {
      return URLEncoder.encode(text, UTF_8.name());
    } catch (UnsupportedEncodingException e) {
      throw new IllegalStateException(String.format("Cannot encode %s", text), e);
    }
  }
}
