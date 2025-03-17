// -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
//
// This class is heavily based on the myalloc.hpp class by Nicolai
// M. Josuttis.  http://www.josuttis.com/cppcode/allocator.html

/* The following code example is taken from the book
 * "The C++ Standard Library - A Tutorial and Reference"
 * by Nicolai M. Josuttis, Addison-Wesley, 1999
 *
 * (C) Copyright Nicolai M. Josuttis 1999.
 * Permission to copy, use, modify, sell and distribute this software
 * is granted provided this copyright notice appears in all copies.
 * This software is provided "as is" without express or implied
 * warranty, and with no claim as to its suitability for any purpose.
 */

// This class also relies heavily on the SpecificHeapAllocator
// example from Item 11 in Scott Meyers' "Effective STL"

#ifndef __HeapAlloc_h__
#define __HeapAlloc_h__

#include <malloc.h>

#define HAVELIMITS

#if defined(HAVELIMITS)
#include <limits>
#else
#include <stdint.h>
#endif

#include <iostream>

using namespace std;

template <typename T, typename HEAP>
class HeapAlloc
{
public:
    // type definitions
    typedef T        		value_type;
    typedef T*       		pointer;
    typedef const T* 		const_pointer;
    typedef T&       		reference;
    typedef const T& 		const_reference;
    typedef size_t			size_type;
    typedef ptrdiff_t		difference_type;

    // rebind allocator to type U
    template <typename U>
    struct rebind
    {
        typedef HeapAlloc<U,HEAP> other;
    };

    // return address of values
    pointer address(reference value) const
    {
        return &value;
    }
    const_pointer address(const_reference value) const
    {
        return &value;
    }

    /* constructors and destructor
     * - nothing to do because the allocator has no state
     */
    HeapAlloc() throw()
    {
    }

    HeapAlloc(const HeapAlloc&) throw()
    {
    }

#if !defined(WIN32)
    template <typename U>
    HeapAlloc(const HeapAlloc<U,HEAP> &) throw()
    {
    }
#endif

    ~HeapAlloc() throw()
    {
    }

    // return maximum number of elements that can be allocated
    size_type max_size() const throw()
    {
#if defined(HAVELIMITS)
        return std::numeric_limits<size_t>::max() / sizeof(T);
#else
        return size_t(-1) / sizeof(T);
#endif
    }

    // allocate but don't initialize num elements of type T
    pointer allocate(size_type num, const void* = 0)
    {
        pointer ret = static_cast<pointer>(HEAP::alloc(num*sizeof(T)));
        return ret;
    }

#if defined(WIN32)
    char * _Charalloc(size_type sz)
    {
        return static_cast<char *>(HEAP::alloc(sz));
    }
#endif

    // initialize elements of allocated storage p with value value
    void construct(pointer p, const T& value)
    {
        // initialize memory with placement new
        new((void*)p)T(value);
    }

    // destroy elements of initialized storage p
    void destroy(pointer p)
    {
        // destroy objects by calling their destructor
        p->~T();
    }

    // deallocate storage p of deleted elements
    void deallocate(pointer p, size_type num)
    {
        HEAP::dealloc(p);
    }

    // deallocate storage p of deleted elements
    void deallocate(void * p, size_type num)
    {
        HEAP::dealloc(p);
    }
};

// return that all specializations of this allocator are interchangeable
template <typename T1, typename T2, typename HEAP>
bool operator==(const HeapAlloc<T1,HEAP>&,
                const HeapAlloc<T2,HEAP>&) throw()
{
    return true;
}
template <typename T1, typename T2, typename HEAP>
bool operator!=(const HeapAlloc<T1,HEAP>&,
                const HeapAlloc<T2,HEAP>&) throw()
{
    return false;
}

#endif // __HeapAlloc_h__
