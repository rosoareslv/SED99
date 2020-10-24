# Copyright (c) 2015, Frappe Technologies Pvt. Ltd. and Contributors
# License: GNU General Public License v3. See license.txt

from __future__ import unicode_literals
import frappe
import json
from frappe import msgprint, _
from frappe.utils import cstr, flt, cint, getdate, now_datetime, formatdate
from frappe.website.website_generator import WebsiteGenerator
from erpnext.setup.doctype.item_group.item_group import invalidate_cache_for, get_parent_item_groups
from frappe.website.render import clear_cache
from frappe.website.doctype.website_slideshow.website_slideshow import get_slideshow

class WarehouseNotSet(frappe.ValidationError): pass
class ItemTemplateCannotHaveStock(frappe.ValidationError): pass
class ItemVariantExistsError(frappe.ValidationError): pass
class InvalidItemAttributeValueError(frappe.ValidationError): pass

class Item(WebsiteGenerator):
	website = frappe._dict(
		page_title_field = "item_name",
		condition_field = "show_in_website",
		template = "templates/generators/item.html",
		parent_website_route_field = "item_group",
	)

	def onload(self):
		super(Item, self).onload()
		self.get("__onload").sle_exists = self.check_if_sle_exists()

	def autoname(self):
		if frappe.db.get_default("item_naming_by")=="Naming Series" and not self.variant_of:
			from frappe.model.naming import make_autoname
			self.item_code = make_autoname(self.naming_series+'.#####')
		elif not self.item_code:
			msgprint(_("Item Code is mandatory because Item is not automatically numbered"), raise_exception=1)

		self.name = self.item_code

	def before_insert(self):
		if self.is_sales_item=="Yes":
			self.publish_in_hub = 1

	def validate(self):
		super(Item, self).validate()

		if not self.stock_uom:
			msgprint(_("Please enter default Unit of Measure"), raise_exception=1)
		if self.image and not self.website_image:
			self.website_image = self.image

		self.check_warehouse_is_set_for_stock_item()
		self.validate_uom()
		self.add_default_uom_in_conversion_factor_table()
		self.validate_conversion_factor()
		self.validate_item_type()
		self.check_for_active_boms()
		self.fill_customer_code()
		self.check_item_tax()
		self.validate_barcode()
		self.cant_change()
		self.validate_reorder_level()
		self.validate_warehouse_for_reorder()
		self.update_item_desc()
		self.synced_with_hub = 0
		self.validate_has_variants()
		# self.validate_stock_for_template_must_be_zero()
		self.validate_attributes()
		self.validate_variant_attributes()

		if not self.get("__islocal"):
			self.old_item_group = frappe.db.get_value(self.doctype, self.name, "item_group")
			self.old_website_item_groups = frappe.db.sql_list("""select item_group from `tabWebsite Item Group`
				where parentfield='website_item_groups' and parenttype='Item' and parent=%s""", self.name)

	def on_update(self):
		super(Item, self).on_update()
		invalidate_cache_for_item(self)
		self.validate_name_with_item_group()
		self.update_item_price()
		self.update_variants()

	def get_context(self, context):
		context["parent_groups"] = get_parent_item_groups(self.item_group) + \
			[{"name": self.name}]
		if self.slideshow:
			context.update(get_slideshow(self))

		context["parents"] = self.get_parents(context)

		return context

	def check_warehouse_is_set_for_stock_item(self):
		if self.is_stock_item==1 and not self.default_warehouse and frappe.get_all("Warehouse"):
			frappe.msgprint(_("Default Warehouse is mandatory for stock Item."),
				raise_exception=WarehouseNotSet)

	def add_default_uom_in_conversion_factor_table(self):
		uom_conv_list = [d.uom for d in self.get("uoms")]
		if self.stock_uom not in uom_conv_list:
			ch = self.append('uoms', {})
			ch.uom = self.stock_uom
			ch.conversion_factor = 1

		to_remove = []
		for d in self.get("uoms"):
			if d.conversion_factor == 1 and d.uom != self.stock_uom:
				to_remove.append(d)

		[self.remove(d) for d in to_remove]

	def update_template_tables(self):
		template = frappe.get_doc("Item", self.variant_of)

		# add item taxes from template
		for d in template.get("taxes"):
			self.append("taxes", {"tax_type": d.tax_type, "tax_rate": d.tax_rate})

		# copy re-order table if empty
		if not self.get("reorder_levels"):
			for d in template.get("reorder_levels"):
				n = {}
				for k in ("warehouse", "warehouse_reorder_level",
					"warehouse_reorder_qty", "material_request_type"):
					n[k] = d.get(k)
				self.append("reorder_levels", n)

	def validate_conversion_factor(self):
		check_list = []
		for d in self.get('uoms'):
			if cstr(d.uom) in check_list:
				frappe.throw(_("Unit of Measure {0} has been entered more than once in Conversion Factor Table").format(d.uom))
			else:
				check_list.append(cstr(d.uom))

			if d.uom and cstr(d.uom) == cstr(self.stock_uom) and flt(d.conversion_factor) != 1:
				frappe.throw(_("Conversion factor for default Unit of Measure must be 1 in row {0}").format(d.idx))

	def validate_item_type(self):
		if self.is_pro_applicable == 1 and self.is_stock_item==0:
			frappe.throw(_("As Production Order can be made for this item, it must be a stock item."))

		if self.has_serial_no == 1 and self.is_stock_item == 0:
			msgprint(_("'Has Serial No' can not be 'Yes' for non-stock item"), raise_exception=1)

		if self.has_serial_no == 0 and self.serial_no_series:
			self.serial_no_series = None


	def check_for_active_boms(self):
		if self.default_bom:
			bom_item = frappe.db.get_value("BOM", self.default_bom, "item")
			if bom_item not in (self.name, self.variant_of):
				frappe.throw(_("Default BOM ({0}) must be active for this item or its template").format(bom_item))

	def fill_customer_code(self):
		""" Append all the customer codes and insert into "customer_code" field of item table """
		cust_code=[]
		for d in self.get('customer_items'):
			cust_code.append(d.ref_code)
		self.customer_code=','.join(cust_code)

	def check_item_tax(self):
		"""Check whether Tax Rate is not entered twice for same Tax Type"""
		check_list=[]
		for d in self.get('taxes'):
			if d.tax_type:
				account_type = frappe.db.get_value("Account", d.tax_type, "account_type")

				if account_type not in ['Tax', 'Chargeable', 'Income Account', 'Expense Account']:
					frappe.throw(_("Item Tax Row {0} must have account of type Tax or Income or Expense or Chargeable").format(d.idx))
				else:
					if d.tax_type in check_list:
						frappe.throw(_("{0} entered twice in Item Tax").format(d.tax_type))
					else:
						check_list.append(d.tax_type)

	def validate_barcode(self):
		if self.barcode:
			duplicate = frappe.db.sql("""select name from tabItem where barcode = %s
				and name != %s""", (self.barcode, self.name))
			if duplicate:
				frappe.throw(_("Barcode {0} already used in Item {1}").format(self.barcode, duplicate[0][0]))

	def cant_change(self):
		if not self.get("__islocal"):
			vals = frappe.db.get_value("Item", self.name,
				["has_serial_no", "is_stock_item", "valuation_method", "has_batch_no"], as_dict=True)

			if vals and ((self.is_stock_item == 0 and vals.is_stock_item == 1) or
				vals.has_serial_no != self.has_serial_no or
				vals.has_batch_no != self.has_batch_no or
				cstr(vals.valuation_method) != cstr(self.valuation_method)):
					if self.check_if_sle_exists() == "exists":
						frappe.throw(_("As there are existing stock transactions for this item, \
							you can not change the values of 'Has Serial No', 'Has Batch No', 'Is Stock Item' and 'Valuation Method'"))

	def validate_reorder_level(self):
		if cint(self.apply_warehouse_wise_reorder_level):
			self.re_order_level, self.re_order_qty = 0, 0
		else:
			self.set("reorder_levels", [])

		if self.re_order_level or len(self.get("reorder_levels", {"material_request_type": "Purchase"})):
			if not self.is_purchase_item:
				frappe.throw(_("""To set reorder level, item must be a Purchase Item"""))

	def validate_warehouse_for_reorder(self):
		warehouse = []
		for i in self.get("reorder_levels"):
			if i.get("warehouse") and i.get("warehouse") not in warehouse:
				warehouse += [i.get("warehouse")]
			else:
				frappe.throw(_("Row {0}: An Reorder entry already exists for this warehouse {1}")
					.format(i.idx, i.warehouse))

	def check_if_sle_exists(self):
		sle = frappe.db.sql("""select name from `tabStock Ledger Entry`
			where item_code = %s""", self.name)
		return sle and 'exists' or 'not exists'

	def validate_name_with_item_group(self):
		# causes problem with tree build
		if frappe.db.exists("Item Group", self.name):
			frappe.throw(_("An Item Group exists with same name, please change the item name or rename the item group"))

	def update_item_price(self):
		frappe.db.sql("""update `tabItem Price` set item_name=%s,
			item_description=%s, modified=NOW() where item_code=%s""",
			(self.item_name, self.description, self.name))

	def get_tax_rate(self, tax_type):
		return { "tax_rate": frappe.db.get_value("Account", tax_type, "tax_rate") }

	def on_trash(self):
		super(Item, self).on_trash()
		frappe.db.sql("""delete from tabBin where item_code=%s""", self.item_code)
		frappe.db.sql("delete from `tabItem Price` where item_code=%s", self.name)
		for variant_of in frappe.get_all("Item", filters={"variant_of": self.name}):
			frappe.delete_doc("Item", variant_of.name)

	def before_rename(self, olddn, newdn, merge=False):
		if merge:
			# Validate properties before merging
			if not frappe.db.exists("Item", newdn):
				frappe.throw(_("Item {0} does not exist").format(newdn))

			field_list = ["stock_uom", "is_stock_item", "has_serial_no", "has_batch_no"]
			new_properties = [cstr(d) for d in frappe.db.get_value("Item", newdn, field_list)]
			if new_properties != [cstr(self.get(fld)) for fld in field_list]:
				frappe.throw(_("To merge, following properties must be same for both items")
					+ ": \n" + ", ".join([self.meta.get_label(fld) for fld in field_list]))

			frappe.db.sql("delete from `tabBin` where item_code=%s", olddn)

	def after_rename(self, olddn, newdn, merge):
		super(Item, self).after_rename(olddn, newdn, merge)
		if self.page_name:
			invalidate_cache_for_item(self)
			clear_cache(self.page_name)

		frappe.db.set_value("Item", newdn, "item_code", newdn)
		if merge:
			self.set_last_purchase_rate(newdn)
			self.recalculate_bin_qty(newdn)

	def set_last_purchase_rate(self, newdn):
		last_purchase_rate = get_last_purchase_details(newdn).get("base_rate", 0)
		frappe.db.set_value("Item", newdn, "last_purchase_rate", last_purchase_rate)

	def recalculate_bin_qty(self, newdn):
		from erpnext.stock.stock_balance import repost_stock
		frappe.db.auto_commit_on_many_writes = 1
		existing_allow_negative_stock = frappe.db.get_value("Stock Settings", None, "allow_negative_stock")
		frappe.db.set_value("Stock Settings", None, "allow_negative_stock", 1)

		for warehouse in frappe.db.sql("select name from `tabWarehouse`"):
			repost_stock(newdn, warehouse[0])

		frappe.db.set_value("Stock Settings", None, "allow_negative_stock", existing_allow_negative_stock)
		frappe.db.auto_commit_on_many_writes = 0

	def copy_specification_from_item_group(self):
		self.set("website_specifications", [])
		if self.item_group:
			for label, desc in frappe.db.get_values("Item Website Specification",
				{"parent": self.item_group}, ["label", "description"]):
					row = self.append("website_specifications")
					row.label = label
					row.description = desc

	def update_item_desc(self):
		if frappe.db.get_value('BOM',self.name, 'description') != self.description:
			frappe.db.sql("""update `tabBOM` set description = %s where item = %s and docstatus < 2""",(self.description, self.name))
			frappe.db.sql("""update `tabBOM Item` set description = %s where
				item_code = %s and docstatus < 2""",(self.description, self.name))
			frappe.db.sql("""update `tabBOM Explosion Item` set description = %s where
				item_code = %s and docstatus < 2""",(self.description, self.name))

	def update_variants(self):
		if self.has_variants:
			updated = []
			variants = frappe.db.get_all("Item", fields=["item_code"], filters={"variant_of": self.name })
			for d in variants:
				variant = frappe.get_doc("Item", d)
				copy_attributes_to_variant(self, variant)
				variant.save()
				updated.append(d.item_code)
			if updated:
				frappe.msgprint(_("Item Variants {0} updated").format(", ".join(updated)))

	def validate_has_variants(self):
		if not self.has_variants and frappe.db.get_value("Item", self.name, "has_variants"):
			if frappe.db.exists("Item", {"variant_of": self.name}):
				frappe.throw(_("Item has variants."))

	def validate_stock_for_template_must_be_zero(self):
		if self.has_variants:
			stock_in = frappe.db.sql_list("""select warehouse from tabBin
				where item_code=%s and (ifnull(actual_qty, 0) > 0 or ifnull(ordered_qty, 0) > 0
				or ifnull(reserved_qty, 0) > 0 or ifnull(indented_qty, 0) > 0 or ifnull(planned_qty, 0) > 0)""", self.name)
			if stock_in:
				frappe.throw(_("Item Template cannot have stock or Open Sales/Purchase/Production Orders."), ItemTemplateCannotHaveStock)

	def validate_uom(self):
		if not self.get("__islocal"):
			check_stock_uom_with_bin(self.name, self.stock_uom)
		if self.has_variants:
			for d in frappe.db.get_all("Item", filters= {"variant_of": self.name}):
				check_stock_uom_with_bin(d.name, self.stock_uom)
		if self.variant_of:
			template_uom = frappe.db.get_value("Item", self.variant_of, "stock_uom")
			if template_uom != self.stock_uom:
				frappe.throw(_("Default Unit of Measure for Variant must be same as Template"))

	def validate_attributes(self):
		if self.has_variants or self.variant_of:
			attributes = []
			if not self.attributes:
				frappe.throw(_("Attribute table is mandatory"))
			for d in self.attributes:
				if d.attribute in attributes:
					frappe.throw(_("Attribute {0} selected multiple times in Attributes Table".format(d.attribute)))
				else:
					attributes.append(d.attribute)

	def validate_variant_attributes(self):
		if self.variant_of:
			args = {}
			for d in self.attributes:
				if not d.attribute_value:
					frappe.throw(_("Please specify Attribute Value for attribute {0}").format(d.attribute))
				args[d.attribute] = d.attribute_value

			if self.get("__islocal"):
				# test this during insert because naming is based on item_code and we cannot use condition like self.name != variant
				variant = get_variant(self.variant_of, args)
				if variant:
					frappe.throw(_("Item variant {0} exists with same attributes").format(variant), ItemVariantExistsError)

def validate_end_of_life(item_code, end_of_life=None, verbose=1):
	if not end_of_life:
		end_of_life = frappe.db.get_value("Item", item_code, "end_of_life")

	if end_of_life and end_of_life!="0000-00-00" and getdate(end_of_life) <= now_datetime().date():
		msg = _("Item {0} has reached its end of life on {1}").format(item_code, formatdate(end_of_life))
		_msgprint(msg, verbose)

def validate_is_stock_item(item_code, is_stock_item=None, verbose=1):
	if not is_stock_item:
		is_stock_item = frappe.db.get_value("Item", item_code, "is_stock_item")

	if is_stock_item != 1:
		msg = _("Item {0} is not a stock Item").format(item_code)

		_msgprint(msg, verbose)

def validate_cancelled_item(item_code, docstatus=None, verbose=1):
	if docstatus is None:
		docstatus = frappe.db.get_value("Item", item_code, "docstatus")

	if docstatus == 2:
		msg = _("Item {0} is cancelled").format(item_code)
		_msgprint(msg, verbose)

def _msgprint(msg, verbose):
	if verbose:
		msgprint(msg, raise_exception=True)
	else:
		raise frappe.ValidationError, msg


def get_last_purchase_details(item_code, doc_name=None, conversion_rate=1.0):
	"""returns last purchase details in stock uom"""
	# get last purchase order item details
	last_purchase_order = frappe.db.sql("""\
		select po.name, po.transaction_date, po.conversion_rate,
			po_item.conversion_factor, po_item.base_price_list_rate,
			po_item.discount_percentage, po_item.base_rate
		from `tabPurchase Order` po, `tabPurchase Order Item` po_item
		where po.docstatus = 1 and po_item.item_code = %s and po.name != %s and
			po.name = po_item.parent
		order by po.transaction_date desc, po.name desc
		limit 1""", (item_code, cstr(doc_name)), as_dict=1)

	# get last purchase receipt item details
	last_purchase_receipt = frappe.db.sql("""\
		select pr.name, pr.posting_date, pr.posting_time, pr.conversion_rate,
			pr_item.conversion_factor, pr_item.base_price_list_rate, pr_item.discount_percentage,
			pr_item.base_rate
		from `tabPurchase Receipt` pr, `tabPurchase Receipt Item` pr_item
		where pr.docstatus = 1 and pr_item.item_code = %s and pr.name != %s and
			pr.name = pr_item.parent
		order by pr.posting_date desc, pr.posting_time desc, pr.name desc
		limit 1""", (item_code, cstr(doc_name)), as_dict=1)

	purchase_order_date = getdate(last_purchase_order and last_purchase_order[0].transaction_date \
		or "1900-01-01")
	purchase_receipt_date = getdate(last_purchase_receipt and \
		last_purchase_receipt[0].posting_date or "1900-01-01")

	if (purchase_order_date > purchase_receipt_date) or \
			(last_purchase_order and not last_purchase_receipt):
		# use purchase order
		last_purchase = last_purchase_order[0]
		purchase_date = purchase_order_date

	elif (purchase_receipt_date > purchase_order_date) or \
			(last_purchase_receipt and not last_purchase_order):
		# use purchase receipt
		last_purchase = last_purchase_receipt[0]
		purchase_date = purchase_receipt_date

	else:
		return frappe._dict()

	conversion_factor = flt(last_purchase.conversion_factor)
	out = frappe._dict({
		"base_price_list_rate": flt(last_purchase.base_price_list_rate) / conversion_factor,
		"base_rate": flt(last_purchase.base_rate) / conversion_factor,
		"discount_percentage": flt(last_purchase.discount_percentage),
		"purchase_date": purchase_date
	})

	conversion_rate = flt(conversion_rate) or 1.0
	out.update({
		"price_list_rate": out.base_price_list_rate / conversion_rate,
		"rate": out.base_rate / conversion_rate,
		"base_rate": out.base_rate
	})

	return out

def invalidate_cache_for_item(doc):
	invalidate_cache_for(doc, doc.item_group)

	website_item_groups = list(set((doc.get("old_website_item_groups") or [])
		+ [d.item_group for d in doc.get({"doctype":"Website Item Group"}) if d.item_group]))

	for item_group in website_item_groups:
		invalidate_cache_for(doc, item_group)

	if doc.get("old_item_group") and doc.get("old_item_group") != doc.item_group:
		invalidate_cache_for(doc, doc.old_item_group)

def check_stock_uom_with_bin(item, stock_uom):
	if stock_uom == frappe.db.get_value("Item", item, "stock_uom"):
		return

	matched=True
	ref_uom = frappe.db.get_value("Stock Ledger Entry",
		{"item_code": item}, "stock_uom")

	if ref_uom:
		if cstr(ref_uom) != cstr(stock_uom):
			matched = False
	else:
		bin_list = frappe.db.sql("select * from tabBin where item_code=%s", item, as_dict=1)
		for bin in bin_list:
			if (bin.reserved_qty > 0 or bin.ordered_qty > 0 or bin.indented_qty > 0 \
				or bin.planned_qty > 0) and cstr(bin.stock_uom) != cstr(stock_uom):
					matched = False
					break

		if matched and bin_list:
			frappe.db.sql("""update tabBin set stock_uom=%s where item_code=%s""", (stock_uom, item))

	if not matched:
		frappe.throw(_("Default Unit of Measure for Item {0} cannot be changed directly because \
			you have already made some transaction(s) with another UOM. To change default UOM, \
			use 'UOM Replace Utility' tool under Stock module.").format(item))

@frappe.whitelist()
def get_variant(item, args):
	"""Validates Attributes and their Values, then looks for an exactly matching Item Variant

		:param item: Template Item
		:param args: A dictionary with "Attribute" as key and "Attribute Value" as value
	"""
	if isinstance(args, basestring):
		args = json.loads(args)

	if not args:
		frappe.throw(_("Please specify at least one attribute in the Attributes table"))

	validate_item_variant_attributes(item, args)

	return find_variant(item, args)

def validate_item_variant_attributes(item, args):
	attribute_values = {}
	for t in frappe.get_all("Item Attribute Value", fields=["parent", "attribute_value"],
		filters={"parent": ["in", args.keys()]}):
		(attribute_values.setdefault(t.parent, [])).append(t.attribute_value)

	numeric_attributes = frappe._dict((t.attribute, t) for t in \
		frappe.db.sql("""select attribute, from_range, to_range, increment from `tabItem Variant Attribute` 
		where parent = %s and numeric_values=1""", (item), as_dict=1))
		
	for attribute, value in args.items():

		if attribute in numeric_attributes:
			numeric_attribute = numeric_attributes[attribute]

			from_range = numeric_attribute.from_range
			to_range = numeric_attribute.to_range
			increment = numeric_attribute.increment

			if increment == 0:
				# defensive validation to prevent ZeroDivisionError
				frappe.throw(_("Increment for Attribute {0} cannot be 0").format(attribute))
			
			is_in_range = from_range <= flt(value) <= to_range
			precision = len(cstr(increment).split(".")[-1].rstrip("0"))
			#avoid precision error by rounding the remainder
			remainder = flt((flt(value) - from_range) % increment, precision)

			is_incremental = remainder==0 or remainder==0 or remainder==increment

			if not (is_in_range and is_incremental):
				frappe.throw(_("Value for Attribute {0} must be within the range of {1} to {2} in the increments of {3}")\
					.format(attribute, from_range, to_range, increment), InvalidItemAttributeValueError)

		elif value not in attribute_values[attribute]:
			frappe.throw(_("Value {0} for Attribute {1} does not exist in the list of valid Item Attribute Values").format(
				value, attribute))

def find_variant(item, args):
	conditions = ["""(iv_attribute.attribute="{0}" and iv_attribute.attribute_value="{1}")"""\
		.format(frappe.db.escape(key), frappe.db.escape(cstr(value))) for key, value in args.items()]

	conditions = " or ".join(conditions)

	# use approximate match and shortlist possible variant matches
	# it is approximate because we are matching using OR condition
	# and it need not be exact match at this stage
	# this uses a simpler query instead of using multiple exists conditions
	possible_variants = frappe.db.sql_list("""select name from `tabItem` item
		where variant_of=%s and exists (
			select name from `tabItem Variant Attribute` iv_attribute
				where iv_attribute.parent=item.name
				and ({conditions})
		)""".format(conditions=conditions), item)

	for variant in possible_variants:
		variant = frappe.get_doc("Item", variant)

		if len(args.keys()) == len(variant.get("attributes")):
			# has the same number of attributes and values
			# assuming no duplication as per the validation in Item
			match_count = 0

			for attribute, value in args.items():
				for row in variant.attributes:
					if row.attribute==attribute and row.attribute_value== cstr(value):
						# this row matches
						match_count += 1
						break

			if match_count == len(args.keys()):
				return variant.name

@frappe.whitelist()
def create_variant(item, args):
	if isinstance(args, basestring):
		args = json.loads(args)

	variant = frappe.new_doc("Item")
	variant_attributes = []
	for d in args:
		variant_attributes.append({
			"attribute": d,
			"attribute_value": args[d]
		})

	variant.set("attributes", variant_attributes)
	template = frappe.get_doc("Item", item)
	copy_attributes_to_variant(template, variant)
	make_variant_item_code(template, variant)

	return variant

def copy_attributes_to_variant(item, variant):
	from frappe.model import no_value_fields
	for field in item.meta.fields:
		if field.fieldtype not in no_value_fields and (not field.no_copy)\
			and field.fieldname not in ("item_code", "item_name"):
			if variant.get(field.fieldname) != item.get(field.fieldname):
				variant.set(field.fieldname, item.get(field.fieldname))
	variant.variant_of = item.name
	variant.has_variants = 0
	variant.show_in_website = 0
	if variant.attributes:
		variant.description += "\n"
		for d in variant.attributes:
			variant.description += "<p>" + d.attribute + ": " + cstr(d.attribute_value) + "</p>"

def make_variant_item_code(template, variant):
	"""Uses template's item code and abbreviations to make variant's item code"""
	if variant.item_code:
		return

	abbreviations = []
	for attr in variant.attributes:
		item_attribute = frappe.db.sql("""select i.numeric_values, v.abbr
			from `tabItem Attribute` i left join `tabItem Attribute Value` v
				on (i.name=v.parent)
			where i.name=%(attribute)s and v.attribute_value=%(attribute_value)s""", {
				"attribute": attr.attribute,
				"attribute_value": attr.attribute_value
			}, as_dict=True)

		if not item_attribute:
			# somehow an invalid item attribute got used
			return

		if item_attribute[0].numeric_values:
			# don't generate item code if one of the attributes is numeric
			return

		abbreviations.append(item_attribute[0].abbr)

	if abbreviations:
		variant.item_code = "{0}-{1}".format(template.item_code, "-".join(abbreviations))

	if variant.item_code:
		variant.item_name = variant.item_code
