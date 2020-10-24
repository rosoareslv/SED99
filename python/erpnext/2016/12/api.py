# -*- coding: utf-8 -*-
# Copyright (c) 2015, Frappe Technologies and contributors
# For license information, please see license.txt

from __future__ import unicode_literals
import frappe
import json
from frappe import _
from frappe.model.mapper import get_mapped_doc
from frappe.utils import flt

@frappe.whitelist()
def enroll_student(source_name):
	"""Creates a Student Record and returns a Program Enrollment.

	:param source_name: Student Applicant.
	"""
	student = get_mapped_doc("Student Applicant", source_name,
		{"Student Applicant": {
			"doctype": "Student",
			"field_map": {
				"name": "student_applicant"
			}
		}}, ignore_permissions=True)
	student.save()
	program_enrollment = frappe.new_doc("Program Enrollment")
	program_enrollment.student = student.name
	program_enrollment.student_name = student.title
	program_enrollment.program = frappe.db.get_value("Student Applicant", source_name, "program")
	return program_enrollment

@frappe.whitelist()
def check_attendance_records_exist(course_schedule=None, student_batch=None, date=None):
	"""Check if Attendance Records are made against the specified Course Schedule or Student Batch for given date.

	:param course_schedule: Course Schedule.
	:param student_batch: Student Batch.
	:param date: Date.
	"""
	if course_schedule:
		return frappe.get_list("Student Attendance", filters={"course_schedule": course_schedule})
	else:
		return frappe.get_list("Student Attendance", filters={"student_batch": student_batch, "date": date})

@frappe.whitelist()
def mark_attendance(students_present, students_absent, course_schedule=None, student_batch=None, date=None):
	"""Creates Multiple Attendance Records.

	:param students_present: Students Present JSON.
	:param students_absent: Students Absent JSON.
	:param course_schedule: Course Schedule.
	:param student_batch: Student Batch.
	:param date: Date.
	"""
	 
	present = json.loads(students_present)
	absent = json.loads(students_absent)
	
	for d in present:
		make_attendance_records(d["student"], d["student_name"], "Present", course_schedule, student_batch, date)

	for d in absent:
		make_attendance_records(d["student"], d["student_name"], "Absent", course_schedule, student_batch, date)

	frappe.db.commit()
	frappe.msgprint(_("Attendance has been marked successfully."))

def make_attendance_records(student, student_name, status, course_schedule=None, student_batch=None, date=None):
	"""Creates Attendance Record.

	:param student: Student.
	:param student_name: Student Name.
	:param course_schedule: Course Schedule.
	:param status: Status (Present/Absent)
	"""
	student_attendance = frappe.new_doc("Student Attendance")
	student_attendance.student = student
	student_attendance.student_name = student_name
	student_attendance.course_schedule = course_schedule
	student_attendance.student_batch = student_batch
	student_attendance.date = date
	student_attendance.status = status
	student_attendance.submit()

@frappe.whitelist()
def get_student_batch_students(student_batch):
	"""Returns List of student, student_name in Student Batch.

	:param student_batch: Student Batch.
	"""
	students = frappe.get_list("Student Batch Student", fields=["student", "student_name", "idx"] , filters={"parent": student_batch}, order_by= "idx")
	return students

@frappe.whitelist()
def get_student_group_students(student_group):
	"""Returns List of student, student_name in Student Group.

	:param student_group: Student Group.
	"""
	students = frappe.get_list("Student Group Student", fields=["student", "student_name"] , filters={"parent": student_group}, order_by= "idx")
	return students

@frappe.whitelist()
def get_fee_structure(program, academic_term=None):
	"""Returns Fee Structure.

	:param program: Program.
	:param academic_term: Academic Term.
	"""
	fee_structure = frappe.db.get_values("Fee Structure", {"program": program,
		"academic_term": academic_term}, 'name', as_dict=True)
	return fee_structure[0].name if fee_structure else None

@frappe.whitelist()
def get_fee_components(fee_structure):
	"""Returns Fee Components.

	:param fee_structure: Fee Structure.
	"""
	if fee_structure:
		fs = frappe.get_list("Fee Component", fields=["fees_category", "amount"] , filters={"parent": fee_structure}, order_by= "idx")
		return fs

@frappe.whitelist()
def get_fee_schedule(program, student_category=None):
	"""Returns Fee Schedule.

	:param program: Program.
	:param student_category: Student Category
	"""
	fs = frappe.get_list("Program Fee", fields=["academic_term", "fee_structure", "due_date", "amount"] ,
		filters={"parent": program, "student_category": student_category }, order_by= "idx")
	return fs

@frappe.whitelist()
def collect_fees(fees, amt):
	paid_amount = flt(amt) + flt(frappe.db.get_value("Fees", fees, "paid_amount"))
	total_amount = flt(frappe.db.get_value("Fees", fees, "total_amount"))
	frappe.db.set_value("Fees", fees, "paid_amount", paid_amount)
	frappe.db.set_value("Fees", fees, "outstanding_amount", (total_amount - paid_amount))
	return paid_amount

@frappe.whitelist()
def get_course_schedule_events(start, end, filters=None):
	"""Returns events for Course Schedule Calendar view rendering.

	:param start: Start date-time.
	:param end: End date-time.
	:param filters: Filters (JSON).
	"""
	from frappe.desk.calendar import get_event_conditions
	conditions = get_event_conditions("Course Schedule", filters)

	data = frappe.db.sql("""select name, course,
			timestamp(schedule_date, from_time) as from_datetime,
			timestamp(schedule_date, to_time) as to_datetime,
			room, student_group, 0 as 'allDay'
		from `tabCourse Schedule`
		where ( schedule_date between %(start)s and %(end)s )
		{conditions}""".format(conditions=conditions), {
			"start": start,
			"end": end
			}, as_dict=True, update={"allDay": 0})

	return data
