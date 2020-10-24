from __future__ import unicode_literals
from frappe import _

def get_data():
	return [
		{
			"label": _("Issues"),
			"items": [
				{
					"type": "doctype",
					"name": "Issue",
					"description": _("Support queries from customers."),
				},
				{
					"type": "doctype",
					"name": "Communication",
					"description": _("Communication log."),
				},
			]
		},
		{
			"label": _("Maintenance"),
			"items": [
				{
					"type": "doctype",
					"name": "Maintenance Schedule",
					"description": _("Plan for maintenance visits."),
				},
				{
					"type": "doctype",
					"name": "Maintenance Visit",
					"description": _("Visit report for maintenance call."),
				},
				{
					"type": "report",
					"name": "Maintenance Schedules",
					"is_query_report": True,
					"doctype": "Maintenance Schedule"
				},
			]
		},
		{
			"label": _("Warranty"),
			"items": [
				{
					"type": "doctype",
					"name": "Warranty Claim",
					"description": _("Warranty Claim against Serial No."),
				},
				{
					"type": "doctype",
					"name": "Serial No",
					"description": _("Single unit of an Item."),
				},
			]
		},
		{
			"label": _("Reports"),
			"icon": "icon-list",
			"items": [
				{
					"type": "page",
					"name": "support-analytics",
					"label": _("Support Analytics"),
					"icon": "icon-bar-chart"
				},
			]
		},
	]
