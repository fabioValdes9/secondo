//This file is part of SECONDO.

//Copyright (C) 2004, University in Hagen, Department of Computer Science,
//Database Systems for New Applications.

//SECONDO is free software; you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation; either version 2 of the License, or
//(at your option) any later version.

//SECONDO is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with SECONDO; if not, write to the Free Software
//Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

package util.common;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import util.domain.Operator;
import util.domain.enums.OperatorType;
import util.domain.enums.ParameterType;
import util.domain.enums.ParanthesisType;

/**
 * Parses the operator definition file of secondo and stores
 * all relevant information in objects of type {@link Operator}
 * @author D.Merle
 */
public class OperatorSpecParser {

	/**
	 *
	 * @param filePath
	 * @return
	 * @throws IOException
	 */
	public static HashMap<String, Operator> parse(final String filePath) throws IOException {
		final HashMap<String, Operator> operators = new HashMap<>();
		final Pattern pattern = Pattern.compile("operator (.*) alias (.*) pattern (.*)");
		FileReader fileReader;
		try {
			fileReader = new FileReader(new File(filePath));
			try (BufferedReader bufferedReader = new BufferedReader(fileReader)) {
				String line = bufferedReader.readLine();
				while (line != null) {
					final Matcher matcher = pattern.matcher(line);
					if (matcher.matches()) {
						final String opName = matcher.group(1);

						if (!operators.containsKey(opName)) {
							final Operator operator = new Operator();
							operator.setName(opName);
							operator.setAlias(matcher.group(2));

							parsePattern(operator, matcher.group(3));

							operators.put(operator.getName(), operator);
							operators.put(operator.getAlias(), operator);
						}
					}
					line = bufferedReader.readLine();
				}
			} catch (final IOException e) {
				throw new IOException("An IOException occured while processing the spec file", e);
			}
		} catch (final FileNotFoundException e) {
			throw e;
		}
		return operators;
	}

	/**
	 *
	 * @param operator
	 * @param pattern
	 */
	private static void parsePattern(final Operator operator, final String pattern) {
		String temp = pattern;
		if (temp.contains("implicit")) {
			temp = temp.substring(0, temp.indexOf("implicit"));
		}
		operator.setPattern(temp);

		int operatorIndex = 0;
		if (temp.contains("infixop")) {
			operator.setOperatorType(OperatorType.INFIXOP);
			operatorIndex = temp.indexOf("infixop") + "infixop".length();
		} else {
			operator.setOperatorType(OperatorType.OP);
			operatorIndex = temp.indexOf("op") + "op".length();
		}

		final ArrayList<ParameterType> preOpParameters = new ArrayList<>();
		final ArrayList<ParameterType> postOpParameters = new ArrayList<>();

		int paramIndex = temp.indexOf("_", 0);
		while (paramIndex != -1 && paramIndex < operatorIndex) {
			preOpParameters.add(ParameterType.WILDCARD);
			paramIndex = temp.indexOf("_", paramIndex+1);
		}
		operator.setPreOpArguments(preOpParameters);


		if (temp.contains("(")) {
			operator.setParanthesisType(ParanthesisType.ROUND);
		} else if (temp.contains("[")) {
			operator.setParanthesisType(ParanthesisType.SQUARED);
		} else {
			operator.setParanthesisType(ParanthesisType.NONE);
			if (operator.getOperatorType().equals(OperatorType.OP)) {
				operator.setPostOpArguments(postOpParameters);
				return;
			}
		}

		temp = temp.substring(operatorIndex, temp.length()).trim();
		final String[] parameters = temp.split("\\s|[|]|,|;|(|)");
		for (int i = 0; i < parameters.length; i++) {
			if (parameters[i].trim().equals("_")) {
				postOpParameters.add(ParameterType.WILDCARD);
			} else if (parameters[i].trim().equals("fun")) {
				postOpParameters.add(ParameterType.FUNCTION);
			} else if (parameters[i].trim().equals("list")) {
				postOpParameters.add(ParameterType.LIST);
			} else if (parameters[i].trim().equals("funlist")) {
				postOpParameters.add(ParameterType.FUNCTION_LIST);
			}
		}
		operator.setPostOpArguments(postOpParameters);
	}
}