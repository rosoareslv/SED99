# -*- coding: utf-8 -*-
from __future__ import absolute_import

from typing import Any, Dict, Iterable
import logging

from django.conf import settings
from django.test import override_settings
from django.template import Template, Context
from django.template.loader import get_template

from zerver.models import get_user_profile_by_email
from zerver.lib.test_helpers import get_all_templates, ZulipTestCase

class get_form_value(object):
    def __init__(self, value):
        # type: (Any) -> None
        self._value = value

    def value(self):
        # type: () -> Any
        return self._value


class DummyForm(dict):
    pass


class TemplateTestCase(ZulipTestCase):
    """
    Tests that backend template rendering doesn't crash.

    This renders all the Zulip backend templates, passing dummy data
    as the context, which allows us to verify whether any of the
    templates are broken enough to not render at all (no verification
    is done that the output looks right).  Please see `get_context`
    function documentation for more information.
    """
    @override_settings(TERMS_OF_SERVICE=None)
    def test_templates(self):
        # type: () -> None

        # Just add the templates whose context has a conflict with other
        # templates' context in `defer`.
        defer = ['analytics/activity.html']
        skip = defer + ['tests/test_markdown.html', 'zerver/terms.html']
        templates = [t for t in get_all_templates() if t not in skip]
        self.render_templates(templates, self.get_context())

        # Test the deferred templates with updated context.
        update = {'data': [('one', 'two')]}
        self.render_templates(defer, self.get_context(**update))

    def render_templates(self, templates, context):
        # type: (Iterable[Template], Dict[str, Any]) -> None
        for template in templates:
            template = get_template(template)
            try:
                template.render(context)
            except Exception:
                logging.error("Exception while rendering '{}'".format(template.template.name))
                raise

    def get_context(self, **kwargs):
        # type: (**Any) -> Dict[str, Any]
        """Get the dummy context for shallow testing.

        The context returned will always contain a parameter called
        `shallow_tested`, which tells the signal receiver that the
        test was not rendered in an actual logical test (so we can
        still do coverage reporting on which templates have a logical
        test).

        Note: `context` just holds dummy values used to make the test
        pass. This context only ensures that the templates do not
        throw a 500 error when rendered using dummy data.  If new
        required parameters are added to a template, this test will
        fail; the usual fix is to just update the context below to add
        the new parameter to the dummy data.

        :param kwargs: Keyword arguments can be used to update the base
            context.

        """
        email = "hamlet@zulip.com"
        user_profile = get_user_profile_by_email(email)

        context = dict(
            shallow_tested=True,
            user_profile=user_profile,
            user=user_profile,
            product_name='testing',
            form=DummyForm(
                full_name=get_form_value('John Doe'),
                terms=get_form_value(True),
                email=get_form_value(email),
            ),
            current_url=lambda: 'www.zulip.com',
            integrations_dict={},
            referrer=dict(
                full_name='John Doe',
                realm=dict(name='zulip.com'),
            ),
            uid='uid',
            token='token',
            message_count=0,
            messages=[dict(header='Header')],
            new_streams=dict(html=''),
            data=dict(title='Title'),
        )

        context.update(kwargs)
        return context

    def test_markdown_in_template(self):
        # type: () -> None
        template = get_template("tests/test_markdown.html")
        context = {
            'markdown_test_file': "zerver/tests/markdown/test_markdown.md"
        }
        content = template.render(context)

        content_sans_whitespace = content.replace(" ", "").replace('\n', '')
        self.assertEqual(content_sans_whitespace,
                         'header<h1>Hello!</h1><p>Thisissome<em>boldtext</em>.</p>footer')

    def test_custom_tos_template(self):
        # type: () -> None
        response = self.client_get("/terms/")
        self.assertEqual(response.status_code, 200)
        self.assert_in_response(u"Thanks for using our products and services (\"Services\"). ", response)
        self.assert_in_response(u"By using our Services, you are agreeing to these terms", response)
