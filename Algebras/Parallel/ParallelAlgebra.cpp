/*
----
This file is part of SECONDO.

Copyright (C) 2019, 
University in Hagen, 
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


#include "NestedList.h"
#include "QueryProcessor.h"
#include "AlgebraManager.h"
#include "Algebra.h"
#include "StandardTypes.h"
#include "Algebras/Relation-C++/RelationAlgebra.h"
#include "SecondoSystem.h"
#include "Symbols.h"
#include "ListUtils.h"
#include "Algebras/Stream/Stream.h"
#include <boost/thread.hpp>
#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>
#include "semaphore.h"
#include <bounded_buffer.hpp>
#include <vector>
#include <queue>

extern NestedList* nl;
extern QueryProcessor* qp;
extern AlgebraManager* am;


namespace parallelalg {

ListExpr multicountTM(ListExpr args){
   if(nl->IsEmpty(args)){
     return listutils::typeError("at least one argument expected");
   }
   ListExpr appendList = nl->TheEmptyList();
   ListExpr appendLast = nl->TheEmptyList();

   while(!nl->IsEmpty(args)){
      bool isAttr=false;
      ListExpr arg = nl->First(args);
      args = nl->Rest(args);
      
      if(Stream<Attribute>::checkType(arg)){
         isAttr = true;
      } else if(Stream<Tuple>::checkType(arg)){
         isAttr = false;
      } else {
         return listutils::typeError("each argument must be an stream "
                                     "of tuple or a stream of DATA");
      }
      if(nl->IsEmpty(appendList)){
        appendList = nl->OneElemList(nl->BoolAtom(isAttr));
        appendLast = appendList;
      } else {
        appendLast = nl->Append(appendLast, nl->BoolAtom(isAttr));
      }
   }
   return nl->ThreeElemList( nl->SymbolAtom(Symbols::APPEND()),
                             appendList,
                             listutils::basicSymbol<CcInt>());
}


 class Aggregator{
    public:
       virtual void inc() = 0;
       virtual ~Aggregator(){};
 }; 

 class streamCounter{
   public:
     streamCounter(Word& _stream, bool _isAttr,
                   Aggregator* _aggr):
     stream(_stream), isAttr(_isAttr), aggr(_aggr){
     }
     ~streamCounter(){
        qp->Close(stream.addr);
     }

     void run(){
        qp->Open(stream.addr);         
        bool finished = false;
        while(!finished){
            qp->Request(stream.addr,result);
            if(!qp->Received(stream.addr)){
               finished = true;
            } else {
               if(isAttr){
                  ((Attribute*)result.addr)->DeleteIfAllowed();
               }else {
                  ((Tuple*) result.addr)->DeleteIfAllowed();
               }
               aggr->inc(); 
            }
        }
     }

   private:
      Word stream;
      bool isAttr;
      Aggregator* aggr;
      Word result;
 };

  
class multicountLocal: public Aggregator{
   public:
     multicountLocal( Word* _args, int _numArgs):
        numArgs(_numArgs), args(_args){
        // initialize vectors
        for(int i=numArgs/2;i<numArgs; i++) {
           bool isAttr = ((CcBool*)args[i].addr)->GetValue();
           isAttrVec.push_back(isAttr);
        }
        sum = 0;
     }

     int compute(){
        // create stream counter
        for(int i=0;i<numArgs/2;i++){
          streamCounters.push_back(new streamCounter(args[i],
                                       isAttrVec[i], this));
        }
        for(int i=0;i<numArgs/2;i++){
          runners.push_back(new boost::thread(&streamCounter::run,
                                              streamCounters[i]));
        }
        for(int i=0;i<numArgs/2;i++){
            runners[i]->join();
            delete runners[i];
            delete streamCounters[i];
        }
        return sum;
     }
      
     void inc(){
        boost::lock_guard<boost::mutex> guard(mtx);
        sum++;
     }  

   private:
      int numArgs;
      Word* args;
      std::vector<bool> isAttrVec;
      std::vector<streamCounter*> streamCounters;
      std::vector<boost::thread*> runners;
      boost::mutex mtx;
      int sum;

};

int multicountVM(Word* args, Word& result,
              int message, Word& local, Supplier s){

   result = qp->ResultStorage(s);
   CcInt* res = (CcInt*)result.addr;
   multicountLocal* li = new multicountLocal(args, qp->GetNoSons(s));
   res->Set(true,  li->compute());
   delete li;
   return 0;
}

OperatorSpec multicountSpec(
   "s1 x s2 x ... , sX in {stream(tuple), stream(DATA)} ",
   "multicount (_,_,_)",
   "Summarizes the number of elements in the streams.",
   "query multicount( ten feed, intstream(1,0) feed, plz feed);"
);


Operator multicountOp(
  "multicount",
  multicountSpec.getStr(),
  multicountVM,
  Operator::SimpleSelect,
  multicountTM
);


/*
1.39 pbuffer

*/
ListExpr pbufferTM(ListExpr args){
  if(!nl->HasLength(args,2)){
    return listutils::typeError("two elements expected");
  }
  ListExpr stream = nl->First(args);
  if(!Stream<Attribute>::checkType(stream) 
      && !Stream<Tuple>::checkType(stream)){
    return listutils::typeError("first arg must be a stream of tuple or DATA");
  }
  if(!CcInt::checkType(nl->Second(args))){
    return listutils::typeError("second arg must be an int");
  }
  return nl->First(args);
}


template<class ST>
class pbufferInfo{
   public: 
      pbufferInfo(Word _stream, int _maxElems):
         stream(_stream),
         buffer(_maxElems){
         stream.open();
         collect();
      }

      ~pbufferInfo(){
         running = false;
         ST* f;
         while(!buffer.empty()){
           buffer.pop_back(&f);
           if(f){
              f->DeleteIfAllowed();
           }
         }
         runner->join();
         delete runner;
         // remove freshly inserted elements 
         while(!buffer.empty()){
           buffer.pop_back(&f);
           if(f){
              f->DeleteIfAllowed();
           }
         }
         stream.close();
       }
      
      ST* next(){
         ST* result;
         buffer.pop_back(&result);
         return result; 
      }

   private:
       Stream<ST> stream;
       bounded_buffer<ST*>  buffer;
       bool running;
       boost::thread* runner;
       
 
       void collect(){
         running = true;
         runner = new boost::thread(&pbufferInfo::run,this);
       }

       // creator thread
       void run(){
          while(running){
            ST* elem = stream.request();
            buffer.push_front(elem);
            if(!elem) {
                running = false;
            }
          }
       } 
};



template<class ST>
int pbufferVMT(Word* args, Word& result,
           int message, Word& local, Supplier s){

   pbufferInfo<ST>* li = (pbufferInfo<ST>*) local.addr;
   switch(message){
     case OPEN: {
                  if(li){
                    delete li;
                    local.addr = 0;
                  }
                  CcInt* bufferSize = (CcInt*) args[1].addr;
                  if(!bufferSize->IsDefined()){
                     return 0;
                  }
                  int bs = bufferSize->GetValue();
                  if(bs < 1){
                    return 0;
                  }
                  local.addr = new pbufferInfo<ST>(args[0],bs);
                  return 0;
                }
     case REQUEST :
                {
                   result.addr = li?li->next():0;
                   return result.addr?YIELD:CANCEL;
                }

     case CLOSE: {
                  if(li){
                    delete li;
                    local.addr = 0;
                  }
                  return 0;
                }
             

   }
   return -1;

}

ValueMapping pbufferVM[] = {
   pbufferVMT<Tuple>,
   pbufferVMT<Attribute>
};

int pbufferSelect(ListExpr args){
  return Stream<Attribute>::checkType(nl->First(args))?1:0;
};


OperatorSpec pbufferSpec(
  " stream(X) x int -> stream(X) , X in {tuple,DATA} ",
  " _ pbuffer[_] ",
  "Fills a buffer within a thread with elements from "
  " stream and puts the elements to the output by request."
  "The second argument is the size of the buffer.",
  " query plz feed pbuffer[100] count"
);

Operator pbufferOp(
  "pbuffer",
  pbufferSpec.getStr(),
  2,
  pbufferVM,
  pbufferSelect,
  pbufferTM
);



ListExpr pbuffer1TM(ListExpr args){
  if(!nl->HasLength(args,1)){
    return listutils::typeError("one argument expected");
  }
  ListExpr stream = nl->First(args);
  if(!Stream<Tuple>::checkType(stream) 
     && !Stream<Attribute>::checkType(stream)){
    return listutils::typeError("stream expected");
  }
  return stream;
}


template<class ST>
class pbuffer1Info{
  public:
     pbuffer1Info(Word _stream): stream(_stream){
        stream.open();
        readMtx = new boost::mutex();
        readMtx->lock();
        first = true;
        collect();
     }

     ~pbuffer1Info(){
         runner->join();
         delete runner;
         // remove remaining elements in queue
         if(buffer){
            buffer->DeleteIfAllowed();
         } 
         stream.close();
         delete readMtx;
       }

     ST* next(){
       ST* result;
       if(first){
         readMtx->lock();
         result = buffer;
         buffer = 0;
         first = false;
         readMtx->unlock();
       } else {
          result = stream.request();
       }
       return result; 
     }

  private:
       Stream<ST> stream;
       boost::mutex* readMtx;
       ST* buffer;
       boost::thread* runner;
       bool first;
 
       void collect(){
         runner = new boost::thread(&pbuffer1Info::run,this);
       }

       // creator thread
       void run(){
          ST* elem = stream.request();
          buffer=elem;
          readMtx->unlock();
       } 
};

template<class ST>
int pbuffer1VMT(Word* args, Word& result,
           int message, Word& local, Supplier s){

   pbuffer1Info<ST>* li = (pbuffer1Info<ST>*) local.addr;
   switch(message){
     case OPEN: {
                  if(li){
                    delete li;
                  }
                  local.addr = new pbuffer1Info<ST>(args[0]);
                  return 0;
                }
     case REQUEST :
                {
                   result.addr = li?li->next():0;
                   return result.addr?YIELD:CANCEL;
                }

     case CLOSE: {
                  if(li){
                    delete li;
                    local.addr = 0;
                  }
                  return 0;
                }
             

   }
   return -1;

}

ValueMapping pbuffer1VM[] = {
   pbuffer1VMT<Tuple>,
   pbuffer1VMT<Attribute>
};

int pbuffer1Select(ListExpr args){
  return Stream<Attribute>::checkType(nl->First(args))?1:0;
};


OperatorSpec pbuffer1Spec(
  " stream(X) -> stream(X) , X in {tuple,DATA} ",
  " _ pbuffer1 ",
  "Requests  elements of a stream within a  "
  "separate thread."
  "The only one element is buffered",
  " query plz feed pbuffer1  count"
);

Operator pbuffer1Op(
  "pbuffer1",
  pbuffer1Spec.getStr(),
  2,
  pbuffer1VM,
  pbuffer1Select,
  pbuffer1TM
);


/*
Operator pbufferU

*/
ListExpr pbufferUTM(ListExpr args){
  if(!nl->HasLength(args,1)){
    return listutils::typeError("one argument expected");
  }
  ListExpr s = nl->First(args);
  if(    !Stream<Attribute>::checkType(s) 
     &&  !Stream<Tuple>::checkType(s)){
   return listutils::typeError("argument must be a stream of tuple or DATA");
  }
  return s;
}

template<class ST>
class pbufferUInfo{
  public:
    pbufferUInfo(Word arg): stream(arg){
      stream.open();
      start();
    }

    ~pbufferUInfo(){
       running  = false;
       t->join();
       delete t;
       while(!buffer.empty()){
          ST* top = buffer.front();
          buffer.pop();
          if(top){
             top->DeleteIfAllowed();
          }
       }
       stream.close();
    }

    ST* next(){
        boost::unique_lock<boost::mutex> lock(mtx);
        while(buffer.empty()){
          cond.wait(lock);
        }
        ST* res = buffer.front();
        buffer.pop();
        return res;
    }

  private:
    Stream<ST> stream;
    boost::mutex mtx;
    std::queue<ST*> buffer;
    boost::thread* t;
    boost::condition_variable cond;
    bool running;


    void start(){
       running = true;
       t = new boost::thread(&pbufferUInfo::run,this);
    }

    void run(){
      while(running){
         ST* elem = stream.request();
         mtx.lock();
         buffer.push(elem);
         if(elem==nullptr){
            running = false;
         }
         mtx.unlock();
         cond.notify_one();
      } 
    }
};

template<class ST>
int pbufferUVMT(Word* args, Word& result,
           int message, Word& local, Supplier s){

   pbufferUInfo<ST>* li = (pbufferUInfo<ST>*) local.addr;
   switch(message){
     case OPEN: {
                  if(li){
                    delete li;
                  }
                  local.addr = new pbufferUInfo<ST>(args[0]);
                  return 0;
                }
     case REQUEST :
                {
                   result.addr = li?li->next():0;
                   return result.addr?YIELD:CANCEL;
                }

     case CLOSE: {
                  if(li){
                    delete li;
                    local.addr = 0;
                  }
                  return 0;
                }
             

   }
   return -1;

}

ValueMapping pbufferUVM[] = {
   pbufferUVMT<Tuple>,
   pbufferUVMT<Attribute>
};

int pbufferUSelect(ListExpr args){
  return Stream<Attribute>::checkType(nl->First(args))?1:0;
};


OperatorSpec pbufferUSpec(
  " stream(X) -> stream(X) , X in {tuple,DATA} ",
  " _ pbufferU ",
  "Requests  elements of a stream within a  "
  "separate thread. ", 
  " query plz feed pbufferU  count"
);

Operator pbufferUOp(
  "pbufferU",
  pbufferUSpec.getStr(),
  2,
  pbufferUVM,
  pbufferUSelect,
  pbufferUTM
);


/*
Operator ~pfilterS~

This operator filters a tuple stream with a number of 
threads. This operator is not order preservative.

*/

ListExpr renameFunArgs(ListExpr fun, const std::string post){
   if(nl->HasMinLength(fun,2) && nl->IsEqual(nl->First(fun),"fun")){
      // list is a function
      // step 1. collect the argunnet names
      std::map<std::string, ListExpr> rep;
      ListExpr rest = nl->Rest(fun);
      while(!nl->HasLength(rest,1)){ // last elem is the definition
        ListExpr first = nl->First(rest);
        rest = nl->Rest(rest);
        assert(nl->HasLength(first,2));  // (name type)
        ListExpr n = nl->First(first);
        assert(nl->AtomType(n)==SymbolType);
        std::string name = nl->SymbolValue(n);
        rep[name] = nl->SymbolAtom(name + post);
      }
      fun = listutils::replaceSymbols(fun,rep);
   } 
   if(nl->AtomType(fun) != NoAtom){
      return fun;
   }
   if(nl->IsEmpty(fun)){
      return fun;
   }
   // for no atoms, call renameFunArgs recursively
   ListExpr res = nl->OneElemList( renameFunArgs(nl->First(fun), post));
   ListExpr last = res;
   ListExpr rest = nl->Rest(fun);
   while(!nl->IsEmpty(rest)){
     last = nl->Append(last, renameFunArgs(nl->First(rest), post));
     rest = nl->Rest(rest);
   } 
   return res;
}


ListExpr pfilterSTM(ListExpr args){
  if(!nl->HasLength(args,3)){
    return listutils::typeError("three arguments required");
  }
  ListExpr tmp = args;
  while(!nl->IsEmpty(tmp)){
    if(!nl->HasLength(nl->First(tmp), 2)){
      return listutils::typeError("internal error");
    }
    tmp = nl->Rest(tmp);
  }
  ListExpr stream = nl->First(nl->First(args));
  if(!Stream<Tuple>::checkType(stream)){
    return listutils::typeError("first element must be a tuple stream");
  }
  if(!CcInt::checkType(nl->First(nl->Second(args)))){
    return listutils::typeError("second argument is not of type int");
  }
  ListExpr intL = nl->Second(nl->Second(args));
  if(nl->AtomType(intL) != IntType){
    return listutils::typeError("only constants allowed for number of threads");
  }
  int nt = nl->IntValue(intL);
  if(nt < 1 || nt > 15){
    return listutils::typeError("the number of threads must be "
                                "between 1 and 15");
  }
  ListExpr fun = nl->Third(args);
  ListExpr funt = nl->First(fun);
  if(!listutils::isMap<1>(funt)){
    return listutils::typeError("third artgument is not a unary function");
  }
  ListExpr funarg = nl->Second(funt);
  if(!nl->Equal(funarg,nl->Second(stream))){
    return listutils::typeError("function argument differs from stream type");
  }


  ListExpr funres   = nl->Third(funt);
  if(!CcBool::checkType(funres)){
    return listutils::typeError("function result is not of type bool");
  }
 
  ListExpr fundef = nl->Second(fun);

 ListExpr fa = nl->Second(fundef);
 fundef = nl->ThreeElemList(
                 nl->First(fundef),
                 nl->TwoElemList(
                     nl->First(fa),       
                     funarg),
                 nl->Third(fundef));
  nt--;  // one function is already there
  if(nt==0){
     return stream;
  }

  ListExpr funDefX = renameFunArgs(fundef,"_1");
  ListExpr alist = nl->OneElemList(funDefX);
  ListExpr last = alist;
  nt--;

  for(int i=0;i<nt;i++){
    std::string ns = "_"+stringutils::int2str((i+2));
    funDefX = renameFunArgs(fundef,ns);
    last = nl->Append(last, funDefX);
  }

  return nl->ThreeElemList(
            nl->SymbolAtom(Symbols::APPEND()),
            alist,
            stream
         );
}

template<class T>
class SyncStream{
 public:
  SyncStream(Word& w) : stream(w), endReached(false){}


  ~SyncStream(){
    mtx.lock();
    mtx.unlock();
  } 

  void open(){
    mtx.lock();
    endReached = false;
    stream.open();
    mtx.unlock();
  }

  void close(){
    mtx.lock();
    endReached = true;
    stream.close();
    mtx.unlock();
  }

  T* request(){
    mtx.lock();
    T* res;
    if(endReached){
       res = 0;
    } else {
       res =  stream.request();
       if(res==0){
         endReached = true;
       }
    }
    mtx.unlock();
    return res;
  }

 private:
   Stream<T> stream;
   bool endReached;
   boost::mutex mtx;
}; 

template<class T>
class Consumer{
 
   public:
    
     virtual ~Consumer() {}
 
     virtual void elemAvailable(T* elem) = 0;

     //virtual void cancel() = 0;

};


template<class T>
class pfilterSThread{
  public:
    pfilterSThread(SyncStream<T>* _stream, 
                  Word _fun,
                  Consumer<T>* _consumer): stream(_stream),
                  fun(_fun), consumer(_consumer){

      funArgs = qp->Argument(fun.addr);
      running = true;
      sendNull = false;
      runner = new boost::thread(&pfilterSThread<T>::run, this);
    }

    ~pfilterSThread(){
      runner->join();
      delete runner;  
    }

    void cancel(){
       running = false;
    }
  
 private:
    SyncStream<T>* stream;
    Word fun;
    Consumer<T>* consumer;
    ArgVectorPointer funArgs;
    bool running;
    boost::thread* runner;
    Word res;
    bool sendNull;
    
     void run(){
       while(running){
          T* elem = stream->request();
          if(elem==nullptr){
            running = false;
            consumer->elemAvailable(elem);
            sendNull = true;
          } else {
             if(!check(elem)){
                elem->DeleteIfAllowed();
             } else {
                consumer->elemAvailable(elem);
             }
          }
       }
       if(!sendNull){
         consumer->elemAvailable(0);
         sendNull = true;
       }
    }

    bool check(T* elem){
      (*funArgs)[0] = elem;
      qp->Request(fun.addr,res);
      CcBool* b = (CcBool*) res.addr;
      return b->IsDefined() && b->GetValue();
    }

};

template<class T>
class pfilterSInfo : public Consumer<T>{

  public:

     pfilterSInfo(Word& _stream, std::vector<Word>& _funs): 
        stream(_stream), funs(_funs), buffer(funs.size()*2) {
        stream.open();
        runs = funs.size();
        for(int i=0; i< funs.size();i++){
          runners.push_back(new pfilterSThread<T>(&stream, funs[i], this));
        }
     }

     ~pfilterSInfo(){
       for(int i=0;i<runners.size();i++){
          runners[i]->cancel();
       }
       // ensure that each runner can insert a pending element
       T* elem;
       while( !buffer.empty() ){
          buffer.pop_back(&elem);
          if(elem){
             elem->DeleteIfAllowed();
          }
       } 
       for(int i=0;i<runners.size();i++){
          delete runners[i];
       }
       while( !buffer.empty() ){
          buffer.pop_back(&elem);
          if(elem){
             elem->DeleteIfAllowed();
          }
       } 
       stream.close();
     }

     T* next(){
        T* elem;
        buffer.pop_back(&elem);
        return elem;    
     }

     void elemAvailable(T* elem){
       mtx.lock();
       if(elem){
         buffer.push_front(elem); 
       } else {
          runs--;
          if(runs==0){
            buffer.push_front(elem);
          }
       }
       mtx.unlock();
     }

     void cancel() {
       for(int i=0;i<runners.size();i++){
          runners[i]->cancel();
       }
     } 

  private:
     SyncStream<T> stream;
     std::vector<Word> funs;
     bounded_buffer<T*> buffer;
     std::vector<pfilterSThread<T>*> runners;
     boost::mutex mtx;
     size_t runs; 
     
};


template<class T>
int pfilterSVMT(Word* args, Word& result,
           int message, Word& local, Supplier s){

 pfilterSInfo<T>* li = (pfilterSInfo<T>*) local.addr;
 switch (message){
    case OPEN:{
            if(li){
              delete li;
            }
            std::vector<Word> funs;
            for(int i=2; i< qp->GetNoSons(s); i++){
                funs.push_back(args[i]);
            }
            local.addr = new pfilterSInfo<T>(args[0], funs);
            return 0;
          }
    case REQUEST:{
            result.addr = li?li->next(): 0;
            return result.addr?YIELD:CANCEL;
          }
    case CLOSE: {
           if(li){
             delete li;
             local.addr = 0;
           }
           return 0;
         }
 }
 return -1;
};

OperatorSpec pfilterSSpec(
  " stream(Tuple) x int x (tuple->bool) -> stream(tuple)",
  " _ pfilterS[_,_] ",
  " Filters tuples from a stream that a not fulfilling a "
  " given condition, Filtering is done used several threads "
  "where the number of threads is given in the second argument."
  "Note that the ordering of the input stream is not prevented",
  " query plz pfilterS[10, .PLZ < 7000] count"
);

Operator pfilterSOp(
  "pfilterS",
  pfilterSSpec.getStr(),
  pfilterSVMT<Tuple>,
  Operator::SimpleSelect,
  pfilterSTM
);

/*
Operator pextend

*/
ListExpr pextendTM(ListExpr args){

  // strem(tuple) x funlist
  if(!nl->HasLength(args,3)){
    return listutils::typeError("stream(tuple) x funlist expected");
  }
  // because we have to replicate the functions, we uses the args in 
  // type mapping
  if(!listutils::checkUsesArgsInTypeMapping(args)){
    return listutils::typeError("internal error");
  }
  ListExpr stream = nl->First(nl->First(args));
  if(!Stream<Tuple>::checkType(stream)){
    return listutils::typeError("first argument is not a tuple stream");
  }
  if(!CcInt::checkType(nl->First(nl->Second(args)))){
     return listutils::typeError("second argument is not an int");
  }
  if(nl->AtomType(nl->Second(nl->Second(args)))!=IntType){
     return listutils::typeError("second argument is not a constant int");
  }
  int nt = nl->IntValue(nl->Second(nl->Second(args)));
  if(nt <1 || nt > 15){
    return listutils::typeError("the number of thread must be "
                                "between 1 and 15");
  }

  // the last argument consists of a list of function types and the list of 
  // function definitions
  ListExpr funtypes = nl->First(nl->Third(args));
  ListExpr fundefs = nl->Second(nl->Third(args));
  if(nl->AtomType(funtypes)!=NoAtom){
    return listutils::typeError("the third argument is not a function list");
  }
  if(nl->IsEmpty(funtypes)){
    return listutils::typeError("an empty function list is not allowed");
  }
  if(nl->ListLength(funtypes)!=nl->ListLength(fundefs)){
   return listutils::typeError("The number of function definitions differs "
                                "to the number of function types");
  }

  ListExpr tuple = nl->Second(stream);

  // check the functions and build the result attrlist
  std::vector<ListExpr> fundefvec;

  std::set<std::string> names;
  ListExpr attrList = nl->Second(tuple);
  while(!nl->IsEmpty(attrList)){
    names.insert(nl->SymbolValue(nl->First(nl->First(attrList))));
    attrList = nl->Rest(attrList);
  }
 

  ListExpr attrAppend = nl->TheEmptyList();
  ListExpr attrLast = nl->TheEmptyList();
  bool first = true;

  while(!nl->IsEmpty(funtypes)){
    ListExpr funtype = nl->First(funtypes);
    ListExpr fundef  = nl->First(fundefs);
    funtypes = nl->Rest(funtypes);
    fundefs = nl->Rest(fundefs);
    // check whether type is a named function
    if(!nl->HasLength(funtype, 2) || !nl->HasLength(fundef,2)){
      return listutils::typeError("fund invalid element in function list");
    }    
    if(nl->AtomType(nl->First(funtype)) != SymbolType){
       return listutils::typeError("invalid name for function");
    }
    std::string error;
    if(!listutils::isValidAttributeName(nl->First(funtype),error)){
      return listutils::typeError(error);
    }
    std::string n = nl->SymbolValue(nl->First(funtype));
    if(names.find(n) != names.end()){
      return listutils::typeError("Attribute name " + n 
                  +" is part of the original tuple or used more than once");
    }
    names.insert(n);
    funtype = nl->Second(funtype);
    if(!listutils::isMap<1>(funtype)){
      return listutils::typeError("invalid function definition");
    }
    if(!nl->Equal(tuple, nl->Second(funtype))){
       return listutils::typeError("function argument for attribute " + n 
                                   + " difers to the stream tuple type");
    }
    ListExpr funres = nl->Third(funtype);
    if(!Attribute::checkType(funres)){
      return listutils::typeError("function result for " + n 
                                   + " is not in kind DATA");
    }
    if(first){
      attrAppend = nl->OneElemList(nl->TwoElemList(nl->SymbolAtom(n), funres));
      attrLast = attrAppend;
      first = false;
    }  else {
      attrLast = nl->Append(attrLast, 
                            nl->TwoElemList(nl->SymbolAtom(n), funres));
    }
    fundef = nl->Second(fundef);
    // replace type (maybe some type operator) by the real type
    fundef = nl->ThreeElemList(
                     nl->First(fundef),
                     nl->TwoElemList(
                         nl->First(nl->Second(fundef)),
                         nl->Second(funtype)),
                     nl->Third(fundef));
     fundefvec.push_back(fundef);
  }

  ListExpr resAttrList = listutils::concat(nl->Second(tuple), attrAppend);
  ListExpr resType = Stream<Tuple>::wrap(Tuple::wrap(resAttrList));

  nt--;
  if(nt==0){  // only one thread, nothing to append
    return resType;
  }

  ListExpr appendList = nl->TheEmptyList();
  ListExpr appendLast = nl->TheEmptyList();



  for(int i=0;i<nt;i++){
    std::string ns = "_"+stringutils::int2str((i+2));
    for(size_t f = 0; f<fundefvec.size();f++){
       ListExpr fundef = fundefvec[f];
       ListExpr funDefX = renameFunArgs(fundef,ns);
       if(i==0 && f==0){
         appendList = nl->OneElemList(funDefX);
         appendLast = appendList;
       } else {
          appendLast = nl->Append(appendLast, funDefX);
       }
    }        
  }
  return nl->ThreeElemList( nl->SymbolAtom(Symbols::APPEND()),
                            appendList,
                            resType);

}


class pextendthread{

  public:

      pextendthread(SyncStream<Tuple>* s, std::vector<Supplier>& _functions, 
                    TupleType* _tt, Consumer<Tuple>* consumer):
                    stream(s), functions(_functions),tt(_tt), 
                    consumer(consumer){
         running = true;
         for(size_t i=0;i<functions.size(); i++){
            funargs.push_back(qp->Argument(functions[i]));
         }

         t = new boost::thread(&pextendthread::run, this);
      }

      void cancel(){
          running = false;
      }

      ~pextendthread(){
         t->join();
         delete t;
      }

  private:
      SyncStream<Tuple>* stream;
      std::vector<Supplier> functions;
      TupleType* tt;
      Consumer<Tuple>* consumer;
      std::vector<ArgVectorPointer> funargs;
      bool running;
      boost::thread* t;
      Word resWord;

            
      void run(){
         while(running){
           Tuple* t = stream->request();
           if(t==0){
              running=false;
              consumer->elemAvailable(0);
           } else {
             Tuple* resTuple = extendTuple(t);
             t->DeleteIfAllowed();
             consumer->elemAvailable(resTuple);
           }
         }
      }

      Tuple* extendTuple(Tuple* t){
        Tuple* res = new Tuple(tt);
        // copy original attributes
        int k = t->GetNoAttributes();
        for(int i=0;i<k; i++){
          res->CopyAttribute(i,t,i);
        }
        // extend the tuple
        for(size_t i=0;i<functions.size(); i++){
           res->PutAttribute(k+i, evalFun(i, t));
        }
        return res;
      }


     Attribute* evalFun(int i, Tuple* t){
        (*funargs[i])[0] = t;
        qp->Request(functions[i],resWord);
        return ((Attribute*)resWord.addr)->Clone();
     }


};


class pextendInfo : public Consumer<Tuple>{
  public:
     pextendInfo(Word _stream, 
                 std::vector<std::vector<Supplier>>& _functions,
                 ListExpr _tt): stream(_stream), tt(0), 
                 buffer(2*_functions.size()) {
        stream.open();
        tt = new TupleType(_tt);
        runs = _functions.size();
        for(size_t i = 0; i<_functions.size(); i++){
           runners.push_back(new pextendthread(&stream, _functions[i],
                                               tt, this));         
        }
     }

     ~pextendInfo(){
       for(size_t i=0;i< runners.size();i++){
          runners[i]->cancel();
       }
       Tuple* f;
       while(!buffer.empty()){
         buffer.pop_back(&f);
         if(f){
            f->DeleteIfAllowed();
         }
       }
       for(size_t i=0;i< runners.size();i++){
          delete runners[i];
       }
       while(!buffer.empty()){
         buffer.pop_back(&f);
         if(f){
            f->DeleteIfAllowed();
         }
       }
       tt->DeleteIfAllowed();
       stream.close();
     }

     Tuple* next(){
         Tuple* result;
         buffer.pop_back(&result);
         return result; 
     }

     void elemAvailable(Tuple* elem) {
       if(elem){
         buffer.push_front(elem); 
       } else {
          mtx.lock();
          runs--;
          if(runs==0){
            buffer.push_front(elem);
          }
          mtx.unlock();
       }
     };


  private: 
     SyncStream<Tuple> stream;
     TupleType* tt;
     bounded_buffer<Tuple*>  buffer;
     boost::mutex mtx;
     size_t runs;

     std::vector<pextendthread*> runners;


};



int pextendVM(Word* args, Word& result,
           int message, Word& local, Supplier s){

  pextendInfo* li = (pextendInfo*) local.addr;
  switch(message){
     case OPEN : {
                    if(li) {
                       delete li;
                    }
                    int nt = ((CcInt*)args[1].addr)->GetValue();
                    std::vector<std::vector<Supplier> > functions;
                    // append original functions
                    Supplier supplier = args[2].addr;
                    int nooffuns = qp->GetNoSons(supplier);
                    std::vector<Supplier> f1;
                    for(int i=0;i<nooffuns;i++){
                      Supplier supplier2 = qp->GetSupplier(supplier, i);
                      Supplier fun =  qp->GetSupplier(supplier2,1);
                      f1.push_back(fun);
                    }
                    functions.push_back(f1);
                    int o = 3;

                    int nosons = qp->GetNoSons(s);
                    for(int i=0;i<nt-1;i++){
                      std::vector<Supplier> fi;
                      for(int i=0;i<nooffuns;i++){
                         assert(o<nosons);
                         fi.push_back(args[o].addr);
                         o++;
                      }
                      functions.push_back(fi);
                    } 
                    local.addr = new pextendInfo(args[0], functions, 
                                     nl->Second(GetTupleResultType(s))); 
                    return 0;
                 }

      case REQUEST:  result.addr = li?li->next():0;
                     return result.addr?YIELD:CANCEL; 
      case CLOSE: if(li){
                    delete li;
                    local.addr = 0;
                  }
                  return 0;

  }

  return -1;


}

OperatorSpec pextendSpec(
  "stream(tuple(X)) x int x funlist -> stream(tuple(X@EXT)) ",
  " _ pexetend[ f1, f2 ,... ] ",
  "Extends each tuple of the incoming stream by new attributes.",
  "query plz feed extend[10, P1 : .PLZ +1 ] "
);


Operator pextendOp(
  "pextend",
  pextendSpec.getStr(),
  pextendVM,
  Operator::SimpleSelect,
  pextendTM
);


/*
7 Creating the Algebra

*/

class ParallelAlgebra : public Algebra
{
public:
  ParallelAlgebra() : Algebra()
  {
    AddOperator(&multicountOp);
    AddOperator(&pbufferOp);
    AddOperator(&pbuffer1Op);
    AddOperator(&pbufferUOp);

    AddOperator(&pfilterSOp);
    pfilterSOp.SetUsesArgsInTypeMapping();

    AddOperator(&pextendOp);
    pextendOp.SetUsesArgsInTypeMapping();

  }

  ~ParallelAlgebra() {};
};



} // end of namespace

/*
7 Initialization

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
InitializeParallelAlgebra( NestedList* nlRef, QueryProcessor* qpRef )
{
  nl = nlRef;
  qp = qpRef;
  return (new parallelalg::ParallelAlgebra());
}

