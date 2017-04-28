/*
----
This file is part of SECONDO.

Copyright (C) 2004-2009, University in Hagen, Faculty of Mathematics
and Computer Science, Database Systems for New Applications.

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

#pragma once

#include "AttrArray.h"
#include "Attribute.h"
#include <cstddef>
#include <cstring>
#include "NestedList.h"
#include "ReadWrite.h"
#include <string>

extern NestedList *nl;

namespace CRelAlgebra
{
  template <class T, void*(*cast)(void*) = nullptr>
  class SimpleFSAttrArrayIterator;

  /*
  Class template that allows easy implementation of ~AttrArray~s with entries
  of fixed size.

  For an example see ~LongInts~.

  */
  template <class T, void*(*cast)(void*) = nullptr>
  class SimpleFSAttrArray : public AttrArray
  {
  public:
    SimpleFSAttrArray() :
      m_count(0),
      m_size(0),
      m_capacity(0),
      m_data(nullptr),
      m_values(nullptr)
    {
    }

    SimpleFSAttrArray(Reader &source) :
      SimpleFSAttrArray(source, source.ReadOrThrow<size_t>())
    {
    }

    SimpleFSAttrArray(Reader &source, size_t rowCount) :
      m_count(rowCount),
      m_size(rowCount * sizeof(T)),
      m_capacity(rowCount),
      m_data(rowCount > 0 ? new char[m_size] : nullptr),
      m_values((T*)m_data)
    {
      const size_t size = m_size;

      if (size > 0)
      {
        source.ReadOrThrow(m_data, size);

        if (cast != nullptr)
        {
          T *value = m_values,
            *end = value + rowCount;

          while (value < end)
          {
            cast(value);

            ++value;
          }
        }
      }
    }

    virtual ~SimpleFSAttrArray()
    {
      if (m_data != nullptr)
      {
        delete[] m_data;
      }
    }

    virtual size_t GetCount() const
    {
      return m_count;
    }

    virtual size_t GetSize() const
    {
      return m_size;
    }

    virtual void Save(Writer &target, bool includeHeader = true) const
    {
      if (includeHeader)
      {
        target.WriteOrThrow(m_count);
      }

      if (m_count > 0)
      {
        target.WriteOrThrow(m_data, m_size);
      }
    }

    virtual void Append(const AttrArray &array, size_t row)
    {
      Append(((SimpleFSAttrArray<T, cast>&)array).m_values[row]);
    }

    virtual void Append(Attribute &value) = 0;

    void Append(const T &value)
    {
      size_t capacity = m_capacity,
        count = m_count++,
        size = m_size;

      char *data;
      T *values;

      if (capacity == count)
      {
        if (capacity == 0)
        {
          capacity = 1;

          data = m_data = new char[sizeof(T)];
          values = m_values = (T*)data;
        }
        else
        {
          const size_t newCapacity = capacity * 2;

          data = new char[newCapacity * sizeof(T)];
          values = (T*)data;

          char *oldData = m_data;

          memcpy(data, oldData, size);

          delete[] oldData;

          capacity = newCapacity;

          m_data = data;
          m_values = values;
        }

        m_capacity = capacity;
      }
      else
      {
        values = m_values;
      }

      new (values + count) T(value);

      m_size = size + sizeof(T);
    }

    virtual void Remove()
    {
      --m_count;
      m_size -= sizeof(T);
    }

    virtual void Clear()
    {
      m_count = 0;
      m_size = 0;
    }

    const T &GetAt(size_t index) const
    {
      return m_values[index];
    }

    const T &operator[](size_t index) const
    {
      return m_values[index];
    }

    virtual bool IsDefined(size_t row) const
    {
      return m_values[row].IsDefined();
    }

    virtual int Compare(size_t rowA, const AttrArray &arrayB, size_t rowB) const
    {
      const T &value = ((SimpleFSAttrArray<T, cast>&)arrayB).m_values[rowB];

      return m_values[rowA].Compare(value);
    }

    virtual int Compare(size_t row, Attribute &value) const
    {
      return m_values[row].Compare(value);
    }

    virtual bool Equals(size_t rowA, const AttrArray &arrayB, size_t rowB) const
    {
      const T &value = ((SimpleFSAttrArray<T, cast>&)arrayB).m_values[rowB];

      return m_values[rowA].Equals(value);
    }

    virtual bool Equals(size_t row, Attribute &value) const
    {
      return m_values[row].Equals(value);
    }

    virtual size_t GetHash(size_t row) const
    {
      return m_values[row].GetHash();
    }

    SimpleFSAttrArrayIterator<T, cast> GetIterator() const
    {
      return SimpleFSAttrArrayIterator<T, cast>(*this);
    }

    SimpleFSAttrArrayIterator<T, cast> begin() const
    {
      return GetIterator();
    }

    SimpleFSAttrArrayIterator<T, cast> end() const
    {
      return SimpleFSAttrArrayIterator<T, cast>();
    }

  private:
    friend class SimpleFSAttrArrayIterator<T, cast>;

    size_t m_count,
      m_size,
      m_capacity;

    char *m_data;

    T *m_values;

    SimpleFSAttrArray(const SimpleFSAttrArray &instance) = delete;
  };

  /*
  Iterator over the entries of a SimpleFSAttrArray<T, cast>.

  */
  template <class T, void*(*cast)(void*)>
  class SimpleFSAttrArrayIterator
  {
  public:
    SimpleFSAttrArrayIterator() :
      m_instance(nullptr),
      m_current(nullptr),
      m_end(nullptr)
    {
    }

    SimpleFSAttrArrayIterator(const SimpleFSAttrArray<T, cast> &instance) :
      m_instance(&instance),
      m_current(instance.m_values),
      m_end(m_current + instance.m_count)
    {
    }

    bool IsValid() const
    {
      return m_current < m_end;
    }

    const T &Get() const
    {
      return *m_current;
    }

    bool MoveToNext()
    {
      if (IsValid())
      {
        ++m_current;

        return IsValid();
      }

      return false;
    }

    const T &operator * ()
    {
      return Get();
    }

    SimpleFSAttrArrayIterator &operator ++ ()
    {
      MoveToNext();

      return *this;
    }

    bool operator == (const SimpleFSAttrArrayIterator &other) const
    {
      return !(*this != other);
    }

    bool operator != (const SimpleFSAttrArrayIterator &other) const
    {
      if (m_current < m_end)
      {
        if (other.m_current < other.m_end)
        {
          return m_current != other.m_current || m_instance != other.m_instance;
        }

        return true;
      }

      return other.m_current < other.m_end;
    }

  private:
    const SimpleFSAttrArray<T, cast> *m_instance;

    T *m_current,
      *m_end;
  };
}