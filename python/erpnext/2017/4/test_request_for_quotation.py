# -*- coding: utf-8 -*-
# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# See license.txt
from __future__ import unicode_literals

import frappe
import unittest
from frappe.utils import nowdate

class TestRequestforQuotation(unittest.TestCase):
	def test_make_supplier_quotation(self):
		from erpnext.buying.doctype.request_for_quotation.request_for_quotation import make_supplier_quotation
		rfq = make_request_for_quotation()
		
		sq = make_supplier_quotation(rfq.name, rfq.get('suppliers')[0].supplier)
		sq.submit()
		
		sq1 = make_supplier_quotation(rfq.name, rfq.get('suppliers')[1].supplier)
		sq1.submit()
		
		self.assertEquals(sq.supplier, rfq.get('suppliers')[0].supplier)
		self.assertEquals(sq.get('items')[0].request_for_quotation, rfq.name)
		self.assertEquals(sq.get('items')[0].item_code, "_Test Item")
		self.assertEquals(sq.get('items')[0].qty, 5)
		
		self.assertEquals(sq1.supplier, rfq.get('suppliers')[1].supplier)
		self.assertEquals(sq1.get('items')[0].request_for_quotation, rfq.name)
		self.assertEquals(sq1.get('items')[0].item_code, "_Test Item")
		self.assertEquals(sq1.get('items')[0].qty, 5)

	def test_make_supplier_quotation_from_portal(self):
		from erpnext.buying.doctype.request_for_quotation.request_for_quotation import create_supplier_quotation
		rfq = make_request_for_quotation()
		rfq.get('items')[0].rate = 100
		rfq.supplier = rfq.suppliers[0].supplier
		supplier_quotation_name = create_supplier_quotation(rfq)
		
		supplier_quotation_doc = frappe.get_doc('Supplier Quotation', supplier_quotation_name)
		
		self.assertEquals(supplier_quotation_doc.supplier, rfq.get('suppliers')[0].supplier)
		self.assertEquals(supplier_quotation_doc.get('items')[0].request_for_quotation, rfq.name)
		self.assertEquals(supplier_quotation_doc.get('items')[0].item_code, "_Test Item")
		self.assertEquals(supplier_quotation_doc.get('items')[0].qty, 5)
		self.assertEquals(supplier_quotation_doc.get('items')[0].amount, 500)
		

def make_request_for_quotation():
	supplier_data = get_supplier_data()
	rfq = frappe.new_doc('Request for Quotation')
	rfq.transaction_date = nowdate()
	rfq.status = 'Draft'
	rfq.company = '_Test Company'
	rfq.message_for_supplier = 'Please supply the specified items at the best possible rates.'
	
	for data in supplier_data:
		rfq.append('suppliers', data)
	
	rfq.append("items", {
		"item_code": "_Test Item",
		"description": "_Test Item",
		"uom": "_Test UOM",
		"qty": 5,
		"warehouse": "_Test Warehouse - _TC",
		"schedule_date": nowdate()
	})
	
	rfq.submit()
	
	return rfq
	
def get_supplier_data():
	return [{
		"supplier": "_Test Supplier",
		"supplier_name": "_Test Supplier"
	},
	{
		"supplier": "_Test Supplier 1",
		"supplier_name": "_Test Supplier 1"
	}]
