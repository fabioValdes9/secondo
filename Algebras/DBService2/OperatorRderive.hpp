/*

1.1 ~OperatorRderive~

This operator reads a tuple stream from a relation and requests its replica
from the ~DBService~ if the relation is not available locally. It allows passing
a function that is executed on the replica so that only the matching tuples have
to be transferred.

----
This file is part of SECONDO.

Copyright (C) 2017,
Faculty of Mathematics and Computer Science,
Database Systems for New Applications.

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

*/
#ifndef ALGEBRAS_DBSERVICE_OperatorRderive_HPP_
#define ALGEBRAS_DBSERVICE_OperatorRderive_HPP_

#include "Operator.h"

namespace DBService {

/*

1.1.1 Operator Specification

*/

struct RderiveInfo: OperatorInfo
{
    RderiveInfo()
    {
      name = "rderive";
      signature = "rel(X) x string x fun(rel(X) -> Y)  -> Y";
      syntax    = "rel rderive[string, fun]";
      meaning   = "Create an object derived from a relation stored " 
                  "(replicated) by the DBService. The DBService "
                  " determines the location of the replicas and "
                  " creates the respective derived objects in "
                  "the same locations. Useful for creating indexes on "
                  "replicated relations. Relations with their indexes can then"
                  " be accessed by read3 operations.";
      example = "query plz rderive[\"plz_ort_btree\", . createbtree[Ort]]";
      remark  = "requires a DBService system";
      usesArgsInTypeMapping = true;
    }
};

/*

1.1.1 Class Definition

*/

class OperatorRderive
{
public:

/*

1.1.1.1 Type Mapping Function

*/
    static ListExpr mapType(ListExpr nestedList);

/*

1.1.1.1 Value Mapping Function

*/
    static int mapValue(Word* args,
                        Word& result,
                        int message,
                        Word& local,
                        Supplier s);
};

} /* namespace DBService */

#endif /* ALGEBRAS_DBSERVICE_OPERATORRDERIVE_HPP */

