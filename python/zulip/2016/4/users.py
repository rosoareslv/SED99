from __future__ import absolute_import

from django.shortcuts import redirect
from django.views.decorators.csrf import csrf_exempt
from six.moves import map

from zerver.decorator import has_request_variables, REQ, JsonableError, \
    require_realm_admin
from zerver.forms import CreateUserForm
from zerver.lib.actions import do_change_full_name, do_change_is_admin, \
    do_create_user, subscribed_to_stream, do_deactivate_user, do_reactivate_user, \
    do_change_default_events_register_stream, do_change_default_sending_stream, \
    do_change_default_all_public_streams, do_regenerate_api_key, do_change_avatar_source
from zerver.lib.avatar import avatar_url, get_avatar_url
from zerver.lib.response import json_error, json_success
from zerver.lib.upload import upload_avatar_image
from zerver.lib.validator import check_bool
from zerver.models import UserProfile, get_user_profile_by_email, get_stream, \
    email_allowed_for_realm

from zerver.lib.rest import rest_dispatch as _rest_dispatch
rest_dispatch = csrf_exempt((lambda request, *args, **kwargs: _rest_dispatch(request, globals(), *args, **kwargs)))

def deactivate_user_backend(request, user_profile, email):
    try:
        target = get_user_profile_by_email(email)
    except UserProfile.DoesNotExist:
        return json_error('No such user')
    if target.is_bot:
        return json_error('No such user')
    return _deactivate_user_profile_backend(request, user_profile, target)

def deactivate_bot_backend(request, user_profile, email):
    try:
        target = get_user_profile_by_email(email)
    except UserProfile.DoesNotExist:
        return json_error('No such bot')
    if not target.is_bot:
        return json_error('No such bot')
    return _deactivate_user_profile_backend(request, user_profile, target)

def _deactivate_user_profile_backend(request, user_profile, target):
    if not user_profile.can_admin_user(target):
        return json_error('Insufficient permission')

    do_deactivate_user(target)
    return json_success({})

def reactivate_user_backend(request, user_profile, email):
    try:
        target = get_user_profile_by_email(email)
    except UserProfile.DoesNotExist:
        return json_error('No such user')

    if not user_profile.can_admin_user(target):
        return json_error('Insufficient permission')

    do_reactivate_user(target)
    return json_success({})

@has_request_variables
def update_user_backend(request, user_profile, email,
                        is_admin=REQ(default=None, validator=check_bool)):
    try:
        target = get_user_profile_by_email(email)
    except UserProfile.DoesNotExist:
        return json_error('No such user')

    if not user_profile.can_admin_user(target):
        return json_error('Insufficient permission')

    if is_admin is not None:
        do_change_is_admin(target, is_admin)
    return json_success({})

def avatar(request, email):
    try:
        user_profile = get_user_profile_by_email(email)
        avatar_source = user_profile.avatar_source
    except UserProfile.DoesNotExist:
        avatar_source = 'G'
    url = get_avatar_url(avatar_source, email)
    if '?' in url:
        sep = '&'
    else:
        sep = '?'
    url += sep + request.META['QUERY_STRING']
    return redirect(url)

def get_stream_name(stream):
    if stream:
        name = stream.name
    else :
        name = None
    return name

def stream_or_none(stream_name, realm):
    if stream_name == '':
        return None
    else:
        stream = get_stream(stream_name, realm)
        if not stream:
            raise JsonableError('No such stream \'%s\'' %  (stream_name, ))
        return stream

@has_request_variables
def patch_bot_backend(request, user_profile, email,
                      full_name=REQ(default=None),
                      default_sending_stream=REQ(default=None),
                      default_events_register_stream=REQ(default=None),
                      default_all_public_streams=REQ(default=None, validator=check_bool)):
    try:
        bot = get_user_profile_by_email(email)
    except:
        return json_error('No such user')

    if not user_profile.can_admin_user(bot):
        return json_error('Insufficient permission')

    if full_name is not None:
        do_change_full_name(bot, full_name)
    if default_sending_stream is not None:
        stream = stream_or_none(default_sending_stream, bot.realm)
        do_change_default_sending_stream(bot, stream)
    if default_events_register_stream is not None:
        stream = stream_or_none(default_events_register_stream, bot.realm)
        do_change_default_events_register_stream(bot, stream)
    if default_all_public_streams is not None:
        do_change_default_all_public_streams(bot, default_all_public_streams)

    if len(request.FILES) == 0:
        pass
    elif len(request.FILES) == 1:
        user_file = list(request.FILES.values())[0]
        upload_avatar_image(user_file, user_profile, bot.email)
        avatar_source = UserProfile.AVATAR_FROM_USER
        do_change_avatar_source(bot, avatar_source)
    else:
        return json_error("You may only upload one file at a time")

    json_result = dict(
        full_name=bot.full_name,
        avatar_url=avatar_url(bot),
        default_sending_stream=get_stream_name(bot.default_sending_stream),
        default_events_register_stream=get_stream_name(bot.default_events_register_stream),
        default_all_public_streams=bot.default_all_public_streams,
    )
    return json_success(json_result)

@has_request_variables
def regenerate_bot_api_key(request, user_profile, email):
    try:
        bot = get_user_profile_by_email(email)
    except:
        return json_error('No such user')

    if not user_profile.can_admin_user(bot):
        return json_error('Insufficient permission')

    do_regenerate_api_key(bot)
    json_result = dict(
        api_key = bot.api_key
    )
    return json_success(json_result)

@has_request_variables
def add_bot_backend(request, user_profile, full_name=REQ, short_name=REQ,
                    default_sending_stream=REQ(default=None),
                    default_events_register_stream=REQ(default=None),
                    default_all_public_streams=REQ(validator=check_bool, default=None)):
    short_name += "-bot"
    email = short_name + "@" + user_profile.realm.domain
    form = CreateUserForm({'full_name': full_name, 'email': email})
    if not form.is_valid():
        # We validate client-side as well
        return json_error('Bad name or username')

    try:
        get_user_profile_by_email(email)
        return json_error("Username already in use")
    except UserProfile.DoesNotExist:
        pass

    if len(request.FILES) == 0:
        avatar_source = UserProfile.AVATAR_FROM_GRAVATAR
    elif len(request.FILES) != 1:
        return json_error("You may only upload one file at a time")
    else:
        user_file = list(request.FILES.values())[0]
        upload_avatar_image(user_file, user_profile, email)
        avatar_source = UserProfile.AVATAR_FROM_USER

    if default_sending_stream is not None:
        default_sending_stream = stream_or_none(default_sending_stream, user_profile.realm)
    if default_sending_stream and not default_sending_stream.is_public() and not \
        subscribed_to_stream(user_profile, default_sending_stream):
        return json_error('Insufficient permission')

    if default_events_register_stream is not None:
        default_events_register_stream = stream_or_none(default_events_register_stream,
                                                         user_profile.realm)
    if default_events_register_stream and not default_events_register_stream.is_public() and not \
        subscribed_to_stream(user_profile, default_events_register_stream):
        return json_error('Insufficient permission')


    bot_profile = do_create_user(email=email, password='',
                                 realm=user_profile.realm, full_name=full_name,
                                 short_name=short_name, active=True, bot=True,
                                 bot_owner=user_profile,
                                 avatar_source=avatar_source,
                                 default_sending_stream=default_sending_stream,
                                 default_events_register_stream=default_events_register_stream,
                                 default_all_public_streams=default_all_public_streams)
    json_result = dict(
            api_key=bot_profile.api_key,
            avatar_url=avatar_url(bot_profile),
            default_sending_stream=get_stream_name(bot_profile.default_sending_stream),
            default_events_register_stream=get_stream_name(bot_profile.default_events_register_stream),
            default_all_public_streams=bot_profile.default_all_public_streams,
    )
    return json_success(json_result)

def get_bots_backend(request, user_profile):
    bot_profiles = UserProfile.objects.filter(is_bot=True, is_active=True,
                                              bot_owner=user_profile)
    bot_profiles = bot_profiles.select_related('default_sending_stream', 'default_events_register_stream')
    bot_profiles = bot_profiles.order_by('date_joined')

    def bot_info(bot_profile):
        default_sending_stream = get_stream_name(bot_profile.default_sending_stream)
        default_events_register_stream = get_stream_name(bot_profile.default_events_register_stream)

        return dict(
            username=bot_profile.email,
            full_name=bot_profile.full_name,
            api_key=bot_profile.api_key,
            avatar_url=avatar_url(bot_profile),
            default_sending_stream=default_sending_stream,
            default_events_register_stream=default_events_register_stream,
            default_all_public_streams=bot_profile.default_all_public_streams,
        )

    return json_success({'bots': list(map(bot_info, bot_profiles))})

def get_members_backend(request, user_profile):
    realm = user_profile.realm
    admins = set(user_profile.realm.get_admin_users())
    members = []
    for profile in UserProfile.objects.select_related().filter(realm=realm):
        avatar_url = get_avatar_url(
            profile.avatar_source,
            profile.email
        )
        member = {"full_name": profile.full_name,
                  "is_bot": profile.is_bot,
                  "is_active": profile.is_active,
                  "is_admin": (profile in admins),
                  "email": profile.email,
                  "avatar_url": avatar_url,}
        if profile.is_bot and profile.bot_owner is not None:
            member["bot_owner"] = profile.bot_owner.email
        members.append(member)
    return json_success({'members': members})

@require_realm_admin
@has_request_variables
def create_user_backend(request, user_profile, email=REQ, password=REQ,
                        full_name=REQ, short_name=REQ):
    form = CreateUserForm({'full_name': full_name, 'email': email})
    if not form.is_valid():
        return json_error('Bad name or username')

    # Check that the new user's email address belongs to the admin's realm
    # (Since this is an admin API, we don't require the user to have been
    # invited first.)
    realm = user_profile.realm
    if not email_allowed_for_realm(email, user_profile.realm):
        return json_error("Email '%s' does not belong to domain '%s'" % (email, realm.domain))

    try:
        get_user_profile_by_email(email)
        return json_error("Email '%s' already in use" % (email,))
    except UserProfile.DoesNotExist:
        pass

    do_create_user(email, password, realm, full_name, short_name)
    return json_success()
