// -*- c++ -*- (enables emacs c++ mode)
//========================================================================
//
// Library : Dynamic Array Library (dal)
// File    : dal_fonc_tables.h : deals fonctionalities tables, reservation
//           search ...
// Date    : August 28, 2001
// Author  : Yves Renard <Yves.Renard@insa-toulouse.fr>
//
//========================================================================
//
// Copyright (C) 2001-2005 Yves Renard
//
// This file is a part of GETFEM++
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; version 2 of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
//========================================================================

#ifndef DAL_FONC_TABLES_H__
#define DAL_FONC_TABLES_H__

#include <dal_tree_sorted.h>

namespace dal
{
  /* ********************************************************************* */
  /* Assumptions :                                                         */
  /*   1 - class LIGHT as a default comparator <.                          */
  /*   2 - class DESC as a constructor with a class LIGHT.                 */
  /* ********************************************************************* */
  
  template<class LIGHT, class DESC> class FONC_TABLE {
  public :
    
    typedef DESC * pDESC;
    typedef typename dynamic_tree_sorted<LIGHT>::size_type size_type;
    typedef dynamic_array<pDESC, 2> desc_table_type;
    
  protected :
    
    dynamic_tree_sorted<LIGHT> light_table_;
    desc_table_type desc_table;
    
  public :
    
    size_type search(const LIGHT &l) const { return light_table_.search(l); }
    
    pDESC add(const LIGHT &l) {
      size_type i = light_table_.search(l);
      if (i == size_type(-1))
	{ i = light_table_.add(l); desc_table[i] = new DESC(l); }
      return desc_table[i];
    }
    void sup(const LIGHT &l) {
      size_type i = light_table_.search(l);
      if (i != size_type(-1))
	{ light_table_.sup(i); delete desc_table[i]; desc_table[i] = 0;}
    }
    const desc_table_type &table(void) const { return desc_table; }
    const dynamic_tree_sorted<LIGHT> &light_table(void) const
      { return light_table_; }
    const bit_vector &index(void) const { return light_table_.index(); }
    ~FONC_TABLE(void) { 
      for (dal::bv_visitor i(light_table_.index()); !i.finished(); ++i) delete desc_table[i];
    }
  };
  
}

#endif /* DAL_FONC_TABLES_H__ */
