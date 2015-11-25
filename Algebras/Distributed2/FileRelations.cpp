
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

*/

/*
1 Implementation of BinRelWriter

*/

#include <boost/thread.hpp>
#include <boost/date_time.hpp>
#include "FileRelations.h"
boost::mutex nlparsemtx;


bool BinRelWriter::writeHeader(ostream& out, ListExpr type){

    if(!Relation::checkType(type)){
       cerr << "invalid relation type " << nl->ToString(type);
       assert(false);
    }

    string relTypeS = nl->ToString(type);
    uint32_t length = relTypeS.length();
    string magic = "srel";
    out.write(magic.c_str(),4);
    out.write((char*) &length, sizeof(uint32_t));
    out.write(relTypeS.c_str(), length);
    return out.good();
}

bool BinRelWriter::writeNextTuple(ostream& out,Tuple* tuple){
  // retrieve sizes
  size_t coreSize;
  size_t extensionSize;
  size_t flobSize;
  size_t blocksize = tuple->GetBlockSize(coreSize, extensionSize, 
                                        flobSize);
  // allocate buffer and write flob into it
  char* buffer = new char[blocksize];
  tuple->WriteToBin(buffer, coreSize, extensionSize, flobSize); 
  uint32_t tsize = blocksize;
  TupleId id = out.tellp();
  tuple->SetTupleId(id);
  out.write((char*) &tsize, sizeof(uint32_t));
  out.write(buffer, tsize);
  delete[] buffer;
  return out.good();
}
           
bool BinRelWriter::finish(ostream& out){
  uint32_t marker = 0;
  out.write((char*) &marker, sizeof(uint32_t));
  return out.good();
}


bool BinRelWriter::writeRelationToFile(Relation* rel, ListExpr relType, 
                   const string& fileName){

  ofstream out(fileName.c_str(), ios::out | ios::binary);
  if(!writeHeader(out,relType)){
     out.close();
     return false;
  }
  GenericRelationIterator* it = rel->MakeScan();
  Tuple* tuple;
  while((tuple = it->GetNextTuple())){
    if(!writeNextTuple(out,tuple)){
      out.close();
      tuple->DeleteIfAllowed();
      delete it;
      return false;
    }
    tuple->DeleteIfAllowed();
  }
  delete it;
  bool res = finish(out);
  out.close();
  return res;
}


/*
2 Implementation of ffeed5Info

*/

ffeed5Info::ffeed5Info(const string& filename, const ListExpr _tt){

  tt = new TupleType(_tt);
  inBuffer = new char[FILE_BUFFER_SIZE];
  in.open(filename.c_str(), ios::in | ios::binary);
  ok = in.good();
  if(ok){
    in.rdbuf()->pubsetbuf(inBuffer, FILE_BUFFER_SIZE);
    readHeader(tt);
  } else {
    // cout << "could not open file " << filename << endl;
  }
}

   
ffeed5Info::ffeed5Info(const string& filename, TupleType* _tt){
  tt = new TupleType(*_tt);
  in.open(filename.c_str(), ios::in | ios::binary);
  inBuffer = new char[FILE_BUFFER_SIZE];
  ok = in.good();
  if(ok){
    in.rdbuf()->pubsetbuf(inBuffer, FILE_BUFFER_SIZE);
    readHeader(tt);
  }else{
    cerr << "could not open file " << filename << endl; 
  }
}

ffeed5Info::ffeed5Info(const string& filename){
  in.open(filename.c_str(), ios::in | ios::binary);
  inBuffer = new char[FILE_BUFFER_SIZE];
  ok = in.good();
  if(ok){
    in.rdbuf()->pubsetbuf(inBuffer, FILE_BUFFER_SIZE);
    readHeader(0);
  }else{
    cerr << "could not open file " << filename << endl; 
  }
  if(ok){
     ListExpr tupleType = nl->Second(fileTypeList);
     tupleType = SecondoSystem::GetCatalog()->NumericType(tupleType);
     tt = new TupleType(tupleType);
  } 
}

    
ffeed5Info::~ffeed5Info(){
  tt->DeleteIfAllowed();
  in.close();
  delete[] inBuffer;
}

ListExpr ffeed5Info::getRelType(){
  return fileTypeList;
}

bool ffeed5Info::isOK(){
  ok = ok && in.good();
  return ok;
}

void ffeed5Info::changePosition(size_t pos){
  in.seekg(pos);
  ok = in.good();
}


Tuple* ffeed5Info::next(){
  if(!ok) {
    return 0;
  }
  if(!in.good() || in.eof()){
    return 0;
  }
  TupleId id = in.tellg();
  uint32_t size;
  in.read( (char*) &size, sizeof(uint32_t));
  if(size==0){
    return 0;
  }
  char* buffer = new char[size];
  in.read(buffer, size);
  if(!in.good()){
    delete [] buffer;
    return 0;
  }
  Tuple* res = new Tuple(tt);
  res->ReadFromBin(buffer );
  res->SetTupleId(id);
  delete[] buffer;
  return res;
}


void ffeed5Info::readHeader(TupleType* tt){
  char marker[4];
  in.read(marker,4);
  string ms(marker,4);
  if(ms!="srel"){
     ok = false;
     return;
  }
  uint32_t length;
  in.read((char*) &length,sizeof(uint32_t));
  char* buffer = new char[length];
  in.read(buffer,length);
  string list(buffer,length);
  delete[] buffer;
  {
     boost::lock_guard<boost::mutex> guard(nlparsemtx);
     ok = nl->ReadFromString(list,fileTypeList); 
     ListExpr tupleType = nl->Second(fileTypeList);
     if(tt){
        tupleType = SecondoSystem::GetCatalog()->NumericType(tupleType);
        TupleType ftt(tupleType);
        if(!ftt.equalSchema(*tt)){
            cerr << "expected scheme does not fit the stored scheme." 
                 << endl;
            ok = false;
         }
     }
  }
  ok = ok && in.good();
}

