package de.fernuni_hagen.dna.jwh.secondopositiontransmitter.representation;

import android.util.Log;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;

/**
 * Abstract Class which must be extended by all Secondo-Representation-Objects
 * Provides the structure and genereal purpose methods to transfer the
 * Java-Objects to the Secondo-Server in a Nested-List-Format
 * 
 * @author Jerome White
 *
 */
public abstract class NLRepresentation {

	@Retention(RetentionPolicy.RUNTIME)
	@Target(ElementType.FIELD)
	public @interface Order {
		int pos();
	}

	/**
	 * Converts the object and its Members to the Secondo Nested-List-Format
	 * Default-Types like String, Boolean, Integer and Double will be handled
	 * here. Other Classes need to override the getSpecialValue() method!
	 */
	@Override
	public String toString() {
		StringBuilder sb = new StringBuilder();

		List<Field> list = Arrays.asList(this.getClass().getDeclaredFields());
		Collections.sort( list, new Comparator<Field>( ){
			@Override
			public int compare(Field lhs, Field rhs) {
				Order lhsorder = lhs.getAnnotation(Order.class);
				Order rhsorder = rhs.getAnnotation(Order.class);
				return lhsorder.pos() - rhsorder.pos();
			}
		} );

		// open NestedList
		sb.append("(");

		for (Field field : list) {
			try {

				switch (field.getType().getSimpleName()) {
				case "String":
					sb.append("\"" + field.get(this) + "\"");
					break;
				case "Boolean":
					// Translate the boolean value to TRUE or FALSE
					sb.append((Boolean) field.get(this) ? "TRUE" : "FALSE");
					break;
				case "Integer":
					sb.append(field.get(this));
					break;
				case "Double":
					sb.append(field.get(this));
					break;
				case "Date":
					sb.append(DateTime.getDateTime((Date) field.get(this)));
					break;
				default:
					// All other classes should implement
					// getSpecialValue(fieldname)
					// to handle the custom representation
					sb.append(this.getSpecialValue(field.getName()));
					break;
				}

				// Seperator after each field
				sb.append(" ");
			} catch (IllegalArgumentException | IllegalAccessException e) {
				Log.e(this.getClass().getSimpleName(), "Inflection-Error while getting Value");
			}
		}
		// Close NestedList
		sb.append(")");

		return sb.toString();
	}

	/**
	 * Generates the Stream-Type which is used initalizing the
	 * Secondo-Connection
	 * 
	 * @return The Stream-Type Example: (stream(tuple((Id int)(Value string))))
	 */
	public String getStreamType() {
		StringBuilder sb = new StringBuilder();
		// Open NestedList with stream and tuple...
		sb.append("(stream(tuple(");

		sb.append(getTupleType());

		// Close NestedList
		sb.append(")))");

		Log.i(this.getClass().getSimpleName(), "Inflection-Error while getting Value");

		return sb.toString();
	}

	/**
	 * Generates the Tuple-Type for the Representation-Class as a Nested-List
	 * string representation Each field is enclosed in brackets Default-Types
	 * like String, Boolean, Integer and Double will be handled here. Other
	 * Classes need to override the getSpecialType() method!
	 * 
	 * @return Tuple-Type as String represenation
	 */
	public String getTupleType() {
		StringBuilder sb = new StringBuilder();
		for (Field field : this.getClass().getDeclaredFields()) {
			// Open the Nested List
			sb.append("(");
			sb.append(field.getName());
			sb.append(" ");

			try {
				switch (field.getType().getSimpleName()) {
				case "String":
					sb.append("string");
					break;
				case "Boolean":
					sb.append("bool");
					break;
				case "Integer":
					sb.append("int");
					break;
				case "Double":
					sb.append("real");
					break;
				case "Date":
					sb.append("real");
					break;
				default:
					// All other classes should implement
					// getSpecialType(fieldname)
					// to handle the custom representation
					sb.append(this.getSpecialType(field.getName()));
					break;
				}
			} catch (IllegalArgumentException e) {
				Log.e(this.getClass().getSimpleName(), "Inflection-Error while getting TupleType");
			}

			// Close the Nested List
			sb.append(")");
		}
		return sb.toString();
	}

	public String getSecondoType() {
		return this.getTupleType();
	}

	public String getSecondoValue() {
		return this.toString();
	}

	/**
	 * Should be overridden by subclasses which implement a non-default
	 * Secondo-Type
	 * 
	 * @param fieldName
	 *            The name of the field holding the special Type
	 * @return Secondo-Type-String of the object of fieldName
	 */
	public String getSpecialType(String fieldName) {
		return null;
	}

	/**
	 * Should be overridden by subclasses which implement a non-default
	 * Secondo-Type
	 * 
	 * @param fieldName
	 *            The name of the field holding the special Type
	 * @return Secondo-Value of the object of fieldName
	 */
	public String getSpecialValue(String fieldName) {
		return null;
	}

	/**
	 * Should be overidden by subclasses for which SECONDO requiers special
	 * constraints
	 * 
	 * @return
	 */
	public boolean isValid() {
		boolean valid = true;
		for (Field field : this.getClass().getDeclaredFields()) {
			Object o;
			try {
				o = field.get(this);
				if (o instanceof NLRepresentation) {
					valid = valid && ((NLRepresentation) o).isValid();
				}
			} catch (IllegalArgumentException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} catch (IllegalAccessException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
		return valid;
	}

}
