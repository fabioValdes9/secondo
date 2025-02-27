/*
----
This file is part of SECONDO.

Copyright (C) 2009, University in Hagen, Faculty of Mathematics and
Computer Science, Database Systems for New Applications.

SECONDO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

SECONDO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SECONDO; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
----

//paragraph [1] Title: [{\Large \bf \begin{center}] [\end{center}}]
//paragraph [10] Footnote: [{\footnote{] [}}]
//[TOC] [\tableofcontents]

May 2009. Sven Jungnickel. Optimized implementation of operators sort,
sortby, sortmergejoin and a new hashjoin implementation called
hybridhashjoin. Result of the work for my master's thesis.

[1] Implementation of the Module Extended Relation2 Algebra for
Persistent Storage

[TOC]

0 Overview

This file contains an optimized implementation of algorithms for
external sorting, sortmergejoin and a new hash-join algorithm called
hybrid hash-join. Hybrid hash-join is an advanced version of the
GRACE hash-join. These implementations are the results of my work
for my master's thesis


1 Includes and defines

*/
#include <vector>
#include <deque>
#include <sstream>
#include <stack>
#include <limits.h>
#include <set>

#include "LogMsg.h"
#include "Algebras/Relation-C++/RelationAlgebra.h"
#include "QueryProcessor.h"
#include "AlgebraManager.h"
#include "CPUTimeMeasurer.h"
#include "StandardTypes.h"
#include "Algebras/Standard-C++/LongInt.h"
#include "SecondoInterface.h"
#include "Counter.h"
#include "Algebras/TupleIdentifier/TupleIdentifier.h"
#include "Progress.h"
#include "RTuple.h"
#include "Symbols.h"
#include "Sort.h"

#include "Stream.h"

#include "Sort2Heaps.h"

#include "Dijkstra.h"

#include "Algebras/CostEstimation/ExtRelation2AlgebraCostEstimation.h"

using namespace std;

/*
2 External linking

*/

extern NestedList* nl;
extern QueryProcessor* qp;
extern AlgebraManager* am;

/*
3 Operators

All operators have been put into namespace ~extrel2~. In this
way it will later be easier to replace the existing operators
by the new implementations.

*/
namespace extrel2
{

/*
3.1 Operator ~sort~

This operator sorts a stream of tuples lexicographically.

3.1.1 Type mapping function of Operator ~sort~

Type mapping for ~sort~ is

----  (stream (tuple ((x1 t1)...(xn tn))))  
          -> ((stream (tuple ((x1 t1)...(xn tn))))
              APPEND (n i1 true i2 true ... in true))
----

The type mapping function for operator ~sort~ determines the attribute
indices of all attributes and generates a sort order specification, where
each attribute is sorted in ascending order. The number of sort attributes,
the attribute indices and a boolean flag is appended to the argument list
of the value mapping function.

*/

ListExpr SortTypeMap( ListExpr args )
{
  NList type(args);

  // check list length
  if ( !type.hasLength(1) )
  {
    return NList::typeError(
        "Operator sort expects a list of length one.");
  }

  // check if first argument is a tuple stream
  NList attr;
  if ( !type.first().checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator sort: first argument is not a tuple stream!"
        "Operator received: " + type.first().convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator sort: first argument does not contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  // create SortOrderSpecification as nested list
  // (n i1 asc i2 asc ... in asc)
  // n: number of tuple attributes
  // i1: index of first attribute (1-based)
  // i2: index of second attribute and so on (1-based)
  // ...
  // asc: boolean value true (ascending sort order = true)
  int n = attr.length();

  NList sortDesc;
  sortDesc.append(NList(n));

  for(int i = 1; i <= n; i++)
  {
    sortDesc.append(NList(i));
    sortDesc.append(NList(true,true));
  }

  return NList(NList(Symbol::APPEND()), sortDesc, type.first()).listExpr();
}

/*
3.1.2 Value mapping function of operator ~sort~

The argument vector ~args~ contains in the first slot ~args[0]~ the
stream and in ~args[2]~ the number of sort attributes. ~args[3]~
contains the index of the first sort attribute, ~args[4]~ a boolean
indicating whether the stream is sorted in ascending order with regard
to the sort first attribute. ~args[5]~ and ~args[6]~ contain these
values for the second sort attribute  and so on.

*/

int SortValueMap( Word* args, Word& result,
                   int message, Word& local, Supplier s );

/*
3.1.3 Specification of operator ~sort~

*/
const string SortSpec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                         "\"Example\" ) "
                         "( <text>((stream (tuple([a1:d1, ... ,an:dn]"
                         ")))) -> (stream (tuple([a1:d1, ... ,an:dn])))"
                         "</text--->"
                         "<text>_ sort</text--->"
                         "<text>Sorts an input stream lexicographically."
                         "</text--->"
                         "<text>query cities feed sort consume</text--->"
                         ") )";

/*
3.1.4 Definition of operator ~sort~

*/
Operator extrelsort (
         "sort",                 // name
         SortSpec,                // specification
         SortValueMap<1, false>,  // value mapping - first argument
                                  // of sort order spec is 1
         Operator::SimpleSelect,  // trivial selection function
         SortTypeMap              // type mapping
);

/*
3.2 Operator ~sortby~

This operator sorts a stream of tuples by a given list of attributes.
For each attribute it can be specified whether the list should be sorted
in ascending (asc) or descending (desc) order with regard to that attribute.

3.2.1 Type mapping function of operator ~sortby~

Type mapping for ~sortby~ is

---- ((stream (tuple ((x1 t1)...(xn tn)))) ((xi1 asc/desc) ... (xij asc/desc)))
             -> ((stream (tuple ((x1 t1)...(xn tn))))
                 APPEND (j i1 true/false i2 true/false ... ij true/false))
----

The type mapping function determines the attribute indices of the given list
of attributes and generates a corresponding sort order specification. The
number of sort attributes, the attribute indices and a boolean flag,
indicating an ascending (true) or descending sort order (false), is
appended to the argument list of the value mapping function.

*/

ListExpr SortByTypeMap( ListExpr args )
{
  NList type(args);

  // check list length
  if ( !type.hasLength(2) )
  {
    return NList::typeError(
        "Operator sortby expects a list of "
        "length two.");
  }

  NList streamDesc = type.first();
  NList attrDesc = type.second();

  // check if first argument is a tuple stream
  NList attr;
  if ( !streamDesc.checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator sortby: first argument is not a tuple stream!"
        "Operator received: " + streamDesc.convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator sortby: first argument does not "
        "contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  int numberOfSortAttrs = attrDesc.length();

  // check length of attribute specification
  if ( numberOfSortAttrs <= 0 )
  {
    return NList::typeError(
        "Operator sortby: sort order specification "
        "list may not be empty!");
  }

  NList sortDesc;
  sortDesc.append(NList(numberOfSortAttrs));
  NList rest = attrDesc;

  // process attribute description
  while( !rest.isEmpty() )
  {
    // extract first element of attribute specification
    NList attrElem = rest.first();

    // cut off first element
    rest.rest();

    // attrElem may be an atom (no optional asc/desc specifier)
    // or a list of two (with asc/desc specifier)
    if ( !(attrElem.isAtom() || attrElem.length() == 2) )
    {
      return NList::typeError(
          "Operator sortby expects as second argument "
          "a list of (attrname [asc, desc])|attrname.");
    }

    string attrName;

    // handle the two different cases
    if ( attrElem.length() == 2 )
    {
      // check types of attrElem
      if ( !(attrElem.first().isAtom() &&
             attrElem.first().isSymbol() &&
             attrElem.second().isAtom() &&
             attrElem.second().isSymbol() ) )
      {
        return NList::typeError(
            "Operator sortby expects as second argument a list"
            " of (attrname [asc, desc])|attrname .\n"
            "Operator sortby gets a list '"
            + attrDesc.convertToString() + "'.");
      }
      attrName = attrElem.first().str();
    }
    else
    {
      // check type of atom in attrElem
      if ( !(attrElem.isSymbol()) )
      {
        return NList::typeError(
            "Operator sortby expects as second argument a list"
            " of (attrname [asc, desc])|attrname .\n"
            "Operator sortby gets a list '"
            + attrDesc.convertToString() + "'.");
      }
      attrName = attrElem.str();
    }

    // determine attribute index (1-based)
    ListExpr attrType;
    int j = FindAttribute(attr.listExpr(), attrName, attrType);

    // determine asc/desc specifier
    if (j > 0)
    {
      bool isAscending = true;

      if( attrElem.length() == 2 )
      {
         if ( !( attrElem.second().str() == "asc" ||
                 attrElem.second().str() == "desc" ) )
         {
           return NList::typeError(
               "Operator sortby sorting criteria must "
               "be asc or desc, not '"
               + attrElem.second().convertToString() + "'!");
         }
         isAscending = attrElem.second().str() == "asc" ? true : false;
      }

      sortDesc.append(NList(j));
      sortDesc.append(NList(isAscending, isAscending));
    }
    else
    {
      return NList::typeError(
          "Operator sortby: attribute name '" + attrName +
          "' is not known.\nKnown Attribute(s): "
          + attr.convertToString());
    }
  }

  return NList(NList(Symbol::APPEND()), sortDesc, streamDesc).listExpr();
}
/*

3.2.2 Specification of operator ~sortby~

*/
const string SortBySpec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                           "\"Example\" ) "
                           "( <text>((stream (tuple([a1:d1, ... ,an:dn])))"
                           " ((xi1 asc/desc) ... (xij [asc/desc]))) -> "
                           "(stream (tuple([a1:d1, ... ,an:dn])))</text--->"
                           "<text>_ sortby [list]</text--->"
                           "<text>Sorts an input stream according to a list "
                           "of attributes ai1 ... aij. For each attribute one "
                           "may specify the sorting order (asc/desc). If no "
                           "order is specified, ascending is assumed."
                           "</text--->"
                           "<text>query employee feed sortby[DeptNo asc] "
                           "consume</text--->"
                              ") )";

/*
3.2.3 Definition of operator ~sortby~

*/
Operator extrelsortby (
         "sortby",               // name
         SortBySpec,              // specification
         SortValueMap<2, false>,  // value mapping - first argument
                                  // of sort order spec is 2
         Operator::SimpleSelect,  // trivial selection function
         SortByTypeMap            // type mapping
);


/*
3.3 Operator ~sortmergejoin~

This operator sorts two input streams and computes their equijoin.

3.3.1 Type mapping function of operator ~sortmergejoin~

The type mapping function determines the attribute indices of the given
join attributes ~xi~ and ~yi~ and appends them to the argument list
of the value mapping function. This type mapping function is also used
by the ~hybridhashjoin~, ~hybridhashjoinParam~, ~gracehashjoin~ and
~gracehashjoinParam~ operator.

Type mapping for ~sortmergejoin~, ~hybridhashjoin~, ~hybridhashjoinParam~,
~gracehashjoin~ and ~gracehashjoinParam~ is

----  ((stream (tuple ((x1 t1) ... (xn tn))))
       (stream (tuple ((y1 d1) ... (ym dm)))) xi yj)
         -> (stream (tuple ((x1 t1) ... (xn tn) (y1 d1) ... (ym dm))))
            APPEND (i j)
----


*/

template<int n>
ListExpr JoinTypeMap( ListExpr args )
{
  NList type(args);

  const char* op[] = { "hybridhashjoin",
                       "hybridhashjoinParam",
                       "sortmergejoin",
                       "sortmergejoinParam",
                       "gracehashjoin",
                       "gracehashjoinParam" };

  const char* ex[] = { "five", "eight",
                       "four", "five",
                       "five", "eight" };

  int expected;

  switch(n)
  {
    case 0: expected = 5; break;
    case 1: expected = 8; break;
    case 2: expected = 4; break;
    case 3: expected = 5; break;
    case 4: expected = 5; break;
    case 5: expected = 8; break;
    default: assert(0); break;
  }

  // check list length
  if ( !type.hasLength(expected) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) +
        ": expects a list of length " + string(ex[n]) );
  }

  NList streamA = type.first();
  NList streamB = type.second();
  NList joinA = type.third();
  NList joinB = type.fourth();

  // check if type of first argument is a tuple stream
  NList attrA;
  if ( !streamA.checkStreamTuple(attrA) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": first argument is not "
        "a tuple stream!"
        "Operator received: " + streamA.convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attrA.listExpr()) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": first argument does "
        "not contain a tuple description!"
        "Operator received: " + attrA.convertToString() );
  }

  // check if type of second argument is a tuple stream
  NList attrB;
  if ( !streamB.checkStreamTuple(attrB) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": second argument is not "
        "a tuple stream!"
        "Operator received: " + streamB.convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attrB.listExpr()) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": second argument does no "
        "contain a tuple description!"
        "Operator received: " + attrB.convertToString() );
  }

  // check if attribute names are disjoint
  if ( !AttributesAreDisjoint(attrA.listExpr(), attrB.listExpr()) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": Attribute names of first "
        "and second argument "
        "list must be disjoint.\n Attribute names of first list are: '" +
        attrA.convertToString() +
        "'.\n Attribute names of second list are: '" +
        attrB.convertToString() + "'." );
  }

  // Concatenate tuple descriptions and create tuple stream
  // for joined relations
  NList attr = NList(ConcatLists(attrA.listExpr(), attrB.listExpr()));
  NList stream = NList(NList(Symbol::STREAM(),
                             NList(NList(Tuple::BasicType()), attr)));

  // check types of join attributes
  if ( !(joinA.isAtom() && joinA.isSymbol() &&
         joinB.isAtom() && joinB.isSymbol() ) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": Join attributes must be of "
        "type SymbolType!\n");
  }

  // check if join attribute of first stream is tuple description
  ListExpr attrTypeA;
  int attrAIndex = FindAttribute(attrA.listExpr(), joinA.str(), attrTypeA);
  if ( !(attrAIndex > 0) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": First join attribute '" +
         joinA.str() + "' is not in first argument list '" +
         attrA.convertToString() +"'.\n");
  }


  // check if join attribute of second stream is tuple description
  ListExpr attrTypeB;
  int attrBIndex = FindAttribute(attrB.listExpr(), joinB.str(), attrTypeB);
  if ( !(attrBIndex > 0) )
  {
    return NList::typeError(
        "Operator " + string(op[n]) + ": Second join attribute '" +
         joinB.str() + "' is not in second argument list '" +
         attrB.convertToString() +"'.\n");
  }

  // check if types of the join attributes match
  if ( NList(attrTypeA) != NList(attrTypeB) )
  {
    return NList::typeError(
          "Operator " + string(op[n]) +
          ": Type of first join attribute "
          "is different from type of second join argument.\n");

  }

  if( n == 0 || n == 1 )
  {
    if ( type.fifth().str() != CcInt::BasicType() )
    {
      return NList::typeError(
            "Operator " + string(op[n]) +
            ": Parameter 'number of buckets' "
            "must be of type int.\n");
    }
  }

  if( n == 3 )
  {
    if ( type.fifth().str() != CcInt::BasicType() )
    {
      return NList::typeError(
            "Operator " + string(op[n]) +
            ": Parameter 'maximum operator memory' "
            "must be of type int.\n");
    }
  }

  if( n == 1 )
  {
    if ( type.elem(6).str() != CcInt::BasicType() )
    {
      return NList::typeError(
            "Operator " + string(op[n]) +
            ": Parameter 'number of partitions' "
            "must be of type int.\n");
    }

    if ( type.elem(7).str() != CcInt::BasicType() )
    {
      return NList::typeError(
            "Operator " + string(op[n]) +
            ": Parameter 'maximum memory size' "
            "must be of type int.\n");
    }

    if ( type.elem(8).str() != CcInt::BasicType() )
    {
      return NList::typeError(
            "Operator " + string(op[n]) +
            ": Parameter 'I/O buffer size' "
            "must be of type int.\n");
    }
  }

  return NList( NList(Symbol::APPEND()), NList( NList(attrAIndex),
      NList(attrBIndex)), stream).listExpr();
}

/*
Some explicit instantiations in order to use them also
outside of the implementation file.

*/

template ListExpr
JoinTypeMap<0>(ListExpr args);

template ListExpr
JoinTypeMap<1>(ListExpr args);

template ListExpr
JoinTypeMap<2>(ListExpr args);

template ListExpr
JoinTypeMap<3>(ListExpr args);

template ListExpr
JoinTypeMap<4>(ListExpr args);

template ListExpr
JoinTypeMap<5>(ListExpr args);

/*
3.3.2 Value mapping function of operator ~sortmergejoin~

This value mapping function is used by both operators ~sortmergejoin~ and
~sortmergejoinParam~. According to the value of the template parameter
~param~ the argument vector ~args~ contains the following values. If ~param~
is set to false ~args~ contains.

  * args[0] : stream A

  * args[1] : stream B

  * args[2] : attribute name of join attribute for stream A

  * args[3] : attribute name join attribute for stream B

  * args[4] : attribute index of the join attribute from stream A

  * args[5] : attribute index of the join attribute from stream B

If ~param~ is set to true ~args~ contains.

  * args[0] : stream A

  * args[1] : stream B

  * args[2] : attribute name of join attribute for stream A

  * args[3] : attribute name join attribute for stream B

  * args[4] : usable main memory in bytes

  * args[5] : attribute index of the join attribute from stream A

  * args[6] : attribute index of the join attribute from stream B


*/
template<bool param>
int SortMergeJoinValueMap( Word* args, Word& result,
                             int message, Word& local, Supplier s);

/*
3.3.3 Specification of operator ~sortmergejoin~

*/
const string SortMergeJoinSpec  = "( ( \"Signature\" \"Syntax\" "
                                   "\"Meaning\" \"Example\" ) "
                                   "( <text>((stream (tuple ((x1 t1) ... "
                                   "(xn tn)))) (stream (tuple ((y1 d1) ..."
                                   " (ym dm)))) xi yj) -> (stream (tuple "
                                   "((x1 t1) ... (xn tn) (y1 d1) ... (ym dm)"
                                   ")))</text--->"
                                   "<text>_ _ sortmergejoin [ _ , _ ]"
                                   "</text--->"
                                   "<text>Computes the equijoin of two "
                                   "streams using the new sort operator "
                                   "implementation.</text--->"
                                   "<text>query duplicates feed ten feed "
                                   "sortmergejoin[no, nr] consume</text--->"
                                   ") )";
/*
3.1.4 Costestimation for operator ~sortmergejoin~

*/
CostEstimation* SortmergeJoinCostEstimationFunc() {
  return new SortMergeJoinCostEstimation();
}


/*
3.3.4 Definition of operator ~sortmergejoin~

*/
Operator extrelsortmergejoin (
         "sortmergejoin",              // name
         SortMergeJoinSpec,             // specification
         SortMergeJoinValueMap<false>,  // value mapping
         Operator::SimpleSelect,        // trivial selection function
         JoinTypeMap<2>,                // type mapping
         SortmergeJoinCostEstimationFunc // cost estimation
);

/*
3.4 Operator ~hybridhashjoin~

This computes the equijoin of two input streams making use of the
hybrid hash join algorithm.

3.4.2 Value mapping function of operator ~hybridhashjoin~

This value mapping function is used by both operators ~hybridhashjoin~ and
~hybridhashjoinParam~. According to the value of the template parameter
~param~ the argument vector ~args~ contains the following values. If ~param~
is set to false ~args~ contains.

  * args[0] : stream A

  * args[1] : stream B

  * args[2] : attribute name of join attribute for stream A

  * args[3] : attribute name join attribute for stream B

  * args[4] : number of buckets

  * args[5] : attribute index of join attribute for stream A

  * args[6] : attribute index of join attribute for stream B

If ~param~ is set to true ~args~ contains.

  * args[0] : stream A

  * args[1] : stream B

  * args[2] : attribute name of join attribute for stream A

  * args[3] : attribute name join attribute for stream B

  * args[4] : number of buckets

  * args[5] : number of partitions (only if param is true)

  * args[6] : usable main memory in bytes (only if param is true)

  * args[7] : I/O buffer size in bytes (only if param is true)

  * args[8] : attribute index of join attribute for stream A

  * args[9] : attribute index of join attribute for stream B


*/
template<bool param>
int HybridHashJoinValueMap( Word* args, Word& result,
                              int message, Word& local, Supplier s );

/*
3.1.3 Specification of operator ~hybridhashjoin~

*/
const string HybridHashJoinSpec  = "( ( \"Signature\" \"Syntax\" "
                                   "\"Meaning\" \"Example\" ) "
                                   "( <text>((stream (tuple ((x1 t1) ... "
                                   "(xn tn)))) (stream (tuple ((y1 d1) ..."
                                   " (ym dm)))) xi yj b) -> (stream (tuple "
                                   "((x1 t1) ... (xn tn) (y1 d1) ... (ym dm)"
                                   ")))</text--->"
                                   "<text>_ _ hybridhashjoin [ _ , _ , _]"
                                   "</text--->"
                                   "<text>Computes the equijoin of two "
                                   "streams using using the hybrid hash-join "
                                   "algorithm. The third and fourth parameter "
                                   "contain the attribute names of the join "
                                   "attributes. The fifth argument specifies "
                                   "the number of buckets used by the "
                                   "algorithm. The number of used partitions "
                                   "used is computed automatically.</text--->"
                                   "<text>query duplicates feed ten feed "
                                   "hybridhashjoin[no, nr, 1000] consume "
                                   "</text--->) )";
/*
3.1.4 Costestimation for operator ~hybridhashjoin~

*/
CostEstimation* HybridHashCostEstimationFunc() {
  return new HybridHashJoinCostEstimation();
}


/*
3.1.5 Definition of operator ~hybridhashjoin~

*/
Operator extrelhybridhashjoin(
         "hybridhashjoin",              // name
         HybridHashJoinSpec,            // specification
         HybridHashJoinValueMap<false>, // value mapping - first
                                        // argument of sort order spec is 1
         Operator::SimpleSelect,        // trivial selection function
         JoinTypeMap<0>,                // type mapping
         HybridHashCostEstimationFunc   // cost estimation
);

/*
3.4 Operator ~gracehashjoin~

This computes the equijoin of two input streams making use of the
GRACE hash join algorithm.

3.4.2 Value mapping function of operator ~gracehashjoin~

This value mapping function is used by both operators ~gracehashjoin~ and
~gracehashjoinParam~. According to the value of the template parameter
~param~ the argument vector ~args~ contains the following values. If ~param~
is set to false ~args~ contains.

  * args[0] : stream A

  * args[1] : stream B

  * args[2] : attribute name of join attribute for stream A

  * args[3] : attribute name join attribute for stream B

  * args[4] : number of buckets

  * args[5] : attribute index of join attribute for stream A

  * args[6] : attribute index of join attribute for stream B

If ~param~ is set to true ~args~ contains.

  * args[0] : stream A

  * args[1] : stream B

  * args[2] : attribute name of join attribute for stream A

  * args[3] : attribute name join attribute for stream B

  * args[4] : number of buckets

  * args[5] : number of partitions (only if param is true)

  * args[6] : usable main memory in bytes (only if param is true)

  * args[7] : I/O buffer size in bytes (only if param is true)

  * args[8] : attribute index of join attribute for stream A

  * args[9] : attribute index of join attribute for stream B


*/
template<bool param>
int GraceHashJoinValueMap( Word* args, Word& result,
                           int message, Word& local, Supplier s );

/*
3.1.3 Specification of operator ~gracehashjoin~

*/
const string GraceHashJoinSpec  = "( ( \"Signature\" \"Syntax\" "
                                   "\"Meaning\" \"Example\" ) "
                                   "( <text>((stream (tuple ((x1 t1) ... "
                                   "(xn tn)))) (stream (tuple ((y1 d1) ..."
                                   " (ym dm)))) xi yj b) -> (stream (tuple "
                                   "((x1 t1) ... (xn tn) (y1 d1) ... (ym dm)"
                                   ")))</text--->"
                                   "<text>_ _ gracehashjoin [ _ , _ , _]"
                                   "</text--->"
                                   "<text>Computes the equijoin of two "
                                   "streams using using the GRACE hash-join "
                                   "algorithm. The third and fourth parameter "
                                   "contain the attribute names of the join "
                                   "attributes. The fifth argument specifies "
                                   "the number of buckets used by the "
                                   "algorithm. The number of used partitions "
                                   "used is computed automatically.</text--->"
                                   "<text>query duplicates feed ten feed "
                                   "gracehashjoin[no, nr, 1000] consume "
                                   "</text--->) )";
/*
3.1.4 Costestimation for operator ~gracehashjoin~

*/
CostEstimation* GraceHashCostEstimationFunc() {
  return new GraceHashJoinCostEstimation();
}

/*
3.1.5 Definition of operator ~gracehashjoin~

*/
Operator extrelgracehashjoin(
         "gracehashjoin",              // name
         GraceHashJoinSpec,            // specification
         GraceHashJoinValueMap<false>, // value mapping - first
                                        // argument of sort order spec is 1
         Operator::SimpleSelect,        // trivial selection function
         JoinTypeMap<4>,                 // type mapping
         GraceHashCostEstimationFunc   // Cost Estimation
);

/*
4 Test Operators

These operators were used to test the functionality of single classes
that are used within the operators of this algebra or for benchmarking.

4.2 Operator ~tuplefile~

This operator stores a tuple stream into a temporary tuple file and
reads the tuples from this file again when requested from the its
successor.

4.2.1 Type mapping function of Operator ~tuplefile~

Type mapping for ~tuplefile~ is

----  (stream (tuple ((x1 t1)...(xn tn))) int )  ->
      (stream (tuple ((x1 t1)...(xn tn))))
----

The second argument specifies the size of the I/O buffer in bytes.
If -1 is specified the default value will be used (page size of
file system).

*/
ListExpr TupleFileTypeMap(ListExpr args)
{
  NList type(args);

  // check list length
  if ( !type.hasLength(2) )
  {
    return NList::typeError(
        "Operator tuplefile expects a list of length two.");
  }

  NList attr;
  if ( !type.first().checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator tuplefile: first argument is not a tuple stream!"
        "Operator received: " + type.first().convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator tuplefile: first argument does "
        "not contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  if ( type.second() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator tuplefile: second argument must be an integer!"
        "Operator received: " + type.second().convertToString() );
  }

  return type.first().listExpr();
}

/*
4.2.2 Value mapping function of operator ~tuplefile~

*/
int TupleFileValueMap( Word* args, Word& result,
                        int message, Word& local, Supplier s );

/*
4.2.3 Specification of operator ~tuplefile~

*/
const string TupleFileSpec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                            "\"Example\" ) "
                            "( <text>((stream (tuple([a1:d1, ... ,an:dn]"
                            ")))) -> ((stream (tuple([a1:d1, ... ,an:dn]))))"
                            "</text--->"
                            "<text>_ tuplefile[_]</text--->"
                            "<text>Stores a stream temporarily in a tuple "
                            "file and restores the tuples from file when "
                            "they are requested by the next operator."
                            "The second argument "
                            "contains the size of the I/O buffer in bytes. "
                            "If -1 is specified the default value will be "
                            "used (page size file system)."
                            "</text---><text>query cities feed "
                            "tuplefile[-1] consume</text--->) )";

/*
4.2.4 Definition of operator ~tuplefile~

*/
Operator tuplefiletest (
         "tuplefile",             // name
         TupleFileSpec,           // specification
         TupleFileValueMap,       // value mapping
         Operator::SimpleSelect,  // trivial selection function
         TupleFileTypeMap         // type mapping
);

/*
4.3 Operator ~tuplebuffer~

This operator stores a tuple stream into a ~TupleBuffer~ instance and
reads the tuples from the buffer again when requested from the its
successor.

4.3.1 Type mapping function of Operator ~tuplebuffer~

Type mapping for ~tuplebuffer~ is

----  ((stream (tuple ((x1 t1)...(xn tn)))) int)  ->
       (stream (tuple ((x1 t1)...(xn tn))))
----

The second argument is the buffer size in KBytes.

*/
ListExpr TupleBufferTypeMap(ListExpr args)
{
  NList type(args);

  // check list length
  if ( !type.hasLength(2) )
  {
    return NList::typeError(
        "Operator tuplebuffer expects a list of length two.");
  }

  NList attr;
  if ( !type.first().checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator tuplebuffer: first argument is not a tuple stream!"
        "Operator received: " + type.first().convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator tuplebuffer: first argument does not "
        "contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  if ( type.second() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator tuplebuffer: second argument must be an integer!"
        "Operator received: " + type.second().convertToString() );
  }

  return type.first().listExpr();
}

/*
4.3.2 Value mapping function of operator ~tuplebuffer~

*/
int TupleBufferValueMap( Word* args, Word& result,
                           int message, Word& local, Supplier s);

/*
4.3.3 Specification of operator ~tuplebuffer~

*/
const string TupleBufferSpec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                            "\"Example\" ) "
                            "( <text>((stream (tuple([a1:d1, ... ,an:dn]"
                            "))) int) -> "
                            "(stream (tuple([a1:d1, ... ,an:dn])))"
                            "</text--->"
                            "<text>_ tuplebuffer[_]</text--->"
                            "<text>Stores a stream temporarily in a tuple "
                            "buffer (class TupleBuffer) and restores the "
                            "tuples when they are "
                            "requested by the next operator. The in-memory "
                            "buffer size is specified in KBytes as an "
                            "additional argument."
                            "</text---><text>query cities feed "
                            "tuplebuffer[256] consume</text--->) )";

/*
4.3.4 Definition of operator ~tuplebuffer~

*/
Operator tuplebuffer(
         "tuplebuffer",           // name
         TupleBufferSpec,         // specification
         TupleBufferValueMap,     // value mapping
         Operator::SimpleSelect,  // trivial selection function
         TupleBufferTypeMap       // type mapping
);

/*
4.4 Operator ~tuplebuffer2~

This operator stores a tuple stream into a ~TupleBuffer2~ instance and
reads the tuples from the buffer again when requested from the its
successor.

4.4.1 Type mapping function of Operator ~tuplebuffer2~

Type mapping for ~tuplebuffer2~ is

----  ((stream (tuple ((x1 t1)...(xn tn)))) int int)  ->
       (stream (tuple ((x1 t1)...(xn tn))))
----

The second argument is the buffer size in KBytes. The third argument
is the size of the I/O buffer. If -1 is specified the default value
(page size of file system will be used)

*/
ListExpr TupleBuffer2TypeMap(ListExpr args)
{
  NList type(args);

  // check list length
  if ( !type.hasLength(3) )
  {
    return NList::typeError(
        "Operator tuplebuffer2 expects a list of length three.");
  }

  NList attr;
  if ( !type.first().checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator tuplebuffer2: first argument is not a tuple stream!"
        "Operator received: " + type.first().convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator tuplebuffer2: first argument does not "
        "contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  if ( type.second() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator tuplebuffer2: second argument must be an integer!"
        "Operator received: " + type.second().convertToString() );
  }

  if ( type.third() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator tuplebuffer2: third argument must be an integer!"
        "Operator received: " + type.third().convertToString() );
  }

  return type.first().listExpr();
}

/*
4.4.2 Value mapping function of operator ~tuplebuffer2~

*/
int TupleBuffer2ValueMap( Word* args, Word& result,
                            int message, Word& local, Supplier s);

/*
4.4.3 Specification of operator ~tuplebuffer2~

*/
const string TupleBuffer2Spec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                            "\"Example\" ) "
                            "( <text>((stream (tuple([a1:d1, ... ,an:dn]"
                            "))) int int) -> "
                            "(stream (tuple([a1:d1, ... ,an:dn])))"
                            "</text--->"
                            "<text>_ tuplebuffer2[_, _]</text--->"
                            "<text>Stores a stream temporarily in a tuple "
                            "buffer (class TupleBuffer2) and restores "
                            "the tuples when they are "
                            "requested by the next operator. The in-memory "
                            "buffer size is specified in KBytes as an "
                            "additional argument. The third argument "
                            "contains the size of the I/O buffer in bytes. "
                            "If -1 is specified the default value will be "
                            "used (page size file system)."
                            "</text---><text>query cities feed "
                            "tuplebuffer2[256,-1] consume</text--->) )";

/*
4.4.4 Definition of operator ~tuplebuffer2~

*/
Operator tuplebuffer2(
         "tuplebuffer2",           // name
         TupleBuffer2Spec,         // specification
         TupleBuffer2ValueMap,     // value mapping
         Operator::SimpleSelect,  // trivial selection function
         TupleBuffer2TypeMap       // type mapping
);

/*
4.8 Operator ~sortParam~

This operator is used to simplify the testing of the new
~sort~ operator implementation. The operator takes three additional
parameters, the second argument specifies the used operators main
memory in bytes, the third argument the maximum fan-in of a merge
phase respectively the maximum number of temporary open tuple
files and the fourth argument specifies the size of the
I/O buffer for read/write operations on disc.

4.8.1 Type mapping function of Operator ~sortParam~

Type mapping for operator ~sortParam~ is

----  ((stream (tuple ((x1 t1)...(xn tn)))) int int int)  ->
      (stream (tuple ((x1 t1)...(xn tn))) int int int
      APPEND (n i1 true i2 true ... in true))
----

*/
ListExpr SortParamTypeMap(ListExpr args)
{
  NList type(args);

  // check list length
  if ( !type.hasLength(4) )
  {
    return NList::typeError(
        "Operator sortParam expects a list of length four.");
  }

  // check if first argument is a tuple stream
  NList attr;
  if ( !type.first().checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator sortParam: first argument is not a tuple stream!"
        "Operator received: " + type.first().convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator sortParam: first argument does not "
        "contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  // create SortOrderSpecification as nested list
  // (n i1 asc i2 asc ... in asc)
  // n: number of tuple attributes
  // i1: index of first attribute (1-based)
  // i2: index of second attribute and so on (1-based)
  // ...
  // asc: boolean value true (ascending sort order = true)
  NList tupleDesc = type.first().second().second();
  int n = tupleDesc.length();

  NList sortDesc;
  sortDesc.append(NList(n));

  for(int i = 1; i <= n; i++)
  {
    sortDesc.append(NList(i));
    sortDesc.append(NList(true,true));
  }

  // check if second argument is an integer (operator memory)
  if ( type.second() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator sortParam: second argument (operator memory) "
        "must be an integer!"
        "Operator received: " + type.second().convertToString() );
  }

  // check if third argument is an integer (maximum fan-in merge-phase)
  if ( type.third() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator sortParam: third argument (maximum fan-in merge-phase)"
        " must be an integer!"
        "Operator received: " + type.third().convertToString() );
  }

  // check if fourth argument is an integer (I/O buffer size)
  if ( type.fourth() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator sortParam: fourth argument ('I/O buffer size') "
        "must be an integer!"
        "Operator received: " + type.fourth().convertToString() );
  }

  return NList(NList(Symbol::APPEND()), sortDesc, type.first()).listExpr();
}

/*
4.8.2 Value mapping function of operator ~sortParam~

The value mapping function used is identical to that of operator ~sort~.


4.8.3 Specification of operator ~sortParam~

*/

const string SortParamSpec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                            "\"Example\" ) "
                            "( <text>((stream (tuple([a1:d1, ... ,an:dn]"
                            "))) int int int) -> "
                            "(stream (tuple([a1:d1, ... ,an:dn])))"
                            "</text--->"
                            "<text>_ sortParam[_, _, _]</text--->"
                            "<text>This operator is used to simplify the "
                            "testing of "
                            "the new sort operator implementation. The "
                            "operator takes three additional parameters. "
                            "The first one specifies the usable main "
                            "memory in bytes. The second one the maximum "
                            "fan-in for the merge phase respectively the "
                            "maximum number of temporary open tuple files. "
                            "The third parameter specifies the I/O buffer "
                            "size for read/write operations on disc. Usable "
                            "main memory size must be between 1-65536 KByte. "
                            "The maximum fan-in is limited by 2-1000. "
                            "The maximum size of the I/O buffer is 16384 "
                            "Bytes. If these limits are exceeded default "
                            "values will be used instead.</text--->"
                            "<text>query plz feed sortParam[16386,50,4096]"
                            " consume</text--->) )";

/*
4.8.4 Definition of operator ~sortParam~

*/
Operator extrelsortParam(
         "sortParam",             // name
         SortParamSpec,           // specification
         SortValueMap<4, true>,   // value mapping
         Operator::SimpleSelect,  // trivial selection function
         SortParamTypeMap         // type mapping
);

/*
4.9 Operator ~sortbyParam~

This operator is used to simplify the testing of the new
~sortby~ operator implementation. The operator takes three additional
parameters, the first one specifies the useable main memory
memory in Bytes, the second one the maximum fan-in of a merge
phase respectively the maximum number of temporary open tuple
files and the third one specifies the I/O buffer size in bytes
for read/write operations on disc.

4.9.1 Type mapping function of Operator ~sortbyParam~

Type mapping for ~sortbyParam~ is

----  ((stream (tuple ((x1 t1)...(xn tn))))
                      ((xi1 asc/desc) ... (xij asc/desc)) int int int)
              -> ((stream (tuple ((x1 t1)...(xn tn)))) int int int
                  APPEND (j i1 true/false i2 true/false ... ij true/false))
----

*/

ListExpr SortByParamTypeMap( ListExpr args )
{
  NList type(args);

  // check list length
  if ( !type.hasLength(5) )
  {
    return NList::typeError(
        "Operator sortbyParam expects a list of "
        "length five.");
  }

  NList streamDesc = type.first();
  NList attrDesc = type.second();

  // check if first argument is a tuple stream
  NList attr;
  if ( !streamDesc.checkStreamTuple(attr) )
  {
    return NList::typeError(
        "Operator sortbyParam: first argument is not a tuple stream!"
        "Operator received: " + streamDesc.convertToString() );
  }

  // check if there is a valid tuple description
  if ( !IsTupleDescription(attr.listExpr()) )
  {
    return NList::typeError(
        "Operator sortbyParam: first argument does not "
        "contain a tuple description!"
        "Operator received: " + attr.convertToString() );
  }

  int numberOfSortAttrs = attrDesc.length();

  // check length of attribute specification
  if ( numberOfSortAttrs <= 0 )
  {
    return NList::typeError(
        "Operator sortbyParam: sort order specification "
        "list may not be empty!");
  }

  NList sortDesc;
  sortDesc.append(NList(numberOfSortAttrs));
  NList rest = attrDesc;

  // process attribute description
  while( !rest.isEmpty() )
  {
    // extract first element of attribute specification
    NList attrElem = rest.first();

    // cut off first element
    rest.rest();

    // attrElem may be an atom (no optional asc/desc specifier)
    // or a list of two (with asc/desc specifier)
    if ( !(attrElem.isAtom() || attrElem.length() == 2) )
    {
      return NList::typeError(
          "Operator sortbyParam expects as second argument "
          "a list of (attrname [asc, desc])|attrname.");
    }

    string attrName;

    // handle the two different cases
    if ( attrElem.length() == 2 )
    {
      // check types of attrElem
      if ( !(attrElem.first().isAtom() &&
             attrElem.first().isSymbol() &&
             attrElem.second().isAtom() &&
             attrElem.second().isSymbol() ) )
      {
        return NList::typeError(
            "Operator sortbyParam expects as second argument a list"
            " of (attrname [asc, desc])|attrname .\n"
            "Operator sortbywith gets a list '"
            + attrDesc.convertToString() + "'.");
      }
      attrName = attrElem.first().str();
    }
    else
    {
      // check type of atom in attrElem
      if ( !(attrElem.isSymbol()) )
      {
        return NList::typeError(
            "Operator sortbyParam expects as second argument a list"
            " of (attrname [asc, desc])|attrname .\n"
            "Operator sortbywith gets a list '"
            + attrDesc.convertToString() + "'.");
      }
      attrName = attrElem.str();
    }

    // determine attribute index (1-based)
    ListExpr attrType;
    int j = FindAttribute(streamDesc.second().second().listExpr(),
                          attrName, attrType);

    // determine asc/desc specifier
    if (j > 0)
    {
      bool isAscending = true;

      if( attrElem.length() == 2 )
      {
         if ( !( attrElem.second().str() == "asc" ||
                 attrElem.second().str() == "desc" ) )
         {
           return NList::typeError(
               "Operator sortbyParam sorting criteria must "
               "be asc or desc, not '"
               + attrElem.second().str() + "'!");
         }
         isAscending = attrElem.second().str() == "asc" ? true : false;
      }

      sortDesc.append(NList(j));
      sortDesc.append(NList(isAscending, isAscending));
    }
    else
    {
      return NList::typeError(
          "Operator sortbyParam: attribute name '" + attrName +
          "' is not known.\nKnown Attribute(s): "
          + streamDesc.second().second().convertToString());
    }
  }

  // check if third argument is an integer (operator memory)
  if ( type.third() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator sortbyParam: third argument (operator memory) "
        "must be an integer!"
        "Operator received: " + type.third().convertToString() );
  }

  // check if fourth argument is an integer (maximum fan-in merge-phase)
  if ( type.fourth() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator sortbyParam: fourth argument (maximum fan-in merge-phase) "
        "must be an integer!"
        "Operator received: " + type.fourth().convertToString() );
  }

  // check if fifth argument is an integer (I/O buffer size)
  if ( type.fifth() != CcInt::BasicType() )
  {
    return NList::typeError(
        "Operator sortbyParam: fifth argument ('I/O buffer size') "
        "must be an integer!"
        "Operator received: " + type.fifth().convertToString() );
  }

  return NList(NList(Symbol::APPEND()), sortDesc, type.first()).listExpr();
}

/*
4.9.2 Value mapping function of operator ~sortbyParam~

*/

int SortByParamValueMap( Word* args, Word& result,
                           int message, Word& local, Supplier s );

/*
4.9.3 Specification of operator ~sortbyParam~

*/

const string SortByParamSpec  = "( ( \"Signature\" \"Syntax\" \"Meaning\" "
                            "\"Example\" ) "
                            "( <text>((stream (tuple([a1:d1, ... ,an:dn]"
                            "))) int int) -> "
                            "(stream (tuple([a1:d1, ... ,an:dn])))"
                            "</text--->"
                            "<text>_ sortbyParam[list; _, _, _]</text--->"
                            "<text>This operator is used to simplify the "
                            "testing of the new sortby operator "
                            "implementation. "
                            "The operator takes three additional parameters, "
                            "the first one specifies the usable main "
                            "memory in Bytes, the second one the maximum "
                            "fan-in for the merge phase respectively the "
                            "maximum number of temporary open tuple files "
                            "and the third parameter specifies the I/O buffer "
                            "size for read/write operations on disc. Usable "
                            "memory size must be between 1-65536 KByte. "
                            "The maximum fan-in is limited by 2-1000. "
                            "The maximum size of the I/O buffer is 16384 "
                            "Bytes. If these limits are exceeded default "
                            "values will be used instead.</text--->"
                            "<text>query plz feed sortbyParam"
                            "[Ort asc,PLZ asc;16386,50,4096] "
                            "consume</text--->) )";

/*
4.9.4 Definition of operator ~sortbyParam~

*/

Operator extrelsortbyParam(
         "sortbyParam",          // name
         SortByParamSpec,        // specification
         SortValueMap<5, true>,   // value mapping
         Operator::SimpleSelect,  // trivial selection function
         SortByParamTypeMap      // type mapping
);

/*
4.10 Operator ~hybridhashjoinParam~

This operator computes the equijoin of two stream. This is
a full parameter-driven version of the ~hybridhashjoin~
operator. In addition to the number of buckets the user
may specify the number of partitions, the usable main memory
and the I/O buffer size.

4.10.1 Specification of operator ~hybridhashjoinParam~

*/
const string HybridHashJoinParamSpec  = "( ( \"Signature\" \"Syntax\" "
                                   "\"Meaning\" \"Example\" ) "
                                   "( <text>((stream (tuple ((x1 t1) ... "
                                   "(xn tn)))) (stream (tuple ((y1 d1) ..."
                                   " (ym dm)))) xi yj b p mem io) -> "
                                   "(stream (tuple "
                                   "((x1 t1) ... (xn tn) (y1 d1) ... (ym dm)"
                                   ")))</text--->"
                                   "<text>_ _ hybridhashjoinParam "
                                   "[ _ , _ , _, _, _, _]"
                                   "</text--->"
                                   "<text>Computes the equijoin of two "
                                   "streams using the hybrid hash-join "
                                   "algorithm. "
                                   "This is the fully parameter-driven version "
                                   "of the hybrid hash-join operator. This "
                                   "operator provides three additional "
                                   "attributes p, mem, io. p specifies the "
                                   "number of partitions to use "
                                   "(with 2 <= p <= <Number of buckets>/2). "
                                   "Paramter mem is "
                                   "used to specify the amount of usable main "
                                   "memory for the operator in bytes. "
                                   "Parameter io specifies the I/O buffer "
                                   "size in bytes used for read/write "
                                   "operations to/from disc. This operator is "
                                   "used to test the operator under different "
                                   "conditions.</text--->"
                                   "<text>query duplicates feed ten feed "
                                   "hybridhashjoinParam[no, nr, 1000, 50, "
                                   "16*1024*1024, 4096] consume</text--->"
                                   ") )";

/*
4.10.2 Definition of operator ~hybridhashjoinParam~

*/
Operator extrelhybridhashjoinParam(
         "hybridhashjoinParam",         // name
         HybridHashJoinParamSpec,       // specification
         HybridHashJoinValueMap<true>,  // value mapping
         Operator::SimpleSelect,        // trivial selection function
         JoinTypeMap<1>,                // type mapping
         HybridHashCostEstimationFunc   // cost estimation
);

/*
4.11 Operator ~gracehashjoinParam~

This operator computes the equijoin of two stream. This is
a full parameter-driven version of the ~gracehashjoin~
operator. In addition to the number of buckets the user
may specify the number of partitions, the usable main memory
and the I/O buffer size.

4.11.1 Specification of operator ~gracehashjoinParam~

*/
const string GraceHashJoinParamSpec  = "( ( \"Signature\" \"Syntax\" "
                                   "\"Meaning\" \"Example\" ) "
                                   "( <text>((stream (tuple ((x1 t1) ... "
                                   "(xn tn)))) (stream (tuple ((y1 d1) ..."
                                   " (ym dm)))) xi yj b p mem io) -> "
                                   "(stream (tuple "
                                   "((x1 t1) ... (xn tn) (y1 d1) ... (ym dm)"
                                   ")))</text--->"
                                   "<text>_ _ gracehashjoinParam "
                                   "[ _ , _ , _, _, _, _]"
                                   "</text--->"
                                   "<text>Computes the equijoin of two "
                                   "streams using the GRACE hash-join "
                                   "algorithm. "
                                   "This is the fully parameter-driven version "
                                   "of the GRACE hash-join operator. This "
                                   "operator provides three additional "
                                   "attributes p, mem, io. p specifies the "
                                   "number of partitions to use "
                                   "(with 2 <= p <= <Number of buckets>/2). "
                                   "Paramter mem is "
                                   "used to specify the amount of usable main "
                                   "memory for the operator in bytes. "
                                   "Parameter io specifies the I/O buffer "
                                   "size in bytes used for read/write "
                                   "operations to/from disc. This operator is "
                                   "used to test the operator under different "
                                   "conditions.</text--->"
                                   "<text>query duplicates feed ten feed "
                                   "gracehashjoinParam[no, nr, 1000, 50, "
                                   "16*1024*1024, 4096] consume</text--->"
                                   ") )";


/*
4.11.2 Definition of operator ~gracehashjoinParam~

*/
Operator extrelgracehashjoinParam(
         "gracehashjoinParam",         // name
         GraceHashJoinParamSpec,       // specification
         GraceHashJoinValueMap<true>,  // value mapping
         Operator::SimpleSelect,       // trivial selection function
         JoinTypeMap<5>,               // type mapping
         GraceHashCostEstimationFunc   // Cost Estimation
);

/*
4.12 Operator ~sortmergejoinParam~

This operator computes the equijoin of two streams. This is
a parameter-driven version of the ~sortmergejoin~
operator. As an additional parameter the main memory size
for the operator may be specified in bytes.

4.12.1 Specification of operator ~sortmergejoinParam~

*/
const string SortMergeJoinParamSpec  = "( ( \"Signature\" \"Syntax\" "
                                   "\"Meaning\" \"Example\" ) "
                                   "( <text>((stream (tuple ((x1 t1) ... "
                                   "(xn tn)))) (stream (tuple ((y1 d1) ..."
                                   " (ym dm)))) xi yj) -> (stream (tuple "
                                   "((x1 t1) ... (xn tn) (y1 d1) ... (ym dm)"
                                   ")))</text--->"
                                   "<text>_ _ sortmergejoinParam [ _ , _ , _ ]"
                                   "</text--->"
                                   "<text>Computes the equijoin of two "
                                   "streams using the new sort operator "
                                   "implementation. "
                                   "This is the parameter-driven version "
                                   "of the sortmergjoin operator. This "
                                   "operator provides an additional "
                                   "attribute mem which specifies the "
                                   "usable main memory of the operator in "
                                   "bytes.</text--->"
                                   "<text>query duplicates feed ten feed "
                                   "sortmergejoin[no, nr, 16*1024*1024] "
                                   "consume</text--->"
                                   ") )";

/*
4.12.2 Definition of operator ~sortmergejoinParam~

*/
Operator extrelsortmergejoinParam (
         "sortmergejoinParam",       // name
         SortMergeJoinParamSpec,     // specification
         SortMergeJoinValueMap<true>, // value mapping
         Operator::SimpleSelect,      // trivial selection function
         JoinTypeMap<3>,               // type mapping
         SortmergeJoinCostEstimationFunc // cost estimation
);



/*
4.13 Operator itHashJoin

This operator performs an iterative hash join.
It works as follows. From the left stream su much as possible
tuples are inserted into a hash table. If all Tuples fit into
main memory, the second stream is scanned once and the resulting
tuples are returned. 

If not the complete first stream could be
stored, during the first scan of the second strean, al tuples are
written to disk. Than, the hashtable is removed and rebuild using
the next part from the first stream. After that, the file storing 
the tuples from the first stream is scanned against the current 
entries in the hashtable. This is repeated until the first stream
is finished.

4.13.1 TypeMapping

The operator gets two tuple streams and two attribute names. The types of the
selected attributes must be the same and all attribute names must differ.

*/
ListExpr itHashJoinTM(ListExpr args){
  string err = "stream(tuple) x stream(tuple) x attr1 x "
               "attr2 [ x int] [x bool] expected";
  if(!nl->HasLength(args,4) && !nl->HasLength(args,5)){
    return listutils::typeError(err);
  }
  ListExpr stream1 = nl->First(args);
  ListExpr stream2 = nl->Second(args);
  ListExpr attr1 = nl->Third(args);
  ListExpr attr2 = nl->Fourth(args);

  if(!Stream<Tuple>::checkType(stream1)){
    return listutils::typeError(err + " (first arg is not a tuple stream)");
  } 
  if(!Stream<Tuple>::checkType(stream2)){
    return listutils::typeError(err + " (second arg is not a tuple stream)");
  } 
  if(!listutils::isSymbol(attr1)){
    return listutils::typeError(err + "(first attrname is not valid)");
  }
  if(!listutils::isSymbol(attr2)){
    return listutils::typeError(err + "(second attrname is not valid)");
  }
  
  ListExpr attrList1 = nl->Second(nl->Second(stream1));
  ListExpr attrList2 = nl->Second(nl->Second(stream2));
  string attrname1 = nl->SymbolValue(attr1);
  string attrname2 = nl->SymbolValue(attr2);
  ListExpr attrType1;
  ListExpr attrType2;

  int index1 = listutils::findAttribute(attrList1,attrname1,attrType1);
  if(index1==0){
    return listutils::typeError(attrname1+
                     " is not an attribute of the first stream");
  } 

  int index2 = listutils::findAttribute(attrList2,attrname2,attrType2);
  if(index2==0){
    return listutils::typeError(attrname1+
                     " is not an attribute of the second stream");
  } 

  if(!nl->Equal(attrType1, attrType2)){
    return listutils::typeError("types of the selected attributes differ");
  }

  ListExpr resAttrList = listutils::concat(attrList1, attrList2);

  if(!listutils::isAttrList(resAttrList)){
    return listutils::typeError("Name conflicts in attributes found");
  }

  ListExpr indexList;
  if(nl->HasLength(args,6)){ // number of buckets and arg switch given
    ListExpr buckNum = nl->Fifth(args);
    if(!CcInt::checkType(buckNum)){
      return listutils::typeError("5th arg is not an int");
    }
    ListExpr switchArgs = nl->Sixth(args);
    if(!CcBool::checkType(switchArgs)){
      return listutils::typeError("6th arg is not a bool");
    }
    indexList = nl->TwoElemList(
                            nl->IntAtom(index1-1),
                            nl->IntAtom(index2-1));
  } else if(nl->HasLength(args,5)){
    // only one optional argument is given
    ListExpr optArg = nl->Fifth(args);
    if(CcInt::checkType(optArg)){
       // number of buckets given
       indexList = nl->ThreeElemList(nl->BoolAtom(false),
                                      nl->IntAtom(index1-1),
                                      nl->IntAtom(index2-1));
    } else if(CcBool::checkType(optArg)){
       // switchArgs is given
       // we append  (-1, TRUE, index, index)
       // the true parameter ist just to tell the value mapping
       // that the optional parameters are in wrong order
       indexList = nl->FourElemList( nl->IntAtom(-1),
                                     nl->BoolAtom(true),
                                     nl->IntAtom(index1-1),
                                     nl->IntAtom(index2-1));
    } else {
       return listutils::typeError("last arg must be of "
                                   "type int or of type bool");
    }
  } else { // len==4, no optional parameter is given
    indexList = nl->FourElemList(
                            nl->IntAtom(-1),
                            nl->BoolAtom(false),
                            nl->IntAtom(index1-1),
                            nl->IntAtom(index2-1));
  }


  return nl->ThreeElemList( nl->SymbolAtom(Symbols::APPEND()),
                            indexList,
                            nl->TwoElemList(
                                nl->SymbolAtom(Stream<Tuple>::BasicType()),
                                nl->TwoElemList(
                                    nl->SymbolAtom(Tuple::BasicType()),
                                    resAttrList)));
}


class itHashJoinInfo{

  public:

/*
Constructor

*/
    itHashJoinInfo(Word& _stream1, Word& _stream2, 
                const int _index1, const int _index2, 
                const ListExpr _resType,
                const size_t _maxMem,
                const size_t _buckNum, 
                ItHashJoinCostEstimation* _costEstimation,
                const bool _argsSwitched): 
                stream1(_stream1), stream2(_stream2), 
                index1(_index1), index2(_index2), 
                hashTable(0), tt(0), maxMem(_maxMem), 
                buffer(0),it(0), 
                usedMem(0), currentTuple(0), bucket(0), bucketPos(0),
                s1finished(false), scans(0), buckNum(_buckNum),
                costEstimation(_costEstimation),
                argsSwitched(_argsSwitched) {

         size_t tableSize = sizeof(void*) * buckNum;
         if(tableSize > maxMem / 5){
           // reduce size of table when table structure takes more than
           // 20 percent of the available memory
           buckNum = maxMem / (5 * sizeof(void*));
         }
         if(buckNum< 3){
           buckNum = 3;
         }

         tt = new TupleType(_resType);
         stream1.open();
         stream2.open();
         readNextPartition();
         updateCurrentTuple();

         // Update cost estimation
         costEstimation -> setBuckets(buckNum);
    }

/*
Destructor

*/
   ~itHashJoinInfo(){
      stream1.close();
      stream2.close();
      tt->DeleteIfAllowed();
      if(hashTable){
         clearTable();
         delete[] hashTable;
      }
      if(it){
        delete it;
      }
      if(buffer){
         delete buffer;
      }
      cout << "iterative hash join finished with " 
           << scans << " partitions" << endl;
   } 

/*
nextTuple

This function returns the next tuple or 0 if no more tuples
can be created.

*/

   Tuple* nextTuple(){
      while(true){
       if(!currentTuple){ //both streams are exhausted 
         costEstimation -> setStream2Exhausted(true);
         return 0;
       }
       if(bucketPos>=bucket->size()){ // current bucket exhausted
         updateCurrentTuple();
       }
       if(!currentTuple){ // no new combination found
          return 0;
       }
       while(bucketPos < bucket->size()){ // search in current bucket
          Tuple* tuple1 = (*bucket)[bucketPos];
          bucketPos++;
          if(equal(tuple1, currentTuple)){
            // hit
            Tuple* res = new Tuple(tt);
            if(argsSwitched){
               Concat(currentTuple,tuple1,res);
            } else {
                Concat(tuple1,currentTuple,res);
            }
            return res;           
          }
       }       
     }  
   }
   
  private:
     Stream<Tuple> stream1;
     Stream<Tuple> stream2;
     int index1;
     int index2;
     vector<Tuple*>** hashTable;
     TupleType* tt;
     size_t maxMem;
     TupleFile* buffer; 
     TupleFileIterator* it;
     size_t usedMem;
     Tuple* currentTuple; // current tuple from stream 2
     const vector< Tuple* >* bucket; // current bucket from hashTable
     unsigned int bucketPos; // current position in bucket
     bool s1finished;
     unsigned int scans;
     unsigned int buckNum;

     // cost estimation
     ItHashJoinCostEstimation* costEstimation;
     // is set to be true, if the arguments are in switched order
     bool argsSwitched;



     size_t getBucket(Tuple* tuple, bool first){
        int index = first?index1:index2;
        return tuple->GetAttribute(index)->HashValue() % buckNum;
     }

     bool equal(Tuple* t1, Tuple* t2){
        return t1->GetAttribute(index1)->Compare(t2->GetAttribute(index2)) == 0;
     }



/*
clearTable

removes all Tuple from the current table

*/
     void clearTable(){
       for(unsigned int i=0;i<buckNum;i++){
         vector<Tuple*>* v = hashTable[i];
         if(v){
           for(unsigned int j=0;j<v->size();j++){
              (*v)[j]->DeleteIfAllowed();
           }
           delete hashTable[i];
           hashTable[i] = 0;
         }
       }
     }

     void updateCurrentTuple( ){
        if(currentTuple){
          currentTuple->DeleteIfAllowed();
          currentTuple = 0;
        }
        bucket = 0;
        bucketPos = 0;
        if(!it){ // read from stream
           
           currentTuple = stream2.request();
           costEstimation -> processedTupleInStream2();
           costEstimation -> incReadInIteration();

           while( (bucket==0) && (currentTuple!=0)){
             if(!s1finished){
               if(!buffer){
                 buffer = new TupleFile(currentTuple->GetTupleType(),0);
                 buffer->Open(); // open for writing
               } 
               currentTuple->PinAttributes();
               buffer->Append(currentTuple);
               costEstimation -> incTuplesInTupleFile();
             }
             size_t hash = getBucket(currentTuple,false);
             bucket = hashTable[hash];
             if(!bucket){
                currentTuple->DeleteIfAllowed();
                currentTuple=stream2.request();
                costEstimation -> processedTupleInStream2();
                costEstimation -> incReadInIteration();
             }
           }
           if(bucket){ // found a bucket/tuple pair
              return;
           }
           if(buffer){
             buffer->Close();
             costEstimation -> setTupleFileWritten(true);
             it = buffer->MakeScan();
             costEstimation -> resetReadInIteration();
             readNextPartition();
           } 
        } 
        if(!it){
          return;
        }
        while(true){
           currentTuple = it->GetNextTuple();
           costEstimation -> incReadInIteration();
           while((bucket==0) && (currentTuple!=0)){
             size_t hash = getBucket(currentTuple,false);
             bucket = hashTable[hash];
             if(!bucket){
                currentTuple->DeleteIfAllowed();
                currentTuple=it->GetNextTuple();
                costEstimation -> incReadInIteration();
             }
           } 
           if(bucket){
             return;
           }
           delete it;
           it = 0;
           if(!s1finished){
             readNextPartition();
             it = buffer->MakeScan();
             costEstimation -> resetReadInIteration();
           } else {
             return;
           }
        } 
     }

     void readNextPartition(){
       if(s1finished){
         return;
       }
       scans++;
       
       // update cost estimation
       costEstimation -> setIteration(scans);
       
       if(hashTable){
         clearTable();
       } else {
          hashTable = new vector<Tuple*>*[buckNum];
          for(unsigned int i=0;i<buckNum;i++){
             hashTable[i] = 0;
          }
       }
      
       usedMem = sizeof(void*)*buckNum;
       if(usedMem>=maxMem){ 
          // ensure to be able to store at least one tuple
          maxMem = usedMem + 1024;
       }

       Tuple* inTuple = stream1.request();
       costEstimation -> processedTupleInStream1();
       
       size_t noTuples = 0; 
       while((inTuple!=0) && (usedMem<maxMem)){
         size_t hash = getBucket(inTuple,true);
         usedMem += inTuple->GetMemSize();
         if(!hashTable[hash]){
           hashTable[hash] = new vector<Tuple*>();
           
           usedMem += sizeof(*hashTable[hash]);
           usedMem += sizeof(void*) * hashTable[hash]->capacity();
         }
         size_t oldcap = hashTable[hash]->capacity();
         hashTable[hash]->push_back(inTuple);
         size_t newcap = hashTable[hash]->capacity();
         if(newcap > oldcap){
           usedMem += sizeof(void*) * (newcap - oldcap);
         }
         noTuples++;
         if(usedMem < maxMem){
            inTuple = stream1.request();
            costEstimation -> processedTupleInStream1();
         }
       }
       
       if(inTuple==0){
         s1finished = true;
         costEstimation -> setStream1Exhausted(true);
       }
    
       costEstimation -> readPartitionDone();
 }

};

/*
2.14.3 Value Mapping

*/
int itHashJoinVM( Word* args, Word& result,
                   int message, Word& local, Supplier s ){
   
   itHashJoinInfo* li = (itHashJoinInfo*) local.addr;
   switch(message){
     case OPEN: { if(li){
                    delete li;
                  }
                  ListExpr ttype = nl->Second(GetTupleResultType(s));
                  size_t mem = qp->GetMemorySize(s)*1024*1024;
                  // the itjoin underestimates its memory usage
                  mem = (size_t)(mem*0.8);
                  if(mem<1024){
                     mem = 1024;
                  }

                  ItHashJoinCostEstimation* costEstimation =
                    (ItHashJoinCostEstimation*) qp->getCostEstimation(s);

                  costEstimation -> init(NULL, NULL);
                  int ibuck,iswitch,iindex1,iindex2;
                  int is1 = 0;  // position of streams in args
                  int is2 = 1;
                  if(qp->GetNoSons(s)==8){
                    ibuck = 4;
                    iswitch = 5;
                    iindex1 = 6;
                    iindex2 = 7; 
                  } else { 
                     assert(qp->GetNoSons(s)==9);
                     ibuck = 5;
                     iswitch = 4;
                     iindex1 = 7;
                     iindex2 = 8; 
                  }

                  int buckets = 999997;
                  CcInt* b = (CcInt*) args[ibuck].addr;
                  if(b->IsDefined() && b->GetValue()>=3){
                      buckets = b->GetValue();
                  }
                  bool argswitch = false;
                  CcBool* s = (CcBool*) args[iswitch].addr;
                  if(s->IsDefined()){
                     argswitch = s->GetValue();
                  } 
                  if(argswitch){ // switch arguments
                     int tmp = iindex1;
                     iindex1 = iindex2;
                     iindex2 = tmp;
                     is1 = 1;
                     is2 = 0;
                  } 

                  local.addr = new itHashJoinInfo(args[is1],args[is2], 
                               ((CcInt*)args[iindex1].addr)->GetValue(),
                               ((CcInt*)args[iindex2].addr)->GetValue(),
                               ttype,mem, buckets, costEstimation, argswitch);
                  return 0;
                }
     case REQUEST: { if(!li){
                       return CANCEL;
                     }
                     result.addr = li->nextTuple();
                     return result.addr?YIELD:CANCEL;
                   }
     case CLOSE : {
                    if(li){
                      delete li;
                      local.addr=0;
                    }
                    return 0;
                  }
   } 
   return 0; 

}

/*
2.14.4 Specification

*/

const string itHashJoinSpec  = 
      "( ( \"Signature\" \"Syntax\" \"Meaning\" \"Example\" ) "
      "  ( <text>stream(tuple(X)) x stream(tuple(Y)) "
          "  x a1 x a2 [x int] [x bool]-> stream(tuple(XY))"
      "    </text--->"
      "   <text> _ _ itHashJoin [_ _ _ _ ] </text--->"
      "   <text> Computes a hash join of two streams. The optional "
      " integer argument  determines the number of buckets. If the "
      " boolean argument is present and has value TRUE, the "
      " input streams are switched in processing.  </text--->"
      "   <text> query ten feed thousand feed {b} itHashJoin[No, No_b] count"
      "   </text--->))";


CostEstimation* ItHashJoinCostEstimationFunc() {
  return new ItHashJoinCostEstimation();
}

Operator itHashJoin (
         "itHashJoin",                // name
         itHashJoinSpec,              // specification
         itHashJoinVM,                // value mapping
         Operator::SimpleSelect,      // trivial selection function
         itHashJoinTM,                // type mapping
         ItHashJoinCostEstimationFunc // cost estimation
);


OperatorSpec sorthSpec(
  "stream(tuple) -> stream(tuple)",
  " _ sorth",
  "Sorts a stream of tuples.",
  "query plz feed sorth consume"
);

Operator sorthOP(
  "sorth",
  sorthSpec.getStr(),
  Sort2HeapsVM,
  Operator::SimpleSelect,
  Sort2HeapsTM
);


OperatorSpec sortbyhSpec(
  "stream(tuple) x list  -> stream(tuple)",
  " _ sortbyh [ ]",
  "Sorts a stream of tuples.",
  "query plz feed sortbyh [PLZ desc] consume"
);

Operator sortbyhOP(
  "sortbyh",
  sortbyhSpec.getStr(),
  Sort2HeapsVM,
  Operator::SimpleSelect,
  SortBy2HeapsTM
);


OperatorSpec sortattrSpec(
  "stream(DATA) x bool -> stream(DATA)",
  " _ sortattr[_] ",
  "Sorts a stream of attributes ascending if "
  "the boolean argument is true, descending otherwise",
  " query intstream(1,10) sortattr[FALSE] count" 
);


Operator sortattrOP(
  "sortattr",
  sortattrSpec.getStr(),
  sortattrVM,
  Operator::SimpleSelect,
  sortattrTM
);


/*
Type ap Operator GD

*/
OperatorSpec GDSpec(
  "fun( X -> stream(tuple(Y))) ... -> tuple(Y)",
  " GD(_)",
  "A Type Map Operator",
  "query gdijkstra(...)"
);

Operator GDOp(
  "GD",
  GDSpec.getStr(),
  0,
  Operator::SimpleSelect,
  general_dijkstra::GDTM
);


/*
Operator ~gdijkstra~


*/


ValueMapping gdijkstraVM[] = {
  general_dijkstra::gdijkstraVMT<CcInt>,
  general_dijkstra::gdijkstraVMT<LongInt>
};

int gdijkstraSelect(ListExpr args){
  return CcInt::checkType(nl->Third(args))?0:1;
}

OperatorSpec gdijkstraSpec(
  " fun( A -> stream(tuple(X)) x AttrName x Source x Target "
  "x fun(tuple(X) -> double), int, int -> stream(tuple(X@(EgdeNoGD:int)))",
  " _ gdijstra[_,_,_,_] ",
  "Computes the shortest path between two nodes of a graph."
  "The representation of the graph is not fixed, but the graph is "
  "defined implicitely by the first function returning the successors "
  "of a node. The node identifier can be of type int or longint. "
  " The return value of the successor function  must contain the "
  "target node. The name of this target node attribute is specified in "
  "the second argument. The third argument gives the source node and "
  "the fourth argument the destination node for the shortest path search."
  "The fifth argument is a function again computing the costs from an "
  "edge tuple returned by the first function." 
  "The  sixth argument specifies the search mode. Currently, the following "
  "modes are implemented: \n"
  "0: returns the shortest path between the source and the destination node.\n"
  "2: returns all edges which are unique determined to be in in shortest apath"
  " tree or not.  Edges not belonging to the shortest path "
  "tree will have a negative sequence number. Egdes czrrently in the front are "
  "not determined and thus not part of the result.\n"
  "3: returns the shortest path tree from the source to all other reachable "
  "node of the graph. The destination node argument is ignored in this "
  "mode.\n\n"
  "The last argument limits the search to a certain path length.  If this "
  "argument is more than zero, the algorithm searches for shortest path having"
  " this value as the maximum length. Note that shorter paths in the complete "
  "graph may exists. Furthermore, the front (mode 1) will be empty if the "
  "target node is not found."
  "The result are the edges of the shortest path (tree) extended by the "
  "distance of the target node of this edge to the source node argument of "
  "the operator. Furthermore, the level in the tree of the target node is "
  "appended to the edge.",
  " query fun(s : int) otestrel orange[s;s]  gdijkstra[Id_s2, 1 , 40, "
  "distance(.GeoData_s1, .GeoData_s2), 0,0] consume " 
);

Operator gdijkstraOp(
  "gdijkstra",
  gdijkstraSpec.getStr(),
  2,
  gdijkstraVM,
  gdijkstraSelect,
  general_dijkstra::gdijkstraTM
);



/*
Operators minPathCost1 and minPathCost2 

*/

OperatorSpec minPathCost1Spec(
  " fun(T -> stream(tuple(X))) x AttrName, T, T, AttrName, int -> real",
  " successors minPathCost1[targetName,source,target,costName,maxHops] ",
  "Computes the minimum costs of a path from source to target in a "
  "graph given by succfun. The target attribute name is given by targetName "
  "the edge costs are part of the tuple has has the attribute name costName."
  "the graph is restricted to a level of maxHops if the last argument is "
  "greater than tero.",
  "query succfun getCosts1[Target, 2, 16, Costs, 3]"
);

OperatorSpec minPathCost2Spec(
  " fun(T -> stream(tuple(X))) x AttrName, T, T, fun(X-double) , int -> real",
  " successors minPathCost1[targetName,source,target,costName,maxHops] ",
  "Computes the minimum costs of a path from source to target in a "
  "graph given by succfun. The target attribute name is given by targetName "
  "the edge costs are computed be the second function."
  "the graph is restricted to a level of maxHops if the last argument is "
  "greater than tero.",
  "query succfun getCosts1[Target, 2, 16, Costs, 3]"
);
ValueMapping minPathCost1VM[] = {
    general_dijkstra::minPathCost1VMT<CcInt>,
    general_dijkstra::minPathCost1VMT<LongInt>
};

ValueMapping minPathCost2VM[] = {
    general_dijkstra::minPathCost2VMT<CcInt>,
    general_dijkstra::minPathCost2VMT<LongInt>
};

int minPathCostsSelect(ListExpr args){
  return CcInt::checkType(nl->Third(args))?0:1;
}


Operator minPathCost1Op(
  "minPathCost1",
  minPathCost1Spec.getStr(),
  2,
  minPathCost1VM,
  minPathCostsSelect,
  general_dijkstra::minPathCostsTM<false> 
);


Operator minPathCost2Op(
  "minPathCost2",
  minPathCost2Spec.getStr(),
  2,
  minPathCost2VM,
  minPathCostsSelect,
  general_dijkstra::minPathCostsTM<true> 
);


OperatorSpec gbidijkstraSpec(
  "succfun x predfun x succName x predName x source x target x costFun",
  "_ _ gbidijkstra [_,_,_,_._] ",
  "Computes a shortest path from source to target using a "
  "bidirectional dijkstra algorithm. "
  "The first function computes the successors of a node as a tuple edge."
  "The actual successor is specified in the tuple by the succName "
  "argument. The second function returns the predecessors of a node "
  "as a stream of edge tuples. The actual predecessor is specified by "
  "the predName argument. The cost of both edges are computed using the "
  "costfun. " ,
  "query fun(i : int) forwardG exactmatch[i] fun(int i) backwardG "
  "exactmatch[i] gbidijkstra[Source, Target, 37, 48, .Costs] consume"
);

ValueMapping gbidijkstraVM[] = {
   general_dijkstra::gbidijkstraVMT<CcInt>,
   general_dijkstra::gbidijkstraVMT<LongInt>
};

int gbidijkstraSelect(ListExpr args){
  return CcInt::checkType(nl->Fifth(args))?0:1;
}


Operator gbidijkstraOp(
  "gbidijkstra",
  gbidijkstraSpec.getStr(),
  2,
  gbidijkstraVM,
  gbidijkstraSelect,
  general_dijkstra::gbidijkstraTM
);


/*
Operators mtMinPathCosts?

*/
OperatorSpec mtMinPathCosts1Spec(
  " fun x stream(tuple) x IDENT x IntType x IDENT x "
  "IDENT x int -> stream(tuple)",
  "succFun targetstream mtMinPathCosts1Spec[succName, source, "
  "targetName, costsName, maxHops]",
  "Computes the minimum path costs from a source node to a set "
  "of target nodes within a graph represented by a successor function.\n"
  "succFun : description of the graph, i.e. fun : node -> stream(tuple)\n"
  "targetstream : represents the set of target nodes. If some target node "
  " occurs twice, only the first tuple is used.\n"
  "succName : attribute name in the tuples of successor function describing "
  "the position of the successor in the tuple\n"
  "Source : the source node\n"
  "targetName : attribute name of the target node in the targetstream\n"
  "costsName : name of attribute describing the edge costs within the "
  " successor function result. Must be of type real\n"
  "maxHops : maximum search depth",
  "query fun(i : int) otestrel orange[i;i] otestrel feed "
  "mtMinPathCosts1Spec[Target, 1, Target, Costs, 3] count"
);

ValueMapping mtMinPathCosts1VM[] = {
  general_dijkstra::mtMinPathCost1VMT<CcInt>,
  general_dijkstra::mtMinPathCost1VMT<LongInt>
};

int mtMinPathCosts1Select(ListExpr args){
   return CcInt::checkType(nl->Fourth(args))?0:1;
}

Operator mtMinPathCosts1Op(
  "mtMinPathCosts1",
  mtMinPathCosts1Spec.getStr(),
  2,
  mtMinPathCosts1VM,
  mtMinPathCosts1Select,
  general_dijkstra::mtMinPathCostsTM<false>
);



OperatorSpec mtMinPathCosts2Spec(
  " fun x stream(tuple) x IDENT x IntType x IDENT x "
  "IDENT x int -> stream(tuple)",
  "succFun targetstream mtMinPathCosts1Spec[succName, source, "
  "targetName, costsFun, maxHops]",
  "Computes the minimum path costs from a source node to a set "
  "of target nodes within a graph represented by a successor function.\n"
  "succFun : description of the graph, i.e. fun : node -> stream(tuple)\n"
  "targetstream : represents the set of target nodes. If some target node "
  " occurs twice, only the first tuple is used.\n"
  "succName : attribute name in the tuples of successor function describing "
  "the position of the successor in the tuple\n"
  "Source : the source node\n"
  "targetName : attribute name of the target node in the targetstream\n"
  "costsFun : function computing the costs of a successor edge\n"
  "maxHops : maximum search depth",
  "query fun(i : int) otestrel orange[i;i] otestrel feed "
  "mtMinPathCosts1Spec[Target, 1, Target, Costs, 3] count"
);

ValueMapping mtMinPathCosts2VM[] = {
  general_dijkstra::mtMinPathCost2VMT<CcInt>,
  general_dijkstra::mtMinPathCost2VMT<LongInt>
};

int mtMinPathCosts2Select(ListExpr args){
   return CcInt::checkType(nl->Fourth(args))?0:1;
}

Operator mtMinPathCosts2Op(
  "mtMinPathCosts2",
  mtMinPathCosts2Spec.getStr(),
  2,
  mtMinPathCosts2VM,
  mtMinPathCosts2Select,
  general_dijkstra::mtMinPathCostsTM<true>
);


/*
Operator kmergediff

Removes from an ordered tuple stream elements which are 
occure in the secord ordered stream. In contrast to the
mergediff operator, in the second stream only a subset of
the attributes in the first stream has to be. Both streams
have to be sorted by the attributes of the second stream.

*/
ListExpr kmergediffTM(ListExpr args){
   if(!nl->HasLength(args,2)){
     return listutils::typeError("two args expected");
   }
   if(  !Stream<Tuple>::checkType(nl->First(args))
      ||!Stream<Tuple>::checkType(nl->Second(args))){
     return listutils::typeError("both arguments must be tuple streams");
   }
   ListExpr attrList1 = nl->Second(nl->Second(nl->First(args)));
   ListExpr attrList2 = nl->Second(nl->Second(nl->Second(args)));

   ListExpr appendList = nl->TheEmptyList();
   ListExpr last = nl->TheEmptyList();
   bool first = true;
   while(!nl->IsEmpty(attrList2)){
     ListExpr attr = nl->First(attrList2);
     attrList2 = nl->Rest(attrList2);
     string name = nl->SymbolValue(nl->First(attr));
     ListExpr attrType;
     int index = listutils::findAttribute(attrList1,name,attrType);
     if(!index){
       return listutils::typeError("Attribute " + name + 
                                   " not part of the first stream"); 
     }
     if(!nl->Equal(nl->Second(attr), attrType)){
        return listutils::typeError("Attribute " + name +
                                    "has a different type in the streams");
     }
     if(first){
         appendList = nl->OneElemList(nl->IntAtom(index-1));
         last = appendList;
         first = false;
     } else {
         last = nl->Append(last, nl->IntAtom(index-1));
     }
   }
   return nl->ThreeElemList(
                  nl->SymbolAtom(Symbols::APPEND()),
                  appendList,
                  nl->First(args));
}


class kmergediffInfo{
    public:
      kmergediffInfo(Word& _stream1, Word& _stream2, vector<int>& _pos):
                     stream1(_stream1), stream2(_stream2), pos(_pos){
         stream1.open();
         stream2.open();
         s2exhausted = false;
         tup2 = 0;
      }
      ~kmergediffInfo(){
         stream1.close();
         stream2.close();
       }

      Tuple* next(){
         Tuple* tup1=0;
         while( (tup1= stream1.request())){
             if(check(tup1)){
               return tup1;
             }
             tup1->DeleteIfAllowed();
         }
         return 0;
      }  

   private:
      Stream<Tuple> stream1;
      Stream<Tuple> stream2;
      vector<int> pos; 
      bool s2exhausted;
      Tuple* tup2;

   /* returns true if the incoming tuple is not
      in the second stream. Both streams must be sorted
      according all attributes in stream 2
    */
    bool check(Tuple* tuple){
      while(!s2exhausted){
         if(!tup2){
            tup2 = stream2.request();
         } 
         if(!tup2){
           s2exhausted = true;
         } else {
           int cmp = compare(tup2,tuple);
           if(cmp<0){
             tup2->DeleteIfAllowed();
             tup2 = 0; 
           } else if(cmp > 0){
             return true;
           } else {
             return false;
           }
         }
      }
      return true;
    }


    /* compares two tuples according the pos vector */
    int compare(Tuple* t1, Tuple* t2){
       for(size_t i=0;i<pos.size();i++){
          int c = t1->GetAttribute(i)->Compare(t2->GetAttribute(pos[i]));
          if(c!=0) return c;
       }
       return 0;
    }
};


int kmergediffVM( Word* args, Word& result,
                   int message, Word& local, Supplier s ){

   kmergediffInfo* li = (kmergediffInfo*) local.addr;
   switch(message){
     case OPEN :{
           if(li){
             delete li;
             local.addr = 0;
           }
           vector<int> v;
           for(int i=2;i<qp->GetNoSons(s);i++){
             v.push_back( ((CcInt*)args[i].addr)->GetValue());
           }
           local.addr = new kmergediffInfo(args[0],args[1],v);
           return 0;
     }
     case REQUEST :
           result.addr = li?li->next():0;
           return result.addr? YIELD:CANCEL;
     case CLOSE :
            if(li){
               delete li;
               local.addr = 0;
            }
            return 0;
   }
   return -1;
}

OperatorSpec kmergediffSpec(
  "stream(tuple(A)) x stream(tuple(B)) -> stream(tuple(A)) ",
  " _ _ kmergediff",
  "Computes the difference between the first and the second stream. "
  "The attributes of the second stream must be a subset of the "
  "attributes in the first stream.  Furthermore, moth streams "
  " has to be sorted by the attributes in the second stream.",
  "query thousand feed extend[ D : 1] ten feed kmergediff count"
);

Operator kmergediffOp(
  "kmergediff",
  kmergediffSpec.getStr(),
  kmergediffVM,
  Operator::SimpleSelect,
  kmergediffTM
);


/*
2.10 Operator ~pMerge~

Merges two sorted streams by an attribute.

*/
ListExpr pMergeTM(ListExpr args){
   if(!nl->HasLength(args,4)){
      return listutils::typeError("4 arguments required");
   }
   if(!Stream<Tuple>::checkType(nl->First(args))){
     return listutils::typeError("First arg is not a tuple stream");
   }
   if(!Stream<Tuple>::checkType(nl->Second(args))){
     return listutils::typeError("Second arg is not a tuple stream");
   }
   if(!listutils::isSymbol(nl->Third(args))){
     return listutils::typeError("Third arg is not a valid attribute name");
   }
   if(!listutils::isSymbol(nl->Fourth(args))){
     return listutils::typeError("Fourth arg is not a valid attribute name");
   }
   string a1 = nl->SymbolValue(nl->Third(args));
   ListExpr al1 = nl->Second(nl->Second(nl->First(args)));
   ListExpr t1;
   int i1 = listutils::findAttribute(al1, a1, t1);
   if(!i1){
     return listutils::typeError("Attribute " + a1 + " not part of stream 1");
   }
   string a2 = nl->SymbolValue(nl->Fourth(args));
   ListExpr al2 = nl->Second(nl->Second(nl->Second(args)));
   ListExpr t2;
   int i2 = listutils::findAttribute(al2, a2, t2);
   if(!i2){
     return listutils::typeError("Attribute " + a2 + " not part of stream 2");
   }
   if(!nl->Equal(t1,t2)){
     return listutils::typeError("Attribute " + a1 + " and " + a2 
                                 + " have different types");
   }
   ListExpr alres = listutils::concat(al1,al2);
   if(!listutils::isAttrList(alres)){
     return listutils::typeError("Input streams have conflicting "
                                 "attribute names");
   }
   ListExpr resstream = nl->TwoElemList(
                          listutils::basicSymbol<Stream<Tuple> >(),
                          nl->TwoElemList(
                               listutils::basicSymbol<Tuple>(),
                               alres));
   return nl->ThreeElemList(
                nl->SymbolAtom(Symbols::APPEND()),
                nl->TwoElemList( nl->IntAtom(i1-1), nl->IntAtom(i2-1)),
                resstream);  
}

class pMergeLocalInfo{
  public:
     pMergeLocalInfo(Word _s1, Word _s2, int _p1, int _p2, TupleType* _tt,
       Tuple* _t1u, Tuple* _t2u
     ):
     s1(_s1), s2(_s2), p1(_p1), p2(_p2), t1(0), t2(0), tt(_tt), t1u(_t1u),
     t2u(_t2u)
     {
        s1.open();
        s2.open();
        t1 = s1.request();
        t2 = s2.request();
        t1u->IncReference();
        t2u->IncReference();
        tt->IncReference();
     }

     ~pMergeLocalInfo(){
         s1.close();
         s2.close();
         if(t1) t1->DeleteIfAllowed();
         if(t2) t2->DeleteIfAllowed();
         t1u->DeleteIfAllowed();
         t2u->DeleteIfAllowed();
         tt->DeleteIfAllowed();
      }

     Tuple* next(){
        if(t1==0){
           if(t2==0){
             return 0;
           }
           return next2();
        }
        if(t2==0){
           return next1();
        }
        // both tuples are not null
        int cmp = t1->GetAttribute(p1)->Compare(t2->GetAttribute(p2));
        if(cmp<0) return next1();
        if(cmp>0) return next2(); 
        Tuple* res = createResultTuple(t1,t2);
        t1->DeleteIfAllowed();
        t2->DeleteIfAllowed();
        t1 = s1.request();
        t2 = s2.request();
        return res;

     }

     private:
       Stream<Tuple> s1;
       Stream<Tuple> s2;
       int p1;
       int p2;
       Tuple* t1;
       Tuple* t2;
       TupleType* tt; // result tuple type
       Tuple* t1u; // undefined version of t1
       Tuple* t2u; // undefined version of t2

       Tuple* createResultTuple(Tuple* t1, Tuple* t2){
           if(t1==0) t1 = t1u;
           if(t2==0) t2 = t2u;
           Tuple* res = new Tuple(tt);
           int no1 = t1->GetNoAttributes();
           for( int i=0; i<no1; i++){
             res->CopyAttribute(i,t1,i);
           }
           for(int i=0;i<t2->GetNoAttributes(); i++){
              res->CopyAttribute(i,t2,i+no1);
           }
           return res;
       }

       Tuple* next2(){
          Tuple* res = createResultTuple(0,t2);
          t2->DeleteIfAllowed();
          t2 = s2.request();
          return res;
       }

       Tuple* next1(){
          Tuple* res = createResultTuple(t1,0);
          t1->DeleteIfAllowed();
          t1 = s1.request();
          return res;
       }


};

struct pMergeGlobalInfo{

  TupleType* tt;
  Tuple* t1u;
  Tuple* t2u;

};


Attribute* getUndefinedAttribute(AttributeType at, 
                                 ListExpr type){

  int aid = at.algId;
  int tid = at.typeId;
  ObjectCreation oc = am->CreateObj(aid,tid);
  return (Attribute*) oc(type).addr;
}


Tuple* getUndefinedTuple(ListExpr _tt, 
                         ListExpr attrList){

  TupleType* tt = new TupleType(_tt);
  Tuple* res = new Tuple(tt);
  for(int i=0;i<tt->GetNoAttributes();i++){
     Attribute* a = getUndefinedAttribute(tt->GetAttributeType(i),
                                          nl->First(attrList));
     res->PutAttribute(i, a);
     attrList = nl->Rest(attrList);
  }  
  tt->DeleteIfAllowed();
  return res;
}


int pMergeVM( Word* args, Word& result,
                   int message, Word& local, Supplier s ){

  switch(message){
     case INIT: {
        TupleType* tt = new TupleType(nl->Second(GetTupleResultType(s)));

        Tuple* t1u = getUndefinedTuple(
                        nl->Second(GetTupleResultType(qp->GetSon(s,0))),
                        nl->Second(nl->Second(qp->GetType(qp->GetSon(s,0)))));
        Tuple* t2u = getUndefinedTuple(
                        nl->Second(GetTupleResultType(qp->GetSon(s,1))),
                        nl->Second(nl->Second(qp->GetType(qp->GetSon(s,1)))));
        pMergeGlobalInfo* gi = new pMergeGlobalInfo();
        gi->tt = tt;
        gi->t1u = t1u;
        gi->t2u = t2u;
        qp->GetLocal2(s).addr = gi;
        return 0;
     }
     case FINISH: {
        pMergeGlobalInfo* gi = (pMergeGlobalInfo*) qp->GetLocal2(s).addr;
        gi->tt->DeleteIfAllowed();
        gi->t1u->DeleteIfAllowed();
        gi->t2u->DeleteIfAllowed();
        delete gi;
        qp->GetLocal2(s).addr = 0;  
        return 0;
     }
     case OPEN: {
          pMergeGlobalInfo* gi = (pMergeGlobalInfo*) qp->GetLocal2(s).addr;
          pMergeLocalInfo* li = (pMergeLocalInfo*) local.addr;
          if(li) delete li;
          int p1 = ((CcInt*) args[4].addr)->GetValue();
          int p2 = ((CcInt*) args[5].addr)->GetValue();
          local.addr = new pMergeLocalInfo(args[0], args[1], 
                                           p1,p2,gi->tt, gi->t1u, gi->t2u);
          return 0;
     }
     case REQUEST: {
         pMergeLocalInfo* li = (pMergeLocalInfo*) local.addr;
         result.addr = li?li->next():0;
         return result.addr?YIELD:CANCEL;
     }
     case CLOSE: {
         pMergeLocalInfo* li = (pMergeLocalInfo*) local.addr;
         if(li){
            delete li;
            local.addr = 0;
         }
         return 0;
     }
  }
  return -1;

}

OperatorSpec pMergeSpec(
   "stream(A) x stream(B) x ID1 x ID2 -> stream(A@B)",
   "_ _ pMerge[_,_]",
   "This operator merges two streams of tuples. "
   "Both streams must be sorted by the corresponding attribute."
   "If the attribute is equal in both streams, the tuples "
   "are concatenated and both stream get the next element. " 
   "if one of the attributes is smaller, this tuple is "
   "concatenated with an undefined one and only this stream "
   "goes one step further. ",
   "query plz feed sortby[PLZ] plz feed sortby{PLZ] {a} pMerge count"
);

Operator pMergeOp(
  "pMerge",
  pMergeSpec.getStr(),
  pMergeVM,
  Operator::SimpleSelect,
  pMergeTM
);




} // end of namespace extrel2

/*

5 Class ~ExtRelation2Algebra~

A new subclass ~ExtRelation2Algebra~ of class ~Algebra~ is declared. The only
specialization with respect to class ~Algebra~ takes place within the
constructor: all type constructors and operators are registered at the
actual algebra.

After declaring the new class, its only instance ~extendedRelation2Algebra~
is defined.

*/


class ExtRelation2Algebra : public Algebra
{
 public:
   ExtRelation2Algebra() : Algebra()
  {
    AddOperator(&extrel2::extrelsort); 
      extrel2::extrelsort.SetUsesMemory(); 
    AddOperator(&extrel2::extrelsortParam);
      extrel2::extrelsortParam.SetUsesMemory(); 

    AddOperator(&extrel2::extrelsortby);
      extrel2::extrelsortby.SetUsesMemory(); 
    AddOperator(&extrel2::extrelsortbyParam);
      extrel2::extrelsortbyParam.SetUsesMemory(); 

    AddOperator(&extrel2::extrelhybridhashjoin);
      extrel2::extrelhybridhashjoin.SetUsesMemory();
    AddOperator(&extrel2::extrelhybridhashjoinParam);
      extrel2::extrelhybridhashjoinParam.SetUsesMemory();

    AddOperator(&extrel2::extrelgracehashjoin);
      extrel2::extrelgracehashjoin.SetUsesMemory();
    AddOperator(&extrel2::extrelgracehashjoinParam);
      extrel2::extrelgracehashjoinParam.SetUsesMemory();

    AddOperator(&extrel2::extrelsortmergejoin);
      extrel2::extrelsortmergejoin.SetUsesMemory(); 
    AddOperator(&extrel2::extrelsortmergejoinParam);
      extrel2::extrelsortmergejoinParam.SetUsesMemory(); 

    AddOperator(&extrel2::tuplefiletest);
    AddOperator(&extrel2::tuplebuffer);
    AddOperator(&extrel2::tuplebuffer2);

    AddOperator(&extrel2::itHashJoin);
    extrel2::itHashJoin.SetUsesMemory();


    AddOperator(&extrel2::sorthOP);
    extrel2::sorthOP.SetUsesMemory();
    extrel2::sorthOP.enableInitFinishSupport();

    AddOperator(&extrel2::sortbyhOP);
    extrel2::sortbyhOP.SetUsesMemory();
    extrel2::sortbyhOP.enableInitFinishSupport();

    AddOperator(&extrel2::sortattrOP);
    extrel2::sortattrOP.SetUsesMemory();

    AddOperator(&extrel2::gdijkstraOp);
    AddOperator(&extrel2::GDOp);

    AddOperator(&extrel2::minPathCost1Op);
    AddOperator(&extrel2::minPathCost2Op);

    AddOperator(&extrel2::gbidijkstraOp);
    extrel2::gbidijkstraOp.enableInitFinishSupport();

    AddOperator(&extrel2::mtMinPathCosts1Op);
    extrel2::mtMinPathCosts1Op.enableInitFinishSupport();

    AddOperator(&extrel2::mtMinPathCosts2Op);
    extrel2::mtMinPathCosts2Op.enableInitFinishSupport();


    AddOperator(&extrel2::kmergediffOp);

    AddOperator(&extrel2::pMergeOp);
    extrel2::pMergeOp.enableInitFinishSupport();


#ifdef USE_PROGRESS
// support for progress queries
   extrel2::extrelsort.EnableProgress();
   extrel2::extrelsortParam.EnableProgress();

   extrel2::extrelsortby.EnableProgress();
   extrel2::extrelsortbyParam.EnableProgress();

   extrel2::extrelhybridhashjoin.EnableProgress();
   extrel2::extrelhybridhashjoinParam.EnableProgress();

   extrel2::extrelgracehashjoin.EnableProgress();
   extrel2::extrelgracehashjoinParam.EnableProgress();

   extrel2::extrelsortmergejoin.EnableProgress();
   extrel2::extrelsortmergejoinParam.EnableProgress();
#endif
  }

  ~ExtRelation2Algebra() {};
};

/*

6 Initialization

Each algebra module needs an initialization function. The algebra manager
has a reference to this function if this algebra is included in the list
of required algebras, thus forcing the linker to include this module.

The algebra manager invokes this function to get a reference to the instance
of the algebra class and to provide references to the global nested list
container (used to store constructor, type, operator and object information)
and to the query processor.

The function has a C interface to make it possible to load the algebra
dynamically at runtime.

*/

extern "C"
Algebra*
InitializeExtRelation2Algebra( NestedList* nlRef,
                               QueryProcessor* qpRef,
                               AlgebraManager* amRef )
{
  nl = nlRef;
  qp = qpRef;
  am = amRef;
  return (new ExtRelation2Algebra());
}

