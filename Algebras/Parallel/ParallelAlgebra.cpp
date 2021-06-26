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
#include "Stream.h"
#include <boost/thread.hpp>
#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>
#include "secsemaphore.h"
#include <bounded_buffer.hpp>
#include <vector>
#include <queue>
#include <atomic>
#include <stack>
#include "SyncStream.h"

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
  if(!nl->HasLength(args,5)){
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
  if(!CcInt::checkType(nl->First(nl->Third(args)))){
    return listutils::typeError("third argument is not an int");
  }
  if(!CcInt::checkType(nl->First(nl->Fourth(args)))){
    return listutils::typeError("fourth argument is not an int");
  }

  ListExpr fun = nl->Fifth(args);
  ListExpr funt = nl->First(fun);
  if(!listutils::isMap<1>(funt)){
    return listutils::typeError("fifth argument is not a unary function");
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
class Consumer{
 
   public:
    
     virtual ~Consumer() {}
 
     virtual void elemAvailable(T* elem) = 0;

};


template<class T>
class ConsumerV : public Consumer<T>{
   public:
     virtual void elemsAvailable(std::vector<T*>* elems) = 0;
    
};


template<class T>
class pfilterSThread{
  public:
    pfilterSThread(SyncStream<T>* _stream,
                  int _inbuffer,
                  int _outbuffer, 
                  Word _fun,
                  ConsumerV<T>* _consumer): stream(_stream),
                  inbuffer(_inbuffer),
                  outbuffer(_outbuffer),
                  fun(_fun), consumer(_consumer){

      funArgs = qp->Argument(fun.addr);
      running = true;
      sendNull = false;
      if(inbuffer<2){
          // single input version
          runner = new boost::thread(&pfilterSThread<T>::run, this);
      } else {
          // make use of inputBuffer 
          runner = new boost::thread(&pfilterSThread<T>::runX, this);
      }
    }

    ~pfilterSThread(){
      runner->join();
      delete runner;
      for(size_t i=currentPos;i<inbufferV.size(); i++){
        if(inbufferV[i] != nullptr){
           inbufferV[i]->DeleteIfAllowed();
        }
      } 
      for(size_t i=0;i<outbufferV.size(); i++){
        if(outbufferV[i]!=nullptr){
           outbufferV[i]->DeleteIfAllowed();
        }
      } 
    }

    void cancel(){
       running = false;
    }
  
 private:
    SyncStream<T>* stream;
    size_t inbuffer;
    size_t outbuffer;
    Word fun;
    ConsumerV<T>* consumer;
    ArgVectorPointer funArgs;
    bool running;
    boost::thread* runner;
    Word res;
    bool sendNull;
    std::vector<T*> inbufferV;
    std::vector<T*> outbufferV;
    size_t currentPos;
    
     void run(){
       while(running){
          T* elem = stream->request();
          if(elem==nullptr){
            running = false;
            elemAvailable(elem);
            sendNull = true;
          } else {
             if(!check(elem)){
                elem->DeleteIfAllowed();
             } else {
                elemAvailable(elem);
             }
          }
       }
       if(!sendNull){
         elemAvailable(0);
         sendNull = true;
       }
    }

    void runX(){
       while(running){
          inbufferV.clear();
          stream->request(inbufferV, inbuffer);
          currentPos = 0;
          while(currentPos < inbufferV.size() && running){
             T* elem = inbufferV[currentPos];
             currentPos ++;
             if(elem==nullptr){
                running = false;
                elemAvailable(elem);
                sendNull = true;
             } else {
                if(!check(elem)){
                   elem->DeleteIfAllowed();
                } else {
                   elemAvailable(elem);
                }
            }
          }
       }
       if(!sendNull){
         elemAvailable(0);
         sendNull = true;
       }
    }


    void elemAvailable(T* elem){
        if(outbuffer<2){
          consumer->elemAvailable(elem);
        } else {
          outbufferV.push_back(elem);
          if((outbufferV.size() >= outbuffer) || (elem==nullptr)){
             // maximum size reached or end of stream
             consumer->elemsAvailable(&outbufferV);
             outbufferV.clear();
          }
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
class pfilterSInfo : public ConsumerV<T>{

  public:

     pfilterSInfo(Word& _stream, 
                  size_t _inbuffer, 
                  size_t _outbuffer,
                  std::vector<Word>& _funs): 
        stream(_stream),inbuffer(_inbuffer), 
        outbuffer(_outbuffer),funs(_funs), 
        buffer(funs.size()*2),
        bufferV(funs.size()*2) {
        stream.open();
        runs = funs.size();
        current_Pos = 0;
        for(size_t i=0; i< funs.size();i++){
          runners.push_back(new pfilterSThread<T>(&stream, inbuffer, 
                                                  outbuffer, funs[i], this));
        }
     }

     ~pfilterSInfo(){
       for(size_t i=0;i<runners.size();i++){
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
       std::vector<T*> v;
       while( !bufferV.empty() ){
          bufferV.pop_back(&v);
          for(size_t i=0;i<v.size();i++){ 
            elem = v[i];
            if(elem){
               elem->DeleteIfAllowed();
            }
          }
       } 
 
       for(size_t i=0;i<runners.size();i++){
          delete runners[i];
       }
       while( !buffer.empty() ){
          buffer.pop_back(&elem);
          if(elem){
             elem->DeleteIfAllowed();
          }
       } 
       while( !bufferV.empty() ){
          bufferV.pop_back(&v);
          for(size_t i=0;i<v.size();i++){ 
            elem = v[i];
            if(elem){
               elem->DeleteIfAllowed();
            }
          }
       } 
       for(size_t i = current_Pos; i<current_out.size(); i++){
         current_out[i]->DeleteIfAllowed();
       }
       stream.close();
     }

     T* next(){
       if(outbuffer < 2){ 
          T* elem;
          buffer.pop_back(&elem);
          return elem;    
       }
       while(current_Pos >= current_out.size()){
          // get the next buffer from bufferV
          current_Pos = 0;
          bufferV.pop_back(&current_out);
       }
       T* res = current_out[current_Pos];
       current_Pos++;
       return res;
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
     
     void elemsAvailable(std::vector<T*>* v1){
       mtx.lock();
       if(v1->size()>0){
         std::vector<T*> v = *v1;
         if(v.back()==nullptr){ // end of stream from this thread
            runs--;
            if(runs>0){
              v.pop_back();
            }
         }
         if(v.size() > 0){
           bufferV.push_front(v); 
         }
       }
       mtx.unlock();
     }
     

  private:
     SyncStream<T> stream;
     int inbuffer;
     int outbuffer; 
     std::vector<Word> funs;
     bounded_buffer<T*> buffer;
     bounded_buffer<std::vector<T*> > bufferV;
     std::vector<pfilterSThread<T>*> runners;
     boost::mutex mtx;
     size_t runs; 
     std::vector<T*> current_out;
     size_t current_Pos;
     
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
            CcInt* inB = (CcInt*) args[2].addr;
            int inbuffer = 1;
            if(inB->IsDefined()){
               inbuffer = inB->GetValue();
               if(inbuffer <1) {
                  inbuffer = 1;
               }
            } 
            CcInt* outB = (CcInt*) args[3].addr;
            int outbuffer = 1;
            if(outB->IsDefined()){
               outbuffer = outB->GetValue();
               if(outbuffer <1) {
                  outbuffer = 1;
               }
            } 
            std::vector<Word> funs;
            for(int i=4; i< qp->GetNoSons(s); i++){
                funs.push_back(args[i]);
            }
            local.addr = new pfilterSInfo<T>(args[0],
                                             inbuffer, 
                                             outbuffer, 
                                             funs);
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
  " stream(Tuple) x int x int x int x (tuple->bool) -> stream(tuple)",
  " _ pfilterS[_,_] ",
  " Filters tuples from a stream that a not fulfilling a "
  " given condition, Filtering is done used several threads "
  "where the number of threads is given in the second argument."
  "the third and the fourth argument are sizes of the input buffer "
  "and the output buffer of each thread. "
  "Note that the ordering of the input stream is not prevented",
  " query plz pfilterS[10, 100, 30,  .PLZ < 7000] count"
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
  if(!nl->HasLength(args,5)){
    return listutils::typeError("stream(tuple) x int x int x"
                                " int x funlist expected");
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
  if(!CcInt::checkType(nl->First(nl->Third(args)))){
    return listutils::typeError("third argument is not an int");
  }
  if(!CcInt::checkType(nl->First(nl->Fourth(args)))){
    return listutils::typeError("fourth argument is not an int");
  }


  // the last argument consists of a list of function types and the list of 
  // function definitions
  ListExpr funtypes = nl->First(nl->Fifth(args));
  ListExpr fundefs = nl->Second(nl->Fifth(args));
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

      pextendthread(SyncStream<Tuple>* s, 
                    size_t _inbufferSize,
                    size_t _outBufferSize,
                    std::vector<Supplier>& _functions, 
                    TupleType* _tt, 
                    ConsumerV<Tuple>* consumer):
                    stream(s), 
                    inbufferSize(_inbufferSize),
                    outbufferSize(_outBufferSize),
                    functions(_functions),tt(_tt), 
                    consumer(consumer){
         running = true;
         for(size_t i=0;i<functions.size(); i++){
            funargs.push_back(qp->Argument(functions[i]));
         }
         if(inbufferSize < 2) {
              t = new boost::thread(&pextendthread::run, this);
         } else {
              t = new boost::thread(&pextendthread::runX, this);
         }
      }

      void cancel(){
          running = false;
      }

      ~pextendthread(){
         t->join();
         delete t;
         // process remaining elements in outbuffer
         for(size_t i = 0; i< outBuffer.size(); i++){
            if(outBuffer[i]){
               outBuffer[i]->DeleteIfAllowed();
            }
         }
      }

  private:
      SyncStream<Tuple>* stream;
      size_t inbufferSize;
      size_t outbufferSize;
      std::vector<Supplier> functions;
      TupleType* tt;
      ConsumerV<Tuple>* consumer;
      std::vector<ArgVectorPointer> funargs;
      bool running;
      boost::thread* t;
      Word resWord;
      std::vector<Tuple*> inBuffer;
      std::vector<Tuple*> outBuffer;


      void run(){
         bool sendNull = false;
         while(running){
           Tuple* t = stream->request();
           if(t==0){
              running=false;
              elemAvailable(0);
              sendNull = true;
           } else {
             Tuple* resTuple = extendTuple(t);
             t->DeleteIfAllowed();
             elemAvailable(resTuple);
           }
         }
         if(!sendNull){
           elemAvailable(0);
         }
      }

            
      void runX(){
        bool sendNull = false;
         while(running){
           inBuffer.clear();
           stream->request(inBuffer, inbufferSize);
           size_t pos = 0;
           while( pos < inBuffer.size() && running){
              Tuple* t = inBuffer[pos];
              pos++;
              if(t==0){
                  running=false;
                  elemAvailable(0);
                  sendNull = true;
              } else {
                 Tuple* resTuple = extendTuple(t);
                 t->DeleteIfAllowed();
                 elemAvailable(resTuple);
              }
           }
           // canceled, remove remaining elements from inBuffer
           while(pos < inBuffer.size()){
              if(inBuffer[pos]){
                 inBuffer[pos]->DeleteIfAllowed();
              }
              pos++;
           }
           inBuffer.clear();
         }
         if(!sendNull){
           elemAvailable(0);
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

     inline void elemAvailable(Tuple* tuple){
        if(outbufferSize < 2){ 
           consumer->elemAvailable(tuple);
        } else {
           outBuffer.push_back(tuple);
           if(outBuffer.size() >= outbufferSize || !running){
             consumer->elemsAvailable(&outBuffer);
             outBuffer.clear();
           }
        }
     }
};

void deleteBuffer(bounded_buffer<Tuple*>&  buffer){
   Tuple* f;
   while(!buffer.empty()){
      buffer.pop_back(&f);
      if(f){
         f->DeleteIfAllowed();
      }
   }
}

void deleteBufferV(bounded_buffer<std::vector<Tuple*> >&  bufferV){
   std::vector<Tuple*> victim;
   while(!bufferV.empty()){
     bufferV.pop_back(&victim);
     for(auto t : victim){
         if(t){
           t->DeleteIfAllowed();
         }
     }
   }
}

class pextendInfo : public ConsumerV<Tuple>{
  public:
     pextendInfo(Word _stream,
                 size_t _inBufferSize,
                 size_t _outBufferSize, 
                 std::vector<std::vector<Supplier>>& _functions,
                 ListExpr _tt): 
                 stream(_stream), 
                 inBufferSize(_inBufferSize),
                 outBufferSize(_outBufferSize),
                 tt(0), 
                 buffer(2*_functions.size()),
                 bufferV(2*_functions.size()) {
        stream.open();
        tt = new TupleType(_tt);
        runs = _functions.size();
        currentOutPos = 0;
        count = 0;
        for(size_t i = 0; i<_functions.size(); i++){
           runners.push_back(new pextendthread(&stream, 
                                               inBufferSize, outBufferSize,
                                               _functions[i],
                                               tt, this));         
        }
     }

     ~pextendInfo(){
       for(size_t i=0;i< runners.size();i++){
          runners[i]->cancel();
       }
       deleteBuffer(buffer);
       deleteBufferV(bufferV);
       for(size_t i=0;i< runners.size();i++){
          delete runners[i];
       }
       deleteBuffer(buffer);
       deleteBufferV(bufferV);
       tt->DeleteIfAllowed();
       stream.close();
       // delete remaining elements in outbuffer
       while(currentOutPos < currentOut.size()){
           Tuple* t = currentOut[currentOutPos];
           currentOutPos++;
           if(t){
              t->DeleteIfAllowed();
           }
       }
     }

     Tuple* next(){
       if(outBufferSize<2){
          Tuple* result;
          buffer.pop_back(&result);
          return result; 
       }
       if(currentOutPos >= currentOut.size()) {
          currentOut.clear();
          bufferV.pop_back(&currentOut);
          currentOutPos = 0;
       }
       Tuple* res = currentOut[currentOutPos];
       currentOutPos++;
       return res;
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

     void elemsAvailable(std::vector<Tuple*>* elems) {
       std::vector<Tuple*> v = *elems;
       if(v.back()==nullptr){
          mtx.lock();
           runs--;
           if(runs>0){
             v.pop_back();
           }
          mtx.unlock();
       }
       if(v.size()>0) {
           bufferV.push_front(v);
       }
     };
     


  private: 
     SyncStream<Tuple> stream;
     size_t inBufferSize;
     size_t outBufferSize; 
     TupleType* tt;
     bounded_buffer<Tuple*>  buffer;
     bounded_buffer<std::vector<Tuple*> >  bufferV;
     boost::mutex mtx;
     size_t runs;
     std::vector<pextendthread*> runners;
     std::vector<Tuple*> currentOut;
     size_t currentOutPos;

     size_t count;




};



int pextendVM(Word* args, Word& result,
           int message, Word& local, Supplier s){

  pextendInfo* li = (pextendInfo*) local.addr;
  switch(message){
     case OPEN : {
                    if(li) {
                       delete li;
                    }
                    // 0 = stream, 
                    // 1 = noThreads
                    // 2 = inputbuffer size
                    // 3 = outputbuffer size
                    // 4 = original funlist
                    // ... appended functions
   
                    int nt = ((CcInt*)args[1].addr)->GetValue();
                    size_t inbufferSize = 1;
                    CcInt* is = (CcInt*) args[2].addr;
                    if(is->IsDefined() && is->GetValue()>0){
                        inbufferSize = is->GetValue();
                    }
                    size_t outbufferSize = 1;
                    CcInt* os = (CcInt*) args[3].addr;
                    if(os->IsDefined() && os->GetValue()>0){
                        outbufferSize = os->GetValue();
                    }

                    std::vector<std::vector<Supplier> > functions;
                    // append original functions
                    Supplier supplier = args[4].addr;
                    int nooffuns = qp->GetNoSons(supplier);
                    std::vector<Supplier> f1;
                    for(int i=0;i<nooffuns;i++){
                      Supplier supplier2 = qp->GetSupplier(supplier, i);
                      Supplier fun =  qp->GetSupplier(supplier2,1);
                      f1.push_back(fun);
                    }
                    functions.push_back(f1);
                    int o = 5;

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
                    local.addr = new pextendInfo(args[0],
                                       inbufferSize,
                                       outbufferSize,
                                       functions, 
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
  "stream(tuple(X)) x int x x int x int x funlist -> stream(tuple(X@EXT)) ",
  " _ pextend[nt, ib, ob; f1, f2 ,... ] ",
  "Extends each tuple of the incoming stream by new attributes."
  "The first int argument specifies the number of threads, the next argument "
  "specifies the size of the input buffer of each thread, the next argument "
  "specifies the size of the output buffer of each thread. " 
  "The functions define the actual extension part. " 
  "Note that the order of the tuples in the stream may be changed.",
  "query plz feed extend[10, 100, 50;  P1 : .PLZ +1 ] "
);


Operator pextendOp(
  "pextend",
  pextendSpec.getStr(),
  pextendVM,
  Operator::SimpleSelect,
  pextendTM
);


/*
Operator ~pextendstream~

*/
ListExpr pextendstreamTM(ListExpr args){
  if(!nl->HasLength(args,5)){
    return listutils::typeError("5 arguments expected");
  }
  if(!listutils::checkUsesArgsInTypeMapping(args)){
    return listutils::typeError("internal error");
  }
  ListExpr stream = nl->First(nl->First(args));
  if(!Stream<Tuple>::checkType(stream)){
    return listutils::typeError("first argument is not a tuple stream");
  }
  ListExpr ntl = nl->First(nl->Second(args));
  if(!CcInt::checkType(ntl)){
    return listutils::typeError("the second argument is not an integer");    
  }
  ntl = nl->Second(nl->Second(args));
  if(nl->AtomType(ntl)!=IntType){
    return listutils::typeError("the second argument is not "
                                "a constant integer");
  }
  int nt = nl->IntValue(ntl);
  if(nt<1 || nt>15){
    return listutils::typeError("the number of threads must be "
                                "a number between 1 and 15");
  }
  if(!CcInt::checkType(nl->First(nl->Third(args)))){
     return listutils::typeError("third arg not of type int");
  }
  if(!CcInt::checkType(nl->First(nl->Fourth(args)))){
     return listutils::typeError("fourth arg not of type int");
  }



  ListExpr funType = nl->First(nl->Fifth(args));
  if(!nl->HasLength(funType,1)){
     return listutils::typeError("exactly one funtion expected");
  }
  funType = nl->First(funType);
  if(!nl->HasLength(funType,2)){
     return listutils::typeError("named function expected");
  }

  ListExpr ename = nl->First(funType);
  std::string error;
  if(!listutils::isValidAttributeName(ename, error)){
    return listutils::typeError(error);
  }
  std::string name = nl->SymbolValue(ename);
  ListExpr dummy;
  ListExpr attrList = nl->Second(nl->Second(stream));
  if(listutils::findAttribute(attrList,name,dummy)>0){
   return listutils::typeError("Attribute " + name + " is already present "
                               "in the incoming tuples");
  }
  funType = nl->Second(funType);
  if(!listutils::isMap<1>(funType)){
    return listutils::typeError("fifth arg is not an unary function");
  }
  if(!nl->Equal(nl->Second(stream),nl->Second(funType))){
    return listutils::typeError("function argument and stream "
                                "element are not equal");
  }
  if(!Stream<Attribute>::checkType(nl->Third(funType))){
    return listutils::typeError("the result of the function is "
                                "not a stream of DATA");
  } 
  ListExpr resAttrList = listutils::concat(attrList, nl->OneElemList( 
                              nl->TwoElemList(
                                nl->SymbolAtom(name),
                               nl->Second(nl->Third(funType)))));
  ListExpr resType = Stream<Tuple>::wrap(Tuple::wrap(resAttrList));
 
  ListExpr fundef = nl->First(nl->Second(nl->Fifth(args)));
  fundef = nl->Second(fundef); // ignore new attribute name
  // exchange argument type
  fundef = nl->ThreeElemList(
                  nl->First(fundef),
                  nl->TwoElemList( 
                     nl->First(nl->Second(fundef)),
                     nl->Second(stream)),
                  nl->Third(fundef));

   nt--;  // the first function is given already
   if(nt==0){
      return resType;
   }
   ListExpr appendList = nl->OneElemList(
                               renameFunArgs(fundef,"_1"));
   ListExpr appendLast = appendList;
   for(int i=1;i<nt;i++){
      appendLast = nl->Append( appendLast,
                  renameFunArgs(fundef, "_" + stringutils::int2str(i+1)));
   }   

   return nl->ThreeElemList( 
                 nl->SymbolAtom(Symbols::APPEND()),
                 appendList,
                 resType);

}


class pextendstreamThread{
  public:
      pextendstreamThread(SyncStream<Tuple>* _stream,
                          size_t _inBufferSize,
                          size_t _outBufferSize,
                          Supplier _function,
                          TupleType* _tt,
                          ConsumerV<Tuple>* _consumer):
                stream(_stream), inBufferSize(_inBufferSize),
                outBufferSize(_outBufferSize),fun(_function),
                tt(_tt), consumer(_consumer) {
          funarg = qp->Argument(fun);
          running = true;
          if(inBufferSize < 2){
             runner = new boost::thread(&pextendstreamThread::run,
                                     this);
          } else {
             runner = new boost::thread(&pextendstreamThread::runX,
                                     this);
          }
      }

      ~pextendstreamThread(){
         runner->join();
         delete runner;
      }

      void cancel(){
        running = false;
      }
                          

  private:
     SyncStream<Tuple>* stream;
     size_t inBufferSize;
     size_t outBufferSize;
     Supplier fun;
     TupleType* tt;
     ConsumerV<Tuple>* consumer;
     ArgVectorPointer funarg;
     boost::thread* runner;
     Word funRes;
     bool running; 
     std::vector<Tuple*> inBuffer;
     std::vector<Tuple*> outBuffer;

     void run(){
        while(running){
           Tuple* t = stream->request();
           if(!t){
              running = false;
              elemAvailable(0);
           } else {
              (*funarg)[0] = t;
              qp->Open(fun);
              qp->Request(fun, funRes);
              while(qp->Received(fun) && running){
                Attribute* na = (Attribute*) funRes.addr;
                Tuple* resTuple = createNewTuple(t,na);
                elemAvailable(resTuple);
                funRes.addr = 0; 
                qp->Request(fun, funRes);
              }
              if(funRes.addr){
                 ((Tuple*) funRes.addr)->DeleteIfAllowed();
              }
              qp->Close(fun);
              t->DeleteIfAllowed();      
           }
        }
     }
     

     void runX(){
        while(running){
           stream->request(inBuffer, inBufferSize);
           size_t pos = 0;
           while(pos < inBuffer.size() && running){
               Tuple* t = inBuffer[pos];
               pos++;
               if(!t){
                  running = false;
                  elemAvailable(0);
               } else {
                  (*funarg)[0] = t;
                  qp->Open(fun);
                  qp->Request(fun, funRes);
                  while(qp->Received(fun) && running){
                    Attribute* na = (Attribute*) funRes.addr;
                    Tuple* resTuple = createNewTuple(t,na);
                    elemAvailable(resTuple);
                    funRes.addr = 0; 
                    qp->Request(fun, funRes);
                  }
                  if(funRes.addr){
                     ((Tuple*) funRes.addr)->DeleteIfAllowed();
                  }
                  qp->Close(fun);
                  t->DeleteIfAllowed();      
               }
           }
           while(pos < inBuffer.size()) {
               if(inBuffer[pos]) inBuffer[pos]->DeleteIfAllowed();
                pos++;
           }
        }
     }



     void elemAvailable( Tuple* t){
        if(outBufferSize < 2){
           consumer->elemAvailable(t);
        } else {
           outBuffer.push_back(t);
           if(outBuffer.size()>= outBufferSize || !running){
               consumer->elemsAvailable(&outBuffer);
               outBuffer.clear();
           }
        }

     }




     Tuple* createNewTuple(Tuple* origTuple, Attribute* na){
       Tuple* resTuple = new Tuple(tt);
       int num = origTuple->GetNoAttributes();
       for(int i=0;i<num;i++){
         resTuple->CopyAttribute(i,origTuple,i);
       }
       resTuple->PutAttribute(num,na);
       return resTuple;
     }
};


class pextendstreamInfo : public ConsumerV<Tuple>{
  public:
     pextendstreamInfo(Word _stream, 
                 size_t _inBufferSize,
                 size_t _outBufferSize,
                 std::vector<Supplier>& _functions,
                 ListExpr _tt): stream(_stream),
                 inBufferSize(_inBufferSize),
                 outBufferSize(_outBufferSize),
                 tt(0), 
                 buffer(10*_functions.size()),
                 bufferV(2*_functions.size()) {
        stream.open();
        tt = new TupleType(_tt);
        runs = _functions.size();
        currentOutPos = 0;
        for(size_t i = 0; i<_functions.size(); i++){
           runners.push_back(new pextendstreamThread(&stream, 
                                               inBufferSize, 
                                               outBufferSize,
                                               _functions[i],
                                               tt, this));         
        }
     }

     ~pextendstreamInfo(){
       for(size_t i=0;i< runners.size();i++){
          runners[i]->cancel();
       }
       deleteBuffer(buffer);
       deleteBufferV(bufferV);
       for(size_t i=0;i< runners.size();i++){
          delete runners[i];
       }
       deleteBuffer(buffer);
       deleteBufferV(bufferV);
       tt->DeleteIfAllowed();
       stream.close();
     }

     Tuple* next(){
        if(outBufferSize < 2) {
           Tuple* result;
           buffer.pop_back(&result);
           return result; 
        }
        while(currentOutPos >= currentOut.size()){
            currentOut.clear();
            bufferV.pop_back(&currentOut);
            currentOutPos = 0;
        }
        return currentOut[currentOutPos++];
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
     }

     void elemsAvailable(std::vector<Tuple*>* elems){
        std::vector<Tuple*> v = *elems;
        mtx.lock();
        if(v.back() == nullptr){
           runs--;
           if(runs > 0){
              v.pop_back();
           }
        }
        mtx.unlock();
        if(v.size() > 0){
          bufferV.push_front(v);
        }
     }



  private: 
     SyncStream<Tuple> stream;
     size_t inBufferSize;
     size_t outBufferSize;
     TupleType* tt;
     bounded_buffer<Tuple*>  buffer;
     bounded_buffer<std::vector<Tuple*> >  bufferV;
     boost::mutex mtx;
     size_t runs;
     std::vector<Tuple*> currentOut;
     size_t currentOutPos;

     std::vector<pextendstreamThread*> runners;


};


int pextendstreamVM(Word* args, Word& result,
           int message, Word& local, Supplier s){

  pextendstreamInfo* li = (pextendstreamInfo*) local.addr;
  switch(message){
     case OPEN : {
                    if(li) {
                       delete li;
                    }
                    int nt = ((CcInt*)args[1].addr)->GetValue();
                    std::vector<Supplier>  functions;
                    // append original functions
                    size_t ibs = 1;
                    CcInt* inbuffer = (CcInt*) args[2].addr;
                    if(inbuffer->IsDefined() && inbuffer->GetValue() >0){
                        ibs = inbuffer->GetValue();
                    }
                    size_t obs = 1;
                    CcInt* outbuffer = (CcInt*) args[3].addr;
                    if(outbuffer->IsDefined() && outbuffer->GetValue() >0){
                        obs = outbuffer->GetValue();
                    }
                    Supplier supplier = args[4].addr;
                    int nooffuns = qp->GetNoSons(supplier);
                    assert(nooffuns == 1);
                    Supplier supplier2 = qp->GetSupplier(supplier, 0);
                    Supplier fun =  qp->GetSupplier(supplier2,1);
                    functions.push_back(fun);
                    
                    int o = 5;

                    for(int i=0;i<nt-1;i++){
                       functions.push_back(args[o].addr);
                       o++;
                    }
                    local.addr = new pextendstreamInfo(args[0],ibs, obs, 
                                          functions, 
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

OperatorSpec pextendstreamSpec(
  "stream(tuple(X) ) x int x int x int x (tuple(X) -> stream(DATA)) "
  "-> stream(tuple(X @DATA))",
  " _ pextendstream[_,_] ",
  " Extends all incoming tuples with all attributes produced by "
  " the function using a given number of threads. The last two "
  "integer arguments specifies the size of the input buffer and output "
  "buffer of each thread.",
  "query plz feed pextendstream[10,50, 100; N : intstream(1,20)] count"
);

Operator pextendstreamOp(
  "pextendstream",
  pextendstreamSpec.getStr(),
  pextendstreamVM,
  Operator::SimpleSelect,
  pextendstreamTM
);

/*
Operator ~punion~

Returns all tuples from both input streams.

*/
ListExpr punionTM(ListExpr args){
  if(!nl->HasLength(args,3)){
    return listutils::typeError("3 arguments expected");
  }
  if(!Stream<Tuple>::checkType(nl->First(args))){
    return listutils::typeError("first argument is not a tuple stream");
  } 
  if(!nl->Equal(nl->First(args), nl->Second(args))){
    return listutils::typeError("two tuples streams having the "
                                "same tuple type expected");
  }
  if(!CcInt::checkType(nl->Third(args))){
     return listutils::typeError("third argument not of type int");
  }
  return nl->First(args);
}


class punionInfo{

  public:
     punionInfo(Word s1, Word s2, size_t buffersize): 
        stream1(s1), stream2(s2), buffer(buffersize), 
        runs(2), running1(true), running2(true){
        stream1.open();
        stream2.open();
        
        t1 = new boost::thread(&punionInfo::run1, this);
        t2 = new boost::thread(&punionInfo::run2, this);
     }

     ~punionInfo(){
        running1 = false;
        running2 = false;
        // remove remaining elements in buffer
        deleteBuffer(buffer);
        t1->join();
        t2->join();
        delete t1;
        delete t2;
        deleteBuffer(buffer);
        stream1.close();
        stream2.close();
     }

     Tuple* next(){
        Tuple* res;
        buffer.pop_back(&res);
        return res; 
     }


  private:
     Stream<Tuple> stream1;
     Stream<Tuple> stream2;
     bounded_buffer<Tuple*> buffer;
     int runs;
     bool running1;
     bool running2;
     boost::thread* t1;
     boost::thread* t2;
     boost::mutex mtx;

     void run1(){
       while(running1){
          Tuple* t = stream1.request();
          if(t){
            buffer.push_front(t);
          } else {
            boost::lock_guard<boost::mutex> guard(mtx);
            runs--;
            if(runs==0){
               buffer.push_front(0);
            }
            running1 = false; 
          }
       }
     }
     
    void run2(){
       while(running2){
          Tuple* t = stream2.request();
          if(t){
            buffer.push_front(t);
          } else {
            boost::lock_guard<boost::mutex> guard(mtx);
            runs--;
            if(runs==0){
               buffer.push_front(0);
            }
            running2 = false; 
          }
       }
     }
};


int punionVM(Word* args, Word& result,
             int message, Word& local, Supplier s){

   punionInfo* li = (punionInfo*) local.addr;
   switch(message){
      case OPEN: {
                  if(li) delete li;
                  size_t bufferSize = 4;
                  CcInt* bs = (CcInt*) args[2].addr;
                  if(bs->IsDefined() && bs->GetValue() > 4){
                      bufferSize = bs->GetValue();
                  } 
                  local.addr = new punionInfo(args[0], args[1], bufferSize);
                   return 0;
                 }
      case REQUEST: result.addr = li?li->next(): 0;
                    return result.addr?YIELD:CANCEL;
      case CLOSE: if(li){
                    delete li;
                    local.addr = 0;
                  }
                  return 0;
   }
   return -1;
}

OperatorSpec punionSpec(
   "stream(tuple) x stream(tuple) x int -> stream(tuple) ",
   "_ _ punion[_]",
   "Returns the union of the tuple streams. The "
   "incoming tuples are asked by separate threads. The "
   "order of the result is not predictable. The last argument "
   "is the size of the outbut buffer.",
   "query plz feed plz feed punion [120] count"
);

Operator punionOp(
  "punion",
  punionSpec.getStr(),
  punionVM,
  Operator::SimpleSelect, 
  punionTM
);

/*
Operator ~ploopsel~

*/
template<bool isJoin>
ListExpr ploopselTM(ListExpr args){
  if(!nl->HasLength(args,5)){
    return listutils::typeError("three arguments expected");
  }
  if(!listutils::checkUsesArgsInTypeMapping(args)){
    return listutils::typeError("internal error");
  }
  ListExpr instream = nl->First(nl->First(args));
  if(!Stream<Tuple>::checkType(instream)){
    return listutils::typeError("first argument is not a tuple stream");
  }
  if(!CcInt::checkType(nl->First(nl->Second(args)))){
    return listutils::typeError("second argument is not an int");
  }
  ListExpr ntl = nl->Second(nl->Second(args));
  if(nl->AtomType(ntl) != IntType){
    return listutils::typeError("second argument is not a constant int");
  }
  int nt = nl->IntValue(ntl);
  if(nt <1 || nt > 15){
    return listutils::typeError("the number of threads must be "
                                "between 1 and 15");
  }
  if(!CcInt::checkType(nl->First(nl->Third(args)))){
    return listutils::typeError("third argument is not an int");
  }
  if(!CcInt::checkType(nl->First(nl->Fourth(args)))){
    return listutils::typeError("Fourth argument is not an int");
  }

  ListExpr funcomplete = nl->Fifth(args);
  ListExpr funtype = nl->First(funcomplete);
  if(!listutils::isMap<1>(funtype)){
    return listutils::typeError("third argument is not a unary function");
  }
  if(!nl->Equal(nl->Second(instream), nl->Second(funtype))){
    return listutils::typeError("argument type of function differs "
                                "from tuple type in stream");
  }
  ListExpr resType = nl->Third(funtype);
  if(!Stream<Tuple>::checkType(resType)){
    return listutils::typeError("the functions does not produce "
                                "a stream of tuples");
  }

  if(isJoin){
    ListExpr alist1 = nl->Second(nl->Second(instream));
    ListExpr alist2 = nl->Second(nl->Second(resType));

    ListExpr alist = listutils::concat(alist1,alist2);
    resType = Stream<Tuple>::wrap(Tuple::wrap(alist));
    if(!Stream<Tuple>::checkType(resType)){
      return listutils::typeError("name conflicts in tuple types");
    }
  }

  if(nt==1){
    return resType;
  }

  ListExpr fundef = nl->Second(funcomplete);
  fundef = nl->ThreeElemList(
                  nl->First(fundef),
                  nl->TwoElemList(
                      nl->First(nl->Second(fundef)),
                      nl->Second(funtype)),
                  nl->Third(fundef));

  ListExpr appendList = nl->OneElemList(renameFunArgs(fundef, "_1"));
  ListExpr appendLast = appendList;

  for(int i=2;i<nt;i++){
     ListExpr fd = renameFunArgs(fundef, "_" + stringutils::int2str(i));
     appendLast = nl->Append(appendLast, fd);
  } 

  return nl->ThreeElemList(nl->SymbolAtom(Symbols::APPEND()),
                           appendList,
                           resType);
}

template<bool isJoin>
class ploopselthread{

  public:

     ploopselthread(SyncStream<Tuple>* _stream, 
                    size_t _inBufferSize,
                    size_t _outBufferSize,
                    Supplier _fun,
                    ConsumerV<Tuple>* _consumer, 
                    TupleType* _tt, 
                    boost::mutex* _fmtx):
        stream(_stream), inBufferSize(_inBufferSize), 
        outBufferSize(_outBufferSize),
        fun(_fun), consumer(_consumer), tt(_tt), fmtx(_fmtx){
        if(tt){
           tt->IncReference();
        }
        funarg = qp->Argument(fun);
        running = true;
        if(inBufferSize < 2) { 
            thread = new boost::thread(&ploopselthread<isJoin>::run, this);
        } else {
            thread = new boost::thread(&ploopselthread<isJoin>::runX, this);
        }
     }
 

     ~ploopselthread(){
        running = false;
        thread->join();
        delete thread;
        if(tt){
           tt->DeleteIfAllowed();
        }
     }

     void cancel(){
        running = false;
     }

  private:
     SyncStream<Tuple>* stream;
     size_t inBufferSize;
     size_t outBufferSize;
     Supplier fun;
     ConsumerV<Tuple>* consumer;
     TupleType* tt;
     boost::mutex* fmtx;
     ArgVectorPointer funarg;
     boost::thread* thread;
     Word funres;
     std::atomic_bool running;
     std::vector<Tuple*> outBuffer;

     void run(){
        while(running){
          Tuple* inTuple = stream->request();
          generateResults(inTuple);
        }
     }

     void runX(){
       std::vector<Tuple*> inBuffer;
       size_t pos;
       while(running){
         inBuffer.clear();
         stream->request(inBuffer, inBufferSize);
         pos = 0;
         while(pos<inBuffer.size() && running){
            generateResults(inBuffer[pos]);
            inBuffer[pos] = nullptr;
            pos++;
         }
         // kill remaining input tuples 
         while( pos < inBuffer.size()) {
            inBuffer[pos]->DeleteIfAllowed();
            pos++;
         }
       }
     }

     void generateResults(Tuple* inTuple){
          if(!inTuple){
             running = false;
             elemAvailable(0);
          } else {
             (*funarg)[0] = inTuple;
             fmtx->lock();
             qp->Open(fun);
             fmtx->unlock();
             qp->Request(fun,funres);
             while( qp->Received(fun) && running){
                Tuple* ftuple = (Tuple*) funres.addr;
                Tuple* resTuple = constructResTuple(inTuple, ftuple);
                funres.addr = 0;
                elemAvailable(resTuple);
                qp->Request(fun,funres);
             }
             if(funres.addr){
               ((Tuple*) funres.addr)->DeleteIfAllowed();
             }
             inTuple->DeleteIfAllowed();
             fmtx->lock();
             qp->Close(fun);
             fmtx->unlock();
          }
     }
     

    void elemAvailable( Tuple* t){
        if(outBufferSize < 2){
           consumer->elemAvailable(t);
        } else {
           outBuffer.push_back(t);
           if(outBuffer.size()>= outBufferSize || !running){
               consumer->elemsAvailable(&outBuffer);
               outBuffer.clear();
           }
        }

     }


     Tuple* constructResTuple(Tuple* inTuple, Tuple* funTuple){
        if(!isJoin){
           return funTuple;
        }
        Tuple* resTuple = new Tuple(tt);

        int num = inTuple->GetNoAttributes();
        for(int i=0;i<num;i++){
           resTuple->CopyAttribute(i,inTuple,i);
        }
        for(int i=0;i<funTuple->GetNoAttributes();i++){
           resTuple->CopyAttribute(i,funTuple,i+num);
        }
        funTuple->DeleteIfAllowed();
        return resTuple;
     }   
};

template<bool isJoin>
class ploopselInfo : public ConsumerV<Tuple>{
  public:
     ploopselInfo(Word _stream,
                 size_t _inBufferSize, size_t _outBufferSize, 
                 std::vector<Supplier> _functions,
                 ListExpr _tt): stream(_stream),
                 inBufferSize(_inBufferSize), outBufferSize(_outBufferSize),
                 tt(0), buffer(2*_functions.size()), 
                 bufferV(2*_functions.size()) {


        stream.open();
        if(isJoin){
            tt = new TupleType(_tt);
        }
        currentOutPos = 0;
        runs = _functions.size();
        for(size_t i = 0; i<_functions.size(); i++){
           runners.push_back(new ploopselthread<isJoin>(&stream, inBufferSize, 
                                               outBufferSize, _functions[i],
                                               this, tt, &fmtx));         
        }
     }

     ~ploopselInfo(){
       for(size_t i=0;i< runners.size();i++){
          runners[i]->cancel();
       }
       deleteBuffer(buffer);
       deleteBufferV(bufferV);
       for(size_t i=0;i< runners.size();i++){
          delete runners[i];
       }
       deleteBuffer(buffer);
       deleteBufferV(bufferV);
       if(tt){
           tt->DeleteIfAllowed();
       }
       stream.close();
     }

     Tuple* next(){
       // without output vectors
       if(outBufferSize < 2){
         Tuple* result;
         buffer.pop_back(&result);
         return result; 
       }
       // with output for each thread
       while(currentOutPos >= currentOut.size()){
         currentOut.clear();
         bufferV.pop_back(&currentOut);
         currentOutPos = 0;
       }      
       return currentOut[currentOutPos++];
     }

     void elemAvailable(Tuple* elem) {
       if(elem){
         buffer.push_front(elem); 
       } else {
          mtx.lock();
          runs--;
          if(runs==0){ // the last runner has no more elements
            buffer.push_front(elem);
          }
          mtx.unlock();
       }
     };

     void elemsAvailable(std::vector<Tuple*>* v1){
        std::vector<Tuple*> v = *v1;
        if(v.back() == nullptr){
          mtx.lock();
          runs--;
          if(runs>0){ // the last runner has no more elements
            v.pop_back();
          }
          mtx.unlock();
        }
        if(v.size() > 0){
           bufferV.push_front(v);
       }
     }

  private: 
     SyncStream<Tuple> stream;
     size_t inBufferSize;
     size_t outBufferSize;
     TupleType* tt;
     bounded_buffer<Tuple*>  buffer;
     bounded_buffer<std::vector<Tuple*> >  bufferV;
     boost::mutex mtx;
     size_t runs;
     boost::mutex fmtx;
     std::vector<Tuple*> currentOut;
     size_t currentOutPos;

     std::vector<ploopselthread<isJoin>*> runners;
};

template<bool isJoin>
int ploopselVM(Word* args, Word& result,
             int message, Word& local, Supplier s){

   ploopselInfo<isJoin>* li = (ploopselInfo<isJoin>*) local.addr;
   switch(message){
      case OPEN: {  if(li) delete li;
                    std::vector<Supplier> funs;
                    size_t inBufferSize = 1;
                    CcInt* ibs = (CcInt*) args[2].addr;
                    if(ibs->IsDefined() && ibs->GetValue() > 1){
                       inBufferSize = ibs->GetValue();
                    }
                    size_t outBufferSize = 1;
                    CcInt* obs = (CcInt*) args[3].addr;
                    if(obs->IsDefined() && obs->GetValue() > 1){
                       outBufferSize = obs->GetValue();
                    }
                    for(int i=4;i<qp->GetNoSons(s);i++){
                       Supplier s = args[i].addr;
                       funs.push_back(s);
                    }
                    size_t nt = ((CcInt*)args[1].addr)->GetValue();
                    assert(nt==funs.size());
                    local.addr = new ploopselInfo<isJoin>(args[0],
                                       inBufferSize, outBufferSize,  funs,
                                       nl->Second(GetTupleResultType(s))) ;
                    return 0;
                  }
      case REQUEST: result.addr = li?li->next(): 0;
                    return result.addr?YIELD:CANCEL;
      case CLOSE: if(li){
                    delete li;
                    local.addr = 0;
                  }
                  return 0;
   }
   return -1;
}

OperatorSpec ploopselSpec(
   "stream(tupleA) x int x fun(tupleA -> stream(tupleB)) -> stream(tupleB) ",
   "_ ploopsel[_,fun]",
   "Returns the tuples created by a function applied to "
   "each tuple of a stream. Processing is done using a set of threads. "
   "The number of threads is given by the first integer argument. " 
   "The other  two integers defined the size of the input buffer and "
   "the output bufffer of each thread.",
   "query Orte feed {o} ploopsel[10, plz_Ort plz exactmatch[.Ort_o]]"
);

Operator ploopselOp(
  "ploopsel",
  ploopselSpec.getStr(),
  ploopselVM<false>,
  Operator::SimpleSelect, 
  ploopselTM<false>
);


OperatorSpec ploopjoinSpec(
   "stream(tupleA) x int x fun(tupleA -> stream(tupleB)) -> stream(tupleAB) ",
   "_ ploopjoin[_,fun]",
   "Returns the concatenation of tuples from the input stream "
   "with all tuples produced by the function." 
   "Processing is done using a set of threads.  "
   "The number of threads is given by the first integer argument. " 
   "The other  two integers defined the size of the input buffer and "
   "the output bufffer of each thread.",
   "query Orte feed {o} ploopjoin[10, plz_Ort plz exactmatch[.Ort_o] {a}]"
);

Operator ploopjoinOp(
  "ploopjoin",
  ploopjoinSpec.getStr(),
  ploopselVM<true>,
  Operator::SimpleSelect, 
  ploopselTM<true>
);



/*
Type Mapping Operator ~PAGGRT~

*/

ListExpr PAGGRTTM(ListExpr args){
  if(!nl->HasMinLength(args,2)){
    return listutils::typeError("at Least two elements expected");
  } 
  if(!Stream<Tuple>::checkType(nl->First(args))){
    return listutils::typeError("first argument is not a tuple stream");
  }
  ListExpr attr = nl->Second(args);
  if(nl->AtomType(attr) != SymbolType){
    return listutils::typeError("second argument is not a valid "
                                 "attribute name");
  }
  std::string aName = nl->SymbolValue(attr);
  ListExpr attrList = nl->Second(nl->Second(nl->First(args)));
  ListExpr attrType = nl->TheEmptyList();
  int index = listutils::findAttribute(attrList, aName, attrType);
  if(index == 0){
    return listutils::typeError("Attribute " + aName + 
                                " is not part of the tuple");
  }
  return attrType;
}

OperatorSpec PAGGRTSpec(
  "stream(tuple) x IDENT  x ... ->  DATA",
  "PAGGRT(_,_,...)",
  "Extracts the type of the attribute specified in the second "
  " argument from the tuple stream . ",
  " query ten feed  paggregate[No,2,2; . + .. ; 0 ] "
);
 
Operator PAGGRTOp(
   "PAGGRT",
   PAGGRTSpec.getStr(),
   0,
   Operator::SimpleSelect,
   PAGGRTTM
);




/*
Operator ~paggregate~

*/
ListExpr paggregateTM(ListExpr args){
  if(!nl->HasLength(args,6)){
    return listutils::typeError("5 arguments expected");
  }
  if(!listutils::checkUsesArgsInTypeMapping(args)){
    return listutils::typeError("internal error");
  }
  // first argument : a tuple stream
  ListExpr stream = nl->First(nl->First(args));
  if(!Stream<Tuple>::checkType(stream)){
    return listutils::typeError("first argument is not a tuple stream");
  }
  // second argument: attribute name in tuples
  ListExpr attrName = nl->First(nl->Second(args));
  if(nl->AtomType(attrName) != SymbolType){
    return listutils::typeError("The second argument is not a "
                                "valid attribute name");
  }
  std::string aName = nl->SymbolValue(attrName);
  ListExpr attrList = nl->Second(nl->Second(stream));
  ListExpr attrType = nl->TheEmptyList();
  int index = listutils::findAttribute(attrList, aName, attrType);
  if(index == 0){
     return listutils::typeError("Attribute " + aName 
                               + " not part of the tuple");
  }
  index--;
  // third argument: number of threads
  if(!CcInt::checkType(nl->First(nl->Third(args)))){
     return listutils::typeError("third argument is not an integer");
  }
  ListExpr noThreads = nl->Second(nl->Third(args));
  if(nl->AtomType(noThreads) != IntType){
     return listutils::typeError("third argument is not a constant integer");
  }
  int threads = nl->IntValue(noThreads);
  if(threads < 2 || threads > 15){
    return listutils::typeError("number of threads must be between 2 and 15");
  }
  // fourth argument: size of the input buffer, invalid values 
  // will be set to 5 in value mapping
  if(!CcInt::checkType(nl->First(nl->Fourth(args)))){
     return listutils::typeError("fourth argument is not an integer");
  }
  // fifth argument: the aggregate function
  ListExpr funtype = nl->First(nl->Fifth(args));
  if(!listutils::isMap<2>(funtype)){
     return listutils::typeError("fifth argument is not a binary function");
  }
  if(!nl->Equal(nl->Second(funtype), attrType)){
     return listutils::typeError("first arg of the function is not of the "
                            "same type as the attribute " + aName);
  }
  if(!nl->Equal(nl->Third(funtype), attrType)){
     return listutils::typeError("second arg of the function is not of the "
                            "same type as the attribute " + aName);
  }
  if(!nl->Equal(nl->Fourth(funtype), attrType)){
     return listutils::typeError("result of the function is not of the "
                            "same type as the attribute " + aName);
  }
  // sixth argument: the default value
  if(!nl->Equal(nl->First(nl->Sixth(args)), attrType)){
     return listutils::typeError("the default value is not of the same  type"
                        " as the attribute " + aName);
  }
  ListExpr fundef = nl->Second(nl->Fifth(args)); 

  // replace attr types in function by real types
  fundef = nl->FourElemList(
                  nl->First(fundef),
                  nl->TwoElemList(nl->First(nl->Second(fundef)), attrType),
                  nl->TwoElemList(nl->First(nl->Third(fundef)), attrType),
                  nl->Fourth(fundef));

  ListExpr appendList = nl->OneElemList(nl->IntAtom(index));
  ListExpr appendLast = appendList;

  for(int i=1;i<threads;i++){ // note: 0 is the original function
     ListExpr fd = renameFunArgs(fundef, "_" + stringutils::int2str(i));
     appendLast = nl->Append(appendLast, fd);
  } 

  return nl->ThreeElemList( nl->SymbolAtom(Symbols::APPEND()),
                            appendList,
                            attrType);
}


class stackelem{
  public:
     stackelem(Attribute* _attr): attr(_attr), level(1) {}
     stackelem(Attribute* _attr, int _level): attr(_attr), level(_level){}
     stackelem(const stackelem& e): attr(e.attr), level(e.level){}

     Attribute* attr;
     int level;

};


template<bool useStack>
class paggregateThread{

 public:
    paggregateThread(SyncStream<Tuple>* _stream,
                    int _attrIndex, 
                    size_t _inBufferSize,
                    Supplier _fun,
                    Consumer<Attribute>* _consumer) :
                stream(_stream), 
                attrIndex(_attrIndex),
                inBufferSize(_inBufferSize),
                fun(_fun), consumer(_consumer) {
                funargs = qp->Argument(fun);
                if(!useStack){
                    thread = new boost::thread(
                                  &paggregateThread<useStack>::run, this);
                } else {
                    thread = new boost::thread(
                                  &paggregateThread<useStack>::runStack, this);
                }
           }
     
    ~paggregateThread(){
        thread->join();
        delete thread;
    }



  private:
      SyncStream<Tuple>* stream;
      int attrIndex;
      size_t inBufferSize;
      Supplier fun;
      Consumer<Attribute>* consumer;
      ArgVectorPointer funargs;
      Word funres;
      std::atomic_bool running;
      boost::thread* thread;
      std::stack<stackelem> stack1; 

      void run(){
         std::vector<Tuple*> inBuffer;
         running = true;
         Attribute* result;
         while(running){
            inBuffer.clear();
            stream->request(inBuffer, inBufferSize);
            result = computeResultSimple(inBuffer); 
            if(result != nullptr) {
               consumer->elemAvailable(result);
            }
         }
      }

      Attribute* computeResultSimple(std::vector<Tuple*>& elems){
          Tuple* tuple = elems[0];
          if(tuple==nullptr){
             running = false;
             return nullptr;
          }
          Attribute* res = tuple->GetAttribute(attrIndex)->Copy();
          tuple->DeleteIfAllowed();
          for(size_t i=1; i< elems.size(); i++){
               tuple = elems[i];
               if(tuple==nullptr){
                  running = false;
               } else {
                  res = aggregate(res, tuple);
               }
          }
          return res;
      }


     void runStack(){
         std::vector<Tuple*> inBuffer;
         running = true;
         size_t count = 0;
         while(running){
            inBuffer.clear();
            stream->request(inBuffer, inBufferSize);
            count += inBuffer.size();
            for(size_t i = 0; i< inBuffer.size(); i++){
               Tuple* tuple = inBuffer[i];
               if(tuple==nullptr){
                 running = false;
               } else {
                 putToStack(tuple->GetAttribute(attrIndex)->Copy());
                 tuple->DeleteIfAllowed();
               }
            }
         }
         aggregateStack(false);
         if(!stack1.empty()){
             Attribute* res = stack1.top().attr;
             stack1.pop();
             assert(stack1.empty());
             consumer->elemAvailable(res);
         }
     }


      Attribute* computeResultStack(std::vector<Tuple*>& elems){
          for(size_t i=0; i< elems.size(); i++){
               Tuple* tuple = elems[i];
               if(tuple==nullptr){
                  running = false;
               } else {
                  putToStack(tuple->GetAttribute(attrIndex)->Copy());
                  tuple->DeleteIfAllowed(); 
               }
          }
          aggregateStack(false);
          if(stack1.empty()) return 0;
          Attribute* result = stack1.top().attr;
          stack1.pop();
          return result;
      }
      
      void putToStack(Attribute* attr){
         stackelem se(attr);
         stack1.push(se);
         aggregateStack(true);
      }
      
      stackelem aggregate(stackelem& s1, stackelem& s2){
          s1.attr = computeFun(s1.attr,s2.attr);
          s1.level++;
          return s1;
      }
      
      void aggregateStack(bool useLevel){
         while(stack1.size() > 1) {
             stackelem top = stack1.top();
             stack1.pop();
             if(useLevel){
               if(top.level != stack1.top().level){
                  stack1.push(top);
                  return;
               }
             } 
             stackelem top2 = stack1.top();
             stack1.pop();
             stack1.push(aggregate(top,top2));
         }
      }



      Attribute* aggregate(Attribute* current, Tuple* tuple){
         Attribute* a2 = tuple->GetAttribute(attrIndex)->Copy();
         tuple->DeleteIfAllowed();
         return computeFun(current, a2);
      }

      Attribute* computeFun(Attribute* a1, Attribute* a2){
         (*funargs)[0] = a1;
         (*funargs)[1] = a2;
         qp->Request(fun, funres);
         qp->ReInitResultStorage(fun);
         Attribute* result = (Attribute*) funres.addr;
         a1->DeleteIfAllowed();
         a2->DeleteIfAllowed();
         return result;
      }



};


template<bool useStack>
class paggregateInfo : public Consumer<Attribute> {

   public:
     paggregateInfo(Word& _stream,
                    int _index,
                    size_t _inBufferSize,
                    std::vector<Word>& _funs
                   ) :
                  stream(_stream) {
           fun = _funs[0].addr;
           funargs = qp->Argument(fun);
           currentResult = nullptr;
           stream.open();
           for(size_t i=1;i<_funs.size(); i++){
              runners.push_back(new paggregateThread<useStack>(
                                          &stream, _index, _inBufferSize, 
                                          _funs[i].addr, this));
           }  
     }

     ~paggregateInfo(){
        stream.close();
     }


     void elemAvailable( Attribute* elem){
       boost::lock_guard<boost::mutex> guard(mtx);
       if(elem != nullptr){
          if(currentResult==nullptr){
             currentResult = elem; 
          } else {
             computeNextResult(elem);
          }
       }
     }

     Attribute* getResult(){
        for(size_t i=0;i<runners.size(); i++){
           delete runners[i];
        }
        return currentResult;
     }

                  
  private:
     SyncStream<Tuple> stream;
     std::vector<paggregateThread<useStack>*> runners;
     Supplier fun;
     ArgVectorPointer funargs;
     boost::mutex mtx;
     Attribute* currentResult;
     Word funres;

     void computeNextResult(Attribute* elem){
       (*funargs)[0] = currentResult;
       (*funargs)[1] = elem; 
       qp->Request(fun, funres);
       qp->ReInitResultStorage(fun);
       currentResult->DeleteIfAllowed();
       elem->DeleteIfAllowed();
       currentResult = (Attribute*) funres.addr;
     }
};


template<bool useStack>
int paggregateVM(Word* args, Word& result,
           int message, Word& local, Supplier s){

   // args[0] : stream
   // args[1] : attribute name
   // args[2] : number of threads
   // args[3] : size of inBuffer
   // args[4] : original function
   // args[5] : default value
   // args[6] : attribute index
   // args[7] ... : copy of function
   
   int attrIndex = ((CcInt*) args[6].addr)-> GetValue();
   int inBufferSize = 4;
   CcInt* bs = (CcInt*) args[3].addr;
   if(bs->IsDefined() && bs->GetValue() > 3){
      inBufferSize = bs->GetValue();
   }
   std::vector<Word> funs;
   funs.push_back(args[4]);
   for(int i=7; i<qp->GetNoSons(s); i++){
      funs.push_back(args[i]);
   }
   paggregateInfo<useStack> info(args[0], attrIndex, inBufferSize, funs);
   Attribute* resultAttr = info.getResult();
   if(resultAttr == nullptr){
      resultAttr = ((Attribute*) args[5].addr) -> Clone();
   }
   Word r(resultAttr);
   qp->DeleteResultStorage(s);
   qp->ChangeResultStorage(s,r);
   result = qp->ResultStorage(s);
   return 0;
}

OperatorSpec paggregateSpec(
   "stream(tuple) x IDENT x int x int x fun x DATA -> DATA",
   "stream paggregate[AttrName, noThreads, inBufferSize; fun ; defaultValue]",
   "Aggregates over all tuples in the stream using a function. "
   "Evaluation is done in parallel. ",
   "query ten feed paggregate[No, 3, 8; fun( int i1, int i2) i1 + i1; 0]"
);

Operator paggregateOp(
   "paggregate",
   paggregateSpec.getStr(),
   paggregateVM<false>,
   Operator::SimpleSelect,
   paggregateTM
);

Operator paggregateBOp(
   "paggregateB",
   paggregateSpec.getStr(),
   paggregateVM<true>,
   Operator::SimpleSelect,
   paggregateTM
);


Operator* getPsortOp();
Operator* getPsortbyOp();


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

    AddOperator(&pextendstreamOp);
    pextendstreamOp.SetUsesArgsInTypeMapping();

    AddOperator(&punionOp);

    AddOperator(&ploopselOp);
    ploopselOp.SetUsesArgsInTypeMapping();
    
    AddOperator(&ploopjoinOp);
    ploopjoinOp.SetUsesArgsInTypeMapping();

    AddOperator(&paggregateOp);
    paggregateOp.SetUsesArgsInTypeMapping();
    
    AddOperator(&paggregateBOp);
    paggregateBOp.SetUsesArgsInTypeMapping();

    AddOperator(&PAGGRTOp);

    Operator* psort = getPsortOp();
    psort->SetUsesMemory();
    AddOperator(psort);
    Operator* psortby = getPsortbyOp();
    psortby->SetUsesMemory();
    AddOperator(psortby);

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


