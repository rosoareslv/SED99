import frappe
from frappe import _

change_icons_map = [
	{
		"module_name": "Patient",
		"color": "#6BE273",
		"icon": "fa fa-user",
		"doctype": "Patient",
		"type": "link",
		"link": "List/Patient",
		"label": _("Patient")
	},
	{
		"module_name": "Patient Encounter",
		"color": "#2ecc71",
		"icon": "fa fa-stethoscope",
		"doctype": "Patient Encounter",
		"type": "link",
		"link": "List/Patient Encounter",
		"label": _("Patient Encounter"),
	},
	{
		"module_name": "Healthcare Practitioner",
		"color": "#2ecc71",
		"icon": "fa fa-user-md",
		"doctype": "Healthcare Practitioner",
		"type": "link",
		"link": "List/Healthcare Practitioner",
		"label": _("Healthcare Practitioner")
	},
	{
		"module_name": "Patient Appointment",
		"color": "#934F92",
		"icon": "fa fa-calendar-plus-o",
		"doctype": "Patient Appointment",
		"type": "link",
		"link": "List/Patient Appointment",
		"label": _("Patient Appointment")
	},
	{
		"module_name": "Lab Test",
		"color": "#7578f6",
		"icon": "octicon octicon-beaker",
		"doctype": "Lab Test",
		"type": "link",
		"link": "List/Lab Test",
		"label": _("Lab Test")
	}
]

def execute():
	change_healthcare_desktop_icons()

def change_healthcare_desktop_icons():
	doctypes = ["patient", "patient_encounter", "healthcare_practitioner",
		"patient_appointment", "lab_test"]
	for doctype in doctypes:
		frappe.reload_doc("healthcare", "doctype", doctype)
	for spec in change_icons_map:
		frappe.db.sql("""
			delete from `tabDesktop Icon`
			where _doctype = '{0}'
		""".format(spec['doctype']))

		desktop_icon = frappe.new_doc("Desktop Icon")
		desktop_icon.hidden = 1
		desktop_icon.standard = 1
		desktop_icon.icon = spec['icon']
		desktop_icon.color = spec['color']
		desktop_icon.module_name = spec['module_name']
		desktop_icon.label = spec['label']
		desktop_icon.app = "erpnext"
		desktop_icon.type = spec['type']
		desktop_icon._doctype = spec['doctype']
		desktop_icon.link = spec['link']
		desktop_icon.save(ignore_permissions=True)

	frappe.db.sql("""
		delete from `tabDesktop Icon`
		where module_name = 'Healthcare' and type = 'module'
	""")

	desktop_icon = frappe.new_doc("Desktop Icon")
	desktop_icon.hidden = 1
	desktop_icon.standard = 1
	desktop_icon.icon = "fa fa-heartbeat"
	desktop_icon.color = "#FF888B"
	desktop_icon.module_name = "Healthcare"
	desktop_icon.label = _("Healthcare")
	desktop_icon.app = "erpnext"
	desktop_icon.type = 'module'
	desktop_icon.save(ignore_permissions=True)
