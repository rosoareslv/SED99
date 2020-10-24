# See https://zulip.readthedocs.io/en/latest/subsystems/events-system.html for
# high-level documentation on how this system works.

import copy

from collections import defaultdict
from django.utils.translation import ugettext as _
from django.conf import settings
from importlib import import_module
from typing import (
    Any, Callable, Dict, Iterable, Optional, Sequence, Set
)

session_engine = import_module(settings.SESSION_ENGINE)

from zerver.lib.alert_words import user_alert_words
from zerver.lib.avatar import avatar_url, get_avatar_field
from zerver.lib.bot_config import load_bot_config_template
from zerver.lib.hotspots import get_next_hotspots
from zerver.lib.integrations import EMBEDDED_BOTS, WEBHOOK_INTEGRATIONS
from zerver.lib.message import (
    aggregate_unread_data,
    apply_unread_message_event,
    get_raw_unread_data,
    get_recent_conversations_recipient_id,
    get_recent_private_conversations,
    get_starred_message_ids,
    remove_message_id_from_unread_mgs,
)
from zerver.lib.narrow import check_supported_events_narrow_filter, read_stop_words
from zerver.lib.push_notifications import push_notifications_enabled
from zerver.lib.soft_deactivation import reactivate_user_if_soft_deactivated
from zerver.lib.realm_icon import realm_icon_url
from zerver.lib.realm_logo import get_realm_logo_url
from zerver.lib.request import JsonableError
from zerver.lib.stream_subscription import handle_stream_notifications_compatibility
from zerver.lib.topic import TOPIC_NAME
from zerver.lib.topic_mutes import get_topic_mutes
from zerver.lib.actions import (
    do_get_streams, get_default_streams_for_realm,
    gather_subscriptions_helper, get_cross_realm_dicts,
    get_status_dict, streams_to_dicts_sorted,
    default_stream_groups_to_dicts_sorted,
    get_owned_bot_dicts,
    get_available_notification_sounds,
)
from zerver.lib.user_groups import user_groups_in_realm_serialized
from zerver.lib.user_status import get_user_info_dict
from zerver.tornado.event_queue import request_event_queue, get_user_events
from zerver.models import Client, Message, Realm, UserPresence, UserProfile, CustomProfileFieldValue, \
    get_user_profile_by_id, \
    get_realm_user_dicts, realm_filters_for_realm, get_user,\
    custom_profile_fields_for_realm, get_realm_domains, \
    get_default_stream_groups, CustomProfileField, Stream
from zproject.backends import email_auth_enabled, password_auth_enabled
from version import ZULIP_VERSION
from zerver.lib.external_accounts import DEFAULT_EXTERNAL_ACCOUNTS

def get_raw_user_data(realm: Realm, client_gravatar: bool) -> Dict[int, Dict[str, str]]:
    user_dicts = get_realm_user_dicts(realm.id)

    # TODO: Consider optimizing this query away with caching.
    custom_profile_field_values = CustomProfileFieldValue.objects.select_related(
        "field").filter(user_profile__realm_id=realm.id)
    profiles_by_user_id = defaultdict(dict)  # type: Dict[int, Dict[str, Any]]
    for profile_field in custom_profile_field_values:
        user_id = profile_field.user_profile_id
        if profile_field.field.is_renderable():
            profiles_by_user_id[user_id][profile_field.field_id] = {
                "value": profile_field.value,
                "rendered_value": profile_field.rendered_value
            }
        else:
            profiles_by_user_id[user_id][profile_field.field_id] = {
                "value": profile_field.value
            }

    def user_data(row: Dict[str, Any]) -> Dict[str, Any]:
        avatar_url = get_avatar_field(
            user_id=row['id'],
            realm_id=realm.id,
            email=row['email'],
            avatar_source=row['avatar_source'],
            avatar_version=row['avatar_version'],
            medium=False,
            client_gravatar=client_gravatar,
        )

        is_admin = row['is_realm_admin']
        is_guest = row['is_guest']
        is_bot = row['is_bot']
        # This format should align with get_cross_realm_dicts() and notify_created_user
        result = dict(
            email=row['email'],
            user_id=row['id'],
            avatar_url=avatar_url,
            is_admin=is_admin,
            is_guest=is_guest,
            is_bot=is_bot,
            full_name=row['full_name'],
            timezone=row['timezone'],
            is_active = row['is_active'],
            date_joined = row['date_joined'].isoformat(),
        )
        if is_bot:
            if row['email'] in settings.CROSS_REALM_BOT_EMAILS:
                result['is_cross_realm_bot'] = True
            elif row['bot_owner_id'] is not None:
                result['bot_owner_id'] = row['bot_owner_id']
        else:
            result['profile_data'] = profiles_by_user_id.get(row['id'], {})
        return result

    return {
        row['id']: user_data(row)
        for row in user_dicts
    }

def add_realm_logo_fields(state: Dict[str, Any], realm: Realm) -> None:
    state['realm_logo_url'] = get_realm_logo_url(realm, night = False)
    state['realm_logo_source'] = realm.logo_source
    state['realm_night_logo_url'] = get_realm_logo_url(realm, night = True)
    state['realm_night_logo_source'] = realm.night_logo_source
    state['max_logo_file_size'] = settings.MAX_LOGO_FILE_SIZE

def always_want(msg_type: str) -> bool:
    '''
    This function is used as a helper in
    fetch_initial_state_data, when the user passes
    in None for event_types, and we want to fetch
    info for every event type.  Defining this at module
    level makes it easier to mock.
    '''
    if settings.PRODUCTION and msg_type == "recent_private_conversations":  # nocoverage
        # Temporary: Don't include recent_private_conversations in production
        # by default while the feature is still experimental.
        return False
    return True

# Fetch initial data.  When event_types is not specified, clients want
# all event types.  Whenever you add new code to this function, you
# should also add corresponding events for changes in the data
# structures and new code to apply_events (and add a test in EventsRegisterTest).
def fetch_initial_state_data(user_profile: UserProfile,
                             event_types: Optional[Iterable[str]],
                             queue_id: str, client_gravatar: bool,
                             include_subscribers: bool = True) -> Dict[str, Any]:
    state = {'queue_id': queue_id}  # type: Dict[str, Any]
    realm = user_profile.realm

    if event_types is None:
        # return True always
        want = always_want  # type: Callable[[str], bool]
    else:
        want = set(event_types).__contains__

    if want('alert_words'):
        state['alert_words'] = user_alert_words(user_profile)

    if want('custom_profile_fields'):
        fields = custom_profile_fields_for_realm(realm.id)
        state['custom_profile_fields'] = [f.as_dict() for f in fields]
        state['custom_profile_field_types'] = CustomProfileField.FIELD_TYPE_CHOICES_DICT

    if want('hotspots'):
        state['hotspots'] = get_next_hotspots(user_profile)

    if want('message'):
        # The client should use get_messages() to fetch messages
        # starting with the max_message_id.  They will get messages
        # newer than that ID via get_events()
        messages = Message.objects.filter(usermessage__user_profile=user_profile).order_by('-id')[:1]
        if messages:
            state['max_message_id'] = messages[0].id
        else:
            state['max_message_id'] = -1

    if want('muted_topics'):
        state['muted_topics'] = get_topic_mutes(user_profile)

    if want('pointer'):
        state['pointer'] = user_profile.pointer

    if want('presence'):
        state['presences'] = get_status_dict(user_profile)

    if want('realm'):
        for property_name in Realm.property_types:
            state['realm_' + property_name] = getattr(realm, property_name)

        # Don't send the zoom API secret to clients.
        if state.get('realm_zoom_api_secret'):
            state['realm_zoom_api_secret'] = ''

        # Most state is handled via the property_types framework;
        # these manual entries are for those realm settings that don't
        # fit into that framework.
        state['realm_authentication_methods'] = realm.authentication_methods_dict()
        state['realm_allow_message_editing'] = realm.allow_message_editing
        state['realm_allow_community_topic_editing'] = realm.allow_community_topic_editing
        state['realm_allow_message_deleting'] = realm.allow_message_deleting
        state['realm_message_content_edit_limit_seconds'] = realm.message_content_edit_limit_seconds
        state['realm_message_content_delete_limit_seconds'] = realm.message_content_delete_limit_seconds
        state['realm_icon_url'] = realm_icon_url(realm)
        state['realm_icon_source'] = realm.icon_source
        state['max_icon_file_size'] = settings.MAX_ICON_FILE_SIZE
        add_realm_logo_fields(state, realm)
        state['realm_bot_domain'] = realm.get_bot_domain()
        state['realm_uri'] = realm.uri
        state['realm_available_video_chat_providers'] = realm.VIDEO_CHAT_PROVIDERS
        state['realm_presence_disabled'] = realm.presence_disabled
        state['settings_send_digest_emails'] = settings.SEND_DIGEST_EMAILS
        state['realm_digest_emails_enabled'] = realm.digest_emails_enabled and settings.SEND_DIGEST_EMAILS
        state['realm_is_zephyr_mirror_realm'] = realm.is_zephyr_mirror_realm
        state['realm_email_auth_enabled'] = email_auth_enabled(realm)
        state['realm_password_auth_enabled'] = password_auth_enabled(realm)
        state['realm_push_notifications_enabled'] = push_notifications_enabled()
        state['realm_upload_quota'] = realm.upload_quota_bytes()
        state['realm_plan_type'] = realm.plan_type
        state['plan_includes_wide_organization_logo'] = realm.plan_type != Realm.LIMITED
        state['upgrade_text_for_wide_organization_logo'] = str(Realm.UPGRADE_TEXT_STANDARD)
        state['realm_default_external_accounts'] = DEFAULT_EXTERNAL_ACCOUNTS

        if realm.notifications_stream and not realm.notifications_stream.deactivated:
            notifications_stream = realm.notifications_stream
            state['realm_notifications_stream_id'] = notifications_stream.id
        else:
            state['realm_notifications_stream_id'] = -1

        signup_notifications_stream = realm.get_signup_notifications_stream()
        if signup_notifications_stream:
            state['realm_signup_notifications_stream_id'] = signup_notifications_stream.id
        else:
            state['realm_signup_notifications_stream_id'] = -1

    if want('realm_domains'):
        state['realm_domains'] = get_realm_domains(realm)

    if want('realm_emoji'):
        state['realm_emoji'] = realm.get_emoji()

    if want('realm_filters'):
        state['realm_filters'] = realm_filters_for_realm(realm.id)

    if want('realm_user_groups'):
        state['realm_user_groups'] = user_groups_in_realm_serialized(realm)

    if want('realm_user'):
        state['raw_users'] = get_raw_user_data(
            realm=realm,
            client_gravatar=client_gravatar,
        )

        # For the user's own avatar URL, we force
        # client_gravatar=False, since that saves some unnecessary
        # client-side code for handing medium-size avatars.  See #8253
        # for details.
        state['avatar_source'] = user_profile.avatar_source
        state['avatar_url_medium'] = avatar_url(
            user_profile,
            medium=True,
            client_gravatar=False,
        )
        state['avatar_url'] = avatar_url(
            user_profile,
            medium=False,
            client_gravatar=False,
        )

        state['can_create_streams'] = user_profile.can_create_streams()
        state['can_subscribe_other_users'] = user_profile.can_subscribe_other_users()
        state['cross_realm_bots'] = list(get_cross_realm_dicts())
        state['is_admin'] = user_profile.is_realm_admin
        state['is_guest'] = user_profile.is_guest
        state['user_id'] = user_profile.id
        state['enter_sends'] = user_profile.enter_sends
        state['email'] = user_profile.email
        state['delivery_email'] = user_profile.delivery_email
        state['full_name'] = user_profile.full_name

    if want('realm_bot'):
        state['realm_bots'] = get_owned_bot_dicts(user_profile)

    # This does not yet have an apply_event counterpart, since currently,
    # new entries for EMBEDDED_BOTS can only be added directly in the codebase.
    if want('realm_embedded_bots'):
        realm_embedded_bots = []
        for bot in EMBEDDED_BOTS:
            realm_embedded_bots.append({'name': bot.name,
                                        'config': load_bot_config_template(bot.name)})
        state['realm_embedded_bots'] = realm_embedded_bots

    # This does not have an apply_events counterpart either since
    # this data is mostly static.
    if want('realm_incoming_webhook_bots'):
        realm_incoming_webhook_bots = []
        for integration in WEBHOOK_INTEGRATIONS:
            realm_incoming_webhook_bots.append({
                'name': integration.name,
                'config': {c[1]: c[2] for c in integration.config_options}
            })
        state['realm_incoming_webhook_bots'] = realm_incoming_webhook_bots

    if want('recent_private_conversations'):
        # A data structure containing records of this form:
        #
        #   [{'max_message_id': 700175, 'user_ids': [801]}]
        #
        # for all recent private message conversations, ordered by the
        # highest message ID in the conversation.  The user_ids list
        # is the list of users other than the current user in the
        # private message conversation (so it is [] for PMs to self).
        # Note that raw_recent_private_conversations is an
        # intermediate form as a dictionary keyed by recipient_id,
        # which is more efficient to update, and is rewritten to the
        # final format in post_process_state.
        state['raw_recent_private_conversations'] = get_recent_private_conversations(user_profile)

    if want('subscription'):
        subscriptions, unsubscribed, never_subscribed = gather_subscriptions_helper(
            user_profile, include_subscribers=include_subscribers)
        state['subscriptions'] = subscriptions
        state['unsubscribed'] = unsubscribed
        state['never_subscribed'] = never_subscribed

    if want('update_message_flags') and want('message'):
        # Keeping unread_msgs updated requires both message flag updates and
        # message updates. This is due to the fact that new messages will not
        # generate a flag update so we need to use the flags field in the
        # message event.
        state['raw_unread_msgs'] = get_raw_unread_data(user_profile)

    if want('starred_messages'):
        state['starred_messages'] = get_starred_message_ids(user_profile)

    if want('stream'):
        state['streams'] = do_get_streams(user_profile)
        state['stream_name_max_length'] = Stream.MAX_NAME_LENGTH
        state['stream_description_max_length'] = Stream.MAX_DESCRIPTION_LENGTH
    if want('default_streams'):
        if user_profile.is_guest:
            state['realm_default_streams'] = []
        else:
            state['realm_default_streams'] = streams_to_dicts_sorted(
                get_default_streams_for_realm(realm.id))
    if want('default_stream_groups'):
        if user_profile.is_guest:
            state['realm_default_stream_groups'] = []
        else:
            state['realm_default_stream_groups'] = default_stream_groups_to_dicts_sorted(
                get_default_stream_groups(realm))

    if want('stop_words'):
        state['stop_words'] = read_stop_words()

    if want('update_display_settings'):
        for prop in UserProfile.property_types:
            state[prop] = getattr(user_profile, prop)
        state['emojiset_choices'] = user_profile.emojiset_choices()

    if want('update_global_notifications'):
        for notification in UserProfile.notification_setting_types:
            state[notification] = getattr(user_profile, notification)
        state['available_notification_sounds'] = get_available_notification_sounds()

    if want('user_status'):
        state['user_status'] = get_user_info_dict(realm_id=realm.id)

    if want('zulip_version'):
        state['zulip_version'] = ZULIP_VERSION

    return state

def apply_events(state: Dict[str, Any], events: Iterable[Dict[str, Any]],
                 user_profile: UserProfile, client_gravatar: bool,
                 include_subscribers: bool = True,
                 fetch_event_types: Optional[Iterable[str]] = None) -> None:
    for event in events:
        if fetch_event_types is not None and event['type'] not in fetch_event_types:
            # TODO: continuing here is not, most precisely, correct.
            # In theory, an event of one type, e.g. `realm_user`,
            # could modify state that doesn't come from that
            # `fetch_event_types` value, e.g. the `our_person` part of
            # that code path.  But it should be extremely rare, and
            # fixing that will require a nontrivial refactor of
            # `apply_event`.  For now, be careful in your choice of
            # `fetch_event_types`.
            continue
        apply_event(state, event, user_profile, client_gravatar, include_subscribers)

def apply_event(state: Dict[str, Any],
                event: Dict[str, Any],
                user_profile: UserProfile,
                client_gravatar: bool,
                include_subscribers: bool) -> None:
    if event['type'] == "message":
        state['max_message_id'] = max(state['max_message_id'], event['message']['id'])
        if 'raw_unread_msgs' in state:
            apply_unread_message_event(
                user_profile,
                state['raw_unread_msgs'],
                event['message'],
                event['flags'],
            )

        if event['message']['type'] != "stream":
            if 'raw_recent_private_conversations' in state:
                # Handle maintaining the recent_private_conversations data structure.
                conversations = state['raw_recent_private_conversations']
                recipient_id = get_recent_conversations_recipient_id(
                    user_profile, event['message']['recipient_id'],
                    event['message']["sender_id"])

                if recipient_id not in conversations:
                    conversations[recipient_id] = dict(
                        user_ids=[user_dict['id'] for user_dict in
                                  event['message']['display_recipient'] if
                                  user_dict['id'] != user_profile.id]
                    )
                conversations[recipient_id]['max_message_id'] = event['message']['id']
            return

        # Below, we handle maintaining first_message_id.
        for sub_dict in state.get('subscriptions', []):
            if event['message']['stream_id'] == sub_dict['stream_id']:
                if sub_dict['first_message_id'] is None:
                    sub_dict['first_message_id'] = event['message']['id']
        for stream_dict in state.get('streams', []):
            if event['message']['stream_id'] == stream_dict['stream_id']:
                if stream_dict['first_message_id'] is None:
                    stream_dict['first_message_id'] = event['message']['id']

    elif event['type'] == "hotspots":
        state['hotspots'] = event['hotspots']
    elif event['type'] == "custom_profile_fields":
        state['custom_profile_fields'] = event['fields']
    elif event['type'] == "pointer":
        state['pointer'] = max(state['pointer'], event['pointer'])
    elif event['type'] == "realm_user":
        person = event['person']
        person_user_id = person['user_id']

        if event['op'] == "add":
            person = copy.deepcopy(person)
            if client_gravatar:
                if 'gravatar.com' in person['avatar_url']:
                    person['avatar_url'] = None
            person['is_active'] = True
            if not person['is_bot']:
                person['profile_data'] = {}
            state['raw_users'][person_user_id] = person
        elif event['op'] == "remove":
            state['raw_users'][person_user_id]['is_active'] = False
        elif event['op'] == 'update':
            is_me = (person_user_id == user_profile.id)

            if is_me:
                if ('avatar_url' in person and 'avatar_url' in state):
                    state['avatar_source'] = person['avatar_source']
                    state['avatar_url'] = person['avatar_url']
                    state['avatar_url_medium'] = person['avatar_url_medium']

                for field in ['is_admin', 'delivery_email', 'email', 'full_name']:
                    if field in person and field in state:
                        state[field] = person[field]

                # In the unlikely event that the current user
                # just changed to/from being an admin, we need
                # to add/remove the data on all bots in the
                # realm.  This is ugly and probably better
                # solved by removing the all-realm-bots data
                # given to admin users from this flow.
                if ('is_admin' in person and 'realm_bots' in state):
                    prev_state = state['raw_users'][user_profile.id]
                    was_admin = prev_state['is_admin']
                    now_admin = person['is_admin']

                    if was_admin and not now_admin:
                        state['realm_bots'] = []
                    if not was_admin and now_admin:
                        state['realm_bots'] = get_owned_bot_dicts(user_profile)

            if client_gravatar and 'avatar_url' in person:
                # Respect the client_gravatar setting in the `users` data.
                if 'gravatar.com' in person['avatar_url']:
                    person['avatar_url'] = None
                    person['avatar_url_medium'] = None

            if person_user_id in state['raw_users']:
                p = state['raw_users'][person_user_id]
                for field in p:
                    if field in person:
                        p[field] = person[field]
                    if 'custom_profile_field' in person:
                        custom_field_id = person['custom_profile_field']['id']
                        custom_field_new_value = person['custom_profile_field']['value']
                        if 'rendered_value' in person['custom_profile_field']:
                            p['profile_data'][custom_field_id] = {
                                'value': custom_field_new_value,
                                'rendered_value': person['custom_profile_field']['rendered_value']
                            }
                        else:
                            p['profile_data'][custom_field_id] = {
                                'value': custom_field_new_value
                            }

    elif event['type'] == 'realm_bot':
        if event['op'] == 'add':
            state['realm_bots'].append(event['bot'])

        if event['op'] == 'remove':
            email = event['bot']['email']
            for bot in state['realm_bots']:
                if bot['email'] == email:
                    bot['is_active'] = False

        if event['op'] == 'delete':
            state['realm_bots'] = [item for item
                                   in state['realm_bots'] if item['email'] != event['bot']['email']]

        if event['op'] == 'update':
            for bot in state['realm_bots']:
                if bot['email'] == event['bot']['email']:
                    if 'owner_id' in event['bot']:
                        bot['owner'] = get_user_profile_by_id(event['bot']['owner_id']).email
                    else:
                        bot.update(event['bot'])

    elif event['type'] == 'stream':
        if event['op'] == 'create':
            for stream in event['streams']:
                if not stream['invite_only']:
                    stream_data = copy.deepcopy(stream)
                    if include_subscribers:
                        stream_data['subscribers'] = []
                    stream_data['stream_weekly_traffic'] = None
                    stream_data['is_old_stream'] = False
                    stream_data['is_announcement_only'] = False
                    # Add stream to never_subscribed (if not invite_only)
                    state['never_subscribed'].append(stream_data)
                state['streams'].append(stream)
            state['streams'].sort(key=lambda elt: elt["name"])

        if event['op'] == 'delete':
            deleted_stream_ids = {stream['stream_id'] for stream in event['streams']}
            state['streams'] = [s for s in state['streams'] if s['stream_id'] not in deleted_stream_ids]
            state['never_subscribed'] = [stream for stream in state['never_subscribed'] if
                                         stream['stream_id'] not in deleted_stream_ids]

        if event['op'] == 'update':
            # For legacy reasons, we call stream data 'subscriptions' in
            # the state var here, for the benefit of the JS code.
            for obj in state['subscriptions']:
                if obj['name'].lower() == event['name'].lower():
                    obj[event['property']] = event['value']
                    if event['property'] == "description":
                        obj['rendered_description'] = event['rendered_description']
            # Also update the pure streams data
            for stream in state['streams']:
                if stream['name'].lower() == event['name'].lower():
                    prop = event['property']
                    if prop in stream:
                        stream[prop] = event['value']
                        if prop == 'description':
                            stream['rendered_description'] = event['rendered_description']
        elif event['op'] == "occupy":
            state['streams'] += event['streams']
        elif event['op'] == "vacate":
            stream_ids = [s["stream_id"] for s in event['streams']]
            state['streams'] = [s for s in state['streams'] if s["stream_id"] not in stream_ids]
    elif event['type'] == 'default_streams':
        state['realm_default_streams'] = event['default_streams']
    elif event['type'] == 'default_stream_groups':
        state['realm_default_stream_groups'] = event['default_stream_groups']
    elif event['type'] == 'realm':
        if event['op'] == "update":
            field = 'realm_' + event['property']
            state[field] = event['value']

            if event['property'] == 'plan_type':
                # Then there are some extra fields that also need to be set.
                state['plan_includes_wide_organization_logo'] = event['value'] != Realm.LIMITED
                state['realm_upload_quota'] = event['extra_data']['upload_quota']

            # Tricky interaction: Whether we can create streams can get changed here.
            if (field in ['realm_create_stream_policy',
                          'realm_waiting_period_threshold']) and 'can_create_streams' in state:
                state['can_create_streams'] = user_profile.can_create_streams()

            if (field in ['realm_invite_to_stream_policy',
                          'realm_waiting_period_threshold']) and 'can_subscribe_other_users' in state:
                state['can_subscribe_other_users'] = user_profile.can_subscribe_other_users()
        elif event['op'] == "update_dict":
            for key, value in event['data'].items():
                state['realm_' + key] = value
                # It's a bit messy, but this is where we need to
                # update the state for whether password authentication
                # is enabled on this server.
                if key == 'authentication_methods':
                    state['realm_password_auth_enabled'] = (value['Email'] or value['LDAP'])
                    state['realm_email_auth_enabled'] = value['Email']
    elif event['type'] == "subscription":
        if not include_subscribers and event['op'] in ['peer_add', 'peer_remove']:
            return

        if event['op'] in ["add"]:
            if not include_subscribers:
                # Avoid letting 'subscribers' entries end up in the list
                for i, sub in enumerate(event['subscriptions']):
                    event['subscriptions'][i] = copy.deepcopy(event['subscriptions'][i])
                    del event['subscriptions'][i]['subscribers']

        def name(sub: Dict[str, Any]) -> str:
            return sub['name'].lower()

        if event['op'] == "add":
            added_names = set(map(name, event["subscriptions"]))
            was_added = lambda s: name(s) in added_names

            # add the new subscriptions
            state['subscriptions'] += event['subscriptions']

            # remove them from unsubscribed if they had been there
            state['unsubscribed'] = [s for s in state['unsubscribed'] if not was_added(s)]

            # remove them from never_subscribed if they had been there
            state['never_subscribed'] = [s for s in state['never_subscribed'] if not was_added(s)]

        elif event['op'] == "remove":
            removed_names = set(map(name, event["subscriptions"]))
            was_removed = lambda s: name(s) in removed_names

            # Find the subs we are affecting.
            removed_subs = list(filter(was_removed, state['subscriptions']))

            # Remove our user from the subscribers of the removed subscriptions.
            if include_subscribers:
                for sub in removed_subs:
                    sub['subscribers'] = [id for id in sub['subscribers'] if id != user_profile.id]

            # We must effectively copy the removed subscriptions from subscriptions to
            # unsubscribe, since we only have the name in our data structure.
            state['unsubscribed'] += removed_subs

            # Now filter out the removed subscriptions from subscriptions.
            state['subscriptions'] = [s for s in state['subscriptions'] if not was_removed(s)]

        elif event['op'] == 'update':
            for sub in state['subscriptions']:
                if sub['name'].lower() == event['name'].lower():
                    sub[event['property']] = event['value']
        elif event['op'] == 'peer_add':
            user_id = event['user_id']
            for sub in state['subscriptions']:
                if (sub['name'] in event['subscriptions'] and
                        user_id not in sub['subscribers']):
                    sub['subscribers'].append(user_id)
            for sub in state['never_subscribed']:
                if (sub['name'] in event['subscriptions'] and
                        user_id not in sub['subscribers']):
                    sub['subscribers'].append(user_id)
        elif event['op'] == 'peer_remove':
            user_id = event['user_id']
            for sub in state['subscriptions']:
                if (sub['name'] in event['subscriptions'] and
                        user_id in sub['subscribers']):
                    sub['subscribers'].remove(user_id)
    elif event['type'] == "presence":
        # TODO: Add user_id to presence update events / state format!
        presence_user_profile = get_user(event['email'], user_profile.realm)
        state['presences'][event['email']] = UserPresence.get_status_dict_by_user(
            presence_user_profile)[event['email']]
    elif event['type'] == "update_message":
        # We don't return messages in /register, so we don't need to
        # do anything for content updates, but we may need to update
        # the unread_msgs data if the topic of an unread message changed.
        if TOPIC_NAME in event:
            stream_dict = state['raw_unread_msgs']['stream_dict']
            topic = event[TOPIC_NAME]
            for message_id in event['message_ids']:
                if message_id in stream_dict:
                    stream_dict[message_id]['topic'] = topic
    elif event['type'] == "delete_message":
        max_message = Message.objects.filter(
            usermessage__user_profile=user_profile).order_by('-id').first()
        if max_message:
            state['max_message_id'] = max_message.id
        else:
            state['max_message_id'] = -1

        if 'raw_unread_msgs' in state:
            remove_id = event['message_id']
            remove_message_id_from_unread_mgs(state['raw_unread_msgs'], remove_id)

        # The remainder of this block is about maintaining recent_private_conversations
        if 'raw_recent_private_conversations' not in state or event['message_type'] != 'private':
            return

        recipient_id = get_recent_conversations_recipient_id(user_profile, event['recipient_id'],
                                                             event['sender_id'])

        # Ideally, we'd have test coverage for these two blocks.  To
        # do that, we'll need a test where we delete not-the-latest
        # messages or delete a private message not in
        # recent_private_conversations.
        if recipient_id not in state['raw_recent_private_conversations']:  # nocoverage
            return

        old_max_message_id = state['raw_recent_private_conversations'][recipient_id]['max_message_id']
        if old_max_message_id != event['message_id']:  # nocoverage
            return

        # OK, we just deleted what had been the max_message_id for
        # this recent conversation; we need to recompute that value
        # from scratch.  Definitely don't need to re-query everything,
        # but this case is likely rare enough that it's reasonable to do so.
        state['raw_recent_private_conversations'] = \
            get_recent_private_conversations(user_profile)
    elif event['type'] == "reaction":
        # The client will get the message with the reactions directly
        pass
    elif event['type'] == "submessage":
        # The client will get submessages with their messages
        pass
    elif event['type'] == 'typing':
        # Typing notification events are transient and thus ignored
        pass
    elif event['type'] == "attachment":
        # Attachment events are just for updating the "uploads" UI;
        # they are not sent directly.
        pass
    elif event['type'] == "update_message_flags":
        # We don't return messages in `/register`, so most flags we
        # can ignore, but we do need to update the unread_msgs data if
        # unread state is changed.
        if 'raw_unread_msgs' in state and event['flag'] == 'read' and event['operation'] == 'add':
            for remove_id in event['messages']:
                remove_message_id_from_unread_mgs(state['raw_unread_msgs'], remove_id)
        if event['flag'] == 'starred' and event['operation'] == 'add':
            state['starred_messages'] += event['messages']
        if event['flag'] == 'starred' and event['operation'] == 'remove':
            state['starred_messages'] = [message for message in state['starred_messages']
                                         if not (message in event['messages'])]
    elif event['type'] == "realm_domains":
        if event['op'] == 'add':
            state['realm_domains'].append(event['realm_domain'])
        elif event['op'] == 'change':
            for realm_domain in state['realm_domains']:
                if realm_domain['domain'] == event['realm_domain']['domain']:
                    realm_domain['allow_subdomains'] = event['realm_domain']['allow_subdomains']
        elif event['op'] == 'remove':
            state['realm_domains'] = [realm_domain for realm_domain in state['realm_domains']
                                      if realm_domain['domain'] != event['domain']]
    elif event['type'] == "realm_emoji":
        state['realm_emoji'] = event['realm_emoji']
    elif event['type'] == 'realm_export':
        # These realm export events are only available to
        # administrators, and aren't included in page_params.
        pass
    elif event['type'] == "alert_words":
        state['alert_words'] = event['alert_words']
    elif event['type'] == "muted_topics":
        state['muted_topics'] = event["muted_topics"]
    elif event['type'] == "realm_filters":
        state['realm_filters'] = event["realm_filters"]
    elif event['type'] == "update_display_settings":
        assert event['setting_name'] in UserProfile.property_types
        state[event['setting_name']] = event['setting']
    elif event['type'] == "update_global_notifications":
        assert event['notification_name'] in UserProfile.notification_setting_types
        state[event['notification_name']] = event['setting']
    elif event['type'] == "invites_changed":
        pass
    elif event['type'] == "user_group":
        if event['op'] == 'add':
            state['realm_user_groups'].append(event['group'])
            state['realm_user_groups'].sort(key=lambda group: group['id'])
        elif event['op'] == 'update':
            for user_group in state['realm_user_groups']:
                if user_group['id'] == event['group_id']:
                    user_group.update(event['data'])
        elif event['op'] == 'add_members':
            for user_group in state['realm_user_groups']:
                if user_group['id'] == event['group_id']:
                    user_group['members'].extend(event['user_ids'])
                    user_group['members'].sort()
        elif event['op'] == 'remove_members':
            for user_group in state['realm_user_groups']:
                if user_group['id'] == event['group_id']:
                    members = set(user_group['members'])
                    user_group['members'] = list(members - set(event['user_ids']))
                    user_group['members'].sort()
        elif event['op'] == 'remove':
            state['realm_user_groups'] = [ug for ug in state['realm_user_groups']
                                          if ug['id'] != event['group_id']]
    elif event['type'] == 'user_status':
        user_id = event['user_id']
        user_status = state['user_status']
        away = event.get('away')
        status_text = event.get('status_text')

        if user_id not in user_status:
            user_status[user_id] = dict()

        if away is not None:
            if away:
                user_status[user_id]['away'] = True
            else:
                user_status[user_id].pop('away', None)

        if status_text is not None:
            if status_text == '':
                user_status[user_id].pop('status_text', None)
            else:
                user_status[user_id]['status_text'] = status_text

        if not user_status[user_id]:
            user_status.pop(user_id, None)

        state['user_status'] = user_status
    else:
        raise AssertionError("Unexpected event type %s" % (event['type'],))

def do_events_register(user_profile: UserProfile, user_client: Client,
                       apply_markdown: bool = True,
                       client_gravatar: bool = False,
                       event_types: Optional[Iterable[str]] = None,
                       queue_lifespan_secs: int = 0,
                       all_public_streams: bool = False,
                       include_subscribers: bool = True,
                       notification_settings_null: bool = False,
                       narrow: Iterable[Sequence[str]] = [],
                       fetch_event_types: Optional[Iterable[str]] = None) -> Dict[str, Any]:
    # Technically we don't need to check this here because
    # build_narrow_filter will check it, but it's nicer from an error
    # handling perspective to do it before contacting Tornado
    check_supported_events_narrow_filter(narrow)

    if user_profile.realm.email_address_visibility == Realm.EMAIL_ADDRESS_VISIBILITY_ADMINS:
        # If email addresses are only available to administrators,
        # clients cannot compute gravatars, so we force-set it to false.
        client_gravatar = False

    # Note that we pass event_types, not fetch_event_types here, since
    # that's what controls which future events are sent.
    queue_id = request_event_queue(user_profile, user_client, apply_markdown, client_gravatar,
                                   queue_lifespan_secs, event_types, all_public_streams,
                                   narrow=narrow)

    if queue_id is None:
        raise JsonableError(_("Could not allocate event queue"))

    if fetch_event_types is not None:
        event_types_set = set(fetch_event_types)  # type: Optional[Set[str]]
    elif event_types is not None:
        event_types_set = set(event_types)
    else:
        event_types_set = None

    # Fill up the UserMessage rows if a soft-deactivated user has returned
    reactivate_user_if_soft_deactivated(user_profile)

    ret = fetch_initial_state_data(user_profile, event_types_set, queue_id,
                                   client_gravatar=client_gravatar,
                                   include_subscribers=include_subscribers)

    # Apply events that came in while we were fetching initial data
    events = get_user_events(user_profile, queue_id, -1)
    apply_events(ret, events, user_profile, include_subscribers=include_subscribers,
                 client_gravatar=client_gravatar,
                 fetch_event_types=fetch_event_types)

    post_process_state(user_profile, ret, notification_settings_null)

    if len(events) > 0:
        ret['last_event_id'] = events[-1]['id']
    else:
        ret['last_event_id'] = -1
    return ret

def post_process_state(user_profile: UserProfile, ret: Dict[str, Any],
                       notification_settings_null: bool) -> None:
    '''
    NOTE:

    Below is an example of post-processing initial state data AFTER we
    apply events.  For large payloads like `unread_msgs`, it's helpful
    to have an intermediate data structure that is easy to manipulate
    with O(1)-type operations as we apply events.

    Then, only at the end, we put it in the form that's more appropriate
    for client.
    '''
    if 'raw_unread_msgs' in ret:
        ret['unread_msgs'] = aggregate_unread_data(ret['raw_unread_msgs'])
        del ret['raw_unread_msgs']

    '''
    See the note above; the same technique applies below.
    '''
    if 'raw_users' in ret:
        user_dicts = list(ret['raw_users'].values())
        user_dicts = sorted(user_dicts, key=lambda x: x['user_id'])

        ret['realm_users'] = [d for d in user_dicts if d['is_active']]
        ret['realm_non_active_users'] = [d for d in user_dicts if not d['is_active']]

        '''
        Be aware that we do intentional aliasing in the below code.
        We can now safely remove the `is_active` field from all the
        dicts that got partitioned into the two lists above.

        We remove the field because it's already implied, and sending
        it to clients makes clients prone to bugs where they "trust"
        the field but don't actually update in live updates.  It also
        wastes bandwidth.
        '''
        for d in user_dicts:
            d.pop('is_active')

        del ret['raw_users']

    if 'raw_recent_private_conversations' in ret:
        # Reformat recent_private_conversations to be a list of dictionaries, rather than a dict.
        ret['recent_private_conversations'] = sorted([
            dict(
                **value
            ) for (recipient_id, value) in ret['raw_recent_private_conversations'].items()
        ], key = lambda x: -x["max_message_id"])
        del ret['raw_recent_private_conversations']

    if not notification_settings_null and 'subscriptions' in ret:
        for stream_dict in ret['subscriptions'] + ret['unsubscribed']:
            handle_stream_notifications_compatibility(user_profile, stream_dict,
                                                      notification_settings_null)
