# -*- coding: utf-8 -*-
from __future__ import absolute_import
import datetime
from django.conf import settings
from django.http import HttpResponse
from django.test import TestCase

from mock import patch
from zerver.lib.test_helpers import MockLDAP

from confirmation.models import Confirmation

from zilencer.models import Deployment

from zerver.forms import HomepageForm
from zerver.views import do_change_password
from zerver.views.invite import get_invitee_emails_set
from zerver.models import (
    get_realm_by_string_id, get_prereg_user_by_email, get_user_profile_by_email,
    PreregistrationUser, Realm, RealmAlias, Recipient,
    Referral, ScheduledJob, UserProfile, UserMessage,
    Stream, Subscription, ScheduledJob
)
from zerver.management.commands.deliver_email import send_email_job

from zerver.lib.actions import (
    set_default_streams,
    do_change_is_admin
)

from zerver.lib.initial_password import initial_password
from zerver.lib.actions import do_deactivate_realm, do_set_realm_default_language, \
    add_new_user_history
from zerver.lib.digest import send_digest_email
from zerver.lib.notifications import (
    enqueue_welcome_emails, one_click_unsubscribe_link, send_local_email_template_with_delay)
from zerver.lib.test_helpers import find_key_by_email, queries_captured, \
    HostRequestMock
from zerver.lib.test_classes import (
    ZulipTestCase,
)
from zerver.lib.test_runner import slow
from zerver.lib.session_user import get_session_dict_user
from zerver.context_processors import common_context

import re
import ujson

from six.moves import urllib
from six.moves import range
import six
from typing import Any, Text
import os

class PublicURLTest(ZulipTestCase):
    """
    Account creation URLs are accessible even when not logged in. Authenticated
    URLs redirect to a page.
    """

    def fetch(self, method, urls, expected_status):
        # type: (str, List[str], int) -> None
        for url in urls:
            # e.g. self.client_post(url) if method is "post"
            response = getattr(self, method)(url)
            self.assertEqual(response.status_code, expected_status,
                             msg="Expected %d, received %d for %s to %s" % (
                                 expected_status, response.status_code, method, url))

    def test_public_urls(self):
        # type: () -> None
        """
        Test which views are accessible when not logged in.
        """
        # FIXME: We should also test the Tornado URLs -- this codepath
        # can't do so because this Django test mechanism doesn't go
        # through Tornado.
        get_urls = {200: ["/accounts/home/", "/accounts/login/"
                          "/en/accounts/home/", "/ru/accounts/home/",
                          "/en/accounts/login/", "/ru/accounts/login/",
                          "/help/"],
                    302: ["/", "/en/", "/ru/"],
                    401: ["/json/streams/Denmark/members",
                          "/api/v1/users/me/subscriptions",
                          "/api/v1/messages",
                          "/json/messages",
                          "/api/v1/streams",
                          ],
                    404: ["/help/nonexistent"],
                    }

        # Add all files in 'templates/zerver/help' directory (except for 'main.html' and
        # 'index.md') to `get_urls['200']` list.
        for doc in os.listdir('./templates/zerver/help'):
            if doc not in {'main.html', 'index.md', 'include'}:
                get_urls[200].append('/help/' + os.path.splitext(doc)[0]) # Strip the extension.

        post_urls = {200: ["/accounts/login/"],
                     302: ["/accounts/logout/"],
                     401: ["/json/messages",
                           "/json/invite_users",
                           "/json/settings/change",
                           "/json/subscriptions/exists",
                           "/json/subscriptions/property",
                           "/json/fetch_api_key",
                           "/json/users/me/subscriptions",
                           "/api/v1/users/me/subscriptions",
                           ],
                     400: ["/api/v1/external/github",
                           "/api/v1/fetch_api_key",
                           ],
                     }
        put_urls = {401: ["/json/users/me/pointer"],
                    }
        for status_code, url_set in six.iteritems(get_urls):
            self.fetch("client_get", url_set, status_code)
        for status_code, url_set in six.iteritems(post_urls):
            self.fetch("client_post", url_set, status_code)
        for status_code, url_set in six.iteritems(put_urls):
            self.fetch("client_put", url_set, status_code)

    def test_get_gcid_when_not_configured(self):
        # type: () -> None
        with self.settings(GOOGLE_CLIENT_ID=None):
            resp = self.client_get("/api/v1/fetch_google_client_id")
            self.assertEqual(400, resp.status_code,
                             msg="Expected 400, received %d for GET /api/v1/fetch_google_client_id" % (
                                 resp.status_code,))
            data = ujson.loads(resp.content)
            self.assertEqual('error', data['result'])

    def test_get_gcid_when_configured(self):
        # type: () -> None
        with self.settings(GOOGLE_CLIENT_ID="ABCD"):
            resp = self.client_get("/api/v1/fetch_google_client_id")
            self.assertEqual(200, resp.status_code,
                             msg="Expected 200, received %d for GET /api/v1/fetch_google_client_id" % (
                                 resp.status_code,))
            data = ujson.loads(resp.content)
            self.assertEqual('success', data['result'])
            self.assertEqual('ABCD', data['google_client_id'])

class AddNewUserHistoryTest(ZulipTestCase):
    def test_add_new_user_history_race(self):
        # type: () -> None
        """Sends a message during user creation"""
        # Create a user who hasn't had historical messages added
        stream_dict = {
            "Denmark": {"description": "A Scandinavian country", "invite_only": False},
            "Verona": {"description": "A city in Italy", "invite_only": False}
        }  # type: Dict[Text, Dict[Text, Any]]
        set_default_streams(get_realm_by_string_id("zulip"), stream_dict)
        with patch("zerver.lib.actions.add_new_user_history"):
            self.register("test", "test")
        user_profile = get_user_profile_by_email("test@zulip.com")

        subs = Subscription.objects.select_related("recipient").filter(
            user_profile=user_profile, recipient__type=Recipient.STREAM)
        streams = Stream.objects.filter(id__in=[sub.recipient.type_id for sub in subs])
        self.send_message("hamlet@zulip.com", streams[0].name, Recipient.STREAM, "test")
        add_new_user_history(user_profile, streams)

class PasswordResetTest(ZulipTestCase):
    """
    Log in, reset password, log out, log in with new password.
    """

    def test_password_reset(self):
        # type: () -> None
        email = 'hamlet@zulip.com'
        old_password = initial_password(email)

        self.login(email)

        # test password reset template
        result = self.client_get('/accounts/password/reset/')
        self.assert_in_response('Reset your password.', result)

        # start the password reset process by supplying an email address
        result = self.client_post('/accounts/password/reset/', {'email': email})

        # check the redirect link telling you to check mail for password reset link
        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith(
                "/accounts/password/reset/done/"))
        result = self.client_get(result["Location"])

        self.assert_in_response("Check your email to finish the process.", result)

        # Visit the password reset link.
        password_reset_url = self.get_confirmation_url_from_outbox(email, "(\S+)")
        result = self.client_get(password_reset_url)
        self.assertEqual(result.status_code, 200)

        # Reset your password
        result = self.client_post(password_reset_url,
                                  {'new_password1': 'new_password',
                                   'new_password2': 'new_password'})

        # password reset succeeded
        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith("/password/done/"))

        # log back in with new password
        self.login(email, password='new_password')
        user_profile = get_user_profile_by_email('hamlet@zulip.com')
        self.assertEqual(get_session_dict_user(self.client.session), user_profile.id)

        # make sure old password no longer works
        self.login(email, password=old_password, fails=True)

    def test_redirect_endpoints(self):
        # type: () -> None
        '''
        These tests are mostly designed to give us 100% URL coverage
        in our URL coverage reports.  Our mechanism for finding URL
        coverage doesn't handle redirects, so we just have a few quick
        tests here.
        '''
        result = self.client_get('/accounts/password/reset/done/')
        self.assert_in_success_response(["Check your email"], result)

        result = self.client_get('/accounts/password/done/')
        self.assert_in_success_response(["We've reset your password!"], result)

        result = self.client_get('/accounts/send_confirm/alice@example.com')
        self.assert_in_success_response(["Still no email?"], result)

class LoginTest(ZulipTestCase):
    """
    Logging in, registration, and logging out.
    """

    def test_login(self):
        # type: () -> None
        self.login("hamlet@zulip.com")
        user_profile = get_user_profile_by_email('hamlet@zulip.com')
        self.assertEqual(get_session_dict_user(self.client.session), user_profile.id)

    def test_login_bad_password(self):
        # type: () -> None
        self.login("hamlet@zulip.com", password="wrongpassword", fails=True)
        self.assertIsNone(get_session_dict_user(self.client.session))

    def test_login_nonexist_user(self):
        # type: () -> None
        result = self.login_with_return("xxx@zulip.com", "xxx")
        self.assert_in_response("Please enter a correct email and password", result)

    def test_register(self):
        # type: () -> None
        realm = get_realm_by_string_id("zulip")
        stream_dict = {"stream_"+str(i): {"description": "stream_%s_description" % i, "invite_only": False}
                       for i in range(40)}  # type: Dict[Text, Dict[Text, Any]]
        for stream_name in stream_dict.keys():
            self.make_stream(stream_name, realm=realm)

        set_default_streams(realm, stream_dict)
        with queries_captured() as queries:
            self.register("test", "test")
        # Ensure the number of queries we make is not O(streams)
        self.assert_max_length(queries, 69)
        user_profile = get_user_profile_by_email('test@zulip.com')
        self.assertEqual(get_session_dict_user(self.client.session), user_profile.id)
        self.assertFalse(user_profile.enable_stream_desktop_notifications)

    def test_register_deactivated(self):
        # type: () -> None
        """
        If you try to register for a deactivated realm, you get a clear error
        page.
        """
        realm = get_realm_by_string_id("zulip")
        realm.deactivated = True
        realm.save(update_fields=["deactivated"])

        result = self.register("test", "test")
        self.assert_in_response("has been deactivated", result)

        with self.assertRaises(UserProfile.DoesNotExist):
            get_user_profile_by_email('test@zulip.com')

    def test_login_deactivated(self):
        # type: () -> None
        """
        If you try to log in to a deactivated realm, you get a clear error page.
        """
        realm = get_realm_by_string_id("zulip")
        realm.deactivated = True
        realm.save(update_fields=["deactivated"])

        result = self.login_with_return("hamlet@zulip.com")
        self.assert_in_response("has been deactivated", result)

    def test_logout(self):
        # type: () -> None
        self.login("hamlet@zulip.com")
        self.client_post('/accounts/logout/')
        self.assertIsNone(get_session_dict_user(self.client.session))

    def test_non_ascii_login(self):
        # type: () -> None
        """
        You can log in even if your password contain non-ASCII characters.
        """
        email = "test@zulip.com"
        password = u"hÃ¼mbÃ¼Çµ"

        # Registering succeeds.
        self.register("test", password)
        user_profile = get_user_profile_by_email(email)
        self.assertEqual(get_session_dict_user(self.client.session), user_profile.id)
        self.client_post('/accounts/logout/')
        self.assertIsNone(get_session_dict_user(self.client.session))

        # Logging in succeeds.
        self.client_post('/accounts/logout/')
        self.login(email, password)
        self.assertEqual(get_session_dict_user(self.client.session), user_profile.id)

class InviteUserTest(ZulipTestCase):

    def invite(self, users, streams):
        # type: (str, List[Text]) -> HttpResponse
        """
        Invites the specified users to Zulip with the specified streams.

        users should be a string containing the users to invite, comma or
            newline separated.

        streams should be a list of strings.
        """

        return self.client_post("/json/invite_users",
                                {"invitee_emails": users,
                                    "stream": streams})

    def check_sent_emails(self, correct_recipients):
        # type: (List[str]) -> None
        from django.core.mail import outbox
        self.assertEqual(len(outbox), len(correct_recipients))
        email_recipients = [email.recipients()[0] for email in outbox]
        self.assertEqual(sorted(email_recipients), sorted(correct_recipients))

    def test_bulk_invite_users(self):
        # type: () -> None
        """The bulk_invite_users code path is for the first user in a realm."""
        self.login('hamlet@zulip.com')
        invitees = ['alice@zulip.com', 'bob@zulip.com']
        params = {
            'invitee_emails': ujson.dumps(invitees)
        }
        result = self.client_post('/json/bulk_invite_users', params)
        self.assert_json_success(result)
        self.check_sent_emails(invitees)

    def test_successful_invite_user(self):
        # type: () -> None
        """
        A call to /json/invite_users with valid parameters causes an invitation
        email to be sent.
        """
        self.login("hamlet@zulip.com")
        invitee = "alice-test@zulip.com"
        self.assert_json_success(self.invite(invitee, ["Denmark"]))
        self.assertTrue(find_key_by_email(invitee))
        self.check_sent_emails([invitee])

    def test_successful_invite_user_with_name(self):
        # type: () -> None
        """
        A call to /json/invite_users with valid parameters causes an invitation
        email to be sent.
        """
        self.login("hamlet@zulip.com")
        email = "alice-test@zulip.com"
        invitee = "Alice Test <{}>".format(email)
        self.assert_json_success(self.invite(invitee, ["Denmark"]))
        self.assertTrue(find_key_by_email(email))
        self.check_sent_emails([email])

    def test_successful_invite_user_with_name_and_normal_one(self):
        # type: () -> None
        """
        A call to /json/invite_users with valid parameters causes an invitation
        email to be sent.
        """
        self.login("hamlet@zulip.com")
        email = "alice-test@zulip.com"
        email2 = "bob-test@zulip.com"
        invitee = "Alice Test <{}>, {}".format(email, email2)
        self.assert_json_success(self.invite(invitee, ["Denmark"]))
        self.assertTrue(find_key_by_email(email))
        self.assertTrue(find_key_by_email(email2))
        self.check_sent_emails([email, email2])

    def test_invite_user_signup_initial_history(self):
        # type: () -> None
        """
        Test that a new user invited to a stream receives some initial
        history but only from public streams.
        """
        self.login("hamlet@zulip.com")
        user_profile = get_user_profile_by_email("hamlet@zulip.com")
        private_stream_name = "Secret"
        self.make_stream(private_stream_name, invite_only=True)
        self.subscribe_to_stream(user_profile.email, private_stream_name)
        public_msg_id = self.send_message("hamlet@zulip.com", "Denmark", Recipient.STREAM,
                                          "Public topic", "Public message")
        secret_msg_id = self.send_message("hamlet@zulip.com", private_stream_name, Recipient.STREAM,
                                          "Secret topic", "Secret message")
        invitee = "alice-test@zulip.com"
        self.assert_json_success(self.invite(invitee, [private_stream_name, "Denmark"]))
        self.assertTrue(find_key_by_email(invitee))

        self.submit_reg_form_for_user("alice-test", "password")
        invitee_profile = get_user_profile_by_email(invitee)
        invitee_msg_ids = [um.message_id for um in
                           UserMessage.objects.filter(user_profile=invitee_profile)]
        self.assertTrue(public_msg_id in invitee_msg_ids)
        self.assertFalse(secret_msg_id in invitee_msg_ids)

    def test_multi_user_invite(self):
        # type: () -> None
        """
        Invites multiple users with a variety of delimiters.
        """
        self.login("hamlet@zulip.com")
        # Intentionally use a weird string.
        self.assert_json_success(self.invite(
            """bob-test@zulip.com,     carol-test@zulip.com,
            dave-test@zulip.com


earl-test@zulip.com""", ["Denmark"]))
        for user in ("bob", "carol", "dave", "earl"):
            self.assertTrue(find_key_by_email("%s-test@zulip.com" % (user,)))
        self.check_sent_emails(["bob-test@zulip.com", "carol-test@zulip.com",
                                "dave-test@zulip.com", "earl-test@zulip.com"])

    def test_missing_or_invalid_params(self):
        # type: () -> None
        """
        Tests inviting with various missing or invalid parameters.
        """
        self.login("hamlet@zulip.com")
        self.assert_json_error(
            self.client_post("/json/invite_users", {"invitee_emails": "foo@zulip.com"}),
            "You must specify at least one stream for invitees to join.")

        for address in ("noatsign.com", "outsideyourdomain@example.net"):
            self.assert_json_error(
                self.invite(address, ["Denmark"]),
                "Some emails did not validate, so we didn't send any invitations.")
        self.check_sent_emails([])

    def test_invalid_stream(self):
        # type: () -> None
        """
        Tests inviting to a non-existent stream.
        """
        self.login("hamlet@zulip.com")
        self.assert_json_error(self.invite("iago-test@zulip.com", ["NotARealStream"]),
                               "Stream does not exist: NotARealStream. No invites were sent.")
        self.check_sent_emails([])

    def test_invite_existing_user(self):
        # type: () -> None
        """
        If you invite an address already using Zulip, no invitation is sent.
        """
        self.login("hamlet@zulip.com")
        self.assert_json_error(
            self.client_post("/json/invite_users",
                             {"invitee_emails": "hamlet@zulip.com",
                              "stream": ["Denmark"]}),
            "We weren't able to invite anyone.")
        self.assertRaises(PreregistrationUser.DoesNotExist,
                          lambda: PreregistrationUser.objects.get(
                            email="hamlet@zulip.com"))
        self.check_sent_emails([])

    def test_invite_some_existing_some_new(self):
        # type: () -> None
        """
        If you invite a mix of already existing and new users, invitations are
        only sent to the new users.
        """
        self.login("hamlet@zulip.com")
        existing = ["hamlet@zulip.com", "othello@zulip.com"]
        new = ["foo-test@zulip.com", "bar-test@zulip.com"]

        result = self.client_post("/json/invite_users",
                                  {"invitee_emails": "\n".join(existing + new),
                                   "stream": ["Denmark"]})
        self.assert_json_error(result,
                               "Some of those addresses are already using Zulip, \
so we didn't send them an invitation. We did send invitations to everyone else!")

        # We only created accounts for the new users.
        for email in existing:
            self.assertRaises(PreregistrationUser.DoesNotExist,
                              lambda: PreregistrationUser.objects.get(
                                email=email))
        for email in new:
            self.assertTrue(PreregistrationUser.objects.get(email=email))

        # We only sent emails to the new users.
        self.check_sent_emails(new)

        prereg_user = get_prereg_user_by_email('foo-test@zulip.com')
        self.assertEqual(prereg_user.email, 'foo-test@zulip.com')

    def test_invite_outside_domain_in_closed_realm(self):
        # type: () -> None
        """
        In a realm with `restricted_to_domain = True`, you can't invite people
        with a different domain from that of the realm or your e-mail address.
        """
        zulip_realm = get_realm_by_string_id("zulip")
        zulip_realm.restricted_to_domain = True
        zulip_realm.save()

        self.login("hamlet@zulip.com")
        external_address = "foo@example.com"

        self.assert_json_error(
            self.invite(external_address, ["Denmark"]),
            "Some emails did not validate, so we didn't send any invitations.")

    def test_invite_outside_domain_in_open_realm(self):
        # type: () -> None
        """
        In a realm with `restricted_to_domain = False`, you can invite people
        with a different domain from that of the realm or your e-mail address.
        """
        zulip_realm = get_realm_by_string_id("zulip")
        zulip_realm.restricted_to_domain = False
        zulip_realm.save()

        self.login("hamlet@zulip.com")
        external_address = "foo@example.com"

        self.assert_json_success(self.invite(external_address, ["Denmark"]))
        self.check_sent_emails([external_address])

    def test_invite_with_non_ascii_streams(self):
        # type: () -> None
        """
        Inviting someone to streams with non-ASCII characters succeeds.
        """
        self.login("hamlet@zulip.com")
        invitee = "alice-test@zulip.com"

        stream_name = u"hÃ¼mbÃ¼Çµ"

        # Make sure we're subscribed before inviting someone.
        self.subscribe_to_stream("hamlet@zulip.com", stream_name)

        self.assert_json_success(self.invite(invitee, [stream_name]))

    def test_refer_friend(self):
        # type: () -> None
        self.login("hamlet@zulip.com")
        user = get_user_profile_by_email('hamlet@zulip.com')
        user.invites_granted = 1
        user.invites_used = 0
        user.save()

        invitee = "alice-test@zulip.com"
        result = self.client_post('/json/refer_friend', dict(email=invitee))
        self.assert_json_success(result)

        # verify this works
        Referral.objects.get(user_profile=user, email=invitee)

        user = get_user_profile_by_email('hamlet@zulip.com')
        self.assertEqual(user.invites_used, 1)

    def test_invitation_reminder_email(self):
        # type: () -> None
        from django.core.mail import outbox
        current_user_email = "hamlet@zulip.com"
        self.login(current_user_email)
        invitee = "alice-test@zulip.com"
        self.assert_json_success(self.invite(invitee, ["Denmark"]))
        self.assertTrue(find_key_by_email(invitee))
        self.check_sent_emails([invitee])

        data = {"email": invitee, "referrer_email": current_user_email}
        invitee = get_prereg_user_by_email(data["email"])
        referrer = get_user_profile_by_email(data["referrer_email"])
        link = Confirmation.objects.get_link_for_object(invitee, host=referrer.realm.host)
        context = common_context(referrer)
        context.update({
            'activate_url': link,
            'referrer': referrer,
            'verbose_support_offers': settings.VERBOSE_SUPPORT_OFFERS,
            'support_email': settings.ZULIP_ADMINISTRATOR
        })
        with self.settings(EMAIL_BACKEND='django.core.mail.backends.console.EmailBackend'):
            send_local_email_template_with_delay(
                [{'email': data["email"], 'name': ""}],
                "zerver/emails/invitation/invitation_reminder_email",
                context,
                datetime.timedelta(days=0),
                tags=["invitation-reminders"],
                sender={'email': settings.ZULIP_ADMINISTRATOR, 'name': 'Zulip'})
        email_jobs_to_deliver = ScheduledJob.objects.filter(
            type=ScheduledJob.EMAIL,
            scheduled_timestamp__lte=datetime.datetime.utcnow())
        self.assertEqual(len(email_jobs_to_deliver), 1)
        email_count = len(outbox)
        for job in email_jobs_to_deliver:
            self.assertTrue(send_email_job(job))
        self.assertEqual(len(outbox), email_count + 1)

class InviteeEmailsParserTests(TestCase):
    def setUp(self):
        # type: () -> None
        self.email1 = "email1@zulip.com"
        self.email2 = "email2@zulip.com"
        self.email3 = "email3@zulip.com"

    def test_if_emails_separated_by_commas_are_parsed_and_striped_correctly(self):
        # type: () -> None
        emails_raw = "{} ,{}, {}".format(self.email1, self.email2, self.email3)
        expected_set = {self.email1, self.email2, self.email3}
        self.assertEqual(get_invitee_emails_set(emails_raw), expected_set)

    def test_if_emails_separated_by_newlines_are_parsed_and_striped_correctly(self):
        # type: () -> None
        emails_raw = "{}\n {}\n {} ".format(self.email1, self.email2, self.email3)
        expected_set = {self.email1, self.email2, self.email3}
        self.assertEqual(get_invitee_emails_set(emails_raw), expected_set)

    def test_if_emails_from_email_client_separated_by_newlines_are_parsed_correctly(self):
        # type: () -> None
        emails_raw = "Email One <{}>\nEmailTwo<{}>\nEmail Three<{}>".format(self.email1, self.email2, self.email3)
        expected_set = {self.email1, self.email2, self.email3}
        self.assertEqual(get_invitee_emails_set(emails_raw), expected_set)

    def test_if_emails_in_mixed_style_are_parsed_correctly(self):
        # type: () -> None
        emails_raw = "Email One <{}>,EmailTwo<{}>\n{}".format(self.email1, self.email2, self.email3)
        expected_set = {self.email1, self.email2, self.email3}
        self.assertEqual(get_invitee_emails_set(emails_raw), expected_set)


class EmailUnsubscribeTests(ZulipTestCase):
    def test_error_unsubscribe(self):
        # type: () -> None
        result = self.client_get('/accounts/unsubscribe/missed_messages/test123')
        self.assert_in_response('Unknown email unsubscribe request', result)

    def test_missedmessage_unsubscribe(self):
        # type: () -> None
        """
        We provide one-click unsubscribe links in missed message
        e-mails that you can click even when logged out to update your
        email notification settings.
        """
        user_profile = get_user_profile_by_email("hamlet@zulip.com")
        user_profile.enable_offline_email_notifications = True
        user_profile.save()

        unsubscribe_link = one_click_unsubscribe_link(user_profile,
                                                      "missed_messages")
        result = self.client_get(urllib.parse.urlparse(unsubscribe_link).path)

        self.assertEqual(result.status_code, 200)
        # Circumvent user_profile caching.
        user_profile = UserProfile.objects.get(email="hamlet@zulip.com")
        self.assertFalse(user_profile.enable_offline_email_notifications)

    def test_welcome_unsubscribe(self):
        # type: () -> None
        """
        We provide one-click unsubscribe links in welcome e-mails that you can
        click even when logged out to stop receiving them.
        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email("hamlet@zulip.com")

        # Simulate a new user signing up, which enqueues 2 welcome e-mails.
        enqueue_welcome_emails(email, "King Hamlet")
        self.assertEqual(2, len(ScheduledJob.objects.filter(
                type=ScheduledJob.EMAIL, filter_string__iexact=email)))

        # Simulate unsubscribing from the welcome e-mails.
        unsubscribe_link = one_click_unsubscribe_link(user_profile, "welcome")
        result = self.client_get(urllib.parse.urlparse(unsubscribe_link).path)

        # The welcome email jobs are no longer scheduled.
        self.assertEqual(result.status_code, 200)
        self.assertEqual(0, len(ScheduledJob.objects.filter(
                type=ScheduledJob.EMAIL, filter_string__iexact=email)))

    def test_digest_unsubscribe(self):
        # type: () -> None
        """
        We provide one-click unsubscribe links in digest e-mails that you can
        click even when logged out to stop receiving them.

        Unsubscribing from these emails also dequeues any digest email jobs that
        have been queued.
        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email("hamlet@zulip.com")
        self.assertTrue(user_profile.enable_digest_emails)

        # Enqueue a fake digest email.
        send_digest_email(user_profile, "", "")
        self.assertEqual(1, len(ScheduledJob.objects.filter(
                    type=ScheduledJob.EMAIL, filter_string__iexact=email)))

        # Simulate unsubscribing from digest e-mails.
        unsubscribe_link = one_click_unsubscribe_link(user_profile, "digest")
        result = self.client_get(urllib.parse.urlparse(unsubscribe_link).path)

        # The setting is toggled off, and scheduled jobs have been removed.
        self.assertEqual(result.status_code, 200)
        # Circumvent user_profile caching.
        user_profile = UserProfile.objects.get(email="hamlet@zulip.com")
        self.assertFalse(user_profile.enable_digest_emails)
        self.assertEqual(0, len(ScheduledJob.objects.filter(
                type=ScheduledJob.EMAIL, filter_string__iexact=email)))

class RealmCreationTest(ZulipTestCase):

    def test_create_realm(self):
        # type: () -> None
        username = "user1"
        password = "test"
        string_id = "zuliptest"
        domain = 'test.com'
        email = "user1@test.com"
        realm = get_realm_by_string_id('test')

        # Make sure the realm does not exist
        self.assertIsNone(realm)

        with self.settings(OPEN_REALM_CREATION=True):
            # Create new realm with the email
            result = self.client_post('/create_realm/', {'email': email})
            self.assertEqual(result.status_code, 302)
            self.assertTrue(result["Location"].endswith(
                    "/accounts/send_confirm/%s" % (email,)))
            result = self.client_get(result["Location"])
            self.assert_in_response("Check your email so we can get started.", result)

            # Visit the confirmation link.
            confirmation_url = self.get_confirmation_url_from_outbox(email)
            result = self.client_get(confirmation_url)
            self.assertEqual(result.status_code, 200)

            result = self.submit_reg_form_for_user(username, password, domain=domain,
                                                   realm_subdomain = string_id)
            self.assertEqual(result.status_code, 302)

            # Make sure the realm is created
            realm = get_realm_by_string_id(string_id)
            self.assertIsNotNone(realm)
            self.assertEqual(realm.string_id, string_id)
            self.assertEqual(get_user_profile_by_email(email).realm, realm)

            # Check defaults
            self.assertEqual(realm.org_type, Realm.COMMUNITY)
            self.assertEqual(realm.restricted_to_domain, False)
            self.assertEqual(realm.invite_required, True)

            self.assertTrue(result["Location"].endswith("/"))

    def test_create_realm_with_subdomain(self):
        # type: () -> None
        username = "user1"
        password = "test"
        string_id = "zuliptest"
        domain = "test.com"
        email = "user1@test.com"
        realm_name = "Test"

        # Make sure the realm does not exist
        self.assertIsNone(get_realm_by_string_id('test'))

        with self.settings(REALMS_HAVE_SUBDOMAINS=True), self.settings(OPEN_REALM_CREATION=True):
            # Create new realm with the email
            result = self.client_post('/create_realm/', {'email': email})
            self.assertEqual(result.status_code, 302)
            self.assertTrue(result["Location"].endswith(
                    "/accounts/send_confirm/%s" % (email,)))
            result = self.client_get(result["Location"])
            self.assert_in_response("Check your email so we can get started.", result)

            # Visit the confirmation link.
            confirmation_url = self.get_confirmation_url_from_outbox(email)
            result = self.client_get(confirmation_url)
            self.assertEqual(result.status_code, 200)

            result = self.submit_reg_form_for_user(username, password, domain=domain,
                                                   realm_subdomain = string_id,
                                                   realm_name=realm_name,
                                                   # Pass HTTP_HOST for the target subdomain
                                                   HTTP_HOST=string_id + ".testserver")
            self.assertEqual(result.status_code, 302)

            # Make sure the realm is created
            realm = get_realm_by_string_id(string_id)
            self.assertIsNotNone(realm)
            self.assertEqual(realm.string_id, string_id)
            self.assertEqual(get_user_profile_by_email(email).realm, realm)

            self.assertEqual(realm.name, realm_name)
            self.assertEqual(realm.subdomain, string_id)

    def test_mailinator_signup(self):
        # type: () -> None
        with self.settings(OPEN_REALM_CREATION=True):
            result = self.client_post('/create_realm/', {'email': "hi@mailinator.com"})
            self.assert_in_response('Please use your real email address.', result)

    def test_subdomain_restrictions(self):
        # type: () -> None
        username = "user1"
        password = "test"
        domain = "test.com"
        email = "user1@test.com"
        realm_name = "Test"

        with self.settings(REALMS_HAVE_SUBDOMAINS=False), self.settings(OPEN_REALM_CREATION=True):
            result = self.client_post('/create_realm/', {'email': email})
            self.client_get(result["Location"])
            confirmation_url = self.get_confirmation_url_from_outbox(email)
            self.client_get(confirmation_url)

            errors = {'id': "at least 3 characters",
                      '-id': "cannot start or end with a",
                      'string-ID': "lowercase letters",
                      'string_id': "lowercase letters",
                      'stream': "unavailable",
                      'streams': "unavailable",
                      'about': "unavailable",
                      'abouts': "unavailable",
                      'mit': "unavailable"}
            for string_id, error_msg in errors.items():
                result = self.submit_reg_form_for_user(username, password, domain = domain,
                                                       realm_subdomain = string_id,
                                                       realm_name = realm_name)
                self.assert_in_response(error_msg, result)

            # test valid subdomain
            result = self.submit_reg_form_for_user(username, password, domain = domain,
                                                   realm_subdomain = 'a-0',
                                                   realm_name = realm_name)
            self.assertEqual(result.status_code, 302)

class UserSignUpTest(ZulipTestCase):

    def test_user_default_language(self):
        # type: () -> None
        """
        Check if the default language of new user is the default language
        of the realm.
        """
        username = "newguy"
        email = "newguy@zulip.com"
        password = "newpassword"
        realm = get_realm_by_string_id('zulip')
        domain = realm.domain
        do_set_realm_default_language(realm, "de")

        result = self.client_post('/accounts/home/', {'email': email})
        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith(
                "/accounts/send_confirm/%s@%s" % (username, domain)))
        result = self.client_get(result["Location"])
        self.assert_in_response("Check your email so we can get started.", result)

        # Visit the confirmation link.
        confirmation_url = self.get_confirmation_url_from_outbox(email)
        result = self.client_get(confirmation_url)
        self.assertEqual(result.status_code, 200)

        # Pick a password and agree to the ToS.
        result = self.submit_reg_form_for_user(username, password, domain)
        self.assertEqual(result.status_code, 302)

        user_profile = get_user_profile_by_email(email)
        self.assertEqual(user_profile.default_language, realm.default_language)
        from django.core.mail import outbox
        outbox.pop()

    def test_unique_completely_open_domain(self):
        # type: () -> None
        username = "user1"
        password = "test"
        email = "user1@acme.com"
        subdomain = "zulip"
        realm_name = "Zulip"

        realm = get_realm_by_string_id('zulip')
        realm.restricted_to_domain = False
        realm.invite_required = False
        realm.save()

        realm = get_realm_by_string_id('mit')
        do_deactivate_realm(realm)
        realm.save()

        result = self.client_post('/register/', {'email': email})

        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith(
                "/accounts/send_confirm/%s" % (email,)))
        result = self.client_get(result["Location"])
        self.assert_in_response("Check your email so we can get started.", result)
        # Visit the confirmation link.
        from django.core.mail import outbox
        for message in reversed(outbox):
            if email in message.to:
                confirmation_link_pattern = re.compile(settings.EXTERNAL_HOST + "(\S+)>")
                confirmation_url = confirmation_link_pattern.search(
                    message.body).groups()[0]
                break
        else:
            raise ValueError("Couldn't find a confirmation email.")

        result = self.client_get(confirmation_url)
        self.assertEqual(result.status_code, 200)

        result = self.submit_reg_form_for_user(username,
                                               password,
                                               domain='acme.com',
                                               realm_name=realm_name,
                                               realm_subdomain=subdomain,
                                               # Pass HTTP_HOST for the target subdomain
                                               HTTP_HOST=subdomain + ".testserver")
        self.assert_in_success_response(["You're almost there."], result)

    def test_completely_open_domain_success(self):
        # type: () -> None
        username = "user1"
        password = "test"
        email = "user1@acme.com"
        subdomain = "zulip"
        realm_name = "Zulip"

        realm = get_realm_by_string_id('zulip')
        realm.restricted_to_domain = False
        realm.invite_required = False
        realm.save()

        result = self.client_post('/register/zulip/', {'email': email})

        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith(
                "/accounts/send_confirm/%s" % (email,)))
        result = self.client_get(result["Location"])
        self.assert_in_response("Check your email so we can get started.", result)
        # Visit the confirmation link.
        from django.core.mail import outbox
        for message in reversed(outbox):
            if email in message.to:
                confirmation_link_pattern = re.compile(settings.EXTERNAL_HOST + "(\S+)>")
                confirmation_url = confirmation_link_pattern.search(
                    message.body).groups()[0]
                break
        else:
            raise ValueError("Couldn't find a confirmation email.")

        result = self.client_get(confirmation_url)
        self.assertEqual(result.status_code, 200)

        result = self.submit_reg_form_for_user(username,
                                               password,
                                               domain='acme.com',
                                               realm_name=realm_name,
                                               realm_subdomain=subdomain,
                                               # Pass HTTP_HOST for the target subdomain
                                               HTTP_HOST=subdomain + ".testserver")
        self.assert_in_success_response(["You're almost there."], result)

    def test_failed_signup_due_to_restricted_domain(self):
        # type: () -> None
        realm = get_realm_by_string_id('zulip')
        with self.settings(REALMS_HAVE_SUBDOMAINS = True):
            request = HostRequestMock(host = realm.host)
            request.session = {} # type: ignore
            form = HomepageForm({'email': 'user@acme.com'}, realm=realm)
            self.assertIn("trying to join, zulip, only allows users with e-mail", form.errors['email'][0])

    def test_failed_signup_due_to_invite_required(self):
        # type: () -> None
        realm = get_realm_by_string_id('zulip')
        realm.invite_required = True
        realm.save()
        request = HostRequestMock(host = realm.host)
        request.session = {} # type: ignore
        form = HomepageForm({'email': 'user@zulip.com'}, realm=realm)
        self.assertIn("Please request an invite from", form.errors['email'][0])

    def test_failed_signup_due_to_nonexistent_realm(self):
        # type: () -> None
        with self.settings(REALMS_HAVE_SUBDOMAINS = True):
            request = HostRequestMock(host = 'acme.' + settings.EXTERNAL_HOST)
            request.session = {} # type: ignore
            form = HomepageForm({'email': 'user@acme.com'}, realm=None)
            self.assertIn("organization you are trying to join does not exist", form.errors['email'][0])

    def test_registration_through_ldap(self):
        # type: () -> None
        username = "newuser"
        password = "testing"
        domain = "zulip.com"
        email = "newuser@zulip.com"
        subdomain = "zulip"
        realm_name = "Zulip"
        ldap_user_attr_map = {'full_name': 'fn', 'short_name': 'sn'}

        ldap_patcher = patch('django_auth_ldap.config.ldap.initialize')
        mock_initialize = ldap_patcher.start()
        mock_ldap = MockLDAP()
        mock_initialize.return_value = mock_ldap

        mock_ldap.directory = {
            'uid=newuser,ou=users,dc=zulip,dc=com': {
                'userPassword': 'testing',
                'fn': ['New User Name']
            }
        }

        with patch('zerver.views.get_subdomain', return_value=subdomain):
            result = self.client_post('/register/', {'email': email})

        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith(
                "/accounts/send_confirm/%s" % (email,)))
        result = self.client_get(result["Location"])
        self.assert_in_response("Check your email so we can get started.", result)
        # Visit the confirmation link.
        from django.core.mail import outbox
        for message in reversed(outbox):
            if email in message.to:
                confirmation_link_pattern = re.compile(settings.EXTERNAL_HOST + "(\S+)>")
                confirmation_url = confirmation_link_pattern.search(
                    message.body).groups()[0]
                break
        else:
            raise ValueError("Couldn't find a confirmation email.")

        with self.settings(
                POPULATE_PROFILE_VIA_LDAP=True,
                LDAP_APPEND_DOMAIN='zulip.com',
                AUTH_LDAP_BIND_PASSWORD='',
                AUTH_LDAP_USER_ATTR_MAP=ldap_user_attr_map,
                AUTHENTICATION_BACKENDS=('zproject.backends.ZulipLDAPAuthBackend',),
                AUTH_LDAP_USER_DN_TEMPLATE='uid=%(user)s,ou=users,dc=zulip,dc=com'):
            result = self.client_get(confirmation_url)
            self.assertEqual(result.status_code, 200)
            result = self.submit_reg_form_for_user(username,
                                                   password,
                                                   domain=domain,
                                                   realm_name=realm_name,
                                                   realm_subdomain=subdomain,
                                                   from_confirmation='1',
                                                   # Pass HTTP_HOST for the target subdomain
                                                   HTTP_HOST=subdomain + ".testserver")

            self.assert_in_success_response(["You're almost there.",
                                             "New User Name",
                                             "newuser@zulip.com"],
                                            result)

            # Test the TypeError exception handler
            mock_ldap.directory = {
                'uid=newuser,ou=users,dc=zulip,dc=com': {
                    'userPassword': 'testing',
                    'fn': None  # This will raise TypeError
                }
            }
            result = self.submit_reg_form_for_user(username,
                                                   password,
                                                   domain=domain,
                                                   realm_name=realm_name,
                                                   realm_subdomain=subdomain,
                                                   from_confirmation='1',
                                                   # Pass HTTP_HOST for the target subdomain
                                                   HTTP_HOST=subdomain + ".testserver")
            self.assert_in_success_response(["You're almost there.",
                                             "newuser@zulip.com"],
                                            result)

        mock_ldap.reset()
        mock_initialize.stop()

    @patch('DNS.dnslookup', return_value=[['sipbtest:*:20922:101:Fred Sipb,,,:/mit/sipbtest:/bin/athena/tcsh']])
    def test_registration_of_mirror_dummy_user(self, ignored):
        # type: (Any) -> None
        username = "sipbtest"
        password = "test"
        domain = "mit.edu"
        email = "sipbtest@mit.edu"
        subdomain = "sipb"
        realm_name = "MIT"

        user_profile = get_user_profile_by_email(email)
        user_profile.is_mirror_dummy = True
        user_profile.is_active = False
        user_profile.save()

        result = self.client_post('/register/', {'email': email})

        self.assertEqual(result.status_code, 302)
        self.assertTrue(result["Location"].endswith(
                "/accounts/send_confirm/%s" % (email,)))
        result = self.client_get(result["Location"])
        self.assert_in_response("Check your email so we can get started.", result)
        # Visit the confirmation link.
        from django.core.mail import outbox
        for message in reversed(outbox):
            if email in message.to:
                confirmation_link_pattern = re.compile(settings.EXTERNAL_HOST + "(\S+)>")
                confirmation_url = confirmation_link_pattern.search(
                    message.body).groups()[0]
                break
        else:
            raise ValueError("Couldn't find a confirmation email.")

        result = self.client_get(confirmation_url)
        self.assertEqual(result.status_code, 200)
        result = self.submit_reg_form_for_user(username,
                                               password,
                                               domain=domain,
                                               realm_name=realm_name,
                                               realm_subdomain=subdomain,
                                               from_confirmation='1',
                                               # Pass HTTP_HOST for the target subdomain
                                               HTTP_HOST=subdomain + ".testserver")
        self.assertEqual(result.status_code, 200)
        result = self.submit_reg_form_for_user(username,
                                               password,
                                               domain=domain,
                                               realm_name=realm_name,
                                               realm_subdomain=subdomain,
                                               # Pass HTTP_HOST for the target subdomain
                                               HTTP_HOST=subdomain + ".testserver")
        self.assertEqual(result.status_code, 302)
        self.assertEqual(get_session_dict_user(self.client.session), user_profile.id)

class DeactivateUserTest(ZulipTestCase):

    def test_deactivate_user(self):
        # type: () -> None
        email = 'hamlet@zulip.com'
        self.login(email)
        user = get_user_profile_by_email('hamlet@zulip.com')
        self.assertTrue(user.is_active)
        result = self.client_delete('/json/users/me')
        self.assert_json_success(result)
        user = get_user_profile_by_email('hamlet@zulip.com')
        self.assertFalse(user.is_active)
        self.login(email, fails=True)

    def test_do_not_deactivate_final_admin(self):
        # type: () -> None
        email = 'iago@zulip.com'
        self.login(email)
        user = get_user_profile_by_email('iago@zulip.com')
        self.assertTrue(user.is_active)
        result = self.client_delete('/json/users/me')
        self.assert_json_error(result, "Cannot deactivate the only organization administrator")
        user = get_user_profile_by_email('iago@zulip.com')
        self.assertTrue(user.is_active)
        self.assertTrue(user.is_realm_admin)
        email = 'hamlet@zulip.com'
        user_2 = get_user_profile_by_email('hamlet@zulip.com')
        do_change_is_admin(user_2, True)
        self.assertTrue(user_2.is_realm_admin)
        result = self.client_delete('/json/users/me')
        self.assert_json_success(result)
        do_change_is_admin(user, True)
