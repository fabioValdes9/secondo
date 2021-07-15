/*
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

Started November 2019, Fabio Vald\'{e}s

*/

#include "Algorithms.h"

namespace stj {

struct AggEntry {
  AggEntry();
  AggEntry(const TupleId id, const temporalalgebra::SecInterval& iv,
           Rect& rect, const unsigned int noTuples);
  ~AggEntry() {}

  void clear();
  void deletePeriods();
  ListExpr toListExpr();
  unsigned int getNoOccs(const TupleId& id) const;
  datetime::DateTime getDuration() const {return duration;}
  void computeCommonTimeInterval(const std::set<TupleId>& commonTupleIds,
                                 temporalalgebra::SecInterval& iv);
  void computeCommonRect(const temporalalgebra::SecInterval& iv,
             const std::set<TupleId>& commonTupleIds, Geoid *geoid, Rect &rect);
  void computeSemanticTimeSpec(const std::set<TupleId>& commonTupleIds,
                               std::string& semanticTimeSpec) const;
  void sequentialJoin(AggEntry& entry1, AggEntry& entry2);
  std::string print(const TupleId& id = 0) const;
  std::string print(const Rect& rect) const;
  
  std::vector<std::tuple<TupleId, temporalalgebra::Periods*, Rect> > occs;
  std::vector<unsigned int> occsPos; // maps tuple id to pos in occs vector
  unsigned int noOccs; // over all tuples
  datetime::DateTime duration; // over all tuples
};

/*

Comparison function; sort by

  * number of tuples with at least one occurrence (support * noTuples)
  * total duration
  * total number of occurrences (>= support * noTuples)
  * lexicographical order of labels

*/


// struct compareEntries {
//   bool operator()(NewPair<const std::string, AggEntry> const& l,
//                   NewPair<const std::string, AggEntry> const& r) const {
//       if (l.second.occurrences.size() != r.second.occurrences.size()) {
//         return l.second.occurrences.size() > r.second.occurrences.size();
//       }
//       if (l.second.duration != r.second.duration) {
//         return l.second.duration > r.second.duration;
//       }
//       if (left.second.noOccurrences != r.second.noOccurrences) {
//         return l.second.noOccurrences > r.second.noOccurrences;
//       }
//       return l.first < r.first;
//     }
// };

struct comparePMResults {
  bool operator()(NewPair<std::string, double> res1,
                  NewPair<std::string, double> res2) {
    if (res1.second == res2.second) {
      if (res1.first.length() == res2.first.length()) {
        return res1.first < res2.first;
      }
      return (res1.first.length() < res2.first.length());
    }
    return (res1.second < res2.second); // ascending order
  }
};

struct compareLabelsWithSupp {
  bool operator()(NewPair<unsigned int, double> lws1,
                  NewPair<unsigned int, double> lws2) const {
    if (lws1.second == lws2.second) {
      return (lws1.first < lws2.first);
    }
    return (lws1.second > lws2.second); // descending order
  }
};

struct compareLabelSeqs {
  bool operator()(std::vector<unsigned int> seq1, 
                  std::vector<unsigned int> seq2) const {
    if (seq1.size() == seq2.size()) {
      for (unsigned int i = 0; i < seq1.size(); i++) {
        if (seq1[i] != seq2[i]) {
          return seq1[i] < seq2[i];
        }
      }
      return false;
    }
    return seq1.size() < seq2.size();
  }
};

extern TypeConstructor fptreeTC;
extern TypeConstructor projecteddbTC;
extern TypeConstructor verticaldbTC;
extern TypeConstructor splSemTrajTC;

/*

The original mlabel objects are transformed into a map from a label onto a set
of tuple ids (one for every tuple containing the label) together with a time
period (the time period in which the label occurs for this tuple), the number
of occurrences inside the tuple, and the total duration of its occurrences.

*/

struct RelAgg {
  RelAgg();
  RelAgg(RelAgg *ra);
  ~RelAgg() {clearEntries();}
  
  void clear();
  void clearEntries();
  ListExpr entriesToListExpr();
  static bool saveToRecord(RelAgg *agg, SmiRecord& valueRecord, size_t& offset);
  static bool readFromRecord(RelAgg *agg,SmiRecord& valueRecord,size_t& offset);
  void getLabelSeqFromMLabel(MLabel *ml, std::vector<unsigned int>& result);
  void insertLabelAndBbox(const std::string& label, const TupleId& id, 
                          const temporalalgebra::SecInterval& iv, Rect& rect);
  void scanRelation(Relation *rel, const NewPair<int, int> attrPos, Geoid *g);
  void filter(const double ms, const size_t memSize);
  bool buildAtom(unsigned int label, AggEntry entry,
                 const std::set<TupleId>& commonTupleIds, std::string& atom);
  void subsetperm(std::vector<unsigned int> source, int left, int index,
                  std::vector<unsigned int>& labelVec, 
                  std::set<std::vector<unsigned int> >& result);
  void subset(std::vector<unsigned int> source, int left, int index,
              std::vector<unsigned int>& labelVec, 
              std::set<std::vector<unsigned int> >& result);
  void retrieveLabelCombs(const unsigned int size, 
                          std::vector<unsigned int>& source, 
                          std::set<std::vector<unsigned int> >& result);
  void retrieveLabelSubsets(const unsigned int size, 
                            std::vector<unsigned int>& source, 
                            std::set<std::vector<unsigned int> >& result);
  double getSupp(unsigned int label);
  bool canLabelsBeFrequent(std::vector<unsigned int>& labelSeq,
                                 std::set<TupleId>& intersection);
  double sequenceSupp(std::vector<unsigned int> labelSeq,
                      std::set<TupleId> intersection);
  void combineApriori(std::set<std::vector<unsigned int> >& frequentLabelCombs,
                      std::set<std::vector<unsigned int> >& labelCombs);
  void retrievePermutations(std::vector<unsigned int>& labelComb,
                            std::set<std::vector<unsigned int> >& labelPerms);
  void derivePatterns(const int mina, const int maxa);
  unsigned long long int computeEntriesSize() const;
  unsigned long long int computeFreqLabelsSize() const;
  void combineEntries(unsigned int endOfPrefix, RelAgg *ra, 
                      unsigned int label, unsigned int minSuppCnt);
  std::string print(const std::map<unsigned int, AggEntry>& contents) const;
  std::string print(const std::map<TupleId, std::vector<unsigned int> >& 
                                                          frequentLabels) const;
  std::string print(const std::set<std::vector<unsigned int> >& labelCombs) 
                                                                          const;
  std::string print(const unsigned int label = UINT_MAX);
  
  template<class T>
  std::string print(const std::vector<T>& anyVec) const {
    std::stringstream result;
    bool first = true;
    result << "<";
    for (auto it : anyVec) {
      if (!first) {
        result << ", ";
      }
      first = false;
      result << it;
    }
    result << ">";
    return result.str();
  }
  
  template<class T>
  std::string print(const std::set<T>& anySet) const {
    std::stringstream result;
    result << "{";
    bool first = true;
    for (auto it : anySet) {
      if (!first) {
        result << ", ";
      }
      first = false;
      result << it;
    }
    result << "}";
    return result.str();
  }
  
  unsigned int noTuples, minNoAtoms, maxNoAtoms;
  std::map<std::string, AggEntry> entriesMap; // only for initial insertions
  std::vector<AggEntry> entries;
  std::vector<std::string> freqLabels; // id for each frequent label
  std::map<std::string, unsigned int> labelPos; // label --> pos in freqLabels
  std::vector<NewPair<std::string, double> > results;
  double minSupp;
  Geoid *geoid;
  Relation *rel;
  NewPair<int, int> attrPos; // textual, spatial
  std::vector<std::set<std::vector<unsigned int>, compareLabelSeqs> > 
    checkedSeqs, freqSets, nonfreqSets;
  // only for fp,pos represents k, avoids repeated supp computations and results
};

struct FPNode {
  FPNode() {}
  FPNode(const unsigned int l, const unsigned int f, const unsigned int a) : 
                             label(l), frequency(f), nodeLink(0), ancestor(a) {}
  FPNode(const unsigned int l, const unsigned f, 
         const std::vector<unsigned int>& c, const unsigned int nl, 
         const unsigned int a) :
               label(l), frequency(f), children(c), nodeLink(nl), ancestor(a) {}
  
  ListExpr toListExpr(std::vector<std::string>& freqLabels) const;
  
  unsigned int label, frequency;
  std::vector<unsigned int> children; // positions of all children
  unsigned int nodeLink; // position of successor in node link; 0 means no link
  unsigned int ancestor; // pos of ancestor node
};

class FPTree {
 public:
  FPTree() {}
  FPTree(FPTree *tree) : 
    minSupp(tree->minSupp), nodes(tree->nodes), nodeLinks(tree->nodeLinks) {}
  
  ~FPTree() {}
  
  void clear() {nodes.clear(); nodeLinks.clear();}
  bool hasNodes() {return !nodes.empty();}
  bool hasNodeLinks() {return !nodeLinks.empty();}
  bool hasAggEntries() {return !agg->entries.empty();}
  unsigned int getNoNodes() {return nodes.size();}
  unsigned int getNoNodeLinks() {return nodeLinks.size();}
  bool isChildOf(unsigned int label, unsigned int pos, unsigned int& nextPos);
  void updateNodeLink(unsigned int label, unsigned int targetPos);
  void insertLabelVector(const std::vector<unsigned int>& labelsOrdered,
                         const unsigned int freq);
  void construct();
  void initialize(const double ms, RelAgg *ra);
  bool isOnePathTree();
  void sortNodeLinks(std::vector<unsigned int>& result);
  void collectPatternsFromSeq(std::vector<unsigned int>& labelSeq,
                  const unsigned int minNoAtoms, const unsigned int maxNoAtoms);
  void computeCondPatternBase(std::vector<unsigned int>& labelSeq, 
        std::vector<NewPair<std::vector<unsigned int>, unsigned int> >& result);
  FPTree* constructCondTree(
        std::vector<NewPair<std::vector<unsigned int>, unsigned int> >& condPB);
  void mineTree(std::vector<unsigned int>& initLabels, 
                const unsigned int minNoAtoms, const unsigned int maxNoAtoms);
  void retrievePatterns(const unsigned int minNoAtoms, 
                        const unsigned int maxNoAtoms);
  unsigned long long int computeNodesSize() const;
  unsigned long long int computeNodeLinksSize() const;
  
  static const std::string BasicType() {return "fptree";}
  static ListExpr Property();
  static Word In(const ListExpr typeInfo, const ListExpr instance,
                 const int errorPos, ListExpr& errorInfo, bool& correct);
  ListExpr getNodeLinksList(unsigned int label);
  static ListExpr Out(ListExpr typeInfo, Word value);
  static Word Create(const ListExpr typeInfo);
  static void Delete(const ListExpr typeInfo, Word& w);
  static bool Save(SmiRecord& valueRecord, size_t& offset,
                   const ListExpr typeInfo, Word& value);
  static bool Open(SmiRecord& valueRecord, size_t& offset,
                   const ListExpr typeInfo, Word& value);
  static void Close(const ListExpr typeInfo, Word& w);
  static Word Clone(const ListExpr typeInfo, const Word& w);
  static int SizeOfObj();
  static bool TypeCheck(ListExpr type, ListExpr& errorInfo);
  static bool checkType(ListExpr t) {return listutils::isSymbol(t,BasicType());}
  
  double minSupp;
  unsigned int minSuppCnt; // \ceil{noTuples * minSupp}
  std::vector<FPNode> nodes; // nodes[0] represents root
  std::map<unsigned int, unsigned int> nodeLinks; // pointer to 1st node of link
//   std::vector<std::string> freqLabels; // id for each frequent label
  RelAgg *agg;
};

struct GetPatternsLI {
  GetPatternsLI(Relation *r, const NewPair<int, int> ap, double ms, int mina,
                int maxa, Geoid *g, const size_t mem);
  ~GetPatternsLI();
  
  static TupleType *getTupleType();
  static Tuple *getNextResult(RelAgg& agg, TupleType *tt);
  
  
  TupleType *tupleType;
  RelAgg agg;
};

struct MineFPTreeLI {
  MineFPTreeLI(FPTree *t, int mina, int maxa);
  ~MineFPTreeLI();
  
  Tuple* getNextResult();
    
  FPTree *tree;
  unsigned int minNoAtoms, maxNoAtoms;
  TupleType *tupleType;
};

struct MatrixSq {
  MatrixSq(unsigned int s = 0) : size(s) {
    init(size);
  }
  
  ~MatrixSq() {
    clear();
  }
  
  void clear() {
    if (size > 0) {
      delete[] values;
    }
  }
  
  void init(unsigned int s) {
    size = s;
    if (size > 0) {
      values = new unsigned int[size * size];
      memset(values, 0, size * size * sizeof(unsigned int));
    }
  }
  
  unsigned int operator() (unsigned int i, unsigned int j) {
    if (i >= size || j >= size) {
      return UINT_MAX;
    }
    return values[i * size + j];
  }
  
  unsigned int* operator[] (unsigned int i) {
    if (i >= size) {
      return 0;
    }
    return values + i * size;
  }
  
  void increment(unsigned int i, unsigned int j) {
    if (i >= size || j >= size) {
      return;
    }
    values[i * size + j]++;
  }
  
  void set(unsigned int i, unsigned int j, unsigned int value) {
    if (i >= size || j >= size) {
      return;
    }
    values[i * size + j] = value;
  }
  
  std::string print() {
    std::stringstream result;
    for (unsigned int i = 0; i < size; i++) {
      for (unsigned int j = 0; j < size; j++) {
        result << values[i * size + j] << " ";
      }
      result << endl;
    }
    return result.str();
  }
  
  unsigned int size;
  unsigned int *values;
};

class ProjectedDB {
 public:
  ProjectedDB() {}
  ProjectedDB(ProjectedDB *pdb) : minSupp(pdb->minSupp) {}
  ProjectedDB(double ms, unsigned int msc, RelAgg *ra, unsigned int projSize)
                                       : minSupp(ms), minSuppCnt(msc), agg(ra) {
    projections.resize(projSize);
  }
  
  ~ProjectedDB() {}
  
  void clear();
  void initialize(const double ms, RelAgg *ra);
  void addProjections(std::vector<unsigned int>& labelSeq, unsigned int label, 
         const std::vector<unsigned int>& prefix = std::vector<unsigned int>());
  void computeSMatrix(std::vector<unsigned int>& freqLabels,
                      std::vector<NewPair<unsigned int, unsigned int> >& fPos);
  void construct();
  void minePDB(std::vector<unsigned int>& prefix, std::string& patPrefix,
               unsigned int pos,
               const unsigned int minNoAtoms, const unsigned int maxNoAtoms);
  void minePDBSMatrix(std::vector<unsigned int>& prefix, std::string& patPrefix,
                      unsigned int minNoAtoms, unsigned int maxNoAtoms);
  void retrievePatterns(const unsigned int minNoAtoms, 
                        const unsigned int maxNoAtoms);
  unsigned long long int computeProjSize() const;
  unsigned long long int computeMatrixSize() const;
  unsigned long long int computeFreqLabelPosSize() const;
  unsigned long long int computeFreqMatrixPosSize() const;
  
  static ListExpr seqToListExpr(std::vector<unsigned int>& seq);
  static ListExpr projToListExpr(std::vector<std::vector<unsigned int> >& proj);
  static const std::string BasicType() {return "projecteddb";}
  static ListExpr Property();
  static Word In(const ListExpr typeInfo, const ListExpr instance,
                 const int errorPos, ListExpr& errorInfo, bool& correct);
  static ListExpr Out(ListExpr typeInfo, Word value);
  static Word Create(const ListExpr typeInfo);
  static void Delete(const ListExpr typeInfo, Word& w);
  static bool Save(SmiRecord& valueRecord, size_t& offset,
                   const ListExpr typeInfo, Word& value);
  static bool Open(SmiRecord& valueRecord, size_t& offset,
                   const ListExpr typeInfo, Word& value);
  static void Close(const ListExpr typeInfo, Word& w);
  static Word Clone(const ListExpr typeInfo, const Word& w);
  static int SizeOfObj();
  static bool TypeCheck(ListExpr type, ListExpr& errorInfo);
  static bool checkType(ListExpr t) {return listutils::isSymbol(t,BasicType());}
  
  double minSupp;
  unsigned int minSuppCnt; // \ceil{noTuples * minSupp}
  RelAgg *agg;
  std::vector<std::vector<std::vector<unsigned int> > > projections;
//   std::vector<unsigned int> projPos; // non-empty positions inside projs
  MatrixSq smatrix;
  std::vector<unsigned int> freqLabelPos; // maps matrix pos to freqLabel
  std::vector<NewPair<unsigned int, unsigned int> > freqMatrixPos;
};

struct PrefixSpanLI {
  PrefixSpanLI(ProjectedDB *db, int mina, int maxa);
  ~PrefixSpanLI();
  
  Tuple* getNextResult();
  
  ProjectedDB *pdb;
  unsigned int minNoAtoms, maxNoAtoms;
  TupleType *tupleType;
};

class VerticalDB {
 public:
  VerticalDB() {}
  VerticalDB(VerticalDB *vdb) : minSupp(vdb->minSupp) {}
  VerticalDB(double ms, unsigned int msc, RelAgg *ra)
    : minSupp(ms), minSuppCnt(msc), agg(ra) {}
  
  ~VerticalDB() {}
  
  void clear() {}
  void initialize(const double ms, RelAgg *ra);
  void construct();
  void mineVerticalDB(std::vector<unsigned int>& prefix, std::string& patPrefix,
                      RelAgg *ra, const unsigned int minNoAtoms, 
                      const unsigned int maxNoAtoms);
  void retrievePatterns(const unsigned int minNoAtoms, 
                        const unsigned int maxNoAtoms);
  static const std::string BasicType() {return "verticaldb";}
  static ListExpr Property();
  static Word In(const ListExpr typeInfo, const ListExpr instance,
                 const int errorPos, ListExpr& errorInfo, bool& correct);
  static ListExpr Out(ListExpr typeInfo, Word value);
  static Word Create(const ListExpr typeInfo);
  static void Delete(const ListExpr typeInfo, Word& w);
  static bool Save(SmiRecord& valueRecord, size_t& offset,
                   const ListExpr typeInfo, Word& value);
  static bool Open(SmiRecord& valueRecord, size_t& offset,
                   const ListExpr typeInfo, Word& value);
  static void Close(const ListExpr typeInfo, Word& w);
  static Word Clone(const ListExpr typeInfo, const Word& w);
  static int SizeOfObj();
  static bool TypeCheck(ListExpr type, ListExpr& errorInfo);
  static bool checkType(ListExpr t) {return listutils::isSymbol(t,BasicType());}
  
  double minSupp;
  unsigned int minSuppCnt; // \ceil{noTuples * minSupp}
  RelAgg *agg;
};

struct SpadeLI {
  SpadeLI(VerticalDB *db, int mina, int maxa);
  ~SpadeLI();
  
  Tuple* getNextResult();
  
  VerticalDB *vdb;
  unsigned int minNoAtoms, maxNoAtoms;
  TupleType *tupleType;
};

/*
  Used for operator ~splitter~
 
*/
struct SplPlace {
  SplPlace() : loc(false), cat(false) {}
  
  SplPlace(const Point& l, const CcString& c) : loc(l), cat(c) {
    assert(loc.IsDefined());
  }
  ~SplPlace() {}
  
  bool operator==(const SplPlace& p) const {
    return AlmostEqual(loc, p.loc) && cat == p.cat;
  }
  
  Point loc;
  CcString cat;
};

struct SplTSPlace : SplPlace {
  SplTSPlace() : SplPlace(), inst(0.0) {}
  
  SplTSPlace(const Instant& t, const Point& l, const CcString& c) :
      SplPlace(l, c), inst(t) {
    assert(inst.IsDefined());
  }
  ~SplTSPlace() {}
  
  
  bool operator<(SplTSPlace& p) {
    return inst < p.inst;
  }
  
  std::string toString() const {
    std::stringstream str;
    str << "(" << inst << ", " << loc << ", " << cat << ")";
    return str.str();
  }
  
  bool fromList(ListExpr src) {
    if (!nl->HasLength(src, 3)) {
      return false;
    }
    if (!inst.ReadFrom(nl->First(src), false)) {
      return false;
    }
    if (!nl->HasLength(nl->Second(src), 2)) {
      return false;
    }
    if (!listutils::isNumeric(nl->First(nl->Second(src))) || 
        !listutils::isNumeric(nl->Second(nl->Second(src)))) {
      return false; 
    }
    loc.Set(true, listutils::getNumValue(nl->First(nl->Second(src))),
                  listutils::getNumValue(nl->Second(nl->Second(src))));
    cout << loc << endl;
    if (!nl->IsAtom(nl->Third(src))) {
      return false;
    }
    if (nl->AtomType(nl->Third(src)) != StringType) {
      return false;
    }
    if (listutils::isSymbolUndefined(nl->Third(src))) {
      cat.SetDefined(false);
    }
    else {
      cat.Set(true, nl->StringValue(nl->Third(src)));
    }
    cout << cat << endl;
    return true;
  }
  
  ListExpr toListExpr() const {
    cout << endl << inst << "   " << loc << "   \"" << cat.GetValue() << "\"" 
         << endl;
    return nl->ThreeElemList(inst.ToListExpr(false),
                             nl->TwoElemList(nl->RealAtom(loc.GetX()),
                                             nl->RealAtom(loc.GetY())),
                             nl->StringAtom(cat.GetValue(), true));
  }
  
  datetime::DateTime inst;
};

struct SplGSequence {
  SplGSequence(const datetime::DateTime& dur) : deltaT(dur) {}
  ~SplGSequence() {}
  
  int size() const {
    return placeSeq.size();
  }
  
  SplPlace get(const int i) const {
    assert (i >= 0 && (unsigned int)i < placeSeq.size());
    return placeSeq[i];
  }
  
  
  datetime::DateTime deltaT; // maximum transition time
  std::vector<SplPlace> placeSeq; // consider only groups of size 1
};

class SplSemTraj : public Attribute {
 public:
  SplSemTraj() {}
    
  SplSemTraj(const int dummy) : Attribute(true), tsPlaces(0) {}
   
  SplSemTraj(const datetime::DateTime& t, const Point& l, const CcString& c)
      : Attribute(true) {
    SplTSPlace p(t, l, c);
    this->clear();
    this->append(p);
  }
  
  SplSemTraj(const SplSemTraj& sst) : 
                              Attribute(sst.IsDefined()), tsPlaces(sst.size()) {
    tsPlaces.copyFrom(sst.getTSPlaces());
  }
  
  SplSemTraj& operator=(const SplSemTraj& src) {
    SetDefined(src.IsDefined());
    tsPlaces.copyFrom(src.getTSPlaces());
    return *this;
  }
  
  ~SplSemTraj() {}
  
  static const std::string BasicType() {return "splsemtraj";}
  static ListExpr Property();
  bool ReadFrom(ListExpr LE, const ListExpr typeInfo);
  ListExpr ToListExpr(ListExpr typeInfo) const;
  int Compare(const Attribute* arg) const;
  bool Adjacent(const Attribute* arg) const;
  Attribute* Clone() const;
  size_t HashValue() const;
  void CopyFrom(const Attribute* arg);
  int NumOfFLOBs() const {return 1;}
  Flob* GetFLOB(const int i) {assert(i == 0); return &tsPlaces;}
  size_t Sizeof() const;
  static bool checkType(ListExpr t) {return listutils::isSymbol(t,BasicType());}
  static bool CheckKind(ListExpr type, ListExpr& errorInfo);
  
  void clear() {
    tsPlaces.clean();
  }
  
  void append(const SplTSPlace& p) {
    tsPlaces.Append(p);
  }
  
  int size() const {
    return tsPlaces.Size();
  }
  
  DbArray<SplTSPlace> getTSPlaces() const {
    return tsPlaces;
  }
  
  bool isEmpty() const {
    return size() == 0;
  }
  
  SplTSPlace get(const int i) const {
    assert(i >= 0 && i < size());
    SplTSPlace tsp;
    tsPlaces.Get(i, tsp);
    return tsp;
  }
  
  Instant firstInst() const {
    if (isEmpty()) {
      datetime::DateTime result(datetime::instanttype);
      result.SetDefined(false);
      return result;
    }
    return get(0).inst;
  }
  
  std::string toString() const {
    std::stringstream str;
    str << "(" << endl;
    SplTSPlace tsp;
    for (int i = 0; i < size(); i++) {
      tsPlaces.Get(i, tsp);
      str << tsp.toString() << endl;
    }
    str << ")" << endl;
    return str.str();
  }
  
  void sort();
  
  void convertFromMPointMLabel(const temporalalgebra::MPoint& mp,
                               const MLabel& ml, const Geoid* geoid = 0);

 private:
  DbArray<SplTSPlace> tsPlaces;
};




}
