#ifndef FILE_ARRAY
#define FILE_ARRAY


#include <string.h>

/**************************************************************************/
/* File:   array.hpp                                                      */
/* Author: Joachim Schoeberl                                              */
/* Date:   01. Jun. 95                                                    */
/**************************************************************************/


/**
   A simple array container.
   Array represented by size and data-pointer.
   No memory allocation and deallocation, must be provided by user.
   Helper functions for printing. 
   Optional range check by macro RANGE_CHECK
 */

template <class T, int BASE = 0>
class FlatArray
{
protected:
  /// the size
  int size;
  /// the data
  T * data;
public:

  /// provide size and memory
  inline FlatArray (int asize, T * adata) 
    : size(asize), data(adata) { ; }

  /// the size
  inline int Size() const { return size; }


  /// access array. 
  inline T & operator[] (int i) 
  { 
#ifdef DEBUG
    if (i-BASE < 0 || i-BASE >= size)
      cout << "array<" << typeid(T).name() << "> out of range, i = " << i << ", s = " << size << endl;
#endif

    return data[i-BASE]; 
  }


  /// Access array. 
  inline const T & operator[] (int i) const
  {
#ifdef DEBUG
    if (i-BASE < 0 || i-BASE >= size)
      cout << "array<" << typeid(T).name() << "> out of range, i = " << i << ", s = " << size << endl;
#endif

    return data[i-BASE]; 
  }

  ///
  T & Elem (int i)
  {
#ifdef DEBUG
    if (i < 1 || i > size)
      cout << "ARRAY<" << typeid(T).name() 
	   << ">::Elem out of range, i = " << i
	   << ", s = " << size << endl;
#endif

    return ((T*)data)[i-1]; 
  }
  
  ///
  const T & Get (int i) const 
  {
#ifdef DEBUG
    if (i < 1 || i > size)
      cout << "ARRAY<" << typeid(T).name() << ">::Get out of range, i = " << i
	   << ", s = " << size << endl;
#endif

    return ((const T*)data)[i-1]; 
  }

  ///
  void Set (int i, const T & el)
  { 
#ifdef DEBUG
    if (i < 1 || i > size)
      cout << "ARRAY<" << typeid(T).name() << ">::Set out of range, i = " << i
	   << ", s = " << size << endl;
#endif

    ((T*)data)[i-1] = el; 
  }


  /// access last element. check by macro CHECK_RANGE
  T & Last ()
  {
    return data[size-1];
  }

  /// access last element. check by macro CHECK_RANGE
  const T & Last () const
  {
    return data[size-1];
  }

  /// Fill array with value val
  FlatArray & operator= (const T & val)
  {
    for (int i = 0; i < size; i++)
      data[i] = val;
    return *this;
  }
};




// print array
template <class T, int BASE>
inline std::ostream & operator<< (std::ostream & s, const FlatArray<T,BASE> & a)
{
  for (int i = BASE; i < a.Size()+BASE; i++)
    s << i << ": " << a[i] << std::endl;
  return s;
}




/** 
   Dynamic array container.
   
   ARRAY<T> is an automatically increasing array container.
   The allocated memory doubles on overflow. 
   Either the container takes care of memory allocation and deallocation,
   or the user provides one block of data.
*/
template <class T, int BASE = 0> 
class ARRAY : public FlatArray<T, BASE>
{
protected:
  /// physical size of array
  int allocsize;
  /// memory is responsibility of container
  bool ownmem;

public:

  /// Generate array of logical and physical size asize
  explicit ARRAY(int asize = 0)
    : FlatArray<T, BASE> (asize, asize ? new T[asize] : 0)
  {
    allocsize = asize; 
    ownmem = 1;
  }

  /// Generate array in user data
  ARRAY(int asize, T* adata)
    : FlatArray<T, BASE> (asize, adata)
  {
    allocsize = asize; 
    ownmem = 0;
  }

  /// array copy 
  explicit ARRAY (const ARRAY<T> & a2)
    : FlatArray<T, BASE> (a2.Size(), a2.Size() ? new T[a2.Size()] : 0)
  {
    allocsize = this->size;
    ownmem = 1;
    for (int i = BASE; i < this->size+BASE; i++)
      (*this)[i] = a2[i];
  }



  /// if responsible, deletes memory
  ~ARRAY()
  {
    if (ownmem)
      delete [] this->data;
  }

  /// Change logical size. If necessary, do reallocation. Keeps contents.
  void SetSize(int nsize)
  {
    if (nsize > allocsize) 
      ReSize (nsize);
    this->size = nsize; 
  }

  /// Change physical size. Keeps logical size. Keeps contents.
  void SetAllocSize (int nallocsize)
  {
    if (nallocsize > allocsize)
      ReSize (nallocsize);
  }


  /// Add element at end of array. reallocation if necessary.
  int Append (const T & el)
  {
    if (this->size == allocsize) 
      ReSize (this->size+1);
    this->data[this->size] = el;
    this->size++;
    return this->size;
  }


  /// Delete element i (0-based). Move last element to position i.
  void Delete (int i)
  {
#ifdef CHECK_ARRAY_RANGE
    RangeCheck (i+1);
#endif

    this->data[i] = this->data[this->size-1];
    this->size--;
    //    DeleteElement (i+1);
  }


  /// Delete element i (1-based). Move last element to position i.
  void DeleteElement (int i)
  {
#ifdef CHECK_ARRAY_RANGE
    RangeCheck (i);
#endif

    this->data[i-1] = this->data[this->size-1];
    this->size--;
  }

  /// Delete last element. 
  void DeleteLast ()
  {
    this->size--;
  }

  /// Deallocate memory
  void DeleteAll ()
  {
    if (ownmem)
      delete [] this->data;
    this->data = 0;
    this->size = allocsize = 0;
  }

  /// Fill array with val
  ARRAY & operator= (const T & val)
  {
    FlatArray<T, BASE>::operator= (val);
    return *this;
  }

  /// array copy
  ARRAY & operator= (const ARRAY & a2)
  {
    SetSize (a2.Size());
    for (int i = BASE; i < this->size+BASE; i++)
      (*this)[i] = a2[i];
    return *this;
  }

private:

  /// resize array, at least to size minsize. copy contents
  void ReSize (int minsize)
  {
    int nsize = 2 * allocsize;
    if (nsize < minsize) nsize = minsize;

    if (this->data)
      {
	T * p = new T[nsize];
	
	int mins = (nsize < this->size) ? nsize : this->size; 
	memcpy (p, this->data, mins * sizeof(T));

	if (ownmem)
	  delete [] this->data;
	ownmem = 1;
	this->data = p;
      }
    else
      {
	this->data = new T[nsize];
	ownmem = 1;
      }
    
    allocsize = nsize;
  }
};



template <class T, int S> 
class ArrayMem : public ARRAY<T>
{
  // T mem[S];
  // char mem[S*sizeof(T)];
  double mem[(S*sizeof(T)+7) / 8];
public:
  /// Generate array of logical and physical size asize
  explicit ArrayMem(int asize = 0)
    : ARRAY<T> (S, static_cast<T*> (static_cast<void*>(&mem[0])))
  {
    this->SetSize (asize);
  }

  ArrayMem & operator= (const T & val)  
  {
    ARRAY<T>::operator= (val);
    return *this;
  }
};











///
template <class T> class MoveableArray 
{
  int size;
  int allocsize;
  MoveableMem<T> data;

public:

  MoveableArray()
  { 
    size = allocsize = 0; 
    data.SetName ("MoveableArray");
  }

  MoveableArray(int asize)
    : size(asize), allocsize(asize), data(asize)
  { ; }
  
  ~MoveableArray () { ; }

  int Size() const { return size; }

  void SetSize(int nsize)
  {
    if (nsize > allocsize) 
      {
	data.ReAlloc (nsize);
	allocsize = nsize;
      }
    size = nsize;
  }

  void SetAllocSize (int nallocsize)
  {
    data.ReAlloc (nallocsize);
    allocsize = nallocsize;
  }

  ///
  T & operator[] (int i)
  { return ((T*)data)[i]; }

  ///
  const T & operator[] (int i) const
  { return ((const T*)data)[i]; }

  ///
  T & Elem (int i)
  { return ((T*)data)[i-1]; }
  
  ///
  const T & Get (int i) const 
  { return ((const T*)data)[i-1]; }

  ///
  void Set (int i, const T & el)
  { ((T*)data)[i-1] = el; }

  ///
  T & Last ()
  { return ((T*)data)[size-1]; }
  
  ///
  const T & Last () const
  { return ((const T*)data)[size-1]; }
  
  ///
  int Append (const T & el)
  {
    if (size == allocsize) 
      {
	SetAllocSize (2*allocsize+1);
      }
    ((T*)data)[size] = el;
    size++;
    return size;
  }
  
  ///
  void Delete (int i)
  {
    DeleteElement (i+1);
  }

  ///
  void DeleteElement (int i)
  {
    ((T*)data)[i-1] = ((T*)data)[size-1];
    size--;
  }
  
  ///
  void DeleteLast ()
  { size--; }

  ///
  void DeleteAll ()
  {
    size = allocsize = 0;
    data.Free();
  }

  ///
  void PrintMemInfo (std::ostream & ost) const
  {
    ost << Size() << " elements of size " << sizeof(T) << " = " 
	<< Size() * sizeof(T) << std::endl;
  }

  MoveableArray & operator= (const T & el)
  {
    for (int i = 0; i < size; i++)
      ((T*)data)[i] = el;
    return *this;
  }

  void SetName (const char * aname)
  {
    data.SetName(aname);
  }
private:
  ///
  MoveableArray & operator= (MoveableArray &);
  ///
  MoveableArray (const MoveableArray &);
};


template <class T>
inline std::ostream & operator<< (std::ostream & ost, MoveableArray<T> & a)
{
  for (int i = 0; i < a.Size(); i++)
    ost << i << ": " << a[i] << std::endl;
  return ost;
}








#endif

