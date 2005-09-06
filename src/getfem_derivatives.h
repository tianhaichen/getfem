// -*- c++ -*- (enables emacs c++ mode)
//========================================================================
//
// Library : GEneric Tool for Finite Element Methods (getfem)
// File    : getfem_derivatives.h : .
//           
// Date    : June 17, 2002.
// Authors : Yves Renard <Yves.Renard@insa-toulouse.fr>
//           Julien Pommier <Julien.Pommier@insa-toulouse.fr>
//
//========================================================================
//
// Copyright (C) 2002-2005 Yves Renard
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

/**@file getfem_derivatives.h
   @brief Compute the gradient of a field on a getfem::mesh_fem.
*/
#ifndef GETFEM_DERIVATIVES_H__
#define GETFEM_DERIVATIVES_H__

#include <getfem_mesh_fem.h>

namespace getfem
{
  /** Compute the gradient of a field on a getfem::mesh_fem.
      @param mf the source mesh_fem.
      @param U the source field.
      @param mf_target should be a lagrange discontinous element
      does not work with vectorial elements. ... to be done ...
      @param V contains on output the gradient of U, evaluated on mf_target.

      mf_target may have the same Qdim than mf, or it may
      be a scalar mesh_fem, in which case the derivatives are stored in
      the order: DxUx,DyUx,DzUx,DxUy,DyUy,...

      in any case, the size of V should be N*(mf.qdim)*(mf_target.nbdof/mf_target.qdim)
      elements (this is not checked by the function!)
  */
  template<class VECT1, class VECT2>
  void compute_gradient(const mesh_fem &mf, const mesh_fem &mf_target,
			const VECT1 &U, VECT2 &V)
  {
    typedef typename gmm::linalg_traits<VECT1>::value_type T;

    size_type N = mf.linked_mesh().dim();
    size_type qdim = mf.get_qdim();
    size_type target_qdim = mf_target.get_qdim();


    if (&mf.linked_mesh() != &mf_target.linked_mesh())
      DAL_THROW(std::invalid_argument, "meshes are different.");

    if (target_qdim != qdim && target_qdim != 1) {
      DAL_THROW(std::invalid_argument, "invalid Qdim for gradient mesh_fem");
    }

    base_matrix G;
    std::vector<T> coeff;
 
    bgeot::pgeotrans_precomp pgp = NULL;
    pfem_precomp pfp = NULL;
    pfem pf, pf_target, pf_old = NULL, pf_targetold = NULL;
    bgeot::pgeometric_trans pgt;

    for (dal::bv_visitor cv(mf.convex_index()); !cv.finished(); ++cv) {
      pf = mf.fem_of_element(cv);
      pf_target = mf_target.fem_of_element(cv);
      if (pf_target->need_G() || !(pf_target->is_lagrange()))
	DAL_THROW(std::invalid_argument, 
		  "finite element target not convenient");
      
      bgeot::vectors_to_base_matrix(G, mf.linked_mesh().points_of_convex(cv));

      pgt = mf.linked_mesh().trans_of_convex(cv);
      if (pf_targetold != pf_target) {
        pgp = bgeot::geotrans_precomp(pgt, pf_target->node_tab(cv));
      }
      pf_targetold = pf_target;

      if (pf_old != pf) {
	pfp = fem_precomp(pf, pf_target->node_tab(cv));
      }
      pf_old = pf;

      gmm::dense_matrix<T> grad(N,qdim), gradt(qdim,N);
      fem_interpolation_context ctx(pgp,pfp,0,G,cv);
      gmm::resize(coeff, mf.nb_dof_of_element(cv));
      gmm::copy(gmm::sub_vector(U, gmm::sub_index(mf.ind_dof_of_element(cv))), 
		coeff);
      for (size_type j = 0; j < pf_target->nb_dof(cv); ++j) {
	size_type dof_t = mf_target.ind_dof_of_element(cv)[j*target_qdim] * 
	  N*(qdim/target_qdim);
	ctx.set_ii(j);
	pf->interpolation_grad(ctx, coeff, gradt, qdim); //gmm::transposed(grad), (dim_type)qdim);
        gmm::copy(gmm::transposed(gradt),grad);
	std::copy(grad.begin(), grad.end(), V.begin() + dof_t);
      }
    }
  }

  /**Compute the Von-Mises stress of a field.
   */
  template <typename VEC1, typename VEC2>
  void interpolation_von_mises(const getfem::mesh_fem &mf_u, const VEC1 &U,
			       const getfem::mesh_fem &mf_vm, VEC2 &VM,
			       scalar_type mu=1) {
    assert(mf_vm.get_qdim() == 1); 
    unsigned N = mf_u.linked_mesh().dim(); assert(N == mf_u.get_qdim());
    std::vector<scalar_type> DU(mf_vm.nb_dof() * N * N);
    
    getfem::compute_gradient(mf_u, mf_vm, U, DU);
    
    scalar_type vm_min, vm_max;
    for (size_type i=0; i < mf_vm.nb_dof(); ++i) {
      VM[i] = 0;
      scalar_type sdiag = 0.;
      for (unsigned j=0; j < N; ++j) {
	sdiag += DU[i*N*N + j*N + j];
	for (unsigned k=0; k < N; ++k) {
	  scalar_type e = .5*(DU[i*N*N + j*N + k] + DU[i*N*N + k*N + j]);
	  VM[i] += e*e;	
	}
      }
      VM[i] -= 1./N * sdiag * sdiag;
      vm_min = (i == 0 ? VM[0] : std::min(vm_min, VM[i]));
      vm_max = (i == 0 ? VM[0] : std::max(vm_max, VM[i]));
    }
    cout << "Von Mises : min=" << 4*mu*mu*vm_min << ", max=" << 4*mu*mu*vm_max << "\n";
    gmm::scale(VM, 4*mu*mu);
  }

}

#endif
