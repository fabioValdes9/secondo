
/*
----
This file is part of SECONDO.

Copyright (C) 2021,
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


Open the same database from multiple clients and try to 
execute operations on this database.

1.1 Includes

*/

#include <string>
#include <iostream>
#include <stdlib.h>
#include <future>

#include "SecondoInterface.h"
#include "SecondoInterfaceCS.h"
#include "NestedList.h"
#include "NList.h"


using namespace std;

void handleSecondoResult(SecErrInfo err,  NestedList* nl, ListExpr res) {
  
  if(err.code!=0){ 
    cout << "Error during command. Error code :" << err.code << endl;
    cout << "Error message = " << err.msg << endl;
  } else {
    cout << "Command successful processed" << endl;
    cout << "Result is:" << endl;
    cout << nl->ToString(res) << endl << endl;
  }

}

void performConnection() {
   // create an interface to the secondo server
   // the paramneter must be true to communicate as client
   SecondoInterface* si = new SecondoInterfaceCS(true,0,false);

   // define the name of the configuration file
   string config = "Config.ini";

   // read in runtime flags from the config file
   si->InitRTFlags(config);
   // set some variables needed to connect with the server

  string user="";
  string passwd="";
  string host="localhost";
  string port ="12340";
  string errMsg;          // return param
  bool   multiUser=true;

  // try to connect
  if(!si->Initialize(user,passwd,host,port,config,"",errMsg,multiUser)){
     // connection failed, handle error
     cerr << "Cannot initialize secondo system" << endl;
     cerr << "Error message = " << errMsg << endl;
     return;
  }
  
  // connected
  cout << "SecondoInterface successfull initialized" << endl;

  NestedList* nl = si->GetNestedList();
  NList::setNLRef(nl);
  SecErrInfo err;

  ListExpr res = nl->TheEmptyList();
  si->Secondo("open database opt", res, err); 
  handleSecondoResult(err, nl, res);

  res = nl->TheEmptyList(); 
  si->Secondo("delete x", res, err); 
  handleSecondoResult(err, nl, res);
      
  res = nl->TheEmptyList();
  si->Secondo("let x = 10", res, err); 
  handleSecondoResult(err, nl, res);
     
  // close connection
  si->Terminate();

  // destroy the interface
  delete si;
}

void performMultipleConnections() {
  for(size_t i = 0; i < 10; i++) {
    performConnection();
  }
}

int main(int argc, char** argv){

  std::vector<future<void>> futures;

  for(size_t i = 0; i < 10; i++) {
    auto future = std::async(&performMultipleConnections);
    futures.push_back((std::move(future)));
  }

  std::cout << "Waiting for futures" << endl;

  for(future<void> &future : futures) {
    future.get();
  }

  std::cout << "Waiting for futures DONE" << endl;

  return 0;
}
