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
package org.sonar.server.issue.notification;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.Multimap;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import org.sonar.api.notifications.Notification;
import org.sonar.api.notifications.NotificationChannel;
import org.sonar.api.web.UserRole;
import org.sonar.server.notification.NotificationDispatcher;
import org.sonar.server.notification.NotificationDispatcherMetadata;
import org.sonar.server.notification.NotificationManager;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class ChangesOnMyIssueNotificationDispatcherTest {

  @Mock
  NotificationManager notifications;

  @Mock
  NotificationDispatcher.Context context;

  @Mock
  NotificationChannel emailChannel;

  @Mock
  NotificationChannel twitterChannel;

  ChangesOnMyIssueNotificationDispatcher dispatcher;

  @Before
  public void setUp() {
    dispatcher = new ChangesOnMyIssueNotificationDispatcher(notifications);
  }

  @Test
  public void test_metadata() throws Exception {
    NotificationDispatcherMetadata metadata = ChangesOnMyIssueNotificationDispatcher.newMetadata();
    assertThat(metadata.getDispatcherKey()).isEqualTo(dispatcher.getKey());
    assertThat(metadata.getProperty(NotificationDispatcherMetadata.GLOBAL_NOTIFICATION)).isEqualTo("true");
    assertThat(metadata.getProperty(NotificationDispatcherMetadata.PER_PROJECT_NOTIFICATION)).isEqualTo("true");
  }

  @Test
  public void should_not_dispatch_if_other_notification_type() {
    Notification notification = new Notification("other-notif");
    dispatcher.performDispatch(notification, context);

    verify(context, never()).addUser(any(String.class), any(NotificationChannel.class));
  }

  @Test
  public void should_dispatch_to_assignee() {
    Multimap<String, NotificationChannel> recipients = HashMultimap.create();
    recipients.put("simon", emailChannel);
    recipients.put("freddy", twitterChannel);
    recipients.put("godin", twitterChannel);
    when(notifications.findSubscribedRecipientsForDispatcher(dispatcher, "struts",
      new NotificationManager.SubscriberPermissionsOnProject(UserRole.USER))).thenReturn(recipients);

    Notification notification = new IssueChangeNotification()
      .setFieldValue("projectKey", "struts")
      .setFieldValue("changeAuthor", "olivier")
      .setFieldValue("assignee", "freddy");
    dispatcher.performDispatch(notification, context);

    verify(context).addUser("freddy", twitterChannel);
    verify(context, never()).addUser("godin", twitterChannel);
    verifyNoMoreInteractions(context);
  }

  @Test
  public void should_not_dispatch_to_author_of_changes() {
    Multimap<String, NotificationChannel> recipients = HashMultimap.create();
    recipients.put("simon", emailChannel);
    recipients.put("freddy", twitterChannel);
    recipients.put("godin", twitterChannel);
    when(notifications.findSubscribedRecipientsForDispatcher(dispatcher, "uuid1", new NotificationManager.SubscriberPermissionsOnProject(UserRole.USER))).thenReturn(recipients);

    // change author is the assignee
    dispatcher.performDispatch(
      new IssueChangeNotification()
        .setFieldValue("projectKey", "struts")
        .setFieldValue("projectUuid", "uuid1")
        .setFieldValue("changeAuthor", "simon")
        .setFieldValue("assignee", "simon"),
      context);

    // no change author
    dispatcher.performDispatch(new IssueChangeNotification().setFieldValue("projectKey", "struts")
      .setFieldValue("new.resolution", "FIXED"), context);

    verifyNoMoreInteractions(context);
  }
}
