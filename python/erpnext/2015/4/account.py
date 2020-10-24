# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

from __future__ import unicode_literals
import frappe
from frappe.utils import cstr, cint
from frappe import throw, _
from frappe.model.document import Document

class Account(Document):
	nsm_parent_field = 'parent_account'

	def onload(self):
		frozen_accounts_modifier = frappe.db.get_value("Accounts Settings", "Accounts Settings",
			"frozen_accounts_modifier")
		if not frozen_accounts_modifier or frozen_accounts_modifier in frappe.user.get_roles():
			self.get("__onload").can_freeze_account = True

	def autoname(self):
		self.name = self.account_name.strip() + ' - ' + \
			frappe.db.get_value("Company", self.company, "abbr")

	def validate(self):
		self.validate_parent()
		self.validate_root_details()
		self.validate_mandatory()
		self.validate_warehouse_account()
		self.validate_frozen_accounts_modifier()
		self.validate_balance_must_be_debit_or_credit()

	def validate_parent(self):
		"""Fetch Parent Details and validate parent account"""
		if self.parent_account:
			par = frappe.db.get_value("Account", self.parent_account,
				["name", "is_group", "report_type", "root_type", "company"], as_dict=1)
			if not par:
				throw(_("Account {0}: Parent account {1} does not exist").format(self.name, self.parent_account))
			elif par.name == self.name:
				throw(_("Account {0}: You can not assign itself as parent account").format(self.name))
			elif not par.is_group:
				throw(_("Account {0}: Parent account {1} can not be a ledger").format(self.name, self.parent_account))
			elif par.company != self.company:
				throw(_("Account {0}: Parent account {1} does not belong to company: {2}")
					.format(self.name, self.parent_account, self.company))

			if par.report_type:
				self.report_type = par.report_type
			if par.root_type:
				self.root_type = par.root_type

	def validate_root_details(self):
		#does not exists parent
		if frappe.db.exists("Account", self.name):
			if not frappe.db.get_value("Account", self.name, "parent_account"):
				throw(_("Root cannot be edited."))

	def validate_frozen_accounts_modifier(self):
		old_value = frappe.db.get_value("Account", self.name, "freeze_account")
		if old_value and old_value != self.freeze_account:
			frozen_accounts_modifier = frappe.db.get_value('Accounts Settings', None, 'frozen_accounts_modifier')
			if not frozen_accounts_modifier or \
				frozen_accounts_modifier not in frappe.user.get_roles():
					throw(_("You are not authorized to set Frozen value"))

	def validate_balance_must_be_debit_or_credit(self):
		from erpnext.accounts.utils import get_balance_on
		if not self.get("__islocal") and self.balance_must_be:
			account_balance = get_balance_on(self.name)

			if account_balance > 0 and self.balance_must_be == "Credit":
				frappe.throw(_("Account balance already in Debit, you are not allowed to set 'Balance Must Be' as 'Credit'"))
			elif account_balance < 0 and self.balance_must_be == "Debit":
				frappe.throw(_("Account balance already in Credit, you are not allowed to set 'Balance Must Be' as 'Debit'"))

	def convert_group_to_ledger(self):
		if self.check_if_child_exists():
			throw(_("Account with child nodes cannot be converted to ledger"))
		elif self.check_gle_exists():
			throw(_("Account with existing transaction cannot be converted to ledger"))
		else:
			self.is_group = 0
			self.save()
			return 1

	def convert_ledger_to_group(self):
		if self.check_gle_exists():
			throw(_("Account with existing transaction can not be converted to group."))
		elif self.account_type:
			throw(_("Cannot covert to Group because Account Type is selected."))
		else:
			self.is_group = 1
			self.save()
			return 1

	# Check if any previous balance exists
	def check_gle_exists(self):
		return frappe.db.get_value("GL Entry", {"account": self.name})

	def check_if_child_exists(self):
		return frappe.db.sql("""select name from `tabAccount` where parent_account = %s
			and docstatus != 2""", self.name)

	def validate_mandatory(self):
		if not self.report_type:
			throw(_("Report Type is mandatory"))

		if not self.root_type:
			throw(_("Root Type is mandatory"))

	def validate_warehouse_account(self):
		if not cint(frappe.defaults.get_global_default("auto_accounting_for_stock")):
			return

		if self.account_type == "Warehouse":
			if not self.warehouse:
				throw(_("Warehouse is mandatory if account type is Warehouse"))

			old_warehouse = cstr(frappe.db.get_value("Account", self.name, "warehouse"))
			if old_warehouse != cstr(self.warehouse):
				if old_warehouse:
					self.validate_warehouse(old_warehouse)
				if self.warehouse:
					self.validate_warehouse(self.warehouse)

	def validate_warehouse(self, warehouse):
		if frappe.db.get_value("Stock Ledger Entry", {"warehouse": warehouse}):
			throw(_("Stock entries exist against warehouse {0}, hence you cannot re-assign or modify Warehouse").format(warehouse))

	def update_nsm_model(self):
		"""update lft, rgt indices for nested set model"""
		import frappe
		import frappe.utils.nestedset
		frappe.utils.nestedset.update_nsm(self)

	def on_update(self):
		self.update_nsm_model()

	def validate_trash(self):
		"""checks gl entries and if child exists"""
		if not self.parent_account:
			throw(_("Root account can not be deleted"))

		if self.check_gle_exists():
			throw(_("Account with existing transaction can not be deleted"))
		if self.check_if_child_exists():
			throw(_("Child account exists for this account. You can not delete this account."))

	def on_trash(self):
		self.validate_trash()
		self.update_nsm_model()

	def before_rename(self, old, new, merge=False):
		# Add company abbr if not provided
		from erpnext.setup.doctype.company.company import get_name_with_abbr
		new_account = get_name_with_abbr(new, self.company)

		# Validate properties before merging
		if merge:
			if not frappe.db.exists("Account", new):
				throw(_("Account {0} does not exist").format(new))

			val = list(frappe.db.get_value("Account", new_account,
				["is_group", "root_type", "company"]))

			if val != [self.is_group, self.root_type, self.company]:
				throw(_("""Merging is only possible if following properties are same in both records. Is Group, Root Type, Company"""))

		return new_account

	def after_rename(self, old, new, merge=False):
		if not merge:
			frappe.db.set_value("Account", new, "account_name",
				" - ".join(new.split(" - ")[:-1]))
		else:
			from frappe.utils.nestedset import rebuild_tree
			rebuild_tree("Account", "parent_account")

def get_parent_account(doctype, txt, searchfield, start, page_len, filters):
	return frappe.db.sql("""select name from tabAccount
		where is_group = 1 and docstatus != 2 and company = %s
		and %s like %s order by name limit %s, %s""" %
		("%s", searchfield, "%s", "%s", "%s"),
		(filters["company"], "%%%s%%" % txt, start, page_len), as_list=1)
