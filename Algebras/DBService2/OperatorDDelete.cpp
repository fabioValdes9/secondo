/*

1.1.1 Class Implementation

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
#include "FileSystem.h"
#include "NestedList.h"
#include "StandardTypes.h"
#include "QueryProcessor.h"

#include "Algebras/DBService2/DBServiceClient.hpp"
#include "Algebras/DBService2/DebugOutput.hpp"
#include "Algebras/DBService2/OperatorDDelete.hpp"
#include "Algebras/DBService2/ReplicationUtils.hpp"

#include "boost/filesystem.hpp"

#include <loguru.hpp>

namespace fs = boost::filesystem;

using namespace std;

extern boost::recursive_mutex nlparsemtx;

namespace DBService
{

ListExpr OperatorDDelete::mapType(ListExpr nestedList)
{
    print(nestedList, std::cout);

    LOG_F(INFO, "%s", "Acquiring lock for nlparsemtx...");
    boost::lock_guard<boost::recursive_mutex> guard(nlparsemtx);
    LOG_F(INFO, "%s", "Successfully acquired lock for nlparsemtx...");

    if (!nl->HasLength(nestedList, 2) && !nl->HasLength(nestedList,3))
    {
        ErrorReporter::ReportError(
                "expected signature: string x bool");
                return nl->TypeError();
    }

    if(!CcString::checkType(nl->First(nestedList)))
    {
        ErrorReporter::ReportError(
                "first argument must be: string");
        return nl->TypeError();
    }
    if(nl->HasLength(nestedList,2)){
      if(!CcBool::checkType(nl->Second(nestedList)))
      {
        ErrorReporter::ReportError(
                "second argument must be: bool");
        return nl->TypeError();
      }
    } else {
        if(!CcString::checkType(nl->Second(nestedList))){
           return listutils::typeError("second arg is not a string");
        }
        if(!CcBool::checkType(nl->Third(nestedList))){
           return listutils::typeError("last argument is not of type bool");
        }
    }

    return listutils::basicSymbol<CcBool>();
}

int OperatorDDelete::mapValue(Word* args,
                              Word& result,
                              int message,
                              Word& local,
                              Supplier s)
{
    string relationName = static_cast<CcString*>(args[0].addr)->GetValue();
    bool deleteLocalRelation;
    string derivateName = "";
    if(qp->GetNoSons(s)==2)
    {
      deleteLocalRelation = static_cast<CcBool*>(args[1].addr)->GetValue();
    } else 
    {
      derivateName = static_cast<CcString*>(args[1].addr)->GetValue();
      deleteLocalRelation = static_cast<CcBool*>(args[2].addr)->GetValue();
    }

    print("relationName", relationName, std::cout);
    print("derivateName", derivateName, std::cout);
    print("deleteLocalRelation", deleteLocalRelation, std::cout);

    // Deleting the local relation's .bin-file.
    fs::path filepath = ReplicationUtils::getFilePathOnDBServiceWorker(
        SecondoSystem::GetInstance()->GetDatabaseName(),
        relationName);
    FileSystem::DeleteFileOrFolder(filepath.string());            

    bool success = false;
    try{
        DBServiceClient* cl = DBServiceClient::getInstance();
        if(cl){
           success  = cl->deleteReplicas(
                    SecondoSystem::GetInstance()->GetDatabaseName(),
                    relationName,
                    derivateName);
        }
    } catch(...){
       success = false;
    }


    if(deleteLocalRelation)
    {
        print("deleting local relation", std::cout);
        SecondoCatalog* catalog = SecondoSystem::GetCatalog();
        if (!catalog->DeleteObject(relationName))
        {
            success = false;
        }
    }

    result = qp->ResultStorage(s);
    static_cast<CcBool*>(result.addr)->Set(true, success);
    return 0;
}

} /* namespace DBService */
