# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt
from __future__ import unicode_literals
import frappe
from frappe.utils import flt, add_days
import frappe.permissions
import unittest
from erpnext.selling.doctype.sales_order.sales_order \
	import make_material_request, make_delivery_note, make_sales_invoice, WarehouseRequired

class TestSalesOrder(unittest.TestCase):
	def tearDown(self):
		frappe.set_user("Administrator")

	def test_make_material_request(self):
		so = make_sales_order(do_not_submit=True)

		self.assertRaises(frappe.ValidationError, make_material_request, so.name)

		so.submit()
		mr = make_material_request(so.name)

		self.assertEquals(mr.material_request_type, "Purchase")
		self.assertEquals(len(mr.get("items")), len(so.get("items")))

	def test_make_delivery_note(self):
		so = make_sales_order(do_not_submit=True)

		self.assertRaises(frappe.ValidationError, make_delivery_note, so.name)

		so.submit()
		dn = make_delivery_note(so.name)

		self.assertEquals(dn.doctype, "Delivery Note")
		self.assertEquals(len(dn.get("items")), len(so.get("items")))

	def test_make_sales_invoice(self):
		so = make_sales_order(do_not_submit=True)

		self.assertRaises(frappe.ValidationError, make_sales_invoice, so.name)

		so.submit()
		si = make_sales_invoice(so.name)

		self.assertEquals(len(si.get("items")), len(so.get("items")))
		self.assertEquals(len(si.get("items")), 1)

		si.insert()
		si.submit()

		si1 = make_sales_invoice(so.name)
		self.assertEquals(len(si1.get("items")), 0)

	def test_update_qty(self):
		so = make_sales_order()

		create_dn_against_so(so.name, 6)

		so.load_from_db()
		self.assertEquals(so.get("items")[0].delivered_qty, 6)

		# Check delivered_qty after make_sales_invoice without update_stock checked
		si1 = make_sales_invoice(so.name)
		si1.get("items")[0].qty = 6
		si1.insert()
		si1.submit()

		so.load_from_db()
		self.assertEquals(so.get("items")[0].delivered_qty, 6)

		# Check delivered_qty after make_sales_invoice with update_stock checked
		si2 = make_sales_invoice(so.name)
		si2.set("update_stock", 1)
		si2.get("items")[0].qty = 3
		si2.insert()
		si2.submit()

		so.load_from_db()
		self.assertEquals(so.get("items")[0].delivered_qty, 9)

	def test_reserved_qty_for_partial_delivery(self):
		existing_reserved_qty = get_reserved_qty()

		so = make_sales_order()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty + 10)

		dn = create_dn_against_so(so.name)
		self.assertEqual(get_reserved_qty(), existing_reserved_qty + 5)

		# stop so
		so.load_from_db()
		so.stop_sales_order()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty)

		# unstop so
		so.load_from_db()
		so.unstop_sales_order()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty + 5)

		dn.cancel()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty + 10)

		# cancel
		so.load_from_db()
		so.cancel()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty)

	def test_reserved_qty_for_over_delivery(self):
		# set over-delivery tolerance
		frappe.db.set_value('Item', "_Test Item", 'tolerance', 50)

		existing_reserved_qty = get_reserved_qty()

		so = make_sales_order()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty + 10)


		dn = create_dn_against_so(so.name, 15)
		self.assertEqual(get_reserved_qty(), existing_reserved_qty)

		dn.cancel()
		self.assertEqual(get_reserved_qty(), existing_reserved_qty + 10)

	def test_reserved_qty_for_partial_delivery_with_packing_list(self):
		existing_reserved_qty_item1 = get_reserved_qty("_Test Item")
		existing_reserved_qty_item2 = get_reserved_qty("_Test Item Home Desktop 100")

		so = make_sales_order(item_code="_Test Product Bundle Item")

		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1 + 50)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2 + 20)

		dn = create_dn_against_so(so.name)

		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1 + 25)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2 + 10)

		# stop so
		so.load_from_db()
		so.stop_sales_order()

		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"), existing_reserved_qty_item2)

		# unstop so
		so.load_from_db()
		so.unstop_sales_order()

		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1 + 25)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2 + 10)

		dn.cancel()
		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1 + 50)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2 + 20)

		so.load_from_db()
		so.cancel()
		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"), existing_reserved_qty_item2)

	def test_reserved_qty_for_over_delivery_with_packing_list(self):
		# set over-delivery tolerance
		frappe.db.set_value('Item', "_Test Product Bundle Item", 'tolerance', 50)

		existing_reserved_qty_item1 = get_reserved_qty("_Test Item")
		existing_reserved_qty_item2 = get_reserved_qty("_Test Item Home Desktop 100")

		so = make_sales_order(item_code="_Test Product Bundle Item")

		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1 + 50)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2 + 20)

		dn = create_dn_against_so(so.name, 15)

		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2)

		dn.cancel()
		self.assertEqual(get_reserved_qty("_Test Item"), existing_reserved_qty_item1 + 50)
		self.assertEqual(get_reserved_qty("_Test Item Home Desktop 100"),
			existing_reserved_qty_item2 + 20)

	def test_warehouse_user(self):
		frappe.permissions.add_user_permission("Warehouse", "_Test Warehouse 1 - _TC", "test@example.com")
		frappe.permissions.add_user_permission("Warehouse", "_Test Warehouse 2 - _TC1", "test2@example.com")
		frappe.permissions.add_user_permission("Company", "_Test Company 1", "test2@example.com")

		test_user = frappe.get_doc("User", "test@example.com")
		test_user.add_roles("Sales User", "Stock User")
		test_user.remove_roles("Sales Manager")

		test_user_2 = frappe.get_doc("User", "test2@example.com")
		test_user_2.add_roles("Sales User", "Stock User")
		test_user_2.remove_roles("Sales Manager")

		frappe.set_user("test@example.com")

		so = make_sales_order(company="_Test Company 1",
			warehouse="_Test Warehouse 2 - _TC1", do_not_save=True)
		so.conversion_rate = 0.02
		so.plc_conversion_rate = 0.02
		self.assertRaises(frappe.PermissionError, so.insert)

		frappe.set_user("test2@example.com")
		so.insert()

		frappe.permissions.remove_user_permission("Warehouse", "_Test Warehouse 1 - _TC", "test@example.com")
		frappe.permissions.remove_user_permission("Warehouse", "_Test Warehouse 2 - _TC1", "test2@example.com")
		frappe.permissions.remove_user_permission("Company", "_Test Company 1", "test2@example.com")

	def test_block_delivery_note_against_cancelled_sales_order(self):
		so = make_sales_order()

		dn = make_delivery_note(so.name)
		dn.insert()

		so.cancel()

		self.assertRaises(frappe.CancelledLinkError, dn.submit)

	def test_service_type_product_bundle(self):
		from erpnext.stock.doctype.item.test_item import make_item
		from erpnext.selling.doctype.product_bundle.test_product_bundle import make_product_bundle

		make_item("_Test Service Product Bundle", {"is_stock_item": 0, "is_sales_item": 1})
		make_item("_Test Service Product Bundle Item 1", {"is_stock_item": 0, "is_sales_item": 1})
		make_item("_Test Service Product Bundle Item 2", {"is_stock_item": 0, "is_sales_item": 1})

		make_product_bundle("_Test Service Product Bundle",
			["_Test Service Product Bundle Item 1", "_Test Service Product Bundle Item 2"])

		so = make_sales_order(item_code = "_Test Service Product Bundle", warehouse=None)

		self.assertTrue("_Test Service Product Bundle Item 1" in [d.item_code for d in so.packed_items])
		self.assertTrue("_Test Service Product Bundle Item 2" in [d.item_code for d in so.packed_items])

	def test_mix_type_product_bundle(self):
		from erpnext.stock.doctype.item.test_item import make_item
		from erpnext.selling.doctype.product_bundle.test_product_bundle import make_product_bundle

		make_item("_Test Mix Product Bundle", {"is_stock_item": 0, "is_sales_item": 1})
		make_item("_Test Mix Product Bundle Item 1", {"is_stock_item": 1, "is_sales_item": 1})
		make_item("_Test Mix Product Bundle Item 2", {"is_stock_item": 0, "is_sales_item": 1})

		make_product_bundle("_Test Mix Product Bundle",
			["_Test Mix Product Bundle Item 1", "_Test Mix Product Bundle Item 2"])

		self.assertRaises(WarehouseRequired, make_sales_order, item_code = "_Test Mix Product Bundle", warehouse="")

	def test_auto_insert_price(self):
		from erpnext.stock.doctype.item.test_item import make_item
		make_item("_Test Item for Auto Price List", {"is_stock_item": 0, "is_sales_item": 1})
		frappe.db.set_value("Stock Settings", None, "auto_insert_price_list_rate_if_missing", 1)

		item_price = frappe.db.get_value("Item Price", {"price_list": "_Test Price List",
			"item_code": "_Test Item for Auto Price List"})
		if item_price:
			frappe.delete_doc("Item Price", item_price)

		make_sales_order(item_code = "_Test Item for Auto Price List", selling_price_list="_Test Price List", rate=100)

		self.assertEquals(frappe.db.get_value("Item Price",
			{"price_list": "_Test Price List", "item_code": "_Test Item for Auto Price List"}, "price_list_rate"), 100)


		# do not update price list
		frappe.db.set_value("Stock Settings", None, "auto_insert_price_list_rate_if_missing", 0)

		item_price = frappe.db.get_value("Item Price", {"price_list": "_Test Price List",
			"item_code": "_Test Item for Auto Price List"})
		if item_price:
			frappe.delete_doc("Item Price", item_price)

		make_sales_order(item_code = "_Test Item for Auto Price List", selling_price_list="_Test Price List", rate=100)

		self.assertEquals(frappe.db.get_value("Item Price",
			{"price_list": "_Test Price List", "item_code": "_Test Item for Auto Price List"}, "price_list_rate"), None)

		frappe.db.set_value("Stock Settings", None, "auto_insert_price_list_rate_if_missing", 1)

def make_sales_order(**args):
	so = frappe.new_doc("Sales Order")
	args = frappe._dict(args)
	if args.transaction_date:
		so.transaction_date = args.transaction_date

	so.company = args.company or "_Test Company"
	so.customer = args.customer or "_Test Customer"
	so.delivery_date = add_days(so.transaction_date, 10)
	so.currency = args.currency or "INR"
	if args.selling_price_list:
		so.selling_price_list = args.selling_price_list

	if "warehouse" not in args:
		args.warehouse = "_Test Warehouse - _TC"

	so.append("items", {
		"item_code": args.item or args.item_code or "_Test Item",
		"warehouse": args.warehouse,
		"qty": args.qty or 10,
		"rate": args.rate or 100,
		"conversion_factor": 1.0,
	})

	if not args.do_not_save:
		so.insert()
		if not args.do_not_submit:
			so.submit()

	return so

def create_dn_against_so(so, delivered_qty=0):
	frappe.db.set_value("Stock Settings", None, "allow_negative_stock", 1)

	dn = make_delivery_note(so)
	dn.get("items")[0].qty = delivered_qty or 5
	dn.insert()
	dn.submit()
	return dn

def get_reserved_qty(item_code="_Test Item", warehouse="_Test Warehouse - _TC"):
	return flt(frappe.db.get_value("Bin", {"item_code": item_code, "warehouse": warehouse},
		"reserved_qty"))

test_dependencies = ["Currency Exchange"]
