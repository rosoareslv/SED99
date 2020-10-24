from __future__ import absolute_import
from typing import Optional, Any
from typing import Text

from django.utils.translation import ugettext as _
from django.conf import settings
from django.contrib.auth import authenticate, update_session_auth_hash
from django.http import HttpRequest, HttpResponse

from zerver.decorator import authenticated_json_post_view, has_request_variables, REQ
from zerver.lib.actions import do_change_password, \
    do_change_full_name, do_change_enable_desktop_notifications, \
    do_change_enter_sends, do_change_enable_sounds, \
    do_change_enable_offline_email_notifications, do_change_enable_digest_emails, \
    do_change_enable_offline_push_notifications, do_change_enable_online_push_notifications, \
    do_change_default_desktop_notifications, do_change_autoscroll_forever, \
    do_change_enable_stream_desktop_notifications, do_change_enable_stream_sounds, \
    do_regenerate_api_key, do_change_avatar_source, do_change_twenty_four_hour_time, \
    do_change_left_side_userlist, do_change_default_language, \
    do_change_pm_content_in_desktop_notifications
from zerver.lib.avatar import avatar_url
from zerver.lib.i18n import get_available_language_codes
from zerver.lib.response import json_success, json_error
from zerver.lib.upload import upload_avatar_image
from zerver.lib.validator import check_bool, check_string
from zerver.lib.request import JsonableError
from zerver.models import UserProfile, Realm, name_changes_disabled

@has_request_variables
def json_change_ui_settings(request, user_profile,
                            autoscroll_forever=REQ(validator=check_bool,
                                                   default=None),
                            default_desktop_notifications=REQ(validator=check_bool,
                                                              default=None)):
    # type: (HttpRequest, UserProfile, Optional[bool], Optional[bool]) -> HttpResponse

    result = {}

    if autoscroll_forever is not None and \
            user_profile.autoscroll_forever != autoscroll_forever:
        do_change_autoscroll_forever(user_profile, autoscroll_forever)
        result['autoscroll_forever'] = autoscroll_forever

    if default_desktop_notifications is not None and \
            user_profile.default_desktop_notifications != default_desktop_notifications:
        do_change_default_desktop_notifications(user_profile, default_desktop_notifications)
        result['default_desktop_notifications'] = default_desktop_notifications

    return json_success(result)

@authenticated_json_post_view
@has_request_variables
def json_change_settings(request, user_profile,
                         full_name=REQ(default=""),
                         old_password=REQ(default=""),
                         new_password=REQ(default=""),
                         confirm_password=REQ(default="")):
    # type: (HttpRequest, UserProfile, Text, Text, Text, Text) -> HttpResponse
    if not (full_name or new_password):
        return json_error(_("No new data supplied"))

    if new_password != "" or confirm_password != "":
        if new_password != confirm_password:
            return json_error(_("New password must match confirmation password!"))
        if not authenticate(username=user_profile.email, password=old_password):
            return json_error(_("Wrong password!"))
        do_change_password(user_profile, new_password)
        # In Django 1.10, password changes invalidates sessions, see
        # https://docs.djangoproject.com/en/1.10/topics/auth/default/#session-invalidation-on-password-change
        # for details. To avoid this logging the user out of his own
        # session (which would provide a confusing UX at best), we
        # update the session hash here.
        update_session_auth_hash(request, user_profile)
        # We also save the session to the DB immediately to mitigate
        # race conditions. In theory, there is still a race condition
        # and to completely avoid it we will have to use some kind of
        # mutex lock in `django.contrib.auth.get_user` where session
        # is verified. To make that lock work we will have to control
        # the AuthenticationMiddleware which is currently controlled
        # by Django,
        request.session.save()

    result = {}
    if user_profile.full_name != full_name and full_name.strip() != "":
        if name_changes_disabled(user_profile.realm):
            # Failingly silently is fine -- they can't do it through the UI, so
            # they'd have to be trying to break the rules.
            pass
        else:
            new_full_name = full_name.strip()
            if len(new_full_name) > UserProfile.MAX_NAME_LENGTH:
                return json_error(_("Name too long!"))
            do_change_full_name(user_profile, new_full_name)
            result['full_name'] = new_full_name

    return json_success(result)

@has_request_variables
def update_display_settings_backend(request, user_profile,
                                    twenty_four_hour_time=REQ(validator=check_bool, default=None),
                                    default_language=REQ(validator=check_string, default=None),
                                    left_side_userlist=REQ(validator=check_bool, default=None)):
    # type: (HttpRequest, UserProfile, Optional[bool], Optional[str], Optional[bool]) -> HttpResponse
    if (default_language is not None and
            default_language not in get_available_language_codes()):
        raise JsonableError(_("Invalid language '%s'" % (default_language,)))

    result = {} # type: Dict[str, Any]
    if (default_language is not None and
            user_profile.default_language != default_language):
        do_change_default_language(user_profile, default_language)
        result['default_language'] = default_language

    elif (twenty_four_hour_time is not None and
            user_profile.twenty_four_hour_time != twenty_four_hour_time):
        do_change_twenty_four_hour_time(user_profile, twenty_four_hour_time)
        result['twenty_four_hour_time'] = twenty_four_hour_time

    elif (left_side_userlist is not None and
            user_profile.left_side_userlist != left_side_userlist):
        do_change_left_side_userlist(user_profile, left_side_userlist)
        result['left_side_userlist'] = left_side_userlist

    return json_success(result)

@has_request_variables
def json_change_notify_settings(request, user_profile,
                                enable_stream_desktop_notifications=REQ(validator=check_bool,
                                                                        default=None),
                                enable_stream_sounds=REQ(validator=check_bool,
                                                         default=None),
                                enable_desktop_notifications=REQ(validator=check_bool,
                                                                 default=None),
                                enable_sounds=REQ(validator=check_bool,
                                                  default=None),
                                enable_offline_email_notifications=REQ(validator=check_bool,
                                                                       default=None),
                                enable_offline_push_notifications=REQ(validator=check_bool,
                                                                      default=None),
                                enable_online_push_notifications=REQ(validator=check_bool,
                                                                     default=None),
                                enable_digest_emails=REQ(validator=check_bool,
                                                         default=None),
                                pm_content_in_desktop_notifications=REQ(validator=check_bool,
                                                                        default=None)):
    # type: (HttpRequest, UserProfile, Optional[bool], Optional[bool], Optional[bool], Optional[bool], Optional[bool], Optional[bool], Optional[bool], Optional[bool], Optional[bool]) -> HttpResponse
    result = {}

    # Stream notification settings.

    if enable_stream_desktop_notifications is not None and \
            user_profile.enable_stream_desktop_notifications != enable_stream_desktop_notifications:
        do_change_enable_stream_desktop_notifications(
            user_profile, enable_stream_desktop_notifications)
        result['enable_stream_desktop_notifications'] = enable_stream_desktop_notifications

    if enable_stream_sounds is not None and \
            user_profile.enable_stream_sounds != enable_stream_sounds:
        do_change_enable_stream_sounds(user_profile, enable_stream_sounds)
        result['enable_stream_sounds'] = enable_stream_sounds

    # PM and @-mention settings.

    if enable_desktop_notifications is not None and \
            user_profile.enable_desktop_notifications != enable_desktop_notifications:
        do_change_enable_desktop_notifications(user_profile, enable_desktop_notifications)
        result['enable_desktop_notifications'] = enable_desktop_notifications

    if enable_sounds is not None and \
            user_profile.enable_sounds != enable_sounds:
        do_change_enable_sounds(user_profile, enable_sounds)
        result['enable_sounds'] = enable_sounds

    if enable_offline_email_notifications is not None and \
            user_profile.enable_offline_email_notifications != enable_offline_email_notifications:
        do_change_enable_offline_email_notifications(user_profile, enable_offline_email_notifications)
        result['enable_offline_email_notifications'] = enable_offline_email_notifications

    if enable_offline_push_notifications is not None and \
            user_profile.enable_offline_push_notifications != enable_offline_push_notifications:
        do_change_enable_offline_push_notifications(user_profile, enable_offline_push_notifications)
        result['enable_offline_push_notifications'] = enable_offline_push_notifications

    if enable_online_push_notifications is not None and \
            user_profile.enable_online_push_notifications != enable_online_push_notifications:
        do_change_enable_online_push_notifications(user_profile, enable_online_push_notifications)
        result['enable_online_push_notifications'] = enable_online_push_notifications

    if enable_digest_emails is not None and \
            user_profile.enable_digest_emails != enable_digest_emails:
        do_change_enable_digest_emails(user_profile, enable_digest_emails)
        result['enable_digest_emails'] = enable_digest_emails

    if pm_content_in_desktop_notifications is not None and \
            user_profile.pm_content_in_desktop_notifications != pm_content_in_desktop_notifications:
        do_change_pm_content_in_desktop_notifications(user_profile, pm_content_in_desktop_notifications)
        result['pm_content_in_desktop_notifications'] = pm_content_in_desktop_notifications

    return json_success(result)

def set_avatar_backend(request, user_profile):
    # type: (HttpRequest, UserProfile) -> HttpResponse
    if len(request.FILES) != 1:
        return json_error(_("You must upload exactly one avatar."))

    user_file = list(request.FILES.values())[0]
    upload_avatar_image(user_file, user_profile, user_profile.email)
    do_change_avatar_source(user_profile, UserProfile.AVATAR_FROM_USER)
    user_avatar_url = avatar_url(user_profile)

    json_result = dict(
        avatar_url = user_avatar_url
    )
    return json_success(json_result)

def delete_avatar_backend(request, user_profile):
    # type: (HttpRequest, UserProfile) -> HttpResponse
    do_change_avatar_source(user_profile, UserProfile.AVATAR_FROM_GRAVATAR)
    gravatar_url = avatar_url(user_profile)

    json_result = dict(
        avatar_url = gravatar_url
    )
    return json_success(json_result)

@has_request_variables
def regenerate_api_key(request, user_profile):
    # type: (HttpRequest, UserProfile) -> HttpResponse
    do_regenerate_api_key(user_profile)
    json_result = dict(
        api_key = user_profile.api_key
    )
    return json_success(json_result)

@has_request_variables
def change_enter_sends(request, user_profile,
                       enter_sends=REQ(validator=check_bool)):
    # type: (HttpRequest, UserProfile, bool) -> HttpResponse
    do_change_enter_sends(user_profile, enter_sends)
    return json_success()
