# -*- coding: utf-8 -*-
import mock

from django.test import TestCase
from django.utils.translation import ugettext as _
from django.http import HttpResponse
from django.test.client import RequestFactory
from django.conf import settings

from zerver.lib.actions import do_deactivate_realm, do_deactivate_user, \
    do_reactivate_user, do_reactivate_realm
from zerver.lib.initial_password import initial_password
from zerver.lib.test_helpers import (
    HostRequestMock,
)
from zerver.lib.test_classes import (
    ZulipTestCase,
    WebhookTestCase,
)
from zerver.lib.response import json_response
from zerver.lib.request import \
    REQ, has_request_variables, RequestVariableMissingError, \
    RequestVariableConversionError, JsonableError
from zerver.decorator import (
    api_key_only_webhook_view,
    authenticated_json_post_view, authenticated_json_view,
    authenticate_notify,
    get_client_name, internal_notify_view, is_local_addr,
    rate_limit, validate_api_key, logged_in_and_active,
    return_success_on_head_request
    )
from zerver.lib.validator import (
    check_string, check_dict, check_bool, check_int, check_list
)
from zerver.models import \
    get_realm_by_string_id, get_user_profile_by_email

import ujson

class DecoratorTestCase(TestCase):
    def __init__(self, *args, **kwargs):
        # type: (*Any, **Any) -> None
        # This method should be removed when we migrate to version 3 of Python
        import six
        if six.PY2:
            self.assertRaisesRegex = self.assertRaisesRegexp
        super(TestCase, self).__init__(*args, **kwargs)

    def test_get_client_name(self):
        class Request(object):
            def __init__(self, GET, POST, META):
                self.GET = GET
                self.POST = POST
                self.META = META

        req = Request(
            GET=dict(),
            POST=dict(),
            META=dict(),
            )

        self.assertEqual(get_client_name(req, is_json_view=True), 'website')
        self.assertEqual(get_client_name(req, is_json_view=False), 'Unspecified')

        req = Request(
            GET=dict(),
            POST=dict(),
            META=dict(HTTP_USER_AGENT='Mozilla/bla bla bla'),
            )

        self.assertEqual(get_client_name(req, is_json_view=True), 'website')
        self.assertEqual(get_client_name(req, is_json_view=False), 'Mozilla')

        req = Request(
            GET=dict(),
            POST=dict(),
            META=dict(HTTP_USER_AGENT='ZulipDesktop/bla bla bla'),
            )

        self.assertEqual(get_client_name(req, is_json_view=True), 'ZulipDesktop')
        self.assertEqual(get_client_name(req, is_json_view=False), 'ZulipDesktop')

        req = Request(
            GET=dict(client='fancy phone'),
            POST=dict(),
            META=dict(),
            )

        self.assertEqual(get_client_name(req, is_json_view=True), 'fancy phone')
        self.assertEqual(get_client_name(req, is_json_view=False), 'fancy phone')

    def test_REQ_converter(self):

        def my_converter(data):
            lst = ujson.loads(data)
            if not isinstance(lst, list):
                raise ValueError('not a list')
            if 13 in lst:
                raise JsonableError(_('13 is an unlucky number!'))
            return lst

        @has_request_variables
        def get_total(request, numbers=REQ(converter=my_converter)):
            return sum(numbers)

        class Request(object):
            GET = {}  # type: Dict[str, str]
            POST = {}  # type: Dict[str, str]

        request = Request()

        with self.assertRaises(RequestVariableMissingError):
            get_total(request)

        request.POST['numbers'] = 'bad_value'
        with self.assertRaises(RequestVariableConversionError) as cm:
            get_total(request)
        self.assertEqual(str(cm.exception), "Bad value for 'numbers': bad_value")

        request.POST['numbers'] = ujson.dumps([2, 3, 5, 8, 13, 21])
        with self.assertRaises(JsonableError) as cm:
            get_total(request)
        self.assertEqual(str(cm.exception), "13 is an unlucky number!")

        request.POST['numbers'] = ujson.dumps([1, 2, 3, 4, 5, 6])
        result = get_total(request)
        self.assertEqual(result, 21)

    def test_REQ_validator(self):

        @has_request_variables
        def get_total(request, numbers=REQ(validator=check_list(check_int))):
            return sum(numbers)

        class Request(object):
            GET = {} # type: Dict[str, str]
            POST = {} # type: Dict[str, str]

        request = Request()

        with self.assertRaises(RequestVariableMissingError):
            get_total(request)

        request.POST['numbers'] = 'bad_value'
        with self.assertRaises(JsonableError) as cm:
            get_total(request)
        self.assertEqual(str(cm.exception), 'argument "numbers" is not valid json.')

        request.POST['numbers'] = ujson.dumps([1, 2, "what?", 4, 5, 6])
        with self.assertRaises(JsonableError) as cm:
            get_total(request)
        self.assertEqual(str(cm.exception), 'numbers[2] is not an integer')

        request.POST['numbers'] = ujson.dumps([1, 2, 3, 4, 5, 6])
        result = get_total(request)
        self.assertEqual(result, 21)

    def test_REQ_argument_type(self):

        @has_request_variables
        def get_payload(request, payload=REQ(argument_type='body')):
            return payload

        class MockRequest(object):
            body = {}

        request = MockRequest()

        request.body = 'notjson'
        with self.assertRaises(JsonableError) as cm:
            get_payload(request)
        self.assertEqual(str(cm.exception), 'Malformed JSON')

        request.body = '{"a": "b"}'
        self.assertEqual(get_payload(request), {'a': 'b'})

        # Test we properly handle an invalid argument_type.
        with self.assertRaises(Exception) as cm:
            @has_request_variables
            def test(request, payload=REQ(argument_type="invalid")):
                pass
            test(request)

    def test_api_key_only_webhook_view(self):
        @api_key_only_webhook_view('ClientName')
        def my_webhook(request, user_profile, client):
            return user_profile.email

        class Request(HostRequestMock):
            GET = {} # type: Dict[str, str]
            POST = {} # type: Dict[str, str]
            COOKIES = {}
            META = {'PATH_INFO': ''}

        webhook_bot_email = 'webhook-bot@zulip.com'
        webhook_bot = get_user_profile_by_email(webhook_bot_email)
        webhook_bot_api_key = webhook_bot.api_key

        request = Request()
        request.host = settings.EXTERNAL_HOST

        request.POST['api_key'] = 'not_existing_api_key'
        with self.assertRaisesRegex(JsonableError, "Invalid API key"):
            my_webhook(request)

        # Start a valid request here
        request.POST['api_key'] = webhook_bot_api_key

        with self.settings(REALMS_HAVE_SUBDOMAINS=True):
            with mock.patch('logging.warning') as mock_warning:
                with self.assertRaisesRegex(JsonableError,
                                            "Account is not associated with this subdomain"):
                    api_result = my_webhook(request)

                mock_warning.assert_called_with(
                    "User {} attempted to access webhook API on wrong "
                    "subdomain {}".format(webhook_bot_email, ''))

            with mock.patch('logging.warning') as mock_warning:
                with self.assertRaisesRegex(JsonableError,
                                            "Account is not associated with this subdomain"):
                    request.host = "acme." + settings.EXTERNAL_HOST
                    api_result = my_webhook(request)

                mock_warning.assert_called_with(
                    "User {} attempted to access webhook API on wrong "
                    "subdomain {}".format(webhook_bot_email, 'acme'))

        with self.settings(RATE_LIMITING=True):
            with mock.patch('zerver.decorator.rate_limit_user') as rate_limit_mock:
                api_result = my_webhook(request)

        # Verify rate limiting was attempted.
        self.assertTrue(rate_limit_mock.called)

        # Verify decorator set the magic _email field used by some of our back end logging.
        self.assertEqual(request._email, webhook_bot_email)

        # Verify the main purpose of the decorator, which is that it passed in the
        # user_profile to my_webhook, allowing it return the correct
        # email for the bot (despite the API caller only knowing the API key).
        self.assertEqual(api_result, webhook_bot_email)

        # Now deactivate the user
        webhook_bot.is_active = False
        webhook_bot.save()
        with self.assertRaisesRegex(JsonableError, "Account not active"):
            my_webhook(request)

        # Reactive the user, but deactivate their realm.
        webhook_bot.is_active = True
        webhook_bot.save()
        webhook_bot.realm.deactivated = True
        webhook_bot.realm.save()
        with self.assertRaisesRegex(JsonableError, "Realm for account has been deactivated"):
            my_webhook(request)


class RateLimitTestCase(TestCase):
    def errors_disallowed(self):
        # Due to what is probably a hack in rate_limit(),
        # some tests will give a false positive (or succeed
        # for the wrong reason), unless we complain
        # about logging errors.  There might be a more elegant way
        # make logging errors fail than what I'm doing here.
        class TestLoggingErrorException(Exception):
            pass
        return mock.patch('logging.error', side_effect=TestLoggingErrorException)

    def test_internal_local_clients_skip_rate_limiting(self):
        class Client(object):
            name = 'internal'

        class Request(object):
            client = Client()
            META = {'REMOTE_ADDR': '127.0.0.1'}

        req = Request()

        def f(req):
            return 'some value'

        f = rate_limit()(f)

        with self.settings(RATE_LIMITING=True):
            with mock.patch('zerver.decorator.rate_limit_user') as rate_limit_mock:
                with self.errors_disallowed():
                    self.assertEqual(f(req), 'some value')

        self.assertFalse(rate_limit_mock.called)

    def test_debug_clients_skip_rate_limiting(self):
        class Client(object):
            name = 'internal'

        class Request(object):
            client = Client()
            META = {'REMOTE_ADDR': '3.3.3.3'}

        req = Request()

        def f(req):
            return 'some value'

        f = rate_limit()(f)

        with self.settings(RATE_LIMITING=True):
            with mock.patch('zerver.decorator.rate_limit_user') as rate_limit_mock:
                with self.errors_disallowed():
                    with self.settings(DEBUG_RATE_LIMITING=True):
                        self.assertEqual(f(req), 'some value')

        self.assertFalse(rate_limit_mock.called)

    def test_rate_limit_setting_of_false_bypasses_rate_limiting(self):
        class Client(object):
            name = 'external'

        class Request(object):
            client = Client()
            META = {'REMOTE_ADDR': '3.3.3.3'}
            user = 'stub' # any non-None value here exercises the correct code path

        req = Request()

        def f(req):
            return 'some value'

        f = rate_limit()(f)

        with self.settings(RATE_LIMITING=False):
            with mock.patch('zerver.decorator.rate_limit_user') as rate_limit_mock:
                with self.errors_disallowed():
                    self.assertEqual(f(req), 'some value')

        self.assertFalse(rate_limit_mock.called)

    def test_rate_limiting_happens_in_normal_case(self):
        class Client(object):
            name = 'external'

        class Request(object):
            client = Client()
            META = {'REMOTE_ADDR': '3.3.3.3'}
            user = 'stub' # any non-None value here exercises the correct code path

        req = Request()

        def f(req):
            return 'some value'

        f = rate_limit()(f)

        with self.settings(RATE_LIMITING=True):
            with mock.patch('zerver.decorator.rate_limit_user') as rate_limit_mock:
                with self.errors_disallowed():
                    self.assertEqual(f(req), 'some value')

        self.assertTrue(rate_limit_mock.called)

class ValidatorTestCase(TestCase):
    def test_check_string(self):
        x = "hello"
        self.assertEqual(check_string('x', x), None)

        x = 4
        self.assertEqual(check_string('x', x), 'x is not a string')

    def test_check_bool(self):
        x = True
        self.assertEqual(check_bool('x', x), None)

        x = 4
        self.assertEqual(check_bool('x', x), 'x is not a boolean')

    def test_check_int(self):
        x = 5
        self.assertEqual(check_int('x', x), None)

        x = [{}]
        self.assertEqual(check_int('x', x), 'x is not an integer')

    def test_check_list(self):
        x = 999
        error = check_list(check_string)('x', x)
        self.assertEqual(error, 'x is not a list')

        x = ["hello", 5]
        error = check_list(check_string)('x', x)
        self.assertEqual(error, 'x[1] is not a string')

        x = [["yo"], ["hello", "goodbye", 5]]
        error = check_list(check_list(check_string))('x', x)
        self.assertEqual(error, 'x[1][2] is not a string')

        x = ["hello", "goodbye", "hello again"]
        error = check_list(check_string, length=2)('x', x)
        self.assertEqual(error, 'x should have exactly 2 items')

    def test_check_dict(self):
        keys = [
            ('names', check_list(check_string)),
            ('city', check_string),
        ]

        x = {
            'names': ['alice', 'bob'],
            'city': 'Boston',
        }
        error = check_dict(keys)('x', x)
        self.assertEqual(error, None)

        x = 999
        error = check_dict(keys)('x', x)
        self.assertEqual(error, 'x is not a dict')

        x = {}
        error = check_dict(keys)('x', x)
        self.assertEqual(error, 'names key is missing from x')

        x = {
            'names': ['alice', 'bob', {}]
        }
        error = check_dict(keys)('x', x)
        self.assertEqual(error, 'x["names"][2] is not a string')

        x = {
            'names': ['alice', 'bob'],
            'city': 5
        }
        error = check_dict(keys)('x', x)
        self.assertEqual(error, 'x["city"] is not a string')

    def test_encapsulation(self):
        # There might be situations where we want deep
        # validation, but the error message should be customized.
        # This is an example.
        def check_person(val):
            error = check_dict([
                ['name', check_string],
                ['age', check_int],
            ])('_', val)
            if error:
                return 'This is not a valid person'

        person = {'name': 'King Lear', 'age': 42}
        self.assertEqual(check_person(person), None)

        person = 'misconfigured data'
        self.assertEqual(check_person(person), 'This is not a valid person')

class DeactivatedRealmTest(ZulipTestCase):
    def test_send_deactivated_realm(self):
        """
        rest_dispatch rejects requests in a deactivated realm, both /json and api

        """
        realm = get_realm_by_string_id("zulip")
        do_deactivate_realm(get_realm_by_string_id("zulip"))

        result = self.client_post("/json/messages", {"type": "private",
                                                     "content": "Test message",
                                                     "client": "test suite",
                                                     "to": "othello@zulip.com"})
        self.assert_json_error_contains(result, "Not logged in", status_code=401)

        # Even if a logged-in session was leaked, it still wouldn't work
        realm.deactivated = False
        realm.save()
        self.login("hamlet@zulip.com")
        realm.deactivated = True
        realm.save()

        result = self.client_post("/json/messages", {"type": "private",
                                                     "content": "Test message",
                                                     "client": "test suite",
                                                     "to": "othello@zulip.com"})
        self.assert_json_error_contains(result, "has been deactivated", status_code=400)

        result = self.client_post("/api/v1/messages", {"type": "private",
                                                       "content": "Test message",
                                                       "client": "test suite",
                                                       "to": "othello@zulip.com"},
                                  **self.api_auth("hamlet@zulip.com"))
        self.assert_json_error_contains(result, "has been deactivated", status_code=401)

    def test_fetch_api_key_deactivated_realm(self):
        """
        authenticated_json_view views fail in a deactivated realm

        """
        realm = get_realm_by_string_id("zulip")
        email = "hamlet@zulip.com"
        test_password = "abcd1234"
        user_profile = get_user_profile_by_email(email)
        user_profile.set_password(test_password)

        self.login(email)
        realm.deactivated = True
        realm.save()
        result = self.client_post("/json/fetch_api_key", {"password": test_password})
        self.assert_json_error_contains(result, "has been deactivated", status_code=400)

    def test_login_deactivated_realm(self):
        """
        logging in fails in a deactivated realm

        """
        do_deactivate_realm(get_realm_by_string_id("zulip"))
        result = self.login_with_return("hamlet@zulip.com")
        self.assert_in_response("has been deactivated", result)

    def test_webhook_deactivated_realm(self):
        """
        Using a webhook while in a deactivated realm fails

        """
        do_deactivate_realm(get_realm_by_string_id("zulip"))
        email = "hamlet@zulip.com"
        api_key = self.get_api_key(email)
        url = "/api/v1/external/jira?api_key=%s&stream=jira_custom" % (api_key,)
        data = self.fixture_data('jira', "created")
        result = self.client_post(url, data,
                                  content_type="application/json")
        self.assert_json_error_contains(result, "has been deactivated", status_code=400)

class LoginRequiredTest(ZulipTestCase):
    def test_login_required(self):
        """
        Verifies the zulip_login_required decorator blocks deactivated users.
        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email(email)

        # Verify fails if logged-out
        result = self.client_get('/accounts/accept_terms/')
        self.assertEqual(result.status_code, 302)

        # Verify succeeds once logged-in
        self.login(email)
        result = self.client_get('/accounts/accept_terms/')
        self.assert_in_response("I agree to the", result)

        # Verify fails if user deactivated (with session still valid)
        user_profile.is_active = False
        user_profile.save()
        result = self.client_get('/accounts/accept_terms/')
        self.assertEqual(result.status_code, 302)

        # Verify succeeds if user reactivated
        do_reactivate_user(user_profile)
        self.login(email)
        result = self.client_get('/accounts/accept_terms/')
        self.assert_in_response("I agree to the", result)

        # Verify fails if realm deactivated
        user_profile.realm.deactivated = True
        user_profile.realm.save()
        result = self.client_get('/accounts/accept_terms/')
        self.assertEqual(result.status_code, 302)

class FetchAPIKeyTest(ZulipTestCase):
    def test_fetch_api_key_success(self):
        email = "cordelia@zulip.com"

        self.login(email)
        result = self.client_post("/json/fetch_api_key", {"password": initial_password(email)})
        self.assert_json_success(result)

    def test_fetch_api_key_wrong_password(self):
        email = "cordelia@zulip.com"

        self.login(email)
        result = self.client_post("/json/fetch_api_key", {"password": "wrong_password"})
        self.assert_json_error_contains(result, "password is incorrect")

class InactiveUserTest(ZulipTestCase):
    def test_send_deactivated_user(self):
        """
        rest_dispatch rejects requests from deactivated users, both /json and api

        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email(email)
        self.login(email)
        do_deactivate_user(user_profile)

        result = self.client_post("/json/messages", {"type": "private",
                                                     "content": "Test message",
                                                     "client": "test suite",
                                                     "to": "othello@zulip.com"})
        self.assert_json_error_contains(result, "Not logged in", status_code=401)

        # Even if a logged-in session was leaked, it still wouldn't work
        do_reactivate_user(user_profile)
        self.login(email)
        user_profile.is_active = False
        user_profile.save()

        result = self.client_post("/json/messages", {"type": "private",
                                                     "content": "Test message",
                                                     "client": "test suite",
                                                     "to": "othello@zulip.com"})
        self.assert_json_error_contains(result, "Account not active", status_code=400)

        result = self.client_post("/api/v1/messages", {"type": "private",
                                                       "content": "Test message",
                                                       "client": "test suite",
                                                       "to": "othello@zulip.com"},
                                  **self.api_auth("hamlet@zulip.com"))
        self.assert_json_error_contains(result, "Account not active", status_code=401)

    def test_fetch_api_key_deactivated_user(self):
        """
        authenticated_json_view views fail with a deactivated user

        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email(email)
        test_password = "abcd1234"
        user_profile.set_password(test_password)
        user_profile.save()

        self.login(email, password=test_password)
        user_profile.is_active = False
        user_profile.save()
        result = self.client_post("/json/fetch_api_key", {"password": test_password})
        self.assert_json_error_contains(result, "Account not active", status_code=400)

    def test_login_deactivated_user(self):
        """
        logging in fails with an inactive user

        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email(email)
        do_deactivate_user(user_profile)

        result = self.login_with_return("hamlet@zulip.com")
        self.assert_in_response("Please enter a correct email and password", result)

    def test_webhook_deactivated_user(self):
        """
        Deactivated users can't use webhooks

        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email(email)
        do_deactivate_user(user_profile)

        api_key = self.get_api_key(email)
        url = "/api/v1/external/jira?api_key=%s&stream=jira_custom" % (api_key,)
        data = self.fixture_data('jira', "created")
        result = self.client_post(url, data,
                                  content_type="application/json")
        self.assert_json_error_contains(result, "Account not active", status_code=400)


class TestValidateApiKey(ZulipTestCase):
    def setUp(self):
        self.webhook_bot = get_user_profile_by_email('webhook-bot@zulip.com')
        self.default_bot = get_user_profile_by_email('default-bot@zulip.com')

    def test_validate_api_key_if_profile_does_not_exist(self):
        with self.assertRaises(JsonableError):
            validate_api_key(HostRequestMock(), 'email@doesnotexist.com', 'api_key')

    def test_validate_api_key_if_api_key_does_not_match_profile_api_key(self):
        with self.assertRaises(JsonableError):
            validate_api_key(HostRequestMock(), self.webhook_bot.email, 'not_32_length')

        with self.assertRaises(JsonableError):
            validate_api_key(HostRequestMock(), self.webhook_bot.email, self.default_bot.api_key)

    def test_validate_api_key_if_profile_is_not_active(self):
        self._change_is_active_field(self.default_bot, False)
        with self.assertRaises(JsonableError):
            validate_api_key(HostRequestMock(), self.default_bot.email, self.default_bot.api_key)
        self._change_is_active_field(self.default_bot, True)

    def test_validate_api_key_if_profile_is_incoming_webhook_and_is_webhook_is_unset(self):
        with self.assertRaises(JsonableError):
            validate_api_key(HostRequestMock(), self.webhook_bot.email, self.webhook_bot.api_key)

    def test_validate_api_key_if_profile_is_incoming_webhook_and_is_webhook_is_set(self):
        profile = validate_api_key(HostRequestMock(), self.webhook_bot.email, self.webhook_bot.api_key, is_webhook=True)
        self.assertEqual(profile.pk, self.webhook_bot.pk)

    def test_valid_api_key_if_user_is_on_wrong_subdomain(self):
        with self.settings(REALMS_HAVE_SUBDOMAINS=True):
            with mock.patch('logging.warning') as mock_warning:
                with self.assertRaisesRegex(JsonableError,
                                            "Account is not associated with this subdomain"):
                    validate_api_key(HostRequestMock(host=settings.EXTERNAL_HOST),
                                     self.default_bot.email,
                                     self.default_bot.api_key)

                mock_warning.assert_called_with(
                    "User {} attempted to access API on wrong "
                    "subdomain {}".format(self.default_bot.email, ''))

            with mock.patch('logging.warning') as mock_warning:
                with self.assertRaisesRegex(JsonableError,
                                            "Account is not associated with this subdomain"):
                    validate_api_key(HostRequestMock(host='acme.' + settings.EXTERNAL_HOST),
                                     self.default_bot.email,
                                     self.default_bot.api_key)

                mock_warning.assert_called_with(
                    "User {} attempted to access API on wrong "
                    "subdomain {}".format(self.default_bot.email, 'acme'))

    def _change_is_active_field(self, profile, value):
        profile.is_active = value
        profile.save()

class TestInternalNotifyView(TestCase):
    BORING_RESULT = 'boring'

    class Request(object):
        def __init__(self, POST, META):
            self.POST = POST
            self.META = META
            self.method = 'POST'

    def internal_notify(self, req):
        boring_view = lambda req: self.BORING_RESULT
        return internal_notify_view(boring_view)(req)

    def test_valid_internal_requests(self):
        secret = 'random'
        req = self.Request(
            POST=dict(secret=secret),
            META=dict(REMOTE_ADDR='127.0.0.1'),
            )

        req._tornado_handler = 'set'
        with self.settings(SHARED_SECRET=secret):
            self.assertTrue(authenticate_notify(req))
            self.assertEqual(self.internal_notify(req), self.BORING_RESULT)
            self.assertEqual(req._email, 'internal')

    def test_internal_requests_with_broken_secret(self):
        secret = 'random'
        req = self.Request(
            POST=dict(secret=secret),
            META=dict(REMOTE_ADDR='127.0.0.1'),
            )

        with self.settings(SHARED_SECRET='broken'):
            self.assertFalse(authenticate_notify(req))
            self.assertEqual(self.internal_notify(req).status_code, 403)

    def test_external_requests(self):
        secret = 'random'
        req = self.Request(
            POST=dict(secret=secret),
            META=dict(REMOTE_ADDR='3.3.3.3'),
            )

        with self.settings(SHARED_SECRET=secret):
            self.assertFalse(authenticate_notify(req))
            self.assertEqual(self.internal_notify(req).status_code, 403)

    def test_is_local_address(self):
        self.assertTrue(is_local_addr('127.0.0.1'))
        self.assertTrue(is_local_addr('::1'))
        self.assertFalse(is_local_addr('42.43.44.45'))

class TestAuthenticatedJsonPostViewDecorator(ZulipTestCase):
    def test_authenticated_json_post_view_if_everything_is_correct(self):
        user_email = 'hamlet@zulip.com'
        self._login(user_email)
        response = self._do_test(user_email)
        self.assertEqual(response.status_code, 200)

    def test_authenticated_json_post_view_if_subdomain_is_invalid(self):
        user_email = 'hamlet@zulip.com'
        self._login(user_email)
        with self.settings(REALMS_HAVE_SUBDOMAINS=True):
            with mock.patch('logging.warning') as mock_warning, \
                    mock.patch('zerver.decorator.get_subdomain', return_value=''):
                self.assert_json_error_contains(self._do_test(user_email),
                                                "Account is not associated with this "
                                                "subdomain")
                mock_warning.assert_called_with(
                    "User {} attempted to access JSON API on wrong "
                    "subdomain {}".format(user_email, ''))

            with mock.patch('logging.warning') as mock_warning, \
                    mock.patch('zerver.decorator.get_subdomain', return_value='acme'):
                self.assert_json_error_contains(self._do_test(user_email),
                                                "Account is not associated with this "
                                                "subdomain")
                mock_warning.assert_called_with(
                    "User {} attempted to access JSON API on wrong "
                    "subdomain {}".format(user_email, 'acme'))

    def test_authenticated_json_post_view_if_user_is_incoming_webhook(self):
        user_email = 'webhook-bot@zulip.com'
        self._login(user_email, password="test")  # we set a password because user is a bot
        self.assert_json_error_contains(self._do_test(user_email), "Webhook bots can only access webhooks")

    def test_authenticated_json_post_view_if_user_is_not_active(self):
        user_email = 'hamlet@zulip.com'
        self._login(user_email, password="test")
        # Get user_profile after _login so that we have the latest data.
        user_profile = get_user_profile_by_email(user_email)
        # we deactivate user manually because do_deactivate_user removes user session
        user_profile.is_active = False
        user_profile.save()
        self.assert_json_error_contains(self._do_test(user_email), "Account not active")
        do_reactivate_user(user_profile)

    def test_authenticated_json_post_view_if_user_realm_is_deactivated(self):
        user_email = 'hamlet@zulip.com'
        user_profile = get_user_profile_by_email(user_email)
        self._login(user_email)
        # we deactivate user's realm manually because do_deactivate_user removes user session
        user_profile.realm.deactivated = True
        user_profile.realm.save()
        self.assert_json_error_contains(self._do_test(user_email), "Realm for account has been deactivated")
        do_reactivate_realm(user_profile.realm)

    def _do_test(self, user_email):
        data = {"status": '"started"'}
        return self.client_post(r'/json/tutorial_status', data)

    def _login(self, user_email, password=None):
        if password:
            user_profile = get_user_profile_by_email(user_email)
            user_profile.set_password(password)
            user_profile.save()
        self.login(user_email, password)

class TestAuthenticatedJsonViewDecorator(ZulipTestCase):
    def test_authenticated_json_view_if_subdomain_is_invalid(self):
        user_email = 'hamlet@zulip.com'
        self._login(user_email)
        with self.settings(REALMS_HAVE_SUBDOMAINS=True):
            with mock.patch('logging.warning') as mock_warning, \
                    mock.patch('zerver.decorator.get_subdomain', return_value=''):
                self.assert_json_error_contains(self._do_test(user_email),
                                                "Account is not associated with this "
                                                "subdomain")
                mock_warning.assert_called_with(
                    "User {} attempted to access JSON API on wrong "
                    "subdomain {}".format(user_email, ''))

            with mock.patch('logging.warning') as mock_warning, \
                    mock.patch('zerver.decorator.get_subdomain', return_value='acme'):
                self.assert_json_error_contains(self._do_test(user_email),
                                                "Account is not associated with this "
                                                "subdomain")
                mock_warning.assert_called_with(
                    "User {} attempted to access JSON API on wrong "
                    "subdomain {}".format(user_email, 'acme'))

    def _do_test(self, user_email):
        data = {"status": '"started"'}
        return self.client_post(r'/json/tutorial_status', data)

    def _login(self, user_email, password=None):
        if password:
            user_profile = get_user_profile_by_email(user_email)
            user_profile.set_password(password)
            user_profile.save()
        self.login(user_email, password)

class TestZulipLoginRequiredDecorator(ZulipTestCase):
    def test_zulip_login_required_if_subdomain_is_invalid(self):
        user_email = 'hamlet@zulip.com'
        self.login(user_email)

        with self.settings(REALMS_HAVE_SUBDOMAINS=True):
            with mock.patch('zerver.decorator.get_subdomain', return_value='zulip'):
                result = self.client_get('/accounts/accept_terms/')
                self.assertEqual(result.status_code, 200)

            with mock.patch('zerver.decorator.get_subdomain', return_value=''):
                result = self.client_get('/accounts/accept_terms/')
                self.assertEqual(result.status_code, 302)

            with mock.patch('zerver.decorator.get_subdomain', return_value='acme'):
                result = self.client_get('/accounts/accept_terms/')
                self.assertEqual(result.status_code, 302)

class TestZulipInternalDecorator(ZulipTestCase):
    def test_zulip_internal_decorator(self):
        user_email = 'hamlet@zulip.com'
        self.login(user_email)

        result = self.client_get('/activity')
        self.assertEqual(result.status_code, 302)

        user_profile = get_user_profile_by_email(user_email)
        user_profile.is_staff = True
        user_profile.save()

        result = self.client_get('/activity')
        self.assertEqual(result.status_code, 200)

class ReturnSuccessOnHeadRequestDecorator(ZulipTestCase):
    def test_return_success_on_head_request_returns_200_if_request_method_is_head(self):
        class HeadRequest(object):
            method = 'HEAD'

        request = HeadRequest()

        @return_success_on_head_request
        def test_function(request):
            return json_response(msg=u'from_test_function')

        response = test_function(request)
        self.assert_json_success(response)
        self.assertNotEqual(ujson.loads(response.content).get('msg'), u'from_test_function')

    def test_return_success_on_head_request_returns_normal_response_if_request_method_is_not_head(self):
            class HeadRequest(object):
                method = 'POST'

            request = HeadRequest()

            @return_success_on_head_request
            def test_function(request):
                return json_response(msg=u'from_test_function')

            response = test_function(request)
            self.assertEqual(ujson.loads(response.content).get('msg'), u'from_test_function')
