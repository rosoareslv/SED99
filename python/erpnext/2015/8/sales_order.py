# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

from __future__ import unicode_literals
import frappe
import json
import frappe.utils
from frappe.utils import cstr, flt, getdate, comma_and, cint
from frappe import _
from frappe.model.mapper import get_mapped_doc
from erpnext.stock.stock_balance import update_bin_qty, get_reserved_qty
from frappe.desk.notifications import clear_doctype_notifications

from erpnext.controllers.selling_controller import SellingController

form_grid_templates = {
	"items": "templates/form_grid/item_grid.html"
}

class WarehouseRequired(frappe.ValidationError): pass

class SalesOrder(SellingController):
	def validate_mandatory(self):
		# validate transaction date v/s delivery date
		if self.delivery_date:
			if getdate(self.transaction_date) > getdate(self.delivery_date):
				frappe.throw(_("Expected Delivery Date cannot be before Sales Order Date"))

	def validate_po(self):
		# validate p.o date v/s delivery date
		if self.po_date and self.delivery_date and getdate(self.po_date) > getdate(self.delivery_date):
			frappe.throw(_("Expected Delivery Date cannot be before Purchase Order Date"))

		if self.po_no and self.customer:
			so = frappe.db.sql("select name from `tabSales Order` \
				where ifnull(po_no, '') = %s and name != %s and docstatus < 2\
				and customer = %s", (self.po_no, self.name, self.customer))
			if so and so[0][0] and not \
				cint(frappe.db.get_single_value("Selling Settings", "allow_against_multiple_purchase_orders")):
				frappe.msgprint(_("Warning: Sales Order {0} already exists against Customer's Purchase Order {1}").format(so[0][0], self.po_no))

	def validate_for_items(self):
		check_list = []
		for d in self.get('items'):
			check_list.append(cstr(d.item_code))

			if (frappe.db.get_value("Item", d.item_code, "is_stock_item")==1 or
				(self.has_product_bundle(d.item_code) and self.product_bundle_has_stock_item(d.item_code))) \
				and not d.warehouse:
				frappe.throw(_("Delivery warehouse required for stock item {0}").format(d.item_code),
					WarehouseRequired)

			# used for production plan
			d.transaction_date = self.transaction_date

			tot_avail_qty = frappe.db.sql("select projected_qty from `tabBin` \
				where item_code = %s and warehouse = %s", (d.item_code,d.warehouse))
			d.projected_qty = tot_avail_qty and flt(tot_avail_qty[0][0]) or 0

		# check for same entry multiple times
		unique_chk_list = set(check_list)
		if len(unique_chk_list) != len(check_list) and \
			not cint(frappe.db.get_single_value("Selling Settings", "allow_multiple_items")):
			frappe.msgprint(_("Warning: Same item has been entered multiple times."))

	def product_bundle_has_stock_item(self, product_bundle):
		"""Returns true if product bundle has stock item"""
		ret = len(frappe.db.sql("""select i.name from tabItem i, `tabProduct Bundle Item` pbi
			where pbi.parent = %s and pbi.item_code = i.name and i.is_stock_item = 1""", product_bundle))
		return ret

	def validate_sales_mntc_quotation(self):
		for d in self.get('items'):
			if d.prevdoc_docname:
				res = frappe.db.sql("select name from `tabQuotation` where name=%s and order_type = %s", (d.prevdoc_docname, self.order_type))
				if not res:
					frappe.msgprint(_("Quotation {0} not of type {1}").format(d.prevdoc_docname, self.order_type))

	def validate_order_type(self):
		super(SalesOrder, self).validate_order_type()

	def validate_delivery_date(self):
		if self.order_type == 'Sales' and not self.delivery_date:
			frappe.throw(_("Please enter 'Expected Delivery Date'"))

		self.validate_sales_mntc_quotation()

	def validate_proj_cust(self):
		if self.project_name and self.customer_name:
			res = frappe.db.sql("""select name from `tabProject` where name = %s
				and (customer = %s or ifnull(customer,'')='')""",
					(self.project_name, self.customer))
			if not res:
				frappe.throw(_("Customer {0} does not belong to project {1}").format(self.customer, self.project_name))

	def validate(self):
		super(SalesOrder, self).validate()

		self.validate_order_type()
		self.validate_delivery_date()
		self.validate_mandatory()
		self.validate_proj_cust()
		self.validate_po()
		self.validate_uom_is_integer("stock_uom", "qty")
		self.validate_for_items()
		self.validate_warehouse()

		from erpnext.stock.doctype.packed_item.packed_item import make_packing_list
		make_packing_list(self,'items')

		self.validate_with_previous_doc()

		if not self.status:
			self.status = "Draft"

		from erpnext.controllers.status_updater import validate_status
		validate_status(self.status, ["Draft", "Submitted", "Stopped",
			"Cancelled"])

		if not self.billing_status: self.billing_status = 'Not Billed'
		if not self.delivery_status: self.delivery_status = 'Not Delivered'

	def validate_warehouse(self):
		from erpnext.stock.utils import validate_warehouse_company

		warehouses = list(set([d.warehouse for d in
			self.get("items") if d.warehouse]))

		for w in warehouses:
			validate_warehouse_company(w, self.company)

	def validate_with_previous_doc(self):
		super(SalesOrder, self).validate_with_previous_doc({
			"Quotation": {
				"ref_dn_field": "prevdoc_docname",
				"compare_fields": [["company", "="], ["currency", "="]]
			}
		})


	def update_enquiry_status(self, prevdoc, flag):
		enq = frappe.db.sql("select t2.prevdoc_docname from `tabQuotation` t1, `tabQuotation Item` t2 where t2.parent = t1.name and t1.name=%s", prevdoc)
		if enq:
			frappe.db.sql("update `tabOpportunity` set status = %s where name=%s",(flag,enq[0][0]))

	def update_prevdoc_status(self, flag):
		for quotation in list(set([d.prevdoc_docname for d in self.get("items")])):
			if quotation:
				doc = frappe.get_doc("Quotation", quotation)
				if doc.docstatus==2:
					frappe.throw(_("Quotation {0} is cancelled").format(quotation))

				doc.set_status(update=True)
				doc.update_opportunity()

	def on_submit(self):
		super(SalesOrder, self).on_submit()

		self.check_credit_limit()
		self.update_reserved_qty()

		frappe.get_doc('Authorization Control').validate_approving_authority(self.doctype, self.base_grand_total, self)

		self.update_prevdoc_status('submit')
		frappe.db.set(self, 'status', 'Submitted')

	def on_cancel(self):
		# Cannot cancel stopped SO
		if self.status == 'Stopped':
			frappe.throw(_("Stopped order cannot be cancelled. Unstop to cancel."))

		self.check_nextdoc_docstatus()
		self.update_reserved_qty()

		self.update_prevdoc_status('cancel')

		frappe.db.set(self, 'status', 'Cancelled')

	def check_nextdoc_docstatus(self):
		# Checks Delivery Note
		submit_dn = frappe.db.sql_list("""select t1.name from `tabDelivery Note` t1,`tabDelivery Note Item` t2
			where t1.name = t2.parent and t2.against_sales_order = %s and t1.docstatus = 1""", self.name)
		if submit_dn:
			frappe.throw(_("Delivery Notes {0} must be cancelled before cancelling this Sales Order").format(comma_and(submit_dn)))

		# Checks Sales Invoice
		submit_rv = frappe.db.sql_list("""select t1.name
			from `tabSales Invoice` t1,`tabSales Invoice Item` t2
			where t1.name = t2.parent and t2.sales_order = %s and t1.docstatus = 1""",
			self.name)
		if submit_rv:
			frappe.throw(_("Sales Invoice {0} must be cancelled before cancelling this Sales Order").format(comma_and(submit_rv)))

		#check maintenance schedule
		submit_ms = frappe.db.sql_list("""select t1.name from `tabMaintenance Schedule` t1,
			`tabMaintenance Schedule Item` t2
			where t2.parent=t1.name and t2.prevdoc_docname = %s and t1.docstatus = 1""", self.name)
		if submit_ms:
			frappe.throw(_("Maintenance Schedule {0} must be cancelled before cancelling this Sales Order").format(comma_and(submit_ms)))

		# check maintenance visit
		submit_mv = frappe.db.sql_list("""select t1.name from `tabMaintenance Visit` t1, `tabMaintenance Visit Purpose` t2
			where t2.parent=t1.name and t2.prevdoc_docname = %s and t1.docstatus = 1""",self.name)
		if submit_mv:
			frappe.throw(_("Maintenance Visit {0} must be cancelled before cancelling this Sales Order").format(comma_and(submit_mv)))

		# check production order
		pro_order = frappe.db.sql_list("""select name from `tabProduction Order`
			where sales_order = %s and docstatus = 1""", self.name)
		if pro_order:
			frappe.throw(_("Production Order {0} must be cancelled before cancelling this Sales Order").format(comma_and(pro_order)))

	def check_modified_date(self):
		mod_db = frappe.db.get_value("Sales Order", self.name, "modified")
		date_diff = frappe.db.sql("select TIMEDIFF('%s', '%s')" %
			( mod_db, cstr(self.modified)))
		if date_diff and date_diff[0][0]:
			frappe.throw(_("{0} {1} has been modified. Please refresh.").format(self.doctype, self.name))

	def stop_sales_order(self):
		self.check_modified_date()
		frappe.db.set(self, 'status', 'Stopped')
		self.update_reserved_qty()
		frappe.msgprint(_("{0} {1} status is Stopped").format(self.doctype, self.name))
		self.notify_modified()
		clear_doctype_notifications(self)

	def unstop_sales_order(self):
		self.check_modified_date()
		frappe.db.set(self, 'status', 'Submitted')
		self.update_reserved_qty()
		frappe.msgprint(_("{0} {1} status is Unstopped").format(self.doctype, self.name))
		clear_doctype_notifications(self)

	def update_reserved_qty(self, so_item_rows=None):
		"""update requested qty (before ordered_qty is updated)"""
		item_wh_list = []
		def _valid_for_reserve(item_code, warehouse):
			if item_code and warehouse and [item_code, warehouse] not in item_wh_list \
				and frappe.db.get_value("Item", item_code, "is_stock_item"):
					item_wh_list.append([item_code, warehouse])

		for d in self.get("items"):
			if (not so_item_rows or d.name in so_item_rows):
				_valid_for_reserve(d.item_code, d.warehouse)

				if self.has_product_bundle(d.item_code):
					for p in self.get("packed_items"):
						if p.parent_detail_docname == d.name and p.parent_item == d.item_code:
							_valid_for_reserve(p.item_code, p.warehouse)

		for item_code, warehouse in item_wh_list:
			update_bin_qty(item_code, warehouse, {
				"reserved_qty": get_reserved_qty(item_code, warehouse)
			})

	def on_update(self):
		pass

def get_list_context(context=None):
	from erpnext.controllers.website_list_for_contact import get_list_context
	list_context = get_list_context(context)
	list_context["title"] = _("My Orders")
	return list_context

@frappe.whitelist()
def stop_or_unstop_sales_orders(names, status):
	if not frappe.has_permission("Sales Order", "write"):
		frappe.throw(_("Not permitted"), frappe.PermissionError)

	names = json.loads(names)
	for name in names:
		so = frappe.get_doc("Sales Order", name)
		if so.docstatus == 1:
			if status=="Stop":
				if so.status not in ("Stopped", "Cancelled") and (so.per_delivered < 100 or so.per_billed < 100):
					so.stop_sales_order()
			else:
				if so.status == "Stopped":
					so.unstop_sales_order()

	frappe.local.message_log = []

	def before_recurring(self):
		super(SalesOrder, self).before_recurring()

		for field in ("delivery_status", "per_delivered", "billing_status", "per_billed"):
			self.set(field, None)

		for d in self.get("items"):
			for field in ("delivered_qty", "billed_amt", "planned_qty", "prevdoc_docname"):
				d.set(field, None)


@frappe.whitelist()
def make_material_request(source_name, target_doc=None):
	def postprocess(source, doc):
		doc.material_request_type = "Purchase"

	so = frappe.get_doc("Sales Order", source_name)

	item_table = "Packed Item" if so.packed_items else "Sales Order Item"

	doc = get_mapped_doc("Sales Order", source_name, {
		"Sales Order": {
			"doctype": "Material Request",
			"validation": {
				"docstatus": ["=", 1]
			}
		},
		item_table: {
			"doctype": "Material Request Item",
			"field_map": {
				"parent": "sales_order_no",
				"stock_uom": "uom"
			}
		}
	}, target_doc, postprocess)

	return doc

@frappe.whitelist()
def make_delivery_note(source_name, target_doc=None):
	def set_missing_values(source, target):
		if source.po_no:
			if target.po_no:
				target_po_no = target.po_no.split(", ")
				target_po_no.append(source.po_no)
				target.po_no = ", ".join(list(set(target_po_no))) if len(target_po_no) > 1 else target_po_no[0]
			else:
				target.po_no = source.po_no

		target.ignore_pricing_rule = 1
		target.run_method("set_missing_values")
		target.run_method("calculate_taxes_and_totals")

	def update_item(source, target, source_parent):
		target.base_amount = (flt(source.qty) - flt(source.delivered_qty)) * flt(source.base_rate)
		target.amount = (flt(source.qty) - flt(source.delivered_qty)) * flt(source.rate)
		target.qty = flt(source.qty) - flt(source.delivered_qty)

	target_doc = get_mapped_doc("Sales Order", source_name, {
		"Sales Order": {
			"doctype": "Delivery Note",
			"validation": {
				"docstatus": ["=", 1]
			}
		},
		"Sales Order Item": {
			"doctype": "Delivery Note Item",
			"field_map": {
				"rate": "rate",
				"name": "so_detail",
				"parent": "against_sales_order",
			},
			"postprocess": update_item,
			"condition": lambda doc: doc.delivered_qty < doc.qty
		},
		"Sales Taxes and Charges": {
			"doctype": "Sales Taxes and Charges",
			"add_if_empty": True
		},
		"Sales Team": {
			"doctype": "Sales Team",
			"add_if_empty": True
		}
	}, target_doc, set_missing_values)

	return target_doc

@frappe.whitelist()
def make_sales_invoice(source_name, target_doc=None):
	def postprocess(source, target):
		set_missing_values(source, target)
		#Get the advance paid Journal Entries in Sales Invoice Advance
		target.get_advances()

	def set_missing_values(source, target):
		target.is_pos = 0
		target.ignore_pricing_rule = 1
		target.run_method("set_missing_values")
		target.run_method("calculate_taxes_and_totals")

	def update_item(source, target, source_parent):
		target.amount = flt(source.amount) - flt(source.billed_amt)
		target.base_amount = target.amount * flt(source_parent.conversion_rate)
		target.qty = target.amount / flt(source.rate) if (source.rate and source.billed_amt) else source.qty

	doclist = get_mapped_doc("Sales Order", source_name, {
		"Sales Order": {
			"doctype": "Sales Invoice",
			"validation": {
				"docstatus": ["=", 1]
			}
		},
		"Sales Order Item": {
			"doctype": "Sales Invoice Item",
			"field_map": {
				"name": "so_detail",
				"parent": "sales_order",
			},
			"postprocess": update_item,
			"condition": lambda doc: doc.base_amount==0 or doc.billed_amt < doc.amount
		},
		"Sales Taxes and Charges": {
			"doctype": "Sales Taxes and Charges",
			"add_if_empty": True
		},
		"Sales Team": {
			"doctype": "Sales Team",
			"add_if_empty": True
		}
	}, target_doc, postprocess)

	return doclist

@frappe.whitelist()
def make_maintenance_schedule(source_name, target_doc=None):
	maint_schedule = frappe.db.sql("""select t1.name
		from `tabMaintenance Schedule` t1, `tabMaintenance Schedule Item` t2
		where t2.parent=t1.name and t2.prevdoc_docname=%s and t1.docstatus=1""", source_name)

	if not maint_schedule:
		doclist = get_mapped_doc("Sales Order", source_name, {
			"Sales Order": {
				"doctype": "Maintenance Schedule",
				"field_map": {
					"name": "sales_order_no"
				},
				"validation": {
					"docstatus": ["=", 1]
				}
			},
			"Sales Order Item": {
				"doctype": "Maintenance Schedule Item",
				"field_map": {
					"parent": "prevdoc_docname"
				},
				"add_if_empty": True
			}
		}, target_doc)

		return doclist

@frappe.whitelist()
def make_maintenance_visit(source_name, target_doc=None):
	visit = frappe.db.sql("""select t1.name
		from `tabMaintenance Visit` t1, `tabMaintenance Visit Purpose` t2
		where t2.parent=t1.name and t2.prevdoc_docname=%s
		and t1.docstatus=1 and t1.completion_status='Fully Completed'""", source_name)

	if not visit:
		doclist = get_mapped_doc("Sales Order", source_name, {
			"Sales Order": {
				"doctype": "Maintenance Visit",
				"field_map": {
					"name": "sales_order_no"
				},
				"validation": {
					"docstatus": ["=", 1]
				}
			},
			"Sales Order Item": {
				"doctype": "Maintenance Visit Purpose",
				"field_map": {
					"parent": "prevdoc_docname",
					"parenttype": "prevdoc_doctype"
				},
				"add_if_empty": True
			}
		}, target_doc)

		return doclist
