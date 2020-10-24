from __future__ import absolute_import

from rest_framework.response import Response

from sentry.api.base import Endpoint
from sentry.api.bases.group import GroupPermission
from sentry.api.serializers import serialize
from sentry.models import Event, Release


class EventDetailsEndpoint(Endpoint):
    permission_classes = (GroupPermission,)

    def _get_release_info(self, request, event):
        version = event.get_tag('sentry:release')
        if not version:
            return None
        try:
            release = Release.objects.get(
                project=event.project,
                version=version,
            )
        except Release.DoesNotExist:
            return {'version': version}
        return serialize(release, request.user)

    def get(self, request, event_id):
        """
        Retrieve an event

        Return details on an individual event.

            {method} {path}

        """
        event = Event.objects.get(
            id=event_id
        )

        self.check_object_permissions(request, event.group)

        Event.objects.bind_nodes([event], 'data')

        # HACK(dcramer): work around lack of unique sorting on datetime
        base_qs = Event.objects.filter(
            group=event.group_id,
        ).exclude(id=event.id)
        try:
            next_event = sorted(
                base_qs.filter(
                    datetime__gte=event.datetime
                ).order_by('datetime')[0:5],
                key=lambda x: (x.datetime, x.id)
            )[0]
        except IndexError:
            next_event = None

        try:
            prev_event = sorted(
                base_qs.filter(
                    datetime__lte=event.datetime,
                ).order_by('-datetime')[0:5],
                key=lambda x: (x.datetime, x.id),
                reverse=True
            )[0]
        except IndexError:
            prev_event = None

        data = serialize(event, request.user)
        data['release'] = self._get_release_info(request, event)

        if next_event:
            data['nextEventID'] = str(next_event.id)
        else:
            data['nextEventID'] = None
        if prev_event:
            data['previousEventID'] = str(prev_event.id)
        else:
            data['previousEventID'] = None

        return Response(data)
