# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

from __future__ import unicode_literals
import frappe
from frappe import _, msgprint
from frappe.utils import flt
from frappe.utils import formatdate
import time
from erpnext.accounts.utils import get_fiscal_year
from erpnext.controllers.trends import get_period_date_ranges, get_period_month_ranges

def execute(filters=None):
	if not filters: filters = {}

	columns = get_columns(filters)
	period_month_ranges = get_period_month_ranges(filters["period"], filters["fiscal_year"])
	cam_map = get_costcenter_account_month_map(filters)

	data = []
	for cost_center, cost_center_items in cam_map.items():
		for account, monthwise_data in cost_center_items.items():
			row = [cost_center, account]
			totals = [0, 0, 0]
			for relevant_months in period_month_ranges:
				period_data = [0, 0, 0]
				for month in relevant_months:
					month_data = monthwise_data.get(month, {})
					for i, fieldname in enumerate(["target", "actual", "variance"]):
						value = flt(month_data.get(fieldname))
						period_data[i] += value
						totals[i] += value
				period_data[2] = period_data[0] - period_data[1]
				row += period_data
			totals[2] = totals[0] - totals[1]
			row += totals
			data.append(row)

	return columns, sorted(data, key=lambda x: (x[0], x[1]))

def get_columns(filters):
	for fieldname in ["fiscal_year", "period", "company"]:
		if not filters.get(fieldname):
			label = (" ".join(fieldname.split("_"))).title()
			msgprint(_("Please specify") + ": " + label,
				raise_exception=True)

	columns = [_("Cost Center") + ":Link/Cost Center:120", _("Account") + ":Link/Account:120"]

	group_months = False if filters["period"] == "Monthly" else True

	for from_date, to_date in get_period_date_ranges(filters["period"], filters["fiscal_year"]):
		for label in [_("Target") + " (%s)", _("Actual") + " (%s)", _("Variance") + " (%s)"]:
			if group_months:
				label = label % (formatdate(from_date, format_string="MMM") + " - " + formatdate(from_date, format_string="MMM"))
			else:
				label = label % formatdate(from_date, format_string="MMM")

			columns.append(label+":Float:120")

	return columns + [_("Total Target") + ":Float:120", _("Total Actual") + ":Float:120",
		_("Total Variance") + ":Float:120"]

#Get cost center & target details
def get_costcenter_target_details(filters):
	return frappe.db.sql("""select cc.name, cc.distribution_id,
		cc.parent_cost_center, bd.account, bd.budget_allocated
		from `tabCost Center` cc, `tabBudget Detail` bd
		where bd.parent=cc.name and bd.fiscal_year=%s and
		cc.company=%s order by cc.name""" % ('%s', '%s'),
		(filters.get("fiscal_year"), filters.get("company")), as_dict=1)

#Get target distribution details of accounts of cost center
def get_target_distribution_details(filters):
	target_details = {}

	for d in frappe.db.sql("""select md.name, mdp.month, mdp.percentage_allocation
		from `tabMonthly Distribution Percentage` mdp, `tabMonthly Distribution` md
		where mdp.parent=md.name and md.fiscal_year=%s""", (filters["fiscal_year"]), as_dict=1):
			target_details.setdefault(d.name, {}).setdefault(d.month, flt(d.percentage_allocation))

	return target_details

#Get actual details from gl entry
def get_actual_details(filters):
	ac_details = frappe.db.sql("""select gl.account, gl.debit, gl.credit,
		gl.cost_center, MONTHNAME(gl.posting_date) as month_name
		from `tabGL Entry` gl, `tabBudget Detail` bd
		where gl.fiscal_year=%s and company=%s
		and bd.account=gl.account and bd.parent=gl.cost_center""" % ('%s', '%s'),
		(filters.get("fiscal_year"), filters.get("company")), as_dict=1)

	cc_actual_details = {}
	for d in ac_details:
		cc_actual_details.setdefault(d.cost_center, {}).setdefault(d.account, []).append(d)

	return cc_actual_details

def get_costcenter_account_month_map(filters):
	import datetime
	costcenter_target_details = get_costcenter_target_details(filters)
	tdd = get_target_distribution_details(filters)
	actual_details = get_actual_details(filters)

	cam_map = {}

	for ccd in costcenter_target_details:
		for month_id in range(1, 13):
			month = datetime.date(2013, month_id, 1).strftime('%B')

			cam_map.setdefault(ccd.name, {}).setdefault(ccd.account, {})\
				.setdefault(month, frappe._dict({
					"target": 0.0, "actual": 0.0
				}))

			tav_dict = cam_map[ccd.name][ccd.account][month]

			month_percentage = tdd.get(ccd.distribution_id, {}).get(month, 0) \
				if ccd.distribution_id else 100.0/12

			tav_dict.target = flt(ccd.budget_allocated) * month_percentage / 100

			for ad in actual_details.get(ccd.name, {}).get(ccd.account, []):
				if ad.month_name == month:
						tav_dict.actual += flt(ad.debit) - flt(ad.credit)

	return cam_map
