# -*- coding: utf-8 -*-
# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and contributors
# For license information, please see license.txt

from __future__ import unicode_literals
import frappe, json
from frappe import _
from frappe.model.mapper import get_mapped_doc
from frappe.utils import get_url, random_string, cint
from frappe.utils.user import get_user_fullname
from frappe.utils.print_format import download_pdf
from frappe.desk.form.load import get_attachments
from frappe.core.doctype.communication.email import make
from erpnext.accounts.party import get_party_account_currency, get_party_details
from erpnext.stock.doctype.material_request.material_request import set_missing_values
from erpnext.controllers.buying_controller import BuyingController

STANDARD_USERS = ("Guest", "Administrator")

class RequestforQuotation(BuyingController):
	def validate(self):
		self.validate_duplicate_supplier()
		self.validate_common()
		self.update_email_id()

	def validate_duplicate_supplier(self):
		supplier_list = [d.supplier for d in self.suppliers]
		if len(supplier_list) != len(set(supplier_list)):
			frappe.throw(_("Same supplier has been entered multiple times"))

	def validate_common(self):
		pc = frappe.get_doc('Purchase Common')
		pc.validate_for_items(self)

	def update_email_id(self):
		for rfq_supplier in self.suppliers:
			if not rfq_supplier.email_id:
				rfq_supplier.email_id = frappe.db.get_value("Contact", rfq_supplier.contact, "email_id")

	def validate_email_id(self, args):
		if not args.email_id:
			frappe.throw(_("Row {0}: For supplier {0} email id is required to send email").format(args.idx, args.supplier))

	def on_submit(self):
		frappe.db.set(self, 'status', 'Submitted')

	def on_cancel(self):
		frappe.db.set(self, 'status', 'Cancelled')

	def send_to_supplier(self):
		for rfq_supplier in self.suppliers:
			if rfq_supplier.send_email:
				self.validate_email_id(rfq_supplier)

				# make new user if required
				update_password_link = self.update_supplier_contact(rfq_supplier, self.get_link())

				self.update_supplier_part_no(rfq_supplier)
				self.supplier_rfq_mail(rfq_supplier, update_password_link, self.get_link())

	def get_link(self):
		# RFQ link for supplier portal
		return get_url("/rfq/" + self.name)

	def update_supplier_part_no(self, args):
		self.vendor = args.supplier
		for item in self.items:
			item.supplier_part_no = frappe.db.get_value('Item Supplier',
				{'parent': item.item_code, 'supplier': args.supplier}, 'supplier_part_no')

	def update_supplier_contact(self, rfq_supplier, link):
		'''Create a new user for the supplier if not set in contact'''
		update_password_link = ''

		if frappe.db.exists("User", rfq_supplier.email_id):
			user = frappe.get_doc("User", rfq_supplier.email_id)
		else:
			user, update_password_link = self.create_user(rfq_supplier, link)

		self.update_contact_of_supplier(rfq_supplier, user)

		return update_password_link

	def update_contact_of_supplier(self, rfq_supplier, user):
		if rfq_supplier.contact:
			contact = frappe.get_doc("Contact", rfq_supplier.contact)
		else:
			contact = frappe.new_doc("Contact")
			contact.first_name = rfq_supplier.supplier_name or rfq_supplier.supplier
			contact.supplier = rfq_supplier.supplier

		if not contact.email_id and not contact.user:
			contact.email_id = user.name
			contact.user = user.name

		contact.save(ignore_permissions=True)

	def create_user(self, rfq_supplier, link):
		user = frappe.get_doc({
			'doctype': 'User',
			'send_welcome_email': 0,
			'email': rfq_supplier.email_id,
			'first_name': rfq_supplier.supplier_name or rfq_supplier.supplier,
			'user_type': 'Website User',
			'redirect_url': link
		})
		user.save(ignore_permissions=True)
		update_password_link = user.reset_password()

		return user, update_password_link

	def supplier_rfq_mail(self, data, update_password_link, rfq_link):
		full_name = get_user_fullname(frappe.session['user'])
		if full_name == "Guest":
			full_name = "Administrator"

		args = {
			'update_password_link': update_password_link,
			'message': frappe.render_template(self.message_for_supplier, data.as_dict()),
			'rfq_link': rfq_link,
			'user_fullname': full_name
		}

		subject = _("Request for Quotation")
		template = "templates/emails/request_for_quotation.html"
		sender = frappe.session.user not in STANDARD_USERS and frappe.session.user or None
		message = frappe.get_template(template).render(args)
		attachments = self.get_attachments()

		self.send_email(data, sender, subject, message, attachments)

	def send_email(self, data, sender, subject, message, attachments):
		make(subject = subject, content=message,recipients=data.email_id, 
			sender=sender,attachments = attachments, send_email=True,
		     	doctype=self.doctype, name=self.name)["name"]

		frappe.msgprint(_("Email sent to supplier {0}").format(data.supplier))

	def get_attachments(self):
		attachments = [d.name for d in get_attachments(self.doctype, self.name)]
		attachments.append(frappe.attach_print(self.doctype, self.name, doc=self))
		return attachments

@frappe.whitelist()
def send_supplier_emails(rfq_name):
	check_portal_enabled('Request for Quotation')
	rfq = frappe.get_doc("Request for Quotation", rfq_name)
	if rfq.docstatus==1:
		rfq.send_to_supplier()

def check_portal_enabled(reference_doctype):
	if not frappe.db.get_value('Portal Menu Item',
		{'reference_doctype': reference_doctype}, 'enabled'):
		frappe.throw(_("Request for Quotation is disabled to access from portal, for more check portal settings."))

def get_list_context(context=None):
	from erpnext.controllers.website_list_for_contact import get_list_context
	list_context = get_list_context(context)
	list_context["show_sidebar"] = True
	return list_context

# This method is used to make supplier quotation from material request form.
@frappe.whitelist()
def make_supplier_quotation(source_name, for_supplier, target_doc=None):
	def postprocess(source, target_doc):
		target_doc.supplier = for_supplier
		args = get_party_details(for_supplier, party_type="Supplier", ignore_permissions=True)
		target_doc.currency = args.currency or get_party_account_currency('Supplier', for_supplier, source.company)
		target_doc.buying_price_list = args.buying_price_list or frappe.db.get_value('Buying Settings', None, 'buying_price_list')
		set_missing_values(source, target_doc)

	doclist = get_mapped_doc("Request for Quotation", source_name, {
		"Request for Quotation": {
			"doctype": "Supplier Quotation",
			"validation": {
				"docstatus": ["=", 1]
			}
		},
		"Request for Quotation Item": {
			"doctype": "Supplier Quotation Item",
			"field_map": {
				"name": "request_for_quotation_item",
				"parent": "request_for_quotation"
			},
		}
	}, target_doc, postprocess)

	return doclist

# This method is used to make supplier quotation from supplier's portal.
@frappe.whitelist()
def create_supplier_quotation(doc):
	if isinstance(doc, basestring):
		doc = json.loads(doc)

	try:
		sq_doc = frappe.get_doc({
			"doctype": "Supplier Quotation",
			"supplier": doc.get('supplier'),
			"terms": doc.get("terms"),
			"company": doc.get("company"),
			"currency": doc.get('currency') or get_party_account_currency('Supplier', doc.get('supplier'), doc.get('company')),
			"buying_price_list": doc.get('buying_price_list') or frappe.db.get_value('Buying Settings', None, 'buying_price_list')
		})
		add_items(sq_doc, doc.get('supplier'), doc.get('items'))
		sq_doc.flags.ignore_permissions = True
		sq_doc.run_method("set_missing_values")
		sq_doc.save()
		frappe.msgprint(_("Supplier Quotation {0} created").format(sq_doc.name))
		return sq_doc.name
	except Exception:
		return None

def add_items(sq_doc, supplier, items):
	for data in items:
		if data.get("qty") > 0:
			if isinstance(data, dict):
				data = frappe._dict(data)

			create_rfq_items(sq_doc, supplier, data)

def create_rfq_items(sq_doc, supplier, data):
	sq_doc.append('items', {
		"item_code": data.item_code,
		"item_name": data.item_name,
		"description": data.description,
		"qty": data.qty,
		"rate": data.rate,
		"supplier_part_no": frappe.db.get_value("Item Supplier", {'parent': data.item_code, 'supplier': supplier}, "supplier_part_no"),
		"warehouse": data.warehouse or '',
		"request_for_quotation_item": data.name,
		"request_for_quotation": data.parent
	})

@frappe.whitelist()
def get_pdf(doctype, name, supplier_idx):
	doc = get_rfq_doc(doctype, name, supplier_idx)
	if doc:
		download_pdf(doctype, name, doc=doc)

def get_rfq_doc(doctype, name, supplier_idx):
	if cint(supplier_idx):
		doc = frappe.get_doc(doctype, name)
		args = doc.get('suppliers')[cint(supplier_idx) - 1]
		doc.update_supplier_part_no(args)
		return doc
