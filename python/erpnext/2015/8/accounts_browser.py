# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

from __future__ import unicode_literals
import frappe
import frappe.defaults
from frappe.utils import flt
from erpnext.accounts.utils import get_balance_on
from erpnext.accounts.report.financial_statements import sort_root_accounts

@frappe.whitelist()
def get_companies():
	"""get a list of companies based on permission"""
	return [d.name for d in frappe.get_list("Company", fields=["name"],
		order_by="name")]

@frappe.whitelist()
def get_children():
	args = frappe.local.form_dict
	ctype, company = args['ctype'], args['comp']

	# root
	if args['parent'] in ("Accounts", "Cost Centers"):
		select_cond = ", root_type, report_type" if args["parent"]=="Accounts" else ""

		acc = frappe.db.sql(""" select
			name as value, is_group as expandable %s
			from `tab%s`
			where ifnull(parent_%s,'') = ''
			and `company` = %s	and docstatus<2
			order by name""" % (select_cond, ctype, ctype.lower().replace(' ','_'), '%s'),
				company, as_dict=1)

		if args["parent"]=="Accounts":
			sort_root_accounts(acc)
	else:
		# other
		acc = frappe.db.sql("""select
			name as value, is_group as expandable
	 		from `tab%s`
			where ifnull(parent_%s,'') = %s
			and docstatus<2
			order by name""" % (ctype, ctype.lower().replace(' ','_'), '%s'),
				args['parent'], as_dict=1)

	if ctype == 'Account':
		currency = frappe.db.sql("select default_currency from `tabCompany` where name = %s", company)[0][0]
		for each in acc:
			bal = get_balance_on(each.get("value"))
			each["currency"] = currency
			each["balance"] = flt(bal)

	return acc
