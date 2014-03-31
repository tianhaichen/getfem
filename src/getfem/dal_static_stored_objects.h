/* -*- c++ -*- (enables emacs c++ mode) */
/*===========================================================================

Copyright (C) 2002-2012 Yves Renard

This file is a part of GETFEM++

Getfem++  is  free software;  you  can  redistribute  it  and/or modify it
under  the  terms  of the  GNU  Lesser General Public License as published
by  the  Free Software Foundation;  either version 3 of the License,  or
(at your option) any later version along with the GCC Runtime Library
Exception either version 3.1 or (at your option) any later version.
This program  is  distributed  in  the  hope  that it will be useful,  but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or  FITNESS  FOR  A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License and GCC Runtime Library Exception for more details.
You  should  have received a copy of the GNU Lesser General Public License
along  with  this program;  if not, write to the Free Software Foundation,
Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.

As a special exception, you  may use  this file  as it is a part of a free
software  library  without  restriction.  Specifically,  if   other  files
instantiate  templates  or  use macros or inline functions from this file,
or  you compile this  file  and  link  it  with other files  to produce an
executable, this file  does  not  by itself cause the resulting executable
to be covered  by the GNU Lesser General Public License.  This   exception
does not  however  invalidate  any  other  reasons why the executable file
might be covered by the GNU Lesser General Public License.

===========================================================================*/

/** @file dal_static_stored_objects.h 
@author  Yves Renard <Yves.Renard@insa-lyon.fr>
@date February 19, 2005
@brief Stores interdependent getfem objects.

Stored object :  

A type of object to be stored should derive from
dal::static_stored_object and a key should inherit from
static_stored_object_key with an overloaded "compare" method.

To store a new object, you have to test if the object is not
already stored and then call dal::add_stored_object:
@code
if (!search_stored_object(your_object_key(parameters))) {
add_stored_object(new your_object_key(parameters),
new your_object(parameters));
}
@endcode
You can add a dependency of your new object with
@code
add_dependency(pointer_on_your_object,
pointer_on_the_object_object_from_which_it_depends);
@endcode
and then your object will be automatically deleted if the second object is
deleted.
The dependency can be added within the add_stored_object call:
@code
add_stored_object(new your_object_key(parameters), 
new your_object(parameters),
dependency);
@endcode

Boost intrusive_ptr are used.
*/
#ifndef DAL_STATIC_STORED_OBJECTS_H__
#define DAL_STATIC_STORED_OBJECTS_H__

#include "dal_config.h"
#include "getfem_omp.h"
#include <algorithm>
#include "dal_singleton.h"
#include <set>
#include <list>


#include "getfem/getfem_arch_config.h"
#ifdef GETFEM_HAVE_BOOST
#include <boost/intrusive_ptr.hpp>
#else
# include <getfem_boost/intrusive_ptr.hpp>
#endif

#ifdef GETFEM_HAVE_OPENMP
#include <boost/atomic.hpp>
typedef boost::atomic<int> atomic_int;
#else
typedef int atomic_int;
#endif

#include <memory>


namespace dal {

  enum permanence { PERMANENT_STATIC_OBJECT = 0, // not deletable object
    STRONG_STATIC_OBJECT = 1,    // preferable not to delete it
    STANDARD_STATIC_OBJECT = 2,  // standard
    WEAK_STATIC_OBJECT = 3,      // delete it if necessary
    AUTODELETE_STATIC_OBJECT = 4 // automatically deleted 
    // when the last dependent object is deleted
  };


  class static_stored_object_key {
  protected :
    virtual bool compare(const static_stored_object_key &) const {
      GMM_ASSERT1(false, "This method should not be called");
    }

  public :
    bool operator < (const static_stored_object_key &o) const {
      // comparaison des noms d'objet
      if (typeid(*this).before(typeid(o))) return true;
      if (typeid(o).before(typeid(*this))) return false;
      return compare(o);
    }

    virtual ~static_stored_object_key() {}

  };


  template <typename var_type>
  class simple_key : virtual public static_stored_object_key { 
    var_type a;                                                     
  public :                                                           
    virtual bool compare(const static_stored_object_key &oo) const {
      const simple_key &o = dynamic_cast<const simple_key &>(oo);
      if (a < o.a) return true; return false; 
    }
    simple_key(var_type aa) : a(aa) {}
  };

#define DAL_SIMPLE_KEY(class_name, var_type)                         \
  struct class_name : public dal::simple_key<var_type> {	     \
  class_name(var_type aa) : dal::simple_key<var_type>(aa) {}	     \
  }

#define DAL_DOUBLE_KEY(class_name, var_type1, var_type2)	     \
  struct class_name :						     \
  public dal::simple_key<std::pair<var_type1,var_type2> > {	     \
  class_name(var_type1 aa, var_type2 bb) :			     \
  dal::simple_key<std::pair<var_type1,var_type2> >		     \
  (std::make_pair(aa,bb)) {}					     \
  }

#define DAL_TRIPLE_KEY(class_name, var_type1, var_type2, var_type3)	\
  struct class_name :							\
  public dal::simple_key<std::pair<var_type1,				\
  std::pair<var_type2,var_type3> > > { \
  class_name(var_type1 aa, var_type2 bb, var_type3 cc) :		\
  dal::simple_key<std::pair<var_type1,				\
  std::pair<var_type2, var_type3> > >					\
  (std::make_pair(aa,std::make_pair(bb,cc))) {}			\
  }

#define DAL_FOUR_KEY(class_name,var_type1,var_type2,var_type3,var_type4)\
  struct class_name : public						\
  dal::simple_key<std::pair						\
  <var_type1, std::pair<var_type2, std::pair		\
  <var_type3,var_type4> > > > {	\
  class_name(var_type1 aa, var_type2 bb, var_type3 cc,var_type4 dd) : \
  dal::simple_key<std::pair						\
  <var_type1, std::pair<var_type2,			\
  std::pair<var_type3,	\
  var_type4> > > >	\
  (std::make_pair(aa,std::make_pair(bb,std::make_pair(cc, dd)))) {} \
  }



  typedef const static_stored_object_key* pstatic_stored_object_key;

  /**
  base class for reference-counted getfem objects (via
  boost::intrusive_ptr).
  The reference-counting is thread safe
  @see dal_static_stored_objects.h
  */
  class static_stored_object 
  {
    mutable atomic_int pointer_ref_count_;
  public :
    static_stored_object(void) : pointer_ref_count_(0) {}
    static_stored_object(const static_stored_object &) : pointer_ref_count_(0) {}
    static_stored_object &operator =(const static_stored_object &) { return *this; }
    virtual ~static_stored_object() { }
    friend void intrusive_ptr_add_ref(const static_stored_object *o);
    friend void intrusive_ptr_release(const static_stored_object *o);
  };

  typedef boost::intrusive_ptr<const static_stored_object>
    pstatic_stored_object;

  template<class T> boost::intrusive_ptr<const T>
  stored_cast(pstatic_stored_object o) {
    return boost::intrusive_ptr<const T>(dynamic_cast<const T *>(o.get()));
  }


#ifdef GETFEM_HAVE_OPENMP
  inline void intrusive_ptr_add_ref(const static_stored_object * x)
  {
    x->pointer_ref_count_.fetch_add(1, boost::memory_order_relaxed);
  }

  inline void intrusive_ptr_release(const static_stored_object * x)
  {
    if (x->pointer_ref_count_.fetch_sub(1, boost::memory_order_release) == 1) 
    {
      boost::atomic_thread_fence(boost::memory_order_acquire);
      delete x;
    }
  }
#else
  inline void intrusive_ptr_add_ref(const static_stored_object *o)
  { 
  	o->pointer_ref_count_++; 
  }

  inline void intrusive_ptr_release(const static_stored_object *o)
  {    
    if (--(o->pointer_ref_count_) == 0) delete o;  	
  }
#endif

  /** Gives a pointer to an object from a key pointer. */
  pstatic_stored_object search_stored_object(pstatic_stored_object_key k);

  /** Gives a pointer to an object from a key reference. */
  inline pstatic_stored_object
    search_stored_object(const static_stored_object_key &k)
  { return search_stored_object(&k); }

  /** Test if an object is stored in local thread storage. */
  bool exists_stored_object(pstatic_stored_object o);

  /** Test if an object is stored in storage of all threads. */
  bool exists_stored_object_all_threads(pstatic_stored_object o);

  /** Add a dependency, object o1 will depend on object o2. */
  void add_dependency(pstatic_stored_object o1, pstatic_stored_object o2);

  /** remove a dependency. Return true if o2 has no more dependent object. */
  bool del_dependency(pstatic_stored_object o1, pstatic_stored_object o2);

  /** Add an object with two optional dependencies. */
  void add_stored_object(pstatic_stored_object_key k, pstatic_stored_object o,
    permanence perm = STANDARD_STATIC_OBJECT);
  inline void
    add_stored_object(pstatic_stored_object_key k, pstatic_stored_object o,
    pstatic_stored_object dep1,
    permanence perm = STANDARD_STATIC_OBJECT) {
      add_stored_object(k, o, perm);
      add_dependency(o, dep1);
  }

  inline void
    add_stored_object(pstatic_stored_object_key k, pstatic_stored_object o,
    pstatic_stored_object dep1, pstatic_stored_object dep2, 
    permanence perm = STANDARD_STATIC_OBJECT) {
      add_stored_object(k, o, perm);
      add_dependency(o, dep1);
      add_dependency(o, dep2);
  }

  inline void
    add_stored_object(pstatic_stored_object_key k, pstatic_stored_object o,
    pstatic_stored_object dep1, pstatic_stored_object dep2,
    pstatic_stored_object dep3,
    permanence perm = STANDARD_STATIC_OBJECT) {
      add_stored_object(k, o, perm);
      add_dependency(o, dep1);
      add_dependency(o, dep2);
      add_dependency(o, dep3);
  }

  inline void
    add_stored_object(pstatic_stored_object_key k, pstatic_stored_object o,
    pstatic_stored_object dep1, pstatic_stored_object dep2,
    pstatic_stored_object dep3, pstatic_stored_object dep4,
    permanence perm = STANDARD_STATIC_OBJECT) {
      add_stored_object(k, o, perm);
      add_dependency(o, dep1);
      add_dependency(o, dep2);
      add_dependency(o, dep3);
      add_dependency(o, dep4);
  }

  /** Delete an object and the object which depend on it. */
  void del_stored_object(pstatic_stored_object o, bool ignore_unstored=false);

  /** Delete all the object whose permanence is greater or equal to perm. */
  void del_stored_objects(int perm);

  /** Gives a pointer to a key of an object from its pointer. */
  pstatic_stored_object_key key_of_stored_object(pstatic_stored_object o);

  /** Show a list of stored objects (for debugging purpose). */
  void list_stored_objects(std::ostream &ost);

  /** Return the number of stored objects (for debugging purpose). */
  size_t nb_stored_objects(void);

  // Pointer to an object with the dependencies
  struct enr_static_stored_object {
    pstatic_stored_object p;
    bool valid;
    permanence perm;
    std::set<pstatic_stored_object> dependent_object;
    std::set<pstatic_stored_object> dependencies;
    enr_static_stored_object(pstatic_stored_object o, permanence perma)
      : p(o), valid(true), perm(perma) {}
    enr_static_stored_object(void)
      : p(0), valid(true), perm(STANDARD_STATIC_OBJECT) {}
  };

  // Pointer to a key with a coherent order
  struct enr_static_stored_object_key {
    pstatic_stored_object_key p;
    bool operator < (const enr_static_stored_object_key &o) const
    { return (*p) < (*(o.p)); }
    enr_static_stored_object_key(pstatic_stored_object_key o) : p(o) {}
  };

  // Storing array types
  typedef std::map<enr_static_stored_object_key, enr_static_stored_object>
    stored_object_tab;

  // Test the validity of the whole global storage
  void test_stored_objects(void);


  /** Delete a list of objects and their dependencies*/
  void del_stored_objects(std::list<pstatic_stored_object> &to_delete,
    bool ignore_unstored);

  /** delete all the specific type of stored objects*/
  template<typename OBJECT_TYPE>
  void delete_specific_type_stored_objects(bool all_thread = false)
  {
    std::list<pstatic_stored_object> delete_object_list;

    if(!all_thread){
      stored_object_tab& stored_objects
        = dal::singleton<stored_object_tab>::instance();

      stored_object_tab::iterator itb = stored_objects.begin();
      stored_object_tab::iterator ite = stored_objects.end();

      for(stored_object_tab::iterator it = itb; it != ite; ++it){
        const OBJECT_TYPE *p_object =  dal::stored_cast<OBJECT_TYPE>(it->second.p).get();
        if(p_object != 0) delete_object_list.push_back(it->second.p);
      }    
    }
    else{    
      for(int thread = 0; thread<int(getfem::num_threads());thread++){
        stored_object_tab& stored_objects
          = dal::singleton<stored_object_tab>::instance(thread);

        stored_object_tab::iterator itb = stored_objects.begin();
        stored_object_tab::iterator ite = stored_objects.end();

        for(stored_object_tab::iterator it = itb; it != ite; ++it){
          const OBJECT_TYPE *p_object =  dal::stored_cast<OBJECT_TYPE>(it->second.p).get();
          if(p_object != 0) delete_object_list.push_back(it->second.p);
        }
      }
    }

    del_stored_objects(delete_object_list, false);

  }
}

#endif /* DAL_STATIC_STORED_OBJECTS_H__ */
