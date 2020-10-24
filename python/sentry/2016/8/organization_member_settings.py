from __future__ import absolute_import

from django.contrib import messages
from django.core.urlresolvers import reverse
from django.db.models import Q
from django.utils.translation import ugettext_lazy as _, ugettext

from sentry import roles
from sentry.models import OrganizationMember, OrganizationMemberTeam, Team
from sentry.web.frontend.base import OrganizationView
from sentry.web.forms.edit_organization_member import EditOrganizationMemberForm
from sentry.web.helpers import get_login_url


class OrganizationMemberSettingsView(OrganizationView):
    def get_form(self, request, member, allowed_roles):
        return EditOrganizationMemberForm(
            data=request.POST or None,
            instance=member,
            allowed_roles=allowed_roles,
            initial={
                'role': member.role,
                'teams': Team.objects.filter(
                    id__in=OrganizationMemberTeam.objects.filter(
                        organizationmember=member,
                    ).values('team'),
                ),
            }
        )

    def resend_invite(self, request, organization, member, regen=False):
        if regen:
            member.update(token=member.generate_token())
            messages.success(request, ugettext('A new invitation has been generated and sent to %(email)s') % {
                'organization': organization.name,
                'email': member.email,
            })
        else:
            messages.success(request, ugettext('An invitation to join %(organization)s has been sent to %(email)s') % {
                'organization': organization.name,
                'email': member.email,
            })

        member.send_invite_email()

        redirect = reverse('sentry-organization-member-settings',
                           args=[organization.slug, member.id])

        return self.redirect(redirect)

    def view_member(self, request, organization, member):
        context = {
            'member': member,
            'enabled_teams': set(member.teams.all()),
            'all_teams': Team.objects.filter(
                organization=organization,
            ),
            'role_list': roles.get_all(),
        }

        return self.respond('sentry/organization-member-details.html', context)

    def handle(self, request, organization, member_id):
        try:
            member = OrganizationMember.objects.get(
                Q(user__is_active=True) | Q(user__isnull=True),
                organization=organization,
                id=member_id,
            )
        except OrganizationMember.DoesNotExist:
            return self.redirect(get_login_url())

        if request.POST.get('op') == 'reinvite' and member.is_pending:
            return self.resend_invite(request, organization, member)
        elif request.POST.get('op') == 'regenerate' and member.is_pending:
            return self.resend_invite(request, organization, member, regen=True)

        can_admin = request.access.has_scope('member:delete')

        if can_admin and not request.is_superuser():
            acting_member = OrganizationMember.objects.get(
                user=request.user,
                organization=organization,
            )
            if roles.get(acting_member.role).priority < roles.get(member.role).priority:
                can_admin = False
            else:
                allowed_roles = [
                    r for r in roles.get_all()
                    if r.priority <= roles.get(acting_member.role).priority
                ]
                can_admin = bool(allowed_roles)
        elif request.is_superuser():
            allowed_roles = roles.get_all()

        if member.user == request.user or not can_admin:
            return self.view_member(request, organization, member)

        form = self.get_form(request, member, allowed_roles)
        if form.is_valid():
            member = form.save(request.user, organization, request.META['REMOTE_ADDR'])

            messages.add_message(request, messages.SUCCESS,
                _('Your changes were saved.'))

            redirect = reverse('sentry-organization-member-settings',
                               args=[organization.slug, member.id])

            return self.redirect(redirect)

        context = {
            'member': member,
            'form': form,
            'invite_link': member.get_invite_link(),
            'role_list': [
                (r, r in allowed_roles)
                for r in roles.get_all()
            ]
        }

        return self.respond('sentry/organization-member-settings.html', context)
