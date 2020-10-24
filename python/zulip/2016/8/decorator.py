from __future__ import absolute_import

from django.utils.translation import ugettext as _
from django.http import HttpResponseRedirect, HttpResponse
from django.contrib.auth import REDIRECT_FIELD_NAME
from django.views.decorators.csrf import csrf_exempt
from django.http import QueryDict, HttpResponseNotAllowed, HttpRequest
from django.http.multipartparser import MultiPartParser
from zerver.models import UserProfile, get_client, get_user_profile_by_email
from zerver.lib.response import json_error, json_unauthorized
from django.shortcuts import resolve_url
from django.utils.decorators import available_attrs
from django.utils.timezone import now
from django.conf import settings
from zerver.lib.queue import queue_json_publish
from zerver.lib.timestamp import datetime_to_timestamp
from zerver.lib.utils import statsd
from zerver.exceptions import RateLimited
from zerver.lib.rate_limiter import incr_ratelimit, is_ratelimited, \
     api_calls_left
from zerver.lib.request import REQ, has_request_variables, JsonableError, RequestVariableMissingError
from django.core.handlers import base

from functools import wraps
import base64
import logging
import cProfile
from io import BytesIO
from zerver.lib.mandrill_client import get_mandrill_client
from six.moves import zip, urllib

from six import text_type
from typing import Union, Any, Callable, Sequence, Dict, Optional, TypeVar
from zerver.lib.str_utils import force_bytes

if settings.ZILENCER_ENABLED:
    from zilencer.models import get_deployment_by_domain, Deployment
else:
    from mock import Mock
    get_deployment_by_domain = Mock()
    Deployment = Mock() # type: ignore # https://github.com/JukkaL/mypy/issues/1188

FuncT = TypeVar('FuncT', bound=Callable[..., Any])
ViewFuncT = TypeVar('ViewFuncT', bound=Callable[..., HttpResponse])

def get_deployment_or_userprofile(role):
    # type: (text_type) -> Union[UserProfile, Deployment]
    return get_user_profile_by_email(role) if "@" in role else get_deployment_by_domain(role)

class _RespondAsynchronously(object):
    pass

# Return RespondAsynchronously from an @asynchronous view if the
# response will be provided later by calling handler.zulip_finish(),
# or has already been provided this way. We use this for longpolling
# mode.
RespondAsynchronously = _RespondAsynchronously()

def asynchronous(method):
    # type: (Callable[..., Union[HttpResponse, _RespondAsynchronously]]) -> Callable[..., Union[HttpResponse, _RespondAsynchronously]]
    # TODO: this should be the correct annotation when mypy gets fixed: type:
    #   (Callable[[HttpRequest, base.BaseHandler, Sequence[Any], Dict[str, Any]], Union[HttpResponse, _RespondAsynchronously]]) ->
    #   Callable[[HttpRequest, Sequence[Any], Dict[str, Any]], Union[HttpResponse, _RespondAsynchronously]]
    # TODO: see https://github.com/python/mypy/issues/1655
    @wraps(method)
    def wrapper(request, *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> Union[HttpResponse, _RespondAsynchronously]
        return method(request, handler=request._tornado_handler, *args, **kwargs)
    if getattr(method, 'csrf_exempt', False):
        wrapper.csrf_exempt = True # type: ignore # https://github.com/JukkaL/mypy/issues/1170
    return wrapper

def update_user_activity(request, user_profile):
    # type: (HttpRequest, UserProfile) -> None
    # update_active_status also pushes to rabbitmq, and it seems
    # redundant to log that here as well.
    if request.META["PATH_INFO"] == '/json/users/me/presence':
        return

    if hasattr(request, '_query'):
        query = request._query
    else:
        query = request.META['PATH_INFO']

    event={'query': query,
           'user_profile_id': user_profile.id,
           'time': datetime_to_timestamp(now()),
           'client': request.client.name}
    queue_json_publish("user_activity", event, lambda event: None)

# Based on django.views.decorators.http.require_http_methods
def require_post(func):
    # type: (ViewFuncT) -> ViewFuncT
    @wraps(func)
    def wrapper(request, *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> HttpResponse
        if (request.method != "POST"
            and not (request.method == "SOCKET"
                     and request.META['zulip.emulated_method'] == "POST")):
            if request.method == "SOCKET":
                err_method = "SOCKET/%s" % (request.META['zulip.emulated_method'],)
            else:
                err_method = request.method
            logging.warning('Method Not Allowed (%s): %s', err_method, request.path,
                            extra={'status_code': 405, 'request': request})
            return HttpResponseNotAllowed(["POST"])
        return func(request, *args, **kwargs)
    return wrapper # type: ignore # https://github.com/python/mypy/issues/1927

def require_realm_admin(func):
    # type: (ViewFuncT) -> ViewFuncT
    @wraps(func)
    def wrapper(request, user_profile, *args, **kwargs):
        # type: (HttpRequest, UserProfile, *Any, **Any) -> HttpResponse
        if not user_profile.is_realm_admin:
            raise JsonableError(_("Must be a realm administrator"))
        return func(request, user_profile, *args, **kwargs)
    return wrapper # type: ignore # https://github.com/python/mypy/issues/1927

from zerver.lib.user_agent import parse_user_agent

def get_client_name(request, is_json_view):
    # type: (HttpRequest, bool) -> text_type
    # If the API request specified a client in the request content,
    # that has priority.  Otherwise, extract the client from the
    # User-Agent.
    if 'client' in request.REQUEST:
        return request.REQUEST['client']
    elif "HTTP_USER_AGENT" in request.META:
        user_agent = parse_user_agent(request.META["HTTP_USER_AGENT"])
        # We could check for a browser's name being "Mozilla", but
        # e.g. Opera and MobileSafari don't set that, and it seems
        # more robust to just key off whether it was a json view
        if user_agent["name"] != "ZulipDesktop" and is_json_view:
            # Avoid changing the client string for browsers Once this
            # is out to prod, we can name the field to something like
            # Browser for consistency.
            return "website"
        else:
            return user_agent["name"]
    else:
        # In the future, we will require setting USER_AGENT, but for
        # now we just want to tag these requests so we can review them
        # in logs and figure out the extent of the problem
        if is_json_view:
            return "website"
        else:
             return "Unspecified"

def process_client(request, user_profile, is_json_view=False, client_name=None):
    # type: (HttpRequest, UserProfile, bool, Optional[text_type]) -> None
    if client_name is None:
        client_name = get_client_name(request, is_json_view)

    # Transitional hack for early 2014.  Eventually the ios clients
    # will all report ZulipiOS, and we can remove the next couple lines.
    if client_name == 'ios':
        client_name = 'ZulipiOS'

    request.client = get_client(client_name)
    update_user_activity(request, user_profile)

def validate_api_key(role, api_key, is_webhook=False):
    # type: (text_type, text_type, bool) -> Union[UserProfile, Deployment]
    # Remove whitespace to protect users from trivial errors.
    role, api_key = role.strip(), api_key.strip()

    try:
        profile = get_deployment_or_userprofile(role)
    except UserProfile.DoesNotExist:
        raise JsonableError(_("Invalid user: %s") % (role,))
    except Deployment.DoesNotExist:
        raise JsonableError(_("Invalid deployment: %s") % (role,))

    if api_key != profile.api_key:
        if len(api_key) != 32:
            reason = _("Incorrect API key length (keys should be 32 "
                       "characters long) for role '%s'")
        else:
            reason = _("Invalid API key for role '%s'")
        raise JsonableError(reason % (role,))
    if not profile.is_active:
        raise JsonableError(_("Account not active"))
    if profile.is_incoming_webhook and not is_webhook:
        raise JsonableError(_("Account is not valid to post webhook messages"))
    try:
        if profile.realm.deactivated:
            raise JsonableError(_("Realm for account has been deactivated"))
    except AttributeError:
        # Deployment objects don't have realms
        pass
    return profile

# Use this for webhook views that don't get an email passed in.
def api_key_only_webhook_view(client_name):
    # type: (text_type) ->  Callable[..., HttpResponse]
    # This function can't be typed perfectly because returning a generic function
    # isn't supported in mypy - https://github.com/python/mypy/issues/1551.
    def _wrapped_view_func(view_func):
        # type: (Callable[..., HttpResponse]) -> Callable[..., HttpResponse]
        @csrf_exempt
        @has_request_variables
        @wraps(view_func)
        def _wrapped_func_arguments(request, api_key=REQ(),
                                    *args, **kwargs):
            # type: (HttpRequest, text_type, *Any, **Any) -> HttpResponse
            try:
                user_profile = UserProfile.objects.get(api_key=api_key)
            except UserProfile.DoesNotExist:
                raise JsonableError(_("Invalid API key"))
            if not user_profile.is_active:
                raise JsonableError(_("Account not active"))
            if user_profile.realm.deactivated:
                raise JsonableError(_("Realm for account has been deactivated"))

            request.user = user_profile
            request._email = user_profile.email
            webhook_client_name = "Zulip{}Webhook".format(client_name)
            process_client(request, user_profile, client_name=webhook_client_name)
            if settings.RATE_LIMITING:
                rate_limit_user(request, user_profile, domain='all')
            return view_func(request, user_profile, request.client, *args, **kwargs)
        return _wrapped_func_arguments
    return _wrapped_view_func

# From Django 1.8, modified to leave off ?next=/
def redirect_to_login(next, login_url=None,
                      redirect_field_name=REDIRECT_FIELD_NAME):
    # type: (text_type, Optional[text_type], text_type) -> HttpResponseRedirect
    """
    Redirects the user to the login page, passing the given 'next' page
    """
    resolved_url = resolve_url(login_url or settings.LOGIN_URL)

    login_url_parts = list(urllib.parse.urlparse(resolved_url))
    if redirect_field_name:
        querystring = QueryDict(login_url_parts[4], mutable=True)
        querystring[redirect_field_name] = next
        # Don't add ?next=/, to keep our URLs clean
        if next != '/':
            login_url_parts[4] = querystring.urlencode(safe='/')

    return HttpResponseRedirect(urllib.parse.urlunparse(login_url_parts))

# From Django 1.8
def user_passes_test(test_func, login_url=None, redirect_field_name=REDIRECT_FIELD_NAME):
    # type: (Callable[[UserProfile], bool], Optional[text_type], text_type) -> Callable[[Callable[..., HttpResponse]], Callable[..., HttpResponse]]
    """
    Decorator for views that checks that the user passes the given test,
    redirecting to the log-in page if necessary. The test should be a callable
    that takes the user object and returns True if the user passes.
    """
    def decorator(view_func):
        # type: (Callable[..., HttpResponse]) -> Callable[..., HttpResponse]
        @wraps(view_func, assigned=available_attrs(view_func))
        def _wrapped_view(request, *args, **kwargs):
            # type: (HttpRequest, *Any, **Any) -> HttpResponse
            if test_func(request):
                return view_func(request, *args, **kwargs)
            path = request.build_absolute_uri()
            resolved_login_url = resolve_url(login_url or settings.LOGIN_URL)
            # If the login url is the same scheme and net location then just
            # use the path as the "next" url.
            login_scheme, login_netloc = urllib.parse.urlparse(resolved_login_url)[:2]
            current_scheme, current_netloc = urllib.parse.urlparse(path)[:2]
            if ((not login_scheme or login_scheme == current_scheme) and
                    (not login_netloc or login_netloc == current_netloc)):
                path = request.get_full_path()
            return redirect_to_login(
                path, resolved_login_url, redirect_field_name)
        return _wrapped_view
    return decorator

def logged_in_and_active(request):
    # type: (HttpRequest) -> bool
    if not request.user.is_authenticated():
        return False
    if not request.user.is_active:
        return False
    if request.user.realm.deactivated:
        return False
    return True

# Based on Django 1.8's @login_required
def zulip_login_required(function=None,
                         redirect_field_name=REDIRECT_FIELD_NAME,
                         login_url=settings.HOME_NOT_LOGGED_IN):
    # type: (Optional[Callable[..., HttpResponse]], text_type, text_type) -> Union[Callable[[Callable[..., HttpResponse]], Callable[..., HttpResponse]], Callable[..., HttpResponse]]
    actual_decorator = user_passes_test(
        logged_in_and_active,
        login_url=login_url,
        redirect_field_name=redirect_field_name
    )
    if function:
        return actual_decorator(function)
    return actual_decorator

def zulip_internal(view_func):
    # type: (ViewFuncT) -> ViewFuncT
    @zulip_login_required
    @wraps(view_func)
    def _wrapped_view_func(request, *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> HttpResponse
        request._query = view_func.__name__
        if request.user.realm.domain != 'zulip.com':
            return HttpResponseRedirect(settings.HOME_NOT_LOGGED_IN)

        request._email = request.user.email
        process_client(request, request.user)
        return view_func(request, *args, **kwargs)
    return _wrapped_view_func # type: ignore # https://github.com/python/mypy/issues/1927

# authenticated_api_view will add the authenticated user's
# user_profile to the view function's arguments list, since we have to
# look it up anyway.  It is deprecated in favor on the REST API
# versions.
def authenticated_api_view(is_webhook=False):
    # type: (bool) -> Callable[[Callable[..., HttpResponse]], Callable[..., HttpResponse]]
    def _wrapped_view_func(view_func):
        # type: (Callable[..., HttpResponse]) -> Callable[..., HttpResponse]
        @csrf_exempt
        @require_post
        @has_request_variables
        @wraps(view_func)
        def _wrapped_func_arguments(request, email=REQ(), api_key=REQ(default=None),
                                    api_key_legacy=REQ('api-key', default=None),
                                    *args, **kwargs):
            # type: (HttpRequest, text_type, Optional[text_type], Optional[text_type], *Any, **Any) -> HttpResponse
            if not api_key and not api_key_legacy:
                raise RequestVariableMissingError("api_key")
            elif not api_key:
                api_key = api_key_legacy
            user_profile = validate_api_key(email, api_key, is_webhook)
            request.user = user_profile
            request._email = user_profile.email
            process_client(request, user_profile)
            # Apply rate limiting
            limited_func = rate_limit()(view_func)
            return limited_func(request, user_profile, *args, **kwargs)
        return _wrapped_func_arguments
    return _wrapped_view_func

# A more REST-y authentication decorator, using, in particular, HTTP Basic
# authentication.
def authenticated_rest_api_view(is_webhook=False):
    # type: (bool) -> Callable[[Callable[..., HttpResponse]], Callable[..., HttpResponse]]
    def _wrapped_view_func(view_func):
        # type: (Callable[..., HttpResponse]) -> Callable[..., HttpResponse]
        @csrf_exempt
        @wraps(view_func)
        def _wrapped_func_arguments(request, *args, **kwargs):
            # type: (HttpRequest, *Any, **Any) -> HttpResponse
            # First try block attempts to get the credentials we need to do authentication
            try:
                # Grab the base64-encoded authentication string, decode it, and split it into
                # the email and API key
                auth_type, credentials = request.META['HTTP_AUTHORIZATION'].split()
                # case insensitive per RFC 1945
                if auth_type.lower() != "basic":
                    return json_error(_("Only Basic authentication is supported."))
                role, api_key = base64.b64decode(force_bytes(credentials)).decode('utf-8').split(":")
            except ValueError:
                json_error(_("Invalid authorization header for basic auth"))
            except KeyError:
                return json_unauthorized("Missing authorization header for basic auth")

            # Now we try to do authentication or die
            try:
                # Could be a UserProfile or a Deployment
                profile = validate_api_key(role, api_key, is_webhook)
            except JsonableError as e:
                return json_unauthorized(e.error)
            request.user = profile
            process_client(request, profile)
            if isinstance(profile, UserProfile):
                request._email = profile.email
            else:
                assert isinstance(profile, Deployment) # type: ignore # https://github.com/python/mypy/issues/1720#issuecomment-228596830
                request._email = "deployment:" + role
                profile.rate_limits = ""
            # Apply rate limiting
            return rate_limit()(view_func)(request, profile, *args, **kwargs)
        return _wrapped_func_arguments
    return _wrapped_view_func

def process_as_post(view_func):
    # type: (ViewFuncT) -> ViewFuncT
    @wraps(view_func)
    def _wrapped_view_func(request, *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> HttpResponse
        # Adapted from django/http/__init__.py.
        # So by default Django doesn't populate request.POST for anything besides
        # POST requests. We want this dict populated for PATCH/PUT, so we have to
        # do it ourselves.
        #
        # This will not be required in the future, a bug will be filed against
        # Django upstream.

        if not request.POST:
            # Only take action if POST is empty.
            if request.META.get('CONTENT_TYPE', '').startswith('multipart'):
                # Note that request._files is just the private attribute that backs the
                # FILES property, so we are essentially setting request.FILES here.  (In
                # Django 1.5 FILES was still a read-only property.)
                request.POST, request._files = MultiPartParser(request.META, BytesIO(request.body),
                        request.upload_handlers, request.encoding).parse()
            else:
                request.POST = QueryDict(request.body, encoding=request.encoding)

        return view_func(request, *args, **kwargs)

    return _wrapped_view_func # type: ignore # https://github.com/python/mypy/issues/1927

def authenticate_log_and_execute_json(request, view_func, *args, **kwargs):
    # type: (HttpRequest, Callable[..., HttpResponse], *Any, **Any) -> HttpResponse
    if not request.user.is_authenticated():
        return json_error(_("Not logged in"), status=401)
    user_profile = request.user
    if not user_profile.is_active:
        raise JsonableError(_("Account not active"))
    if user_profile.realm.deactivated:
        raise JsonableError(_("Realm for account has been deactivated"))
    if user_profile.is_incoming_webhook:
        raise JsonableError(_("Webhook bots can only access webhooks"))
    process_client(request, user_profile, True)
    request._email = user_profile.email
    return view_func(request, user_profile, *args, **kwargs)

# Checks if the request is a POST request and that the user is logged
# in.  If not, return an error (the @login_required behavior of
# redirecting to a login page doesn't make sense for json views)
def authenticated_json_post_view(view_func):
    # type: (ViewFuncT) -> ViewFuncT
    @require_post
    @has_request_variables
    @wraps(view_func)
    def _wrapped_view_func(request,
                           *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> HttpResponse
        return authenticate_log_and_execute_json(request, view_func, *args, **kwargs)
    return _wrapped_view_func # type: ignore # https://github.com/python/mypy/issues/1927

def authenticated_json_view(view_func):
    # type: (ViewFuncT) -> ViewFuncT
    @wraps(view_func)
    def _wrapped_view_func(request,
                           *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> HttpResponse
        return authenticate_log_and_execute_json(request, view_func, *args, **kwargs)
    return _wrapped_view_func # type: ignore # https://github.com/python/mypy/issues/1927

def is_local_addr(addr):
    # type: (text_type) -> bool
    return addr in ('127.0.0.1', '::1')

# These views are used by the main Django server to notify the Tornado server
# of events.  We protect them from the outside world by checking a shared
# secret, and also the originating IP (for now).
def authenticate_notify(request):
    # type: (HttpRequest) -> bool
    return (is_local_addr(request.META['REMOTE_ADDR'])
            and request.POST.get('secret') == settings.SHARED_SECRET)

def client_is_exempt_from_rate_limiting(request):
    # type: (HttpRequest) -> bool

    # Don't rate limit requests from Django that come from our own servers,
    # and don't rate-limit dev instances
    return ((request.client and request.client.name.lower() == 'internal')
           and (is_local_addr(request.META['REMOTE_ADDR']) or
                settings.DEBUG_RATE_LIMITING))

def internal_notify_view(view_func):
    # type: (ViewFuncT) -> ViewFuncT
    @csrf_exempt
    @require_post
    @wraps(view_func)
    def _wrapped_view_func(request, *args, **kwargs):
        # type: (HttpRequest, *Any, **Any) -> HttpResponse
        if not authenticate_notify(request):
            return json_error(_('Access denied'), status=403)
        if not hasattr(request, '_tornado_handler'):
            # We got called through the non-Tornado server somehow.
            # This is not a security check; it's an internal assertion
            # to help us find bugs.
            raise RuntimeError('notify view called with no Tornado handler')
        request._email = "internal"
        return view_func(request, *args, **kwargs)
    return _wrapped_view_func

# Converter functions for use with has_request_variables
def to_non_negative_int(x):
    # type: (float) -> int
    x = int(x)
    if x < 0:
        raise ValueError("argument is negative")
    return x

def flexible_boolean(boolean):
    # type: (text_type) -> bool
    """Returns True for any of "1", "true", or "True".  Returns False otherwise."""
    if boolean in ("1", "true", "True"):
        return True
    else:
        return False

def statsd_increment(counter, val=1):
    # type: (text_type, int) -> Callable[[Callable[..., Any]], Callable[..., Any]]
    """Increments a statsd counter on completion of the
    decorated function.

    Pass the name of the counter to this decorator-returning function."""
    def wrapper(func):
        # type: (Callable[..., Any]) -> Callable[..., Any]
        @wraps(func)
        def wrapped_func(*args, **kwargs):
            # type: (*Any, **Any) -> Any
            ret = func(*args, **kwargs)
            statsd.incr(counter, val)
            return ret
        return wrapped_func
    return wrapper

def rate_limit_user(request, user, domain):
    # type: (HttpRequest, UserProfile, text_type) -> None
    """Returns whether or not a user was rate limited. Will raise a RateLimited exception
    if the user has been rate limited, otherwise returns and modifies request to contain
    the rate limit information"""

    ratelimited, time = is_ratelimited(user, domain)
    request._ratelimit_applied_limits = True
    request._ratelimit_secs_to_freedom = time
    request._ratelimit_over_limit = ratelimited
    # Abort this request if the user is over her rate limits
    if ratelimited:
        statsd.incr("ratelimiter.limited.%s.%s" % (type(user), user.id))
        raise RateLimited()

    incr_ratelimit(user, domain)
    calls_remaining, time_reset = api_calls_left(user, domain)

    request._ratelimit_remaining = calls_remaining
    request._ratelimit_secs_to_freedom = time_reset

def rate_limit(domain='all'):
    # type: (text_type) -> Callable[[Callable[..., HttpResponse]], Callable[..., HttpResponse]]
    """Rate-limits a view. Takes an optional 'domain' param if you wish to rate limit different
    types of API calls independently.

    Returns a decorator"""
    def wrapper(func):
        # type: (Callable[..., HttpResponse]) -> Callable[..., HttpResponse]
        @wraps(func)
        def wrapped_func(request, *args, **kwargs):
            # type: (HttpRequest, *Any, **Any) -> HttpResponse

            # It is really tempting to not even wrap our original function
            # when settings.RATE_LIMITING is False, but it would make
            # for awkward unit testing in some situations.
            if not settings.RATE_LIMITING:
                return func(request, *args, **kwargs)

            if client_is_exempt_from_rate_limiting(request):
                return func(request, *args, **kwargs)

            try:
                user = request.user
            except:
                # TODO: This logic is not tested, and I'm not sure we are
                # doing the right thing here.
                user = None

            if not user:
                logging.error("Requested rate-limiting on %s but user is not authenticated!" % \
                                 func.__name__)
                return func(request, *args, **kwargs)

            # Rate-limiting data is stored in redis
            # We also only support rate-limiting authenticated
            # views right now.
            # TODO(leo) - implement per-IP non-authed rate limiting
            rate_limit_user(request, user, domain)

            return func(request, *args, **kwargs)
        return wrapped_func
    return wrapper

def profiled(func):
    # type: (FuncT) -> FuncT
    """
    This decorator should obviously be used only in a dev environment.
    It works best when surrounding a function that you expect to be
    called once.  One strategy is to write a backend test and wrap the
    test case with the profiled decorator.

    You can run a single test case like this:

        # edit zerver/tests/test_external.py and place @profiled above the test case below
        ./tools/test-backend zerver.tests.test_external.RateLimitTests.test_ratelimit_decrease

    Then view the results like this:

        ./tools/show-profile-results.py test_ratelimit_decrease.profile

    """
    @wraps(func)
    def wrapped_func(*args, **kwargs):
        # type: (*Any, **Any) -> Any
        fn = func.__name__ + ".profile"
        prof = cProfile.Profile()
        retval = prof.runcall(func, *args, **kwargs)
        prof.dump_stats(fn)
        return retval
    return wrapped_func # type: ignore # https://github.com/python/mypy/issues/1927

def uses_mandrill(func):
    # type: (FuncT) -> FuncT
    """
    This decorator takes a function with keyword argument "mail_client" and
    fills it in with the mail_client for the Mandrill account.
    """
    @wraps(func)
    def wrapped_func(*args, **kwargs):
        # type: (*Any, **Any) -> Any
        kwargs['mail_client'] = get_mandrill_client()
        return func(*args, **kwargs)
    return wrapped_func # type: ignore # https://github.com/python/mypy/issues/1927
