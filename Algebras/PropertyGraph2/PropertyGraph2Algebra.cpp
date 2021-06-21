/*
\framebox{\huge{ PropertyGraphAlgebra.cpp }}

----
This file is part of SECONDO.

Copyright (C) 2004, University in Hagen, Department of Computer Science,
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


//paragraph [1] Title: [{\Large \bf \begin {center}] [\end {center}}]
//[TOC] [\tableofcontents]
//[_] [\_]

[1] Implements a Property-Graph database

This algebra implements a type PropertyGraph and operators to define
a schema and to query the graph.

[TOC]

*/

#include <string>
#include <list>

#include "Algebra.h"
#include "NestedList.h"
#include "ListUtils.h"
#include "NList.h"
#include "LogMsg.h"
#include "QueryProcessor.h"
#include "ConstructorTemplates.h"
#include "StandardTypes.h"
#include "TypeMapUtils.h"
#include "Symbols.h"
#include "SecondoInterfaceTTY.h"
#include "SecParser.h"

#include "../OrderedRelation/OrderedRelationAlgebra.h"
#include "Compute.h"

#include "MPointer.h"
#include "Mem.h"
#include "MemoryObject.h"
#include "MemCatalog.h"

#include "../Relation-C++/RelationAlgebra.h"
#include "Stream.h"
#include "StreamIterator.h"

#include "../FText/FTextAlgebra.h"

#include "Utils.h"
#include "RelationSchemaInfo.h"
#include "PropertyGraph2.h"
#include "PropertyGraphMem.h"
#include "QueryTree.h"
#include "QueryGraph.h"
#include "PropertyGraphQueryProcessor.h"
#include "QueryOutputFields.h"
#include "CypherLang.h"

using namespace std;

extern NestedList* nl;
extern QueryProcessor *qp;

namespace pgraph2 {

/*

1 Helper functions (catalog management)

Insert and access the PropertyGraph In-Memory Part

*/

   void insertPGraph2MemoryObject(PGraph2 *pg, MemoryGraphObject *pgm)
   {
      pgm->name=pg->name;
      string _inmemoryobj_catalogname = pg->name+"_MEMDATA";
      mm2algebra::MemCatalog *memCatalog =
        mm2algebra::MemCatalog::getInstance();
      if (memCatalog->isObject(_inmemoryobj_catalogname))
      {
         memCatalog->deleteObject(_inmemoryobj_catalogname);
      }
      else
      {
         memCatalog->insert(_inmemoryobj_catalogname, pgm);
      }
   }

   MemoryGraphObject *getPGraph2MemoryObject(PGraph2 *pg)
   {

      if(pg == NULL)
      {
         LOGOP(10, "getPGraph2 is not defined!");
         return NULL;
      }

      string _inmemoryobj_catalogname = pg->name+"_MEMDATA";

      mm2algebra::MemCatalog *memCatalog =
        mm2algebra::MemCatalog::getInstance();

      if (!memCatalog->isObject(_inmemoryobj_catalogname))
      {
         LOGOP(30, "getPGraph2MemoryObject: not loaded!");
         return NULL;
      }
      else
      {
        string memtypename = "";
         ListExpr te =
           memCatalog->getMMObjectTypeExpr(_inmemoryobj_catalogname);
         if (nl->ToString(te) != "(mem (mpgraph 0))")
         {
            LOGOP(30, "getPGraph2MemoryObject: invalid type!");
            return NULL;
         }
         MemoryGraphObject *pgm = (MemoryGraphObject *)
              memCatalog->getMMObject(_inmemoryobj_catalogname);

         return pgm;
      }
   }

   void removePGraph2MemoryParts(PGraph2 *pg)
   {
      mm2algebra::MemCatalog *memCatalog =
         mm2algebra::MemCatalog::getInstance();
      //  remove memory objects frst, as it keeps pointers to tuples
      string name=pg->name+"_MEMDATA";
      LOGOP(20,"removePGraph2MemoryParts","remove memory object: "+name);
      memCatalog->deleteObject(name);

      while(true)
      {
         int del=0;
         std::map<std::string,mm2algebra::MemoryObject*>* list =
            memCatalog->getMemContent();
         for(auto&& item:*list)
         {
            if (item.first.rfind(pg->name+"_rel_",0) == 0)
            {
               LOGOP(20,"removePGraph2MemoryParts","remove memory object: "+
                  item.first);
               memCatalog->deleteObject(item.first);
               del++;
               break;
            }
         }
         if (del==0) break;
      }
   }


/*

Accesses the first argument (PropertyGraph object) from within
a typemapping function

*/

   PGraph2 *getPGraph2Object(ListExpr args)
   {

      // ((pgraph dblp) ....)
      ListExpr graphName = nl->First(args);
      if(nl->ListLength(graphName) != 2) {
         LOGOP(10, "getPGraph2Object", "NL size != 2"
                   + nl->ToString(graphName));
         return NULL;
      }

      string graphType = nl->ToString(nl->First(graphName));
      string graphExpr = nl->ToString(nl->Second(graphName));

      if (graphType != "pgraph") {
         LOGOP(10, "getPGraph2Object", "Wrong type (pgraph expected): "
                   + graphType);
         return NULL;
      }

      // Evaluate graphExpr to get the reference to the graph
      Word res;

      if(! QueryProcessor::ExecuteQuery(graphExpr, res)) {
         LOGOP(10, "getPGraph2Object", "Could not evaluate expression: "
                   + graphExpr);
         return NULL;
      }

      return static_cast<PGraph2*>(res.addr);
   }


/*

2 Operators

2.1 Operator info

Dumps information about the PropertyGraph.

*/
//-----------------------------------------------------------------------------
// OPERATOR info
//-----------------------------------------------------------------------------
// typemapping
ListExpr info_OpTm2( ListExpr args )
{
   NList type(args);

   LOG(30,"info_OpTm2");
   LOG(30, "args", type);

   if(!PGraph2::checkType( type.first() ))
      return NList::typeError("first argument is not a pgraph object");

   NList res= NList(
      NList(Symbol::APPEND()),
      NList(NList(0).enclose() ),
      NList(CcString::BasicType())
   );

   LOG(30, res);
   return res.listExpr();
}

// function
int info_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"info_OpFun2");

   PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
   MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);

   cout << pg->DumpInfo(pgm) << "\n";

   // prepare result
   string info="";
   info="ok";
   result = qp->ResultStorage(s);
   CcString* res = static_cast<CcString*>( result.addr );
   res->Set(true, info);

   return 0;
}

// register
struct info_OpInfo2 : OperatorInfo {

   info_OpInfo2()
   {
      name      = "info";
      signature = "pgraph -> bool";
      syntax    = "_ info";
      meaning   = "Returns info about the property graph .";
      example =   "query p1 info;";
   }
};

/*

2.2 Operator addedgesrel

Adds an edge relation to the PropertyGraph.

*/
//-----------------------------------------------------------------------------
// OPERATOR addedgesrel
//-----------------------------------------------------------------------------
// splits notations of the form "name=relation.name"
void splitEdgeEndExpr(string &expr, string &fromfield, string &relname,
    string &relfield)
{
    int i=expr.find("=");
    int j=expr.find(".");
    if ((i<0)||(j<0)||(j<i)) return;
    fromfield=expr.substr(0,i);
    relname=expr.substr(i+1,j-i-1);
    relfield=expr.substr(j+1);
}

// typemapping
ListExpr addedgesrel_OpTm2( ListExpr args )
{
   try
   {
      LOGOP(30,"addedgesrel_OpTm2", "ARGS",nl->ToString(args));

      CheckArgCount(args,4,4);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgType(args,2,CcString::BasicType());
      CheckArgType(args,3,CcString::BasicType());
      CheckArgType(args,4,CcString::BasicType());

      string relname = GetArgValue(args,2);
      string fromclause = GetArgValue(args,3);
      string toclause = GetArgValue(args,4);

      // check from side
      string fieldfrom,relfrom,keyrelfrom;
      string fieldto,relto,keyrelto;
      splitEdgeEndExpr(fromclause ,fieldfrom, relfrom, keyrelfrom );
      splitEdgeEndExpr(toclause ,fieldto, relfrom, keyrelto );

      // check edge relation and foreign keys
      ListExpr reltype= SecondoSystem::GetCatalog()->GetObjectTypeExpr(
            relname);
      if (nl->IsEmpty(reltype))
         throw PGraph2Exception("relation not found");
      RelationSchemaInfo ri(reltype);
      if (ri.GetAttrInfo(fieldfrom)==NULL)
         throw PGraph2Exception("from-name not found!");
      if (ri.GetAttrInfo(fieldto)==NULL)
         throw PGraph2Exception("to-name not found!");


      // return result
      ListExpr res =
            nl->OneElemList(nl->SymbolAtom(CcBool::BasicType()) );

      LOGOP(30,"addedgesrel_OpTm2", "RES", nl->ToString(res));
      return res;
    }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }
}

// function
int addedgesrel_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"addedgesrel_OpFun2");

   // prepare args
   PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
   CcString *relname = static_cast<CcString*>( args[1].addr );
   CcString *fromexpr = static_cast<CcString*>( args[2].addr );
   CcString *toexpr = static_cast<CcString*>( args[3].addr );
   LOGOP(30,"addedgesrel_OpFun2","relname: ",relname->GetValue(), "from:",
        fromexpr->GetValue()," to:",toexpr->GetValue());

   // check from side
   string expr=fromexpr->GetValue();
   string fieldfrom,relfrom,keyrelfrom;
   string fieldto,relto,keyrelto;
   splitEdgeEndExpr(expr ,fieldfrom, relfrom, keyrelfrom );
   expr=toexpr->GetValue();
   splitEdgeEndExpr(expr ,fieldto, relto, keyrelto );

   // add edge
   pg->AddEdgeRel(relname->GetValue(), fieldfrom, relfrom, keyrelfrom, fieldto,
         relto, keyrelto);

   // force save pgraph object
   qp->SetModified(qp->GetSon(s, 0));

   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct addedgesrel_OpInfo2 : OperatorInfo {

   addedgesrel_OpInfo2()
   {
      name      = "addedgesrel";
      signature = "pgraph x string x string x string-> bool";
      syntax    = "_ addedgesrel[_,_,_]";
      meaning   = "Adds an edge relation";
      example =   "query p1 addedgesrel[\"WROTE_PLAY\",\"IdFrom=Author.Id\""
                  ",\"IdTo=Play.Id\"];";
   }
};

/*

2.3 Operator addnodesrel

Adds a node relation to the PropertyGraph.

*/
//-----------------------------------------------------------------------------
// OPERATOR addnodesrel
//-----------------------------------------------------------------------------
// typemapping
ListExpr addnodesrel_OpTm2( ListExpr args )
{
   try
   {
      LOGOP(30,"addnodesrel_OpTm2", "ARGS",nl->ToString(args));

      CheckArgCount(args,2,3);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgType(args,2,CcString::BasicType());

      // optional id name
      string idname="Id";
      if (nl->ListLength(args)==3) {
         idname=GetArgValue(args,3);
      }

      std::string relName = GetArgValue(args,2);

      ListExpr reltype= SecondoSystem::GetCatalog()->GetObjectTypeExpr(relName);
      if (nl->IsEmpty(reltype))
         throw PGraph2Exception("relation not found");
      RelationSchemaInfo ri(reltype);
      if (ri.GetAttrInfo(idname)==NULL)
         throw PGraph2Exception("id-name not found!");

      // return result
      // APPEND relation name
      ListExpr res = nl->ThreeElemList(
      nl->SymbolAtom(Symbol::APPEND()),
            nl->OneElemList(
               nl->StringAtom(idname)),
            nl->OneElemList(nl->SymbolAtom(CcBool::BasicType()) )
      );

      LOGOP(30,"addnodesrel_OpTm2", "RES", nl->ToString(res));
      return res;
    }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }
}

// function
int addnodesrel_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"addnodesrel_OpFun2");

   // prepare args
   PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
   CcString *relname = static_cast<CcString*>( args[1].addr ); //  from append
   CcString *aidname = static_cast<CcString*>( args[2].addr ); //  from append
   string idname=(aidname!=NULL?aidname->GetValue():"Id");
   LOGOP(30,"addnodesrel_OpFun2","relname: ",relname->GetValue(), "idname:",
        idname);

   // check if id in relation
   ListExpr reltype= SecondoSystem::GetCatalog()->GetObjectTypeExpr(
        relname->GetValue());
   if (nl->IsEmpty(reltype))
      throw SecondoException("relation not found");
   RelationSchemaInfo ri(reltype);
   if (ri.GetAttrInfo(idname)==NULL)
      throw SecondoException("id-name not found!");

   // add relation
   pg->AddNodesRel(relname->GetValue(), idname);

   // force save pgraph object
   qp->SetModified(qp->GetSon(s, 0));

   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct addnodesrel_OpInfo2 : OperatorInfo {

   addnodesrel_OpInfo2()
   {
      name      = "addnodesrel";
      signature = "pgraph x string x string-> bool";
      syntax    = "_ addnodesrel[_,_]";
      meaning   = "Adds a node relation (third argument attribute name id field"
                  " (optional))";
      example =   "query p1 addnodesrel[\"Author\",\"Id\"];";
   }
};

/*

2.4 Operator addindex

Defines Properties, that an index will be created for on loadgraph

*/
//-----------------------------------------------------------------------------
// OPERATOR addindex
//-----------------------------------------------------------------------------
// typemapping
ListExpr addindex_OpTm2( ListExpr args )
{
   try
   {

      LOGOP(30,"addindex_OpTm2", "ARGS",nl->ToString(args));

      CheckArgCount(args,3,3);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgType(args,2,CcString::BasicType());
      CheckArgType(args,3,CcString::BasicType());

      std::string relName = GetArgValue(args,2);
      std::string attrName = GetArgValue(args,3);

      ListExpr reltype= SecondoSystem::GetCatalog()->GetObjectTypeExpr(relName);
      if (nl->IsEmpty(reltype))
         throw PGraph2Exception("relation not found");
      RelationSchemaInfo ri(reltype);
      if (ri.GetAttrInfo(attrName)==NULL)
         throw PGraph2Exception("attrname not found!");

      // return result
      ListExpr res =
            nl->OneElemList(nl->SymbolAtom(CcBool::BasicType()) );

      LOGOP(30,"addedgesrel_OpTm2", "RES", nl->ToString(res));
      return res;
    }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }
}

// function
int addindex_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"addindex_OpFun2");

   // prepare args
   PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
   CcString *relname = static_cast<CcString*>( args[1].addr ); // from append
   CcString *attrname = static_cast<CcString*>( args[2].addr ); // from append

   LOGOP(30,"addindex_OpFun2","relname: ",relname->GetValue(), "attrname:",
        attrname->GetValue());

   // check edge relation and foreign keys
   ListExpr reltype= SecondoSystem::GetCatalog()->GetObjectTypeExpr(
         relname->GetValue());
   if (nl->IsEmpty(reltype))
      throw SecondoException("relation not found");
   RelationSchemaInfo ri(reltype);
   if (ri.GetAttrInfo(attrname->GetValue())==NULL)
      throw SecondoException("from-name not found!");

   // add index
   pg->AddIndex(relname->GetValue(), attrname->GetValue());

   // force save pgraph object
   qp->SetModified(qp->GetSon(s, 0));

   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct addindex_OpInfo2 : OperatorInfo {

   addindex_OpInfo2()
   {
      name      = "addindex";
      signature = "pgraph x string x string-> bool";
      syntax    = "_ addindex[_,_]";
      meaning   = "Adds an index for a relation";
      example =   "query p1 addindex[\"Author\",\"Name\"];";
   }
};

/*

2.5 Operator loadgraph

Loads the whole PropertyGraph in memory. A twin object MemoryPGraph2 will
be created and registered in the MemoryCatalog of SECONDO. All registered
relations will be loaded as in-memory relations and additional data structures
will be build. On the first use of loadgraph statistical information will be
gathered and kept in the persistent part (PropertyGraph)

*/
//-----------------------------------------------------------------------------
// OPERATOR loadgraph
//-----------------------------------------------------------------------------
// typemapping
ListExpr loadgraph_OpTm2( ListExpr args )
{

   LOGOP(30,"loadgraph_OpTm2", nl->ToString(args));

   CheckArgCount(args,1,1);
   CheckArgType(args,1,PGraph2::BasicType());

   NList res= NList(
      NList(Symbol::APPEND()),
      NList(NList(0).enclose() ),
      NList(CcBool::BasicType())
   );
   return res.listExpr();
}

// function
int loadgraph_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"loadgraph_OpFun2");
   MemoryGraphObject *pgm =NULL;
   try
   {
      PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
     // pg->structure="memory";

      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);
      if (pgm==NULL)
      {
         LOG(20,"recreating memory object!" );
         pgm = new MemoryGraphObject(
            "(mem (mpgraph 0))",
            getDBname());

         insertPGraph2MemoryObject(pg,pgm);

         pgm->LoadData(pg, false);
         if (pg->dumpGraph)
            pgm->DumpGraphDot("graph.dot");

         // force save pgraph object
         qp->SetModified(qp->GetSon(s, 0));
      }
      else
      {
         LOG(30,"already loaded. (Force reload by unloading first!)");
      }
   }
   catch(PGraph2Exception &e)
   {
      if(pgm!=NULL) pgm->Clear();
      LOGERR("loadgraph_OpFun2", e.msg());
      //throw SecondoException(e.msg());
   }
   catch(exception &e)
   {
      if(pgm!=NULL) pgm->Clear();
      LOGERR("loadgraph_OpFun2", e.what());
      //throw SecondoException(e.msg());
   }

   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct loadgraph_OpInfo2 : OperatorInfo {

   loadgraph_OpInfo2()
   {
      name      = "loadgraph";
      signature = "pgraph -> bool";
      syntax    = "_ loadgraph";
      meaning   = "loads in memory structures of the pgraph";
      example =   "query p1 loadgraph;";
   }
};



//-----------------------------------------------------------------------------
// OPERATOR loadgraphorel
//-----------------------------------------------------------------------------
// typemapping
ListExpr loadgraphorel_OpTm2( ListExpr args )
{

   LOGOP(30,"loadgraphorel_OpTm2", nl->ToString(args));

   CheckArgCount(args,1,1);
   CheckArgType(args,1,PGraph2::BasicType());

   NList res= NList(
      NList(Symbol::APPEND()),
      NList(NList(0).enclose() ),
      NList(CcBool::BasicType())
   );
   return res.listExpr();
}

// function
int loadgraphorel_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"loadgraphorel_OpFun2");
   MemoryGraphObject *pgm =NULL;
   try
   {
      PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );

      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);
      if (pgm==NULL)
      {
         LOG(20,"recreating memory object!" );
         pgm = new MemoryGraphObject(
            "(mem (mpgraph 0))",
            getDBname());

         insertPGraph2MemoryObject(pg,pgm);

         pgm->LoadData2(pg, false, s);
         if (pg->dumpGraph)
            pgm->DumpGraphDot("graph.dot");

         // force save pgraph object
         qp->SetModified(qp->GetSon(s, 0));
      }
      else
      {
         LOG(30,"already loaded. (Force reload by unloading first!)");
      }
   }
   catch(PGraph2Exception &e)
   {
      if(pgm!=NULL) pgm->Clear();
      LOGERR("loadgraphorel_OpFun2", e.msg());
      //throw SecondoException(e.msg());
   }
   catch(exception &e)
   {
      if(pgm!=NULL) pgm->Clear();
      LOGERR("loadgraphorel_OpFun2", e.what());
      //throw SecondoException(e.msg());
   }

   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct loadgraphorel_OpInfo2 : OperatorInfo {

   loadgraphorel_OpInfo2()
   {
      name      = "loadgraphorel";
      signature = "pgraph -> bool";
      syntax    = "_ loadgraphorel";
      meaning   = "loads in memory structures of the pgraph"
       "for the use with ordered Relations";
      example =   "query p1 loadgraphorel;";
   }
};

/*

2.6 Operator unload

Unloads and frees all memory used by the PropertyGraph.

*/
//-----------------------------------------------------------------------------
// OPERATOR unload
//-----------------------------------------------------------------------------
// typemapping
ListExpr unload_OpTm2( ListExpr args )
{
   NList type(args);

   LOG(30,"unload_OpTm2");
   LOG(30, "args", type);

   if(!PGraph2::checkType( type.first() ))
      return NList::typeError("first argument is not a pgraph object");

   NList res= NList(
      NList(Symbol::APPEND()),
      NList(NList(0).enclose() ),
      NList(CcBool::BasicType())
   );

   LOG(30, res);
   return res.listExpr();

}

// function
int unload_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"unload_OpFun2");
   MemoryGraphObject *pgm =NULL;
   try
   {
      PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);
      if (pgm==NULL)
      {
         LOGOP(10,"already unloaded!" );
      }
      else
      {
         pgm->Clear();
         removePGraph2MemoryParts(pg);
         pg->structure="placeholder";
         qp->SetModified(qp->GetSon(s, 0));
      }

   }
   catch(PGraph2Exception &e)
   {
      if(pgm!=NULL) pgm->Clear();
      LOGERR("unload_OpFun2", e.msg());
      //throw SecondoException(e.msg());
   }


   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct unload_OpInfo2 : OperatorInfo {

   unload_OpInfo2()
   {
      name      = "unload";
      signature = "pgraph -> bool";
      syntax    = "_ unload";
      meaning   = "Removes in-memory structures of the pgraph";
      example =   "query p1 unload;";
   }
};

/*

2.7 Operator clearstat

Like the operator ~unload~ - additionally it will clear the statistical part of
the PropertyGraph, so it will be gathered again in ~loadgraph~.

*/
//-----------------------------------------------------------------------------
// OPERATOR clearstat
//-----------------------------------------------------------------------------
// typemapping
ListExpr clearstat_OpTm2( ListExpr args )
{
   NList type(args);

   LOG(30,"clearstat_OpTm2");
   LOG(30, "args", type);

   if(!PGraph2::checkType( type.first() ))
      return NList::typeError("first argument is not a pgraph object");

   NList res= NList(
      NList(Symbol::APPEND()),
      NList(NList(0).enclose() ),
      NList(CcBool::BasicType())
   );

   LOG(30, res);
   return res.listExpr();

}

// function
int clearstat_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"clearstat_OpFun2");
   MemoryGraphObject *pgm =NULL;
   try
   {
      PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );

      // unload
      removePGraph2MemoryParts(pg);
      pg->ClearStat();
      // force save pgraph object
      qp->SetModified(qp->GetSon(s, 0));

   }
   catch(PGraph2Exception &e)
   {
      if(pgm!=NULL) pgm->Clear();
      LOGERR("clearstat_OpFun2", e.msg());
      //throw SecondoException(e.msg());
   }


   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct clearstat_OpInfo2 : OperatorInfo {

   clearstat_OpInfo2()
   {
      name      = "clearstat";
      signature = "pgraph -> bool";
      syntax    = "_ clearstat";
      meaning   = "Removes in-memory and statistical data structures "
                  "of the pgraph";
      example =   "query p1 clearstat;";
   }
};

/*

2.8 Operator cfg

Allows to set the following options.

 - ~log~: Loglevel 0:None 10:Moderate 20:verbose 30:very verbose

 - ~dotquery~: Dumps DOT-files for generated QueryGraph and QueryTree

 - ~dotgraph~: Dumps the whole loaded graph


*/
//-----------------------------------------------------------------------------
// OPERATOR setoption
//-----------------------------------------------------------------------------
// typemapping
ListExpr cfg_OpTm2( ListExpr args )
{
   LOGOP(30,"setoption_OpTm2", nl->ToString(args));

   CheckArgCount(args,3,3);
   CheckArgType(args,1,PGraph2::BasicType());
   CheckArgType(args,2,CcString::BasicType());
   CheckArgType(args,3,CcString::BasicType());

   NList res= NList(
      NList(Symbol::APPEND()),
      NList(NList(0).enclose() ),
      NList(CcBool::BasicType())
   );

   LOG(30, res);
   return res.listExpr();

}

// function
int cfg_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"cfg_OpFun2");
   try
   {
      PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );

      string name = GetArg_STRING(args[1].addr);
      string value = GetArg_STRING(args[2].addr);

      //
      bool handled = false;
      if (name=="log")
              { handled=true; setDebugLevel( stoi( value )); }
      if (name=="structure")
              { handled=true; pg->structure=value; }
      if (name=="dotquery")
              { handled=true; pg->dumpQueryTree=(value=="1"||value=="true"); }
      if (name=="dotgraph")
              { handled=true; pg->dumpGraph=(value=="1"||value=="true"); }

      if (handled)
      {
         LOG(30,"setting "+name+" = "+value);
         // force save pgraph object
         qp->SetModified(qp->GetSon(s, 0));
      }
      else
          throw PGraph2Exception("configuration not found: "+name);

   }
   catch(PGraph2Exception &e)
   {
      LOGERR("cfg_OpFun2", e.msg());
      //throw SecondoException(e.msg());
   }
   catch(exception &e)
   {
      LOGERR("cfg_OpFun2", e.what());
   }


   // prepare result
   result = qp->ResultStorage(s);
   CcBool* res = static_cast<CcBool*>( result.addr );
   res->Set(true, true);
   return 0;
}

// register
struct cfg_OpInfo2 : OperatorInfo {

   cfg_OpInfo2()
   {
      name =      "cfg";
      signature = "(pgraph) x string x string -> bool";
      syntax =    "_ cfg [_,_]";
      meaning   = "set configuration for PropertyGraph processor";
      example =   "query pgraph cfg['log','10']";
   }
};

/*

2.9 Operator createpgraph

Creates a persistent PropertyGraph object.

*/
//----------------------------------------------------------------------------
// OPERATOR createpgraph
//----------------------------------------------------------------------------
// typemapping
ListExpr createpgraph_OpTm2( ListExpr args )
{
   LOG(30,"createpgraph_OpTm2");
   NList type(args);

  if ( type.length() != 1 ) {
    return NList::typeError("Expecting one argument ");
  }

  if ( type.first() != CcString::BasicType() ) {
    return NList::typeError("Expecting graph name ");
  }

  ListExpr resType=listutils::basicSymbol<PGraph2>();

  return resType;

}

// function
int createpgraph_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   LOG(30,"createpgraph_OpFun2");

   // prepare args
   CcString *ms = static_cast<CcString*>( args[0].addr );


   // prepare result
   result = qp->ResultStorage(s);
   PGraph2* pg = (PGraph2*) result.addr;

   pg->name=ms->GetValue();

   LOG(30, "creating pgraph ");

   return 0;
}

// register
struct createpgraph_OpInfo2 : OperatorInfo {

   createpgraph_OpInfo2()
   {
      name =      "createpgraph";
      signature = "string -> (pgraph)";
      syntax =    "createpgraph[_,_]";
      meaning   = "Creates a property graph";
      example =   "let p1=createpgraph(\"p1\");";
   }
};

/*

2.10 Operator match1

Queries the PropertyPGraph2 by using a querytree and an node stream.

*/
//-----------------------------------------------------------------------------
// OPERATOR match1
//-----------------------------------------------------------------------------
// typemapping
ListExpr match1_OpTm2( ListExpr args )
{
   try
   {
      LOGOP(30,"match1_OpTm2", nl->ToString(args));

      CheckArgCount(args,5,5);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgTypeIsTupleStream(args,2);
      CheckArgType(args,3,FText::BasicType());
      CheckArgType(args,4,FText::BasicType());
      CheckArgType(args,5,FText::BasicType());

      //
      PGraph2 *pg = getPGraph2Object(args);

      // get alias list from query tree
      QueryTree *tree=new QueryTree();
      tree->ReadQueryTree(GetArgValue(args,3));
      //
      QueryOutputFields of;
      of.ReadFromList(GetArgValue(args,5));

      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);
      if (pgm==NULL || !pgm->IsLoaded())
         throw PGraph2Exception("graph not loaded!");

      if (!(pg->structure=="memory") && !(pg->structure=="orel"))
        throw PGraph2Exception("structure is not memory or orel: ");

      ListExpr res;
      res=of.StreamTypeDefinition(&tree->AliasList, pgm);
      LOGOP(30,"match1_OpTm2", nl->ToString(res));
      return res;
   }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }
}

// function

int match1_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   try
   {
      PGraphQueryProcessor *pgp;
      switch( message )
      {
         case OPEN: { // initialize the local storage
            LOGOP(30,"match1_OpFun2");
            // prepare args
            PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );

            string querylist = GetArg_FTEXT_AS_STRING(args[2].addr );
            ListExpr filterlist =  GetArg_FTEXT_AS_LIST(args[3].addr );
            ListExpr fieldlist =  GetArg_FTEXT_AS_LIST(args[4].addr );

            // prepare query processor
            pgp = new PGraphQueryProcessor(pg, getPGraph2MemoryObject(pg));
            if (pgp->pgraphMem ==NULL || !pgp->pgraphMem->IsLoaded())
               throw PGraph2Exception("graph not loaded!");

            if (!(pg->structure=="memory") && !(pg->structure=="orel"))
                throw PGraph2Exception("structure is not memory or orel: ");

            // transform list to query tree
            QueryTree *tree=new QueryTree();
            try{
               tree->ReadQueryTree(querylist);
               tree->ReadFilterList(filterlist);
               tree->ReadOutputFieldList(fieldlist);
               pgp->SetInputStream(args[1].addr);
               pgp->SetQueryTree(tree);
             //pgp->SetInputTupleType(qp->GetSupplierTypeExpr(qp->GetSon(s,1)));
               if (pg->dumpQueryTree)
                  tree->DumpTreeDot(NULL,"querytree.dot");
            }
            catch(PGraph2Exception &e)
            {
               cout << "ERROR: " << e.msg() << endl;
            }
            catch(SecondoException &e)
            {
               cout << "ERROR: " << e.msg() << endl;
            }

            ListExpr resultType = GetTupleResultType( s );
            pgp->_OutputTupleType = new TupleType(nl->Second(resultType));
            qp->Open(args[0].addr);
            local.addr = pgp;
            return 0;

         }
         case REQUEST: { // return the next stream element
            PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
            pgp = (PGraphQueryProcessor*)local.addr;
            Tuple* tuple = NULL;
            if (pg->structure=="memory") {
                tuple = pgp->ReadNextResultTuple();
            }
            if (pg->structure=="orel") {
                tuple = pgp->ReadNextResultTuple2();
            }
            if (tuple!=NULL)
            {
               result.setAddr(tuple);
               return YIELD;
            }

            result.addr = 0;
            return CANCEL;
         }
         case CLOSE: { // free the local storage
            if (local.addr)
            {
            pgp = (PGraphQueryProcessor*)local.addr;
            pgp->_OutputTupleType->DeleteIfAllowed();
            delete pgp;
            local.addr = 0;
            }
            qp->Close(args[0].addr);
            return 0;
         }
         default: {
            return -1;
         }
      }
   }
   catch(PGraph2Exception &e)
   {
      LOGERR("match1_OpFun2", e.msg());
      throw SecondoException(e.msg());
   }
}

// register
struct match1_OpInfo2 : OperatorInfo {

   match1_OpInfo2()
   {
      name      = "match1";
      signature = "pgraph x stream(tuple) x string x string x string "
                  "-> stream(tuple)";
      syntax    = "_ _ match1[_,_,_]";
      meaning   = "Query Graph using a query tree";
      example =   "query p1 authors feed match1['(querytree)','(filterlist)',"
                  "'(fieldlist)']";
   }
};


/*

2.10 Operator match1b

Queries the PropertyPGraph2 by using a querytree and an node stream.

*/
//-----------------------------------------------------------------------------
// OPERATOR match1b
//-----------------------------------------------------------------------------
// typemapping
ListExpr match1b_OpTm2( ListExpr args )
{
   try
   {
      LOGOP(30,"match1b_OpTm2", nl->ToString(args));

      CheckArgCount(args,6,6);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgTypeIsTupleStream(args,2);
      CheckArgType(args,3,FText::BasicType());
      CheckArgType(args,4,FText::BasicType());
      CheckArgType(args,5,FText::BasicType());
      CheckArgType(args,6,FText::BasicType());

      //
      PGraph2 *pg = getPGraph2Object(args);

      // get alias list from query tree
      QueryTree *tree=new QueryTree();
      tree->ReadQueryTree(GetArgValue(args,3));
      //
      QueryOutputFields of;
      of.ReadFromList(GetArgValue(args,5));

      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);
      if (pgm==NULL || !pgm->IsLoaded())
         throw PGraph2Exception("graph not loaded!");

      if (!(pg->structure=="pregelpersistent") && \
           !(pg->structure=="pregelmemory"))
        throw PGraph2Exception("structure is not"
         "pregelpersistent or pregelmemory: ");

      string partstr="part";
      ListExpr part= SecondoSystem::GetCatalog()->GetObjectTypeExpr(
            partstr);
      if (nl->IsEmpty(part))
         throw PGraph2Exception("part-function not defined");

      string workerstr="Workers";
      ListExpr worker= SecondoSystem::GetCatalog()->GetObjectTypeExpr(
            workerstr);
      if (nl->IsEmpty(worker))
         throw PGraph2Exception("Workers Object not defined");

      string nworkerstr="NWorkers";
      ListExpr nworker= SecondoSystem::GetCatalog()->GetObjectTypeExpr(
            nworkerstr);
      if (nl->IsEmpty(nworker))
         throw PGraph2Exception("NWorkers Object not defined");

    NList res= NList(
        NList(Symbol::APPEND()),
        NList(NList(0).enclose() ),
        NList(CcBool::BasicType())
    );

   LOG(30, res);
   return res.listExpr();

   }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }
}

// function

int match1b_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   try
   {
        PGraphQueryProcessor *pgp;
        Compute *compute;

        PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );

        string querylist = GetArg_FTEXT_AS_STRING(args[2].addr );
        ListExpr filterlist =  GetArg_FTEXT_AS_LIST(args[3].addr );
        ListExpr fieldlist =  GetArg_FTEXT_AS_LIST(args[4].addr );

        string initialmessage = "";
        CcString *intmessage = static_cast<CcString*>( args[5].addr );
        if (intmessage!=NULL)
        {
            initialmessage = GetArg_FTEXT_AS_STRING(args[5].addr );
        }

        // prepare query processor
        pgp = new PGraphQueryProcessor(pg, getPGraph2MemoryObject(pg));
        if (pgp->pgraphMem ==NULL || !pgp->pgraphMem->IsLoaded())
            throw PGraph2Exception("graph not loaded!");

        if (!(pg->structure=="pregelpersistent") && \
            !(pg->structure=="pregelmemory"))
            throw PGraph2Exception("structure is not "
            "pregelpersistent or pregelmemory: ");

        // transform list to query tree
        QueryTree *tree=new QueryTree();
        try{
            tree->ReadQueryTree(querylist);
            tree->ReadFilterList(filterlist);
            tree->ReadOutputFieldList(fieldlist);
            //pgp->SetInputStream(args[1].addr);
            pgp->SetQueryTree(tree);
            //pgp->SetInputTupleType(qp->GetSupplierTypeExpr(qp->GetSon(s,1)));
            if (pg->dumpQueryTree)
                tree->DumpTreeDot(NULL,"querytree.dot");

            //Check if all edges have the attribute 'Cost'
            uint countercost=0;
            for (uint i=0; i<pgp->poslist.size(); i++)
            {
                RelationInfo *relinfo=pgp->pgraphMem->RelRegistry.\
                    GetRelationInfo(pgp->poslist.at(i)->TypeName);
                int attrcount=relinfo->RelSchema.GetAttrCount();
                for (int j=0; j<attrcount; j++)
                {
                    AttrInfo *attributes=relinfo->RelSchema.GetAttrInfo(j);
                    if (attributes->Name == "Cost" && \
                        attributes->TypeName == "real")
                        countercost++;
                }
            }
            if (!(countercost == pgp->poslist.size()) && \
                pg->structure=="pregelmemory")
                throw PGraph2Exception("Pregelmemory not usable,"
                    "because not all Edges have the attribute 'Cost'");

        if (initialmessage.empty())
        {
            compute=new Compute(pgp,tree,pg->structure);
        }
        else
        {
            compute=new Compute(pgp,tree,pg->structure,initialmessage);
        }
        compute->CreateComputeFunction();
        cout << compute->gesamtstring << endl;

        if (pgp->pgraphMem->pregelcounter == 0) {
            compute->runPregel();
            pgp->pgraphMem->pregelcounter ++;
        }
        else
        {
            compute->runPregelSecondTime();
        }

        }
        catch(PGraph2Exception &e)
        {
            cout << "ERROR: " << e.msg() << endl;
        }
        catch(SecondoException &e)
        {
            cout << "ERROR: " << e.msg() << endl;
        }

        // prepare result
        result = qp->ResultStorage(s);
        CcBool* res = static_cast<CcBool*>( result.addr );
        res->Set(true, true);
        return 0;

    }
    catch(PGraph2Exception &e)
    {
        LOGERR("match1c_OpFun2", e.msg());
        throw SecondoException(e.msg());
    }
}

// register
struct match1b_OpInfo2 : OperatorInfo {

   match1b_OpInfo2()
   {
      name      = "match1b";
      signature = "pgraph x string x string x string "
                  "-> bool";
      syntax    = "_ match1b[_,_,_]";
      meaning   = "Query Graph using a query tree";
      example =   "query p1 authors feed match1b['(querytree)','(filterlist)',"
                  "'(fieldlist)']";
   }
};


/*

1 Operator match2

*/

/*

2.10 Operator match2

Queries the PropertyPGraph2 by using a querygraph.

*/
//-----------------------------------------------------------------------------
// OPERATOR match2
//-----------------------------------------------------------------------------
// typemapping
ListExpr match2_OpTm2( ListExpr args )
{
   try
   {
      NList type(args);
      LOGOP(30,"match2_OpTm2", nl->ToString(args));

      CheckArgCount(args,4,5);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgType(args,2,FText::BasicType());
      CheckArgType(args,3,FText::BasicType());
      CheckArgType(args,4,FText::BasicType());

      //
      PGraph2 *pg = getPGraph2Object(args);
      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);
      if (pgm==NULL || !pgm->IsLoaded())
         throw PGraph2Exception("graph not loaded!");

      if (!(pg->structure=="memory") && !(pg->structure=="orel"))
        throw PGraph2Exception("structure is not memory or orel: ");

      // get alias list from query graph
      QueryGraph qg(&pgm->RelRegistry);
      qg.ReadQueryGraph(GetArgValue(args,2));
      qg.Validate();

      QueryOutputFields of;
      of.ReadFromList(type.fourth().second().str());

      return of.StreamTypeDefinition(&qg.AliasList, pgm);
   }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }

}


// function
int match2_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   try
   {
      PGraphQueryProcessor *pgp;
      switch( message )
      {
         case OPEN: { // initialize the local storage
            LOGOP(30,"match2_OpFun2");

            // prepare args
            PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );

            string options=GetArg_FTEXT_AS_STRING(args[4].addr); // optional
            string querylist=GetArg_FTEXT_AS_STRING(args[1].addr);
            ListExpr filterlist=GetArg_FTEXT_AS_LIST(args[2].addr);
            ListExpr fieldlist=GetArg_FTEXT_AS_LIST(args[3].addr);

            string forcealias="";
            if (args[4].addr!=NULL)
               forcealias=GetArg_FTEXT_AS_STRING(args[4].addr);

            // prepare query processor
            pgp = new PGraphQueryProcessor(pg, getPGraph2MemoryObject(pg));

            // get memory object
            if (pgp->pgraphMem==NULL || !pgp->pgraphMem->IsLoaded())
               throw PGraph2Exception("graph not loaded!");

            if (!(pg->structure=="memory") && !(pg->structure=="orel"))
                throw PGraph2Exception("structure is not memory or orel: ");

            // convert list to optimal querytree
            QueryGraph qg(&pgp->pgraphMem->RelRegistry);
            qg.ReadQueryGraph(querylist);
            qg.Validate();
            if (pg->dumpQueryTree)
               qg.DumpGraphDot("querygraph.dot");

            //
            QueryTree *tree=new QueryTree();
            try
            {
               tree=qg.CreateOptimalQueryTree(forcealias);
               tree->ReadFilterList(filterlist);
               tree->ReadOutputFieldList(fieldlist);
               pgp->SetQueryTree(tree);
               pgp->SetInputRelation(tree);
               LOGOP(20,"QueryTree:", tree->DumpTreeAsList());
               if (pg->dumpQueryTree)
                  tree->DumpTreeDot(NULL,"querytree.dot");
            }
            catch(PGraph2Exception &e)
            {
               cout << "ERROR: " << e.msg() << endl;
            }
            catch(SecondoException &e)
            {
               cout << "ERROR: " << e.msg() << endl;
            }

            ListExpr resultType = GetTupleResultType( s );
            pgp->_OutputTupleType = new TupleType(nl->Second(resultType));
            qp->Open(args[0].addr);
            local.addr = pgp;
            return 0;

         }
         case REQUEST: { // return the next stream element
            PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
            pgp = (PGraphQueryProcessor*)local.addr;

            Tuple* tuple = NULL;
            if (pg->structure=="memory") {
                tuple = pgp->ReadNextResultTuple();
            }
            if (pg->structure=="orel") {
                tuple = pgp->ReadNextResultTuple2();
            }
            if (tuple!=NULL)
            {
               result.setAddr(tuple);
               return YIELD;
            }

            result.addr = 0;
            return CANCEL;

         }
         case CLOSE: { // free the local storage
            if (local.addr)
            {
            pgp = (PGraphQueryProcessor*)local.addr;
            pgp->_OutputTupleType->DeleteIfAllowed();
            delete pgp;
            local.addr = 0;
            }
            qp->Close(args[0].addr);
            return 0;

         }
         default: {
            return -1;
         }
      }
   }
   catch(PGraph2Exception &e)
   {
      LOGERR("match2_OpTm2", e.msg());
      throw SecondoException(e.msg());
   }
}

// register
struct match2_OpInfo2 : OperatorInfo {

   match2_OpInfo2()
   {
      name      = "match2";
      signature = "pgraph x string x string x string -> stream(tuple)";
      syntax    = "_ match2[_,_,_]";
      meaning   = "Queries a PropertyGraph using a querygraph";
      example =   "query match2['(querygraph)','(filterlist)','(fieldlist)']";
   }
};


/*

2.10 Operator match3

Queries the PropertyPGraph2 by using cypher expressions

*/
//-----------------------------------------------------------------------------
// OPERATOR match3
//-----------------------------------------------------------------------------
// typemapping

ListExpr match3_OpTm2( ListExpr args )
{
   try
   {
      NList type(args);
      LOGOP(20, "match3_OpTm2", nl->ToString(args));

      CheckArgCount(args,2,3);
      CheckArgType(args,1,PGraph2::BasicType());
      CheckArgType(args,2,FText::BasicType());

      string options="";
      if (type.length()>2)
         options=GetArgValue(args,3);

      LOGOP(20, "match3_OpTm2",options);

      PGraph2 *pg = getPGraph2Object(args);
      MemoryGraphObject *pgm = getPGraph2MemoryObject(pg);

      if (pgm==NULL || !pgm->IsLoaded())
         throw PGraph2Exception("property graph not loaded");

      if (!(pg->structure=="memory") && !(pg->structure=="orel"))
        throw PGraph2Exception("structure is not memory or orel: ");

      // parse cypher
      string cypherstring = type.second().second().str();
      CypherLanguage cypherexpr;

      ListExpr res=0;
      string cypheraslist;
      if (!cypherexpr.parse(cypherstring))
      {
         cypheraslist=cypherexpr.dumpAsListExpr();
         LOGOP(10,"match3_OpTm2 cypheraslist", cypheraslist);
         nl->ReadFromString(cypheraslist, res);
      }
      else
      {
         throw PGraph2Exception("ERROR parsing Cypher: "+cypherexpr._errormsg);
      }

      //
      QueryGraph qg(&pgm->RelRegistry);
      qg.ReadQueryGraph(nl->First(res));
      qg.Validate();

      QueryOutputFields of;
      of.ReadFromList( nl->ToString( nl->Third(res)) );
      ListExpr outfields = of.StreamTypeDefinition(&qg.AliasList, pgm);

      res = nl->ThreeElemList(
      nl->SymbolAtom(Symbol::APPEND()),
            nl->OneElemList(
               nl->TextAtom( cypheraslist )),
            outfields
      );
      return res;
   }
   catch(PGraph2Exception &e)
   {
      return NList::typeError("ERROR:  "+e.msg());
   }

}


// function
int match3_OpFun2 (Word* args, Word& result, int message,
              Word& local, Supplier s)
{
   try
   {
      PGraphQueryProcessor *pgp;
      switch( message )
      {
         case OPEN: { // initialize the local storage
            LOGOP(30,"match2_OpFun2");

            // prepare args
            PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
            // already parsed in type mapping
            //(take care of the index as the option arg is optional)

            string options="";
            string cypherstring="";
            if (args[3].addr!=0) {
                options=GetArg_FTEXT_AS_STRING(args[2].addr);
                cypherstring=GetArg_FTEXT_AS_STRING(args[3].addr);
            }
            else
                cypherstring=GetArg_FTEXT_AS_STRING(args[2].addr);

            LOGOP(20,"match3_OpFun2","options: ",options);
            LOGOP(10,"match3_OpFun2","cyptherstring from TM: ",cypherstring);
            ListExpr cypherlist=0;
            nl->ReadFromString(cypherstring, cypherlist);

            // prepare query processor
            pgp = new PGraphQueryProcessor(pg, getPGraph2MemoryObject(pg));

            // get memory object
            if (pgp->pgraphMem==NULL || !pgp->pgraphMem->IsLoaded())
               throw PGraph2Exception("graph not loaded!");

            if (!(pg->structure=="memory") && !(pg->structure=="orel"))
                throw PGraph2Exception("structure is not memory or orel: ");

            // convert list to optimal querytree
            QueryGraph qg(&pgp->pgraphMem->RelRegistry);
            qg.ReadQueryGraph(nl->First(cypherlist));
            qg.Validate();
            if (pg->dumpQueryTree)
               qg.DumpGraphDot("querygraph.dot");

            //
            QueryTree *tree=new QueryTree();
            try
            {
               tree=qg.CreateOptimalQueryTree();
               tree->ReadFilterList(nl->Second(cypherlist));
               tree->ReadOutputFieldList(nl->Third(cypherlist));
               tree->Validate();
               pgp->SetQueryTree(tree);
               pgp->SetInputRelation(tree);
               if (pg->dumpQueryTree)
                  tree->DumpTreeDot(NULL,"querytree.dot");
            }
            catch(PGraph2Exception &e)
            {
               cout << "ERROR: " << e.msg() << endl;
            }
            catch(SecondoException &e)
            {
               cout << "ERROR: " << e.msg() << endl;
            }

            ListExpr resultType = GetTupleResultType( s );
            pgp->_OutputTupleType = new TupleType(nl->Second(resultType));
            qp->Open(args[0].addr);
            local.addr = pgp;
            return 0;

         }
         case REQUEST: { // return the next stream element
            pgp = (PGraphQueryProcessor*)local.addr;
            PGraph2 *pg = static_cast<PGraph2*>( args[0].addr );
            pgp = (PGraphQueryProcessor*)local.addr;

            Tuple* tuple = NULL;
            if (pg->structure=="memory") {
                tuple = pgp->ReadNextResultTuple();
            }
            if (pg->structure=="orel") {
                tuple = pgp->ReadNextResultTuple2();
            }
            if (tuple!=NULL)
            {
               result.setAddr(tuple);
               return YIELD;
            }

            result.addr = 0;
            return CANCEL;

         }
         case CLOSE: { // free the local storage
            if (local.addr)
            {
            pgp = (PGraphQueryProcessor*)local.addr;
            pgp->_OutputTupleType->DeleteIfAllowed();
            delete pgp;
            local.addr = 0;
            }
            qp->Close(args[0].addr);
            return 0;

         }
         default: {
            return -1;
         }
      }
   }
   catch(PGraph2Exception &e)
   {
      LOGERR("match3_OpTm2", e.msg());
      throw SecondoException(e.msg());
   }
}

// register
struct match3_OpInfo2 : OperatorInfo {

   match3_OpInfo2()
   {
      name      = "match3";
      signature = "pgraph x string -> stream(tuple)";
      syntax    = "_ match3[_]";
      meaning   = "Queries a PropertyGraph using a cypher expression";
      example =   "query match3['MATCH ... WHERE ... RETURN ...']";
   }
};


/*

3 Algebra

Defines the algebra, the type ~PropertyGraph~ and its Operators.

*/
class PropertyGraph2Algebra : public Algebra
{
public:
   PropertyGraph2Algebra() : Algebra()
   {
      AddTypeConstructor( &pgraph2TC );
      pgraph2TC.AssociateKind( Kind::SIMPLE() );

      Operator*op2=NULL;
      op2 = AddOperator( info_OpInfo2(), info_OpFun2, info_OpTm2 );
      op2 = AddOperator( addnodesrel_OpInfo2(),
                        addnodesrel_OpFun2, addnodesrel_OpTm2);
      op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( addindex_OpInfo2(), addindex_OpFun2, addindex_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( addedgesrel_OpInfo2(),
                        addedgesrel_OpFun2, addedgesrel_OpTm2);
      op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( createpgraph_OpInfo2(), createpgraph_OpFun2,
                        createpgraph_OpTm2 );
      op2 = AddOperator( loadgraph_OpInfo2(), \
        loadgraph_OpFun2, loadgraph_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( loadgraphorel_OpInfo2(), \
        loadgraphorel_OpFun2, loadgraphorel_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      op2->SetUsesMemory();
      op2 = AddOperator( unload_OpInfo2(), unload_OpFun2, unload_OpTm2 );
      op2 = AddOperator( clearstat_OpInfo2(), \
        clearstat_OpFun2, clearstat_OpTm2 );
      op2 = AddOperator( match1_OpInfo2(), match1_OpFun2, match1_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( match1b_OpInfo2(), match1b_OpFun2, match1b_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( match2_OpInfo2(), match2_OpFun2, match2_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      //op2 = AddOperator( match2b_OpInfo2(), match2b_OpFun2, match2b_OpTm2 );
      //op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( match3_OpInfo2(), match3_OpFun2, match3_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
      //op2 = AddOperator( match3b_OpInfo2(), match3b_OpFun2, match3b_OpTm2 );
      //op2->SetUsesArgsInTypeMapping();
      op2 = AddOperator( cfg_OpInfo2(), cfg_OpFun2, cfg_OpTm2 );
      op2->SetUsesArgsInTypeMapping();
   }
   ~PropertyGraph2Algebra() {};
};

} // end of namespace

extern "C"
Algebra*
InitializePropertyGraph2Algebra( NestedList* nlRef,
                               QueryProcessor* qpRef )
{
  return new pgraph2::PropertyGraph2Algebra;
}

