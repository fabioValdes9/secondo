
/*
----
This file is part of SECONDO.

Copyright (C) 2015, University in Hagen, 
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


#ifndef SECONDO_INTERFACE_TTY_H
#define SECONDO_INTERFACE_TTY_H

#include "SecondoInterface.h"


class SecondoInterfaceTTY : public SecondoInterface{
  
  
public:
  SecondoInterfaceTTY(bool isServer = false, NestedList* _nl = 0);
  
  ~SecondoInterfaceTTY();
  
  virtual bool Initialize( const std::string& user, const std::string& pswd,
                   const std::string& host, const std::string& port,
                   const std::string& profile, const std::string& home,
                   std::string& errorMsg,
                   const bool multiUser = false );
  
  virtual void Terminate();
  
  virtual void Secondo( const std::string& commandText,
                const ListExpr commandLE,
                const int commandType,
                const bool commandAsText,
                const bool resultAsText,
                ListExpr& resultList,
                int& errorCode,
                int& errorPos,
                std::string& errorMessage,
                const std::string& resultFileName =
                                "SecondoResult",
                const bool isApplicationLevelCommand = true);

   bool Secondo( const std::string& cmdText,
                 ListExpr& resultList,
                 SecErrInfo& err,
                 const std::string& resultFileName = "SecondoResult",
                 const bool isApplicationLevelCommand = true        ){
     return SecondoInterface::Secondo(cmdText,resultList,err,
                          resultFileName,isApplicationLevelCommand);
   }

   bool Secondo( const ListExpr cmdList,
                 ListExpr& resultList,
                 SecErrInfo& err,
                 const std::string& resultFileName = "SecondoResult",
                 const bool isApplicationLevelCommand = true        ){
     return SecondoInterface::Secondo(cmdList, resultList, err, 
                              resultFileName, isApplicationLevelCommand);
   }

  
  virtual ListExpr NumericTypeExpr( const ListExpr type );
  
  virtual bool GetTypeId( const std::string& name,
                  int& algebraId, int& typeId );
  
  virtual bool LookUpTypeExpr( ListExpr type, std::string& name,
                       int& algebraId, int& typeId );
  
  
  //virtual ListExpr DerivedObjValueExpr();
  
  virtual void SetDebugLevel( const int level );
  
  virtual  bool getOperatorIndexes(
         const std::string OpName,
         ListExpr argList,
         ListExpr& resList,
         int& algId,
         int& opId,
         int& funId,
         NestedList* listStorage);
  
  virtual bool getCosts(const int algId,
              const int opId,
              const int funId,
              const size_t noTuples,
              const size_t sizeOfTuple,
              const size_t noAttributes,
              const double selectivity,
              const size_t memoryMB,
              size_t& costs);
  
  virtual bool getCosts(const int algId,
              const int opId,
              const int funId,
              const size_t noTuples1,
              const size_t sizeOfTuple1,
              const size_t noAttributes1,
              const size_t noTuples2,
              const size_t sizeOfTuple2,
              const size_t noAttributes2,
              const double selectivity,
              const size_t memoryMB,
              size_t& costs);
  
  virtual bool getLinearParams( const int algId,
                      const int opId,
                      const int funId,
                      const size_t noTuples1,
                      const size_t sizeOfTuple1,
                      const size_t noAttributes1,
                      const double selectivity,
                      double& sufficientMemory,
                      double& timeAtSuffMemory,
                      double& timeAt16MB);
  
  virtual bool getLinearParams( const int algId,
                      const int opId,
                      const int funId,
                      const size_t noTuples1,
                      const size_t sizeOfTuple1,
                      const size_t noAttributes1,
                      const size_t noTuples2,
                      const size_t sizeOfTuple2,
                      const size_t noAttributes2,
                      const double selectivity,
                      double& sufficientMemory,
                      double& timeAtSuffMemory,
                      double& timeAt16MB);
  
   virtual bool getFunction(const int algId,
                 const int opId,
                 const int funId,
                 const size_t noTuples,
                 const size_t sizeOfTuple,
                 const size_t noAttributes,
                 const double selectivity,
                 int& funType,
                 double& sufficientMemory,
                 double& timeAtSuffMemory,
                 double& timeAt16MB,
                 double& a, double& b, double&c, double& d);
   
   virtual bool getFunction(const int algId,
                 const int opId,
                 const int funId,
                 const size_t noTuples1,
                 const size_t sizeOfTuple1,
                 const size_t noAttributes1,
                 const size_t noTuples2,
                 const size_t sizeOfTuple2,
                 const size_t noAttributes2,
                 const double selectivity,
                 int& funType,
                 double& sufficientMemory,
                 double& timeAtSuffMemory,
                 double& timeAt16MB,
                 double& a, double& b, double&c, double& d);

   virtual int getPid();
   virtual bool cancelQuery(int pid);
   virtual std::string getHome();

protected:
     virtual void StartCommand();
     virtual bool FinishCommand( SI_Error& errorCode, 
                                 std::string& errorMessage, 
                                 bool autoTransaction = true);
     virtual void constructErrMsg(int& errorCode, std::string& errorMessage);
     virtual SI_Error Command_Query( const ListExpr list,
                          ListExpr& result, std::string& errorMessage,
                          bool autoTransaction );

     virtual SI_Error Command_Create( const ListExpr list,
                           ListExpr& result,
                           ListExpr& error, std::string& errorMessage );

     virtual SI_Error Command_ChangeName( const ListExpr list, 
                                        std::string& errorMessage );

     virtual SI_Error Command_Let( const ListExpr list, 
                                   std::string& errorMessage, 
                                   bool autoTransaction, 
                                   bool replaceExistingValue);

     virtual SI_Error Command_Set( const ListExpr list );
     virtual SI_Error Command_Derive( const ListExpr list, 
                                      std::string& errorMessage );
     virtual SI_Error Command_Update( const ListExpr list, 
                                      std::string& errorMessage );
     virtual SI_Error Command_Conditional( const ListExpr list,
                                ListExpr &resultList,
                                std::string &errorMessage );
     virtual SI_Error Command_Sequence( const ListExpr list,
                             ListExpr &resultList,
                             std::string &errorMessage,
                             bool conjunct );
     virtual SI_Error Command_WhileDoLoop( const ListExpr list,
                                ListExpr &resultList,
                                std::string &errorMessage );
    
};

#endif

