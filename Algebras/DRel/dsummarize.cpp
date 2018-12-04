/*
----
This file is part of SECONDO.

Copyright (C) 2015,
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


//[$][\$]



1 Implementation of the secondo operator drelsummarize

drelsummarize uses a d[f]rel and creates a stream of tuple.

*/
//#define DRELDEBUG

#include "DRel.h"
#include "NestedList.h"
#include "ListUtils.h"
#include "QueryProcessor.h"
#include "StandardTypes.h"

#include "DRelHelpers.h"

extern NestedList* nl;
extern QueryProcessor* qp;

namespace distributed2 {

    ListExpr dsummarizeTM( ListExpr args );

    template<class T, class A>
    int dsummarizeVMT( Word* args, Word& result, int message,
        Word& local, Supplier s );

    template<class A>
    class dsummarizeRelInfo;
}

using namespace distributed2;

namespace drel {

/*
1.1 Type Mapping

Expect a d[f]el.

*/
    ListExpr dsummarizeTM( ListExpr args ) {

        #ifdef DRELDEBUG
        cout << "dsummarizeTM" << endl;
        cout << "args" << endl;
        cout << nl->ToString( args ) << endl;
        #endif

        std::string err = "d[f]rel expected";

        if( ! nl->HasLength( args, 1 ) ) {
            return listutils::typeError( err + 
                ": one argument is expected" );
        }

        if( DFRel::checkType( nl->First( args ) ) ) {
            return distributed2::dsummarizeTM( 
                nl->OneElemList( nl->TwoElemList( 
                    nl->SymbolAtom( DFArray::BasicType( ) ), 
                    nl->Second( nl->First( args ) ) ) ) );
        }
        if( DRel::checkType( nl->First( args ) ) ) {
            return distributed2::dsummarizeTM( 
                nl->OneElemList( nl->TwoElemList( 
                    nl->SymbolAtom( DArray::BasicType( ) ), 
                    nl->Second( nl->First( args ) ) ) ) );
        }

        return listutils::typeError( err +
            ": first argument is not a d[f]rel" );
    }

/*
1.2 Value Mapping

Selects all elements of the d[f]rel from the workers.

*/
    template<class T, class R>
    int dsummarizeVMT( Word* args, Word& result, int message,
        Word& local, Supplier s ) {

        #ifdef DRELDEBUG
        cout << "dsummarizeVMT" << endl;
        #endif

        return distributed2::dsummarizeVMT<T, R>( 
            args, result, message, local, s );
    }

/*
1.3 ValueMapping Array of drelsummarize

*/
    ValueMapping dsummarizeVM[ ] = {
        dsummarizeVMT<dsummarizeRelInfo<DArray>, DArray >,
        dsummarizeVMT<dsummarizeRelInfo<DFArray>, DFArray >
    };

/*
1.4 Selection function of dsummarize

*/
    int dsummarizeSelect( ListExpr args ) {

        return DRel::checkType( nl->First( args ) ) ? 0 : 1;
    }

/*
1.5 Specification of dsummarize

*/
    OperatorSpec dsummarizeSpec(
        "d[f]rel(rel(X)) -> stream(X)",
        "_ dsummarize",
        "Produces a stream of the drel elements.",
        "query drel1 dsummarize count"
    );

/*
1.6 Operator instance of dsummarize operator

*/
    Operator dsummarizeOp(
        "dsummarize",
        dsummarizeSpec.getStr( ),
        2,
        dsummarizeVM,
        dsummarizeSelect,
        dsummarizeTM
    );

} // end of namespace drel