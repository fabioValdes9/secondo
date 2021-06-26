/*
----
This file is part of SECONDO.

Copyright (C) 2007,
Faculty of Mathematics and Computer Science,
Database Systems for New Applications.

SECONDO is free software; you can redistribute it and/or modify
it under the Systems of the GNU General Public License as published by
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


1 QueryExecutor for Distributed-SECONDO


1.1 Includes

*/

#ifndef __QEXECUTOR_WORKER__
#define __QEXECUTOR_WORKER__

#include "state.h"
#include "SecondoInterface.h"
#include "SecondoInterfaceCS.h"

class SecondoWorker {

public:
   SecondoWorker (CassandraAdapter* myCassandra, string mySecondoHost, 
   string mySecondoPort, string myCassandraHost, 
   WorkerQueue *myTokenQueue, size_t myWorkerId, 
   QueryexecutorState *myQueryExecutorState) 
   : cassandra(myCassandra), si(NULL), mynl(NULL), 
   secondoHost(mySecondoHost), secondoPort(mySecondoPort), 
   secondoDatabase(""), cassandraHost(myCassandraHost), 
   queryComplete(false), shutdown(false), query(NULL), 
   tokenQueue(myTokenQueue), workerId(myWorkerId), 
   queryExecutorState(myQueryExecutorState), pid(-1) {
   
      mynl = new NestedList();
      
      initSecondoInterface();
      
      pthread_mutex_init(&processMutex, NULL);
      pthread_cond_init(&processCondition, NULL);  
      
      queryExecutorState -> setState(workerId, "Idle");
   }
      
   virtual ~SecondoWorker() {
      // Shutdown the SECONDO interface
      destroySecondoInterface();
      
      if(query != NULL) {
         delete query;
         query = NULL;
      }
      
      if(mynl != NULL) {
         delete mynl;
         mynl = NULL;
      }
      
      pthread_mutex_destroy(&processMutex);
      pthread_cond_destroy(&processCondition);
   }
   
   void destroySecondoInterface() {
      if(si) {
         si->Terminate();
         delete si;
         si = NULL;
      }
   }
   
   /*
   2.0 Init the secondo c++ api

   */
   void initSecondoInterface() {

      // Destory old instance if exits
      destroySecondoInterface();

      // create an interface to the secondo server
      // the paramneter must be true to communicate as client
      si = new SecondoInterfaceCS(true, mynl, false);

      // define the name of the configuration file
      string config = "Config.ini";

      // read in runtime flags from the config file
      si->InitRTFlags(config);

     // SECONDO Connection data
     string user = "";
     string passwd = "";
  
     bool multiUser = true;
     string errMsg;
  
     // try to connect
     if(!si->Initialize(user, passwd, secondoHost, secondoPort, 
                       config, "", errMsg, multiUser)) {

        // connection failed, handle error
        cerr << "Cannot initialize secondo system" << endl;
        cerr << "Error message = " << errMsg << endl;
        shutdown = true;
     } else {
        pid = si -> getPid();
        shutdown = false;
     }
   }
   
   WorkerQueue* getTokenQueue() {
      return tokenQueue;
   }
   
   void submitQuery(string &myQuery, size_t myQueryId) {
      
      if(shutdown) {
         LOG_ERROR("SECONDO worker is down [ " << secondoPort << " ]: " 
              << "ignoring query");
         return;
      }
      
      pthread_mutex_lock(&processMutex);
      
      queryComplete = false;
      queryCanceled = false;

      query = new string(myQuery);
      queryId = myQueryId;
      pthread_cond_broadcast(&processCondition);
      
      pthread_mutex_unlock(&processMutex);
   }
   
   bool isQueryComplete() {
      return queryComplete;
   }
   
   void stop() {
      shutdown = true;
      pthread_cond_broadcast(&processCondition);
      pthread_join(workerThread, NULL);
   }
   
   void waitForQueryCompletion() {
      pthread_mutex_lock(&processMutex);
      
      while(! isQueryComplete()) {
           pthread_cond_wait(&processCondition, &processMutex);
      } 
      
      pthread_mutex_unlock(&processMutex);
   }

   
   /*
   2.1 Update global query status

   */
   bool updateLastProcessedToken(TokenRange tokenrange, string &queryuuid) {
  
     // Build CQL query
     stringstream ss;
     ss << "INSERT INTO system_progress ";
     ss << "(queryid, ip, begintoken, endtoken, queryuuid) ";
     ss << "values(";
     ss << "" << queryId << ",",
     ss << "'" << cassandraHost << "',";
     ss << "'" << tokenrange.getStart() << "',",
     ss << "'" << tokenrange.getEnd() << "',",
     ss << "'" << queryuuid << "'",
     ss << ");";
  
     // Update last executed command
     bool result = cassandra -> executeCQLSync(
       ss.str(),
       CASS_CONSISTENCY_ONE 
     );
 
     if(! result) {
        cout << "Unable to update last executed query in ";
        cout << "system_progress table" << endl;
        cout << "CQL Statement: " << ss.str() << endl;
        return false;
     }

     return true;
   }
   

   void mainLoop() {
      
      if(si == NULL) {
         LOG_ERROR("---> [ " << secondoPort 
            << " ]: Unable to connect to SECONDO, unable to start MainLoop");
         return;
      }
      
      NestedList* nl = si->GetNestedList();
      NList::setNLRef(nl);
      
      while (! shutdown) {
         pthread_mutex_lock(&processMutex);
         while(query == NULL) {
            pthread_cond_wait(&processCondition, &processMutex);
            
            if(shutdown) {
               return;
            }
         }
         
         if(query != NULL) {
            
            if(QEUtils::containsPlaceholder(*query, "__TOKENRANGE__")) {
               
               while(true) {
                  TokenRange tokenRange = tokenQueue->pop();
                  
                  // special terminal token
                  if(tokenRange.getStart() == 0 && tokenRange.getEnd() == 0) {
                     break;
                  }
                  
                  executeTokenQuery(*query, tokenRange);
               }
               
            } else {
               executeSecondoCommand(*query);
            }
            
            delete query;
            query = NULL;
         }
         
         queryComplete = true;
         LOG_DEBUG("Worker [ " << secondoPort << " ]: Query done");
         pthread_cond_broadcast(&processCondition);
         pthread_mutex_unlock(&processMutex);
      }
   }
   
   
   void setWorkerThread(pthread_t &thread) {
      workerThread = thread;
   }
   
   bool cancelRunningQuery() {
      
     cout << "[Info] Canceling worker: " << secondoPort << endl;
            
     NestedList* nestedList = new NestedList();
     SecondoInterface* controlSecondo 
         = new SecondoInterfaceCS(true, mynl, false);

     string config = "Config.ini";
     controlSecondo->InitRTFlags(config);
     string user = "";
     string passwd = "";
     bool result = false;
  
     bool multiUser = true;
     string errMsg;
  
     // try to connect
     if(controlSecondo->Initialize(user, passwd, secondoHost, secondoPort, 
                       config, "", errMsg, multiUser)) {
         
         // Set queryCanceled = true. Otherwise, the worker will 
         // insert a entry into the system table, that the current
         // unit of work is processed successfully even it is canceled
         queryCanceled = true;
         result = controlSecondo->cancelQuery(pid);
         controlSecondo->Terminate();
         delete controlSecondo;
         controlSecondo = NULL;
     }
     
     if(nestedList != NULL) {
        delete nestedList;
        nestedList = NULL;
     }
     
     return result;
   }
   
private:
   
   void reconnectSecondo() {
      destroySecondoInterface();
      initSecondoInterface();
      executeSecondoCommand(secondoDatabase, false);
   }
   
   /*
   2.2 Execute a command in SECONDO

   */
   bool executeSecondoCommand(string &command, bool autoReconnect = true) {
  
     //  LOG_DEBUG("Worker [ " << secondoPort << " ] executing: " << command);

          ListExpr res = mynl->TheEmptyList(); 
          SecErrInfo err;
          
          // Store database name for reconnect
          if(command.find("open database") != string::npos) {
             secondoDatabase = command;
          }
        
           si->Secondo(command, res, err); 

           // check whether command was successful
           if(err.code != 0) { 
             LOG_ERROR("Error during command. Error code [ " << secondoPort 
                  << " ]: " << err.code << " / " << err.msg);
             return false;
           } else if(err.code == ERR_SYSTEM_DIED) {
              if(autoReconnect) {
                 LOG_ERROR("SECONDO server has died, reconnecting....");
                 reconnectSecondo();
              }
              return false;
           } else {
              LOG_DEBUG("Worker [ " << secondoPort << " ]: computed result " 
                  << mynl->ToString(res));
              
              // Error result
              if("(int -1)" == mynl->ToString(res)) {
                 return false;
              }
              
              return true;
          }
   }
   
   /*
   2.2 Execute a token query for a given tokenrange

   */
   void executeTokenQuery(string query, 
                         TokenRange &tokenrange) {
  
       stringstream ss;
       ss << "'" << tokenrange.getStart() << "'";
       ss << ", ";
       ss << "'" << tokenrange.getEnd() << "'";
       
       stringstream statestream;
       statestream << "[" << tokenrange.getStart() << "";
       statestream << ", ";
       statestream << "" << tokenrange.getEnd() << "]";
       statestream << " [data node: " << tokenrange.getIp() << "]";
        
       queryExecutorState -> setState(workerId, statestream.str());
  
       // Copy query string, so we can replace the
       // placeholder multiple times
       string ourQuery = string(query);
    
       // Replace token range placeholder
       QEUtils::replacePlaceholder(ourQuery, "__TOKENRANGE__", ss.str());
    
       // Replace Query UUID placeholder
       string myQueryUuid;
       QEUtils::createUUID(myQueryUuid);
       QEUtils::replacePlaceholder(ourQuery, "__QUERYUUID__", myQueryUuid);
    
       // Reexecute failing queries
       for(size_t tryCount = 0; tryCount < 10; tryCount++) {
           bool result = executeSecondoCommand(ourQuery);
           
           if(result == true) {
              break;
           }
           
           LOG_WARN("Reexecuting query, because of an error");
       } 
       
       if(! queryCanceled) {
          updateLastProcessedToken(tokenrange, myQueryUuid);
       }
       
       queryExecutorState -> setState(workerId, "Idle");
   }
   
   CassandraAdapter* cassandra;
   SecondoInterface* si;
   NestedList* mynl;
   string secondoHost;
   string secondoPort;
   string secondoDatabase;
   string cassandraHost;
   volatile bool queryComplete;
   volatile bool queryCanceled;
   volatile bool shutdown;
   string* query;
   size_t queryId;
   WorkerQueue *tokenQueue;
   
   // Thread id
   size_t workerId;
   
   // QueryExecutor state
   QueryexecutorState *queryExecutorState;
   
   // Pid of SECONDO instance
   int pid;
   
   // Thread handling
   pthread_t workerThread;
   pthread_mutex_t processMutex;
   pthread_cond_t processCondition;
};

/*
2.4 start the secondo worker thread

*/
void* startSecondoWorkerThreadInternal(void *ptr) {
  SecondoWorker* sw = (SecondoWorker*) ptr;
  sw -> mainLoop();
  
  return NULL;
}

void startSecondoWorkerThread(SecondoWorker *worker) {
  
   pthread_t targetThread;
   pthread_create(&targetThread, NULL, 
                  &startSecondoWorkerThreadInternal, worker);
   

   worker->setWorkerThread(targetThread);
}

#endif
