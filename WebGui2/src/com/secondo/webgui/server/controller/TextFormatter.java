package com.secondo.webgui.server.controller;

import java.util.ArrayList;
import sj.lang.ListExpr;

/**
 * This class represents a controller class which gets the resultlist from a
 * secondo query, searches for known datatypes and formats the result to a nice
 * text format to display in the text panel.
 * 
 * @author Irina Russkaya
 * 
 **/
public class TextFormatter {

	/** The formatted text result list */
	private ArrayList<String> formattedList = new ArrayList<String>();
	private ArrayList<String> formattedListWithMlabel = new ArrayList<String>();

	public TextFormatter() {
	}

	/**
	 * This method gets a listexpression, formats the data and puts it into the
	 * result list
	 * 
	 * @param le
	 *            The query result from secondo
	 * */
	public void formatData(ListExpr le) {

		formattedList.clear();
		formattedListWithMlabel.clear();
		formattedList.add("unknown type");
		formattedListWithMlabel.add("unknown type");

		// static data type int
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("int")) {
			formattedList.clear();
			formatStandardType(le);
		}

		// static data type real
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("real")) {
			formattedList.clear();
			formatStandardType(le);
		}

		// static data type bool
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("bool")) {
			formattedList.clear();
			formatStandardType(le);
		}

		// static data type string
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("string")) {
			formattedList.clear();
			formatStandardType(le);
		}

		// format all inquiries
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("inquiry")) {
			formattedList.clear();
			formatInquiry(le);
		}

		// format point data
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("point")) {
			formattedList.clear();
			formatPoint(le);
		}

		// type is line
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("line")) {
			formattedList.clear();
			formatLine(le);
		}

		// format region data
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("region")) {
			formattedList.clear();
			formatRegion(le);
		}

		// format mpoint data
		if (le.first().atomType() == ListExpr.SYMBOL_ATOM
				&& le.first().symbolValue().equals("mpoint")) {
			formattedList.clear();
			formatMPoint(le);
		}

		// format relation data
		if (le.first().atomType() != ListExpr.SYMBOL_ATOM
				&& le.first().first().writeListExprToString().contains("rel")) {

			formattedList.clear();
			formattedListWithMlabel.clear();
			formatRel(le);
		}
	}

	/**
	 * Adds the value of the standardtypes int, real, bool, string from the
	 * listexpression to the formatted list
	 * 
	 * @param le
	 *            The secondo result containing a standard type
	 * */
	public void formatStandardType(ListExpr le) {
		System.out.println("######## le.second()"
				+ le.second().writeListExprToString());

		if (le.listLength() != 2) {
			formattedList.add("unknown type");
		} else {

			formattedList.add(le.first().symbolValue() + " : ");
			formattedList.add(le.second().writeListExprToString().trim());
		}
	}

	/**
	 * Adds the inquiry data of the listexpression result to the formatted list
	 * if it can be displayed else unknown type
	 * 
	 * @param The
	 *            secondo result containing an inquiry
	 * */
	public void formatInquiry(ListExpr LE) {

		if (LE.listLength() != 2)
			formattedList.add("unknown type");

		ListExpr Value = LE.second();
		if (Value.listLength() != 2)
			formattedList.add("unknown type");

		ListExpr Type = Value.first();

		System.out.println("################### Data-Type: "
				+ LE.second().first().symbolValue());

		String Name = Type.symbolValue();
		if (Name.equals("constructors") || Name.equals("operators")
				|| Name.equals("algebra") || Name.equals("algebras")
				|| Name.equals("databases") || Name.equals("types")
				|| Name.equals("objects")) {

			LE = LE.second(); // ignore first entry

			ListExpr value = LE.second();

			String formattedTextentry = "";

			formattedTextentry = formattedTextentry + "***"
					+ LE.first().symbolValue() + "***" + "\n \n";

			while (!value.isEmpty()) {
				formattedTextentry = formattedTextentry
						+ value.first().writeListExprToString() + "\n";

				formattedList.add(formattedTextentry);
				formattedTextentry = "";

				value = value.rest();
			}
		}
	}

	/**
	 * Adds the point data to the resultlist if the result can be displayed in
	 * the formatted view else unknown type
	 * 
	 * @param LE
	 *            The secondo result containing a point
	 * */
	public void formatPoint(ListExpr LE) {

		// is it really a point?
		System.out.println("################### Data-Type: "
				+ LE.first().symbolValue()); // point

		if (LE.listLength() != 2)
			formattedList.add("unknown type");

		String formattedTextentry = "";

		formattedTextentry = formattedTextentry + "Type: "
				+ LE.first().symbolValue() + "\n \n"; // point
		formattedTextentry = formattedTextentry + "X: "
				+ LE.second().first().realValue() + "\n \n"; // X: 7.472602
		formattedTextentry = formattedTextentry + "Y: "
				+ LE.second().second().realValue() + "\n"; // Y: 51.51242

		formattedList.add(formattedTextentry);
		formattedTextentry = "";
	}

	/**
	 * Adds the line data to the resultlist if the result can be displayed in
	 * the formatted view else unknown type
	 * 
	 * @param le
	 *            The secondo result containing a line
	 * */
	public void formatLine(ListExpr le) {

		if (le.listLength() != 2)
			formattedList.add("unknown type");

		if (le.first().atomType() != ListExpr.SYMBOL_ATOM)
			formattedList.add("unknown type");

		ListExpr values = le.second();

		String formattedTextentry = "";

		formattedTextentry = formattedTextentry + "Type: "
				+ le.first().symbolValue() + "\n \n"; // line

		while (!values.isEmpty()) {

			formattedTextentry = formattedTextentry + "X1: "
					+ values.first().first().realValue() + ", Y1: "
					+ values.first().second().realValue() + "\n";
			formattedTextentry = formattedTextentry + "X2: "
					+ values.first().third().realValue() + ", Y2: "
					+ values.first().fourth().realValue() + "\n";
			formattedTextentry = formattedTextentry
					+ "\n--------------------\n";
			formattedList.add(formattedTextentry);
			formattedTextentry = "";

			values = values.rest();
		}
	}

	/**
	 * Adds the region data to the resultlist if the result can be displayed in
	 * the formatted view else unknown type
	 * 
	 * @param LE
	 *            The secondo result containing a region
	 * */
	public void formatRegion(ListExpr LE) {

		System.out.println("################### Data-Type: "
				+ LE.first().symbolValue()); //

		if (LE.listLength() != 2)
			formattedList.add("unknown type");

		if (LE.first().atomType() != ListExpr.SYMBOL_ATOM)
			formattedList.add("unknown type");

		ListExpr values = LE.second().first().first();

		String formattedTextentry = "";

		formattedTextentry = formattedTextentry + "Type: "
				+ LE.first().symbolValue() + "\n \n"; // region

		while (!values.isEmpty()) {

			formattedTextentry = formattedTextentry + "X: "
					+ values.first().first().realValue() + ", Y: "
					+ values.first().second().realValue() + "\n";
			formattedList.add(formattedTextentry);
			formattedTextentry = "";

			values = values.rest();
		}
	}

	/**
	 * Adds the mpoint data to the resultlist if the result can be displayed in
	 * the formatted view else unknown type
	 * 
	 * @param LE
	 *            The secondo result containing a moving point
	 * */
	public void formatMPoint(ListExpr LE) {

		// is it really an mpoint?
		System.out.println("################### Data-Type: "
				+ LE.first().symbolValue());

		if (LE.listLength() != 2)
			formattedList.add("unknown type");

		if (LE.first().atomType() != ListExpr.SYMBOL_ATOM)
			formattedList.add("unknown type");

		ListExpr values = LE.second();

		formattedList.add("Type: " + LE.first().symbolValue());
		formattedList.add("\n--------------------\n");

		while (!values.isEmpty()) {

			formattedList.add(values.first().first().writeListExprToString()
					+ "\n" + values.first().second().writeListExprToString()
					+ "\n");
			formattedList.add("\n--------------------\n");

			values = values.rest();
		}
	}

	/**
	 * Adds the relation data to the resultlist if the result can be displayed
	 * in the formatted view else unknown type
	 * 
	 * @param LE
	 *            The secondo result containing a relation
	 * */
	public void formatRel(ListExpr LE) {

		if (LE.listLength() != 2)
			formattedList.add("unknown type");

		String formattedTextentry = "";
		String formattedTextentryWithMlabel = "";

		ListExpr values = LE.second();
		// get all values of the result
		while (!values.isEmpty()) {

			ListExpr listentry = values.first();
			ListExpr tuplelist = LE.first().second().second();

			// get all tuples of one value
			while (!listentry.isEmpty()) {

				formattedTextentry = formattedTextentry
						+ tuplelist.first().first().stringValue().trim()
						+ " : ";
				// add all other textentries, but remove the substrings: <text>,
				// </text--->, <date>, </date--->, ""
				formattedTextentry = formattedTextentry.replaceAll("\"", "");
				formattedTextentry = formattedTextentry
						.replaceAll("<text>", "");
				formattedTextentry = formattedTextentry.replaceAll(
						"</text--->", "");
				formattedTextentry = formattedTextentry.replaceAll(
						"<text></text--->", "");
				formattedTextentry = formattedTextentry
						.replaceAll("<date>", "");
				formattedTextentry = formattedTextentry.replaceAll(
						"</date--->", "");
				formattedTextentry = formattedTextentry
						+ listentry.first().writeListExprToString().trim()
						+ "\n \n";
				if(tuplelist.first().second().stringValue().equals("mlabel")){
					formattedTextentryWithMlabel = formattedTextentryWithMlabel
							+ tuplelist.first().first().stringValue().trim()
							+ " : ";
					formattedTextentryWithMlabel = formattedTextentryWithMlabel
							+ listentry.first().writeListExprToString().trim()
							+ "\n \n";
					formattedTextentryWithMlabel = formattedTextentryWithMlabel
							.replaceAll("<text>", "");
					formattedTextentryWithMlabel = formattedTextentryWithMlabel.replaceAll(
							"</text--->", "");
					formattedTextentryWithMlabel = formattedTextentryWithMlabel.replaceAll(
							"<text></text--->", "");
					formattedTextentryWithMlabel = formattedTextentryWithMlabel
							.replaceAll("<date>", "");
					formattedTextentryWithMlabel = formattedTextentryWithMlabel.replaceAll(
							"</date--->", "");
				}

				tuplelist = tuplelist.rest();
				listentry = listentry.rest();
			}

			// add all tuples for one entry
			formattedList.add(formattedTextentry);
			formattedListWithMlabel.add(formattedTextentryWithMlabel);
			formattedTextentry = "";
			formattedTextentryWithMlabel= "";

			formattedList.add("--------------------\n \n");
			formattedListWithMlabel.add("--------------------\n \n");

			values = values.rest();
		}
	}

	/**
	 * Returns the formatted Textlist
	 * 
	 * @return The formatted textlist
	 * */
	public ArrayList<String> getFormattedList() {
		return formattedList;
	}

	public ArrayList<String> getFormattedListWithMlabel() {
		return formattedListWithMlabel;
	}
}
