# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

from __future__ import unicode_literals
import frappe
from frappe import _
from frappe.model.document import Document

class Batch(Document):

	def validate(self):
		self.item_has_batch_enabled()

	def item_has_batch_enabled(self):
		if frappe.db.get_value("Item",self.item,"has_batch_no") == 0:
			frappe.throw(_("The selected item cannot have Batch"))
