# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

# For license information, please see license.txt

from __future__ import unicode_literals
import frappe

from frappe.website.website_generator import WebsiteGenerator
from frappe.utils import quoted
from frappe.utils.user import get_fullname_and_avatar
from frappe import _

class JobOpening(WebsiteGenerator):
	website = frappe._dict(
		template = "templates/generators/job_opening.html",
		condition_field = "publish",
		page_title_field = "job_title",
	)
	
	def get_route(self):
		return 'jobs/' + quoted(self.page_name)

	def get_context(self, context):
		context.parents = [{'name': 'jobs', 'title': _('All Jobs') }]

def get_list_context(context):
	context.title = _("Jobs")
	context.introduction = _('Current Job Openings')
	context.show_sidebar=True
	context.show_search=True
