/* -*- c++ -*- (enables emacs c++ mode)                                    */
/* *********************************************************************** */
/*                                                                         */
/* Library :  Basic GEOmetric Tool  (bgeot)                                */
/* File    :  bgeot_generic_solver.h : generic algorithms on linear        */
/*                                      algebra                            */
/*     									   */
/* Date : October 13, 2002.                                                */
/* Author : Yves Renard, Yves.Renard@gmm.insa-tlse.fr                      */
/*                                                                         */
/* *********************************************************************** */
/*                                                                         */
/* Copyright (C) 2001  Yves Renard.                                        */
/*                                                                         */
/* This file is a part of GETFEM++                                         */
/*                                                                         */
/* This program is free software; you can redistribute it and/or modify    */
/* it under the terms of the GNU General Public License as published by    */
/* the Free Software Foundation; version 2 of the License.                 */
/*                                                                         */
/* This program is distributed in the hope that it will be useful,         */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           */
/* GNU General Public License for more details.                            */
/*                                                                         */
/* You should have received a copy of the GNU General Public License       */
/* along with this program; if not, write to the Free Software Foundation, */
/* Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.         */
/*                                                                         */
/* *********************************************************************** */


#ifndef __BGEOT_GENERIC_SOLVER_H
#define __BGEOT_GENERIC_SOLVER_H

#include <bgeot_abstract_linalg.h>

namespace bgeot {

  /* ******************************************************************** */
  /*		conjugate gradient                           		  */
  /* (unpreconditionned, with parametrable scalar product)        	  */
  /* ******************************************************************** */
  // Inspired from I.T.L. (http://www.osl.iu.edu/research/itl)

  template <class Matrix, class Matps,  class Vector1,  class Vector2>
  int cg(const Matrix& A, Vector1& x, const Vector2& b, const Matps& PS,
	 int itemax, double residu, int noisy = 1) {
    typedef typename temporary_plain_vector<Vector1>::vector_type temp_vector;
    typename linalg_traits<Vector1>::value_type rho(0), rho_1(0),a(0), beta(0);
    temp_vector p(vect_size(x)), q(vect_size(x)), r(vect_size(x));
    int iter = 0;
    mult(A, scaled(x, -1.0), b, r);

    rho = vect_sp(PS, r, r);
    
    while (sqrt(rho) > residu) {

      if (iter == 0) copy(r, p);		  
      else { beta = rho / rho_1; add(r, scaled(p, beta), p); }

      mult(A, p, q);

      a = rho / vect_sp(PS, p, q);

      add(scaled(p, a), x);
      add(scaled(q, -a), r);

      rho_1 = rho; rho = vect_sp(PS, r, r);

      if (++iter >= itemax) return 1;
      if (noisy > 0) cout << "iter " << iter << " residu " << sqrt(rho)<< endl;
    }
    return 0;
  }

  template <class Matrix,  class Vector1, class Vector2> inline
  int cg(const Matrix& A, Vector1& x, const Vector2& b,
	 int itemax, double residu, int noisy = 1)
  { return cg(A, x, b, identity_matrix(), itemax, residu, noisy); } 
      
  /* ******************************************************************** */
  /*		Additive schwartz method                                  */
  /* ******************************************************************** */

  template <class Matrix1, class Matrix2, class Matrix3, class Vector1>
  struct schwarz_additif_matrix {
    typedef typename linalg_traits<Matrix2>::value_type value_type;
    const Matrix1 *A;
    const std::vector<Matrix2> *ml1;
    const std::vector<Matrix3> *ml2;
    const std::vector<Vector1> *cor;
    int itemax, noisy;
    double residu;
    std::vector< vsvector<value_type> > *gi;
    std::vector< vsvector<value_type> > *fi;
  };

  template <class Matrix1, class Matrix2, class Matrix3, class Vector1>
    struct linalg_traits<schwarz_additif_matrix<Matrix1,
    Matrix2, Matrix3, Vector1> > {
    typedef schwarz_additif_matrix<Matrix1,
      Matrix2, Matrix3, Vector1> this_type;
    typedef linalg_false is_reference;
    typedef abstract_matrix linalg_type;
    typedef typename linalg_traits<Vector1>::value_type value_type;
    typedef value_type reference_type;
    typedef abstract_indirect storage_type;
    typedef abstract_null_type sub_col_type;
    typedef abstract_null_type const_sub_col_type;
    typedef abstract_null_type sub_row_type;
    typedef abstract_null_type const_sub_row_type;
    typedef abstract_null_type sub_orientation;
    size_type size(const this_type &m)
      { return linalg_traits<Matrix1>::size(*(m.A));}
    size_type nrows(const this_type &m) { return mat_nrows(*(m.A)); }
    size_type ncols(const this_type &m) { return mat_ncols(*(m.A)); }
    const_sub_col_type col(const this_type &, size_type)
    { DAL_THROW(failure_error,"Rows inaccessible for this object"); }
    const_sub_row_type row(const this_type &m, size_type i)
    { DAL_THROW(failure_error,"Rows inaccessible for this object"); }
    sub_col_type col(this_type &, size_type)
    { DAL_THROW(failure_error,"Rows inaccessible for this object"); }
    sub_row_type row(this_type &m, size_type i) 
    { DAL_THROW(failure_error,"Rows inaccessible for this object"); }
    const void* origin(const this_type &v) { return &v; }
    void do_clear(this_type &v)
    { DAL_THROW(failure_error,"Clear impossible for this object"); }
  };

  template <class Matrix1, class Matrix2, class Matrix3,
    class Vector1, class Vector2, class Vector3>
  void schwarz_additif(const Matrix1 &A,
		       Vector3 &u,
		       const std::vector<Matrix2> &ml1,
		       const std::vector<Matrix3> &ml2,
		       const std::vector<Vector1> &cor,
		       const Vector2 &f,
		       int itemax,  double residu, int noisy = 1) {
    typedef typename linalg_traits<Matrix2>::value_type value_type;
    
    size_type nb_sub = ml1.size() + ml2.size();
    std::vector<vsvector<value_type> > gi(nb_sub);
    std::vector<vsvector<value_type> > fi(nb_sub);

    cout << "nb sub domains : " << nb_sub << endl;

    size_type ms = ml1.size();

    for (int i = 0; i < nb_sub; ++i) {
      size_type k = i < ms ? mat_nrows(ml1[i]) : mat_nrows(ml2[i-ms]);
      cout << "Taille du sous syst�me " << i << " = " << k << endl;
      fi[i] = gi[i] = vsvector<value_type>(k);
    }

    size_type nb_dof = f.size();
    global_to_local(f, fi, cor);

    for (int i = 0; i < nb_sub; ++i)
      cg(i < ms ? ml1[i] : ml2[i-ms], gi[i], fi[i], itemax, residu, noisy - 1);

    std::vector<value_type> g(nb_dof);
    local_to_global(g, gi, cor);
    schwarz_additif_matrix<Matrix1, Matrix2, Matrix3, Vector1> SAM;
    SAM.A = &A; SAM.ml1 = &ml1; SAM.ml2 = &ml2; SAM.cor = &cor;
    SAM.itemax = itemax; SAM.residu = residu; SAM.noisy = noisy;
    SAM.gi = &gi; SAM.fi = &fi;
    cg(SAM,  u, g,  itemax, residu, noisy - 1);
  }

  
  template <class Matrix1, class Matrix2, class Matrix3, class Vector1,
    class Vector2, class Vector3>
  void mult(const schwarz_additif_matrix<Matrix1, Matrix2, Matrix3,Vector1> &M,
	    const Vector2 &p, Vector3 &q) {

    size_type ms = (M.ml1)->size();
    mult(*(M.A), p, q);
    global_to_local(q, *(M.fi), *(M.cor));
    for (int i = 0; i < (M.ml1)->size(); ++i)
      cg((*(M.ml1))[i], (*(M.gi))[i], (*(M.fi))[i], M.itemax, M.residu,
	 M.noisy-1);

    for (int i = 0; i < (M.ml2)->size(); ++i)
      cg((*(M.ml2))[i],(*(M.gi))[i+ms],(*(M.fi))[i+ms],M.itemax, M.residu,
	 M.noisy-1);

    local_to_global(q, *(M.gi), *(M.cor));
  }

  template <class Matrix1, class Matrix2, class Matrix3, class Vector1,
    class Vector2, class Vector3, class Vector4>
  void mult(const schwarz_additif_matrix<Matrix1, Matrix2, Matrix3,Vector1> &M,
	    const Vector2 &p, const Vector3 &p2, Vector4 &q) {
    
    size_type ms = (M.ml1)->size();
    mult(*(M.A), p, q);
    global_to_local(q, *(M.fi), *(M.cor));
    for (int i = 0; i < (M.ml1)->size(); ++i)
      cg((*(M.ml1))[i], (*(M.gi))[i], (*(M.fi))[i], M.itemax, M.residu,
	 M.noisy-1);

    for (int i = 0; i < (M.ml2)->size(); ++i)
      cg((*(M.ml2))[i],(*(M.gi))[i+ms],(*(M.fi))[i+ms],M.itemax,M.residu,
	 M.noisy-1);

    local_to_global(q, *(M.gi), *(M.cor));
    add(p2, q);
  }


  template <class Vector1, class Vector2, class T>
  void global_to_local(const Vector2 &f, std::vector<vsvector<T> > &fi,
		  const std::vector<Vector1> &cor) {
    for (int i = 0; i < fi.size(); ++i) {
      typename Vector1::const_iterator it = cor[i].begin(), ite = cor[i].end();
      typename vsvector<T>::iterator it2 = fi[i].begin(), ite2 = fi[i].end();
      for (; it != ite; ++it, ++it2) *it2 = f[*it]; 
    }
  }

  template <class Vector1, class Vector2, class T>
  void local_to_global(Vector2 &f, const std::vector<vsvector<T> > &fi,
		  const std::vector<Vector1> &cor) {
    clear(f);
    for (int i = 0; i < fi.size(); ++i) {
      typename Vector1::const_iterator it = cor[i].begin(), ite = cor[i].end();
      typename vsvector<T>::const_iterator it2=fi[i].begin(), ite2=fi[i].end();
      for (; it != ite; ++it, ++it2) f[*it] += *it2; 
    }
  }
  
}


#endif //  __BGEOT_GENERIC_SOLVER_H
