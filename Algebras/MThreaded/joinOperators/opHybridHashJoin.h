/*
----
This file is part of SECONDO.

Copyright (C) 2019,
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


//[<] [\ensuremath{<}]
//[>] [\ensuremath{>}]

\setcounter{tocdepth}{3}
\tableofcontents



1 Hybrid Hash Join

*/
#pragma once

#include <include/Stream.h>
#include <Algebras/MThreaded/MThreadedAux.h>
#include "Operator.h"
#include "vector"
#include "thread"
#include "condition_variable"
#include <boost/circular_buffer.hpp>
#include <utility>


namespace mthreaded {

class HashTablePersist {
   public:
   HashTablePersist(const size_t _bucketsNo, const size_t _coreNoWorker,
                    const size_t _maxMem, TupleType* _ttR, TupleType* _ttS,
                    const std::pair<size_t, size_t> _joinAttr);

   ~HashTablePersist();

   void PushR(Tuple* tuple, const size_t bucket);

   void PushS(Tuple* tuple, const size_t bucket);

   Tuple* PullR(const size_t bucket) const;

   Tuple* PullS(const size_t bucket) const;

   void CloseWrite();

   size_t OpenRead(size_t bucket);

   void UseMemHashTable(size_t usedMem);

   void SetHashMod(size_t hashMod);

   void CalcOverflow();

   std::shared_ptr<FileBuffer> GetOverflowS(const size_t bucket) const;

   size_t GetOverflowBucketNo(const size_t bucket) const;

   private:
   std::vector<std::shared_ptr<Buffer>> hashBucketsS;
   std::vector<std::shared_ptr<Buffer>> hashBucketsR;
   std::vector<std::shared_ptr<FileBuffer>> hashBucketsOverflowS;
   std::vector<size_t> sizeR;
   std::vector<size_t> sizeS;
   bool setSPersist;
   size_t lastMemBufferR;
   size_t lastMemBufferS;
   size_t hashMod;
   std::vector<size_t> overflowBucketNo;

   const size_t bucketsNo;
   const size_t coreNoWorker;
   const size_t maxMem;
   TupleType* ttR;
   TupleType* ttS;
   const std::pair<size_t, size_t> joinAttr;
   size_t freeMem;
};

class HashJoinWorker {
   private:
   size_t maxMem;
   size_t coreNoWorker;
   size_t streamInNo;
   std::shared_ptr<SafeQueuePersistent> tupleBuffer;
   std::shared_ptr<SafeQueue<Tuple*>> partBufferR;
   std::shared_ptr<SafeQueue<Tuple*>> partBufferS;
   size_t bucketNo = 20;
   size_t bucketsInMem1st = 999997;
   std::pair<int, int> joinAttr;
   TupleType* resultTupleType;
   std::shared_ptr<HashTablePersist> hashTablePersist;
   TupleType* ttR;
   TupleType* ttS;
   std::shared_ptr<FileBuffer> overflowBufferR;
   std::shared_ptr<FileBuffer> overflowBufferS;

   size_t phase1(size_t &countOverflow);

   // phase 2 hashjoin, return countOverflow
   size_t phase2(size_t hashMod, size_t pos);

   void recursiveOverflow(std::shared_ptr<FileBuffer> overflowR,
                          std::shared_ptr<FileBuffer> overflowS, size_t sizeR,
                          size_t xMod);

   bool tupleEqual(Tuple* a, Tuple* b) const;

   public:
   HashJoinWorker(
           size_t _maxMem, size_t _coreNoWorker, size_t _streamInNo,
           std::shared_ptr<SafeQueuePersistent> _tupleBuffer,
           std::shared_ptr<SafeQueue<Tuple*>> _partBufferR,
           std::shared_ptr<SafeQueue<Tuple*>> _partBufferS,
           std::pair<int, int> _joinAttr,
           TupleType* _resultTupleType,
           TupleType* _ttS);

   ~HashJoinWorker();

   // Thread
   void operator()();
};


class hybridHashJoinLI {
   private:
   Stream<Tuple> streamR;
   Stream<Tuple> streamS;
   const std::pair<size_t, size_t> joinAttr;
   std::vector<std::thread> joinThreads;
   const size_t maxMem;
   const size_t coreNo;
   const size_t coreNoWorker;
   TupleType* resultTupleType;
   std::shared_ptr<SafeQueuePersistent> tupleBuffer;
   std::vector<std::shared_ptr<SafeQueue<Tuple*>>> partBufferR;
   std::vector<std::shared_ptr<SafeQueue<Tuple*>>> partBufferS;
   const size_t cores = MThreadedSingleton::getCoresToUse();

   public:
   //Constructor
   hybridHashJoinLI(
           Word _streamR,
           Word _streamS,
           const std::pair<int, int> _joinAttr,
           const size_t _maxMem,
           ListExpr resultType);


   //Destructor
   ~hybridHashJoinLI();

   //Output
   Tuple* getNext();

   private:
   //Scheduler
   void Scheduler();
};


class op_hybridHashJoin {

   static ListExpr hybridHashJoinTM(ListExpr args);

   static int hybridHashJoinVM(Word* args, Word &result, int message,
                               Word &local, Supplier s);

   std::string getOperatorSpec();

   public:

   explicit op_hybridHashJoin() = default;

   ~op_hybridHashJoin() = default;

   std::shared_ptr<Operator> getOperator();

};


} // end of namespace mthreaded

