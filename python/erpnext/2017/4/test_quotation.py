# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt
from __future__ import unicode_literals

import frappe
from frappe.utils import flt
import unittest

test_dependencies = ["Product Bundle"]

class TestQuotation(unittest.TestCase):
	def test_make_sales_order(self):
		from erpnext.selling.doctype.quotation.quotation import make_sales_order

		quotation = frappe.copy_doc(test_records[0])
		quotation.insert()

		self.assertRaises(frappe.ValidationError, make_sales_order, quotation.name)

		quotation.submit()

		sales_order = make_sales_order(quotation.name)

		self.assertEquals(sales_order.doctype, "Sales Order")
		self.assertEquals(len(sales_order.get("items")), 1)
		self.assertEquals(sales_order.get("items")[0].doctype, "Sales Order Item")
		self.assertEquals(sales_order.get("items")[0].prevdoc_docname, quotation.name)
		self.assertEquals(sales_order.customer, "_Test Customer")

		sales_order.delivery_date = "2014-01-01"
		sales_order.naming_series = "_T-Quotation-"
		sales_order.transaction_date = "2013-05-12"
		sales_order.insert()

	def test_create_quotation_with_margin(self):
		from erpnext.selling.doctype.quotation.quotation import make_sales_order
		from erpnext.selling.doctype.sales_order.sales_order \
			import make_delivery_note, make_sales_invoice

		total_margin = flt((1500*18.75)/100 + 1500)

		test_records[0]['items'][0]['price_list_rate'] = 1500
		test_records[0]['items'][0]['margin_type'] = 'Percentage'
		test_records[0]['items'][0]['margin_rate_or_amount'] = 18.75

		quotation = frappe.copy_doc(test_records[0])
		quotation.insert()

		self.assertEquals(quotation.get("items")[0].rate, total_margin)
		self.assertRaises(frappe.ValidationError, make_sales_order, quotation.name)
		quotation.submit()

		sales_order = make_sales_order(quotation.name)
		sales_order.delivery_date = "2016-01-02"
		sales_order.naming_series = "_T-Quotation-"
		sales_order.transaction_date = "2016-01-01"
		sales_order.insert()

		self.assertEquals(quotation.get("items")[0].rate, total_margin)

		sales_order.submit()

		dn = make_delivery_note(sales_order.name)
		self.assertEquals(quotation.get("items")[0].rate, total_margin)
		dn.save()

		si = make_sales_invoice(sales_order.name)
		self.assertEquals(quotation.get("items")[0].rate, total_margin)
		si.save()

test_records = frappe.get_test_records('Quotation')

def get_quotation_dict(customer=None, item_code=None):
	if not customer:
		customer = '_Test Customer'
	if not item_code:
		item_code = '_Test Item'

	return {
		'doctype': 'Quotation',
		'customer': customer,
		'items': [
			{
				'item_code': item_code,
				'qty': 1,
				'rate': 100
			}
		]
	}