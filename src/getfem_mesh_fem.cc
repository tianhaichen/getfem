// -*- c++ -*- (enables emacs c++ mode)
//========================================================================
//
// Library : GEneric Tool for Finite Elements Methods (getfem)
// File    : getfem_mesh_fem.cc : finite element methods on convex meshes.
//           
// Date    : December 21, 1999.
// Author  : Yves Renard <Yves.Renard@insa-toulouse.fr>
//
//========================================================================
//
// Copyright (C) 1999-2005 Yves Renard
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


#include <queue>
#include <dal_singleton.h>
#include <getfem_mesh_fem.h>

namespace getfem {
  
  bool intfem::operator < (const intfem &l) const {
    if (pf < l.pf) return true; if (pf > l.pf) return false; 
    if (pi < l.pi) return true;
    return false;
  }
  
  pintfem give_intfem(pfem ppf, pintegration_method ppi) {
    static pintegration_method im_none = 0; // the dummy integration method
    if (!im_none)
      im_none = getfem::int_method_descriptor("IM_NONE()");
//      if (ppf->basic_structure() != ppi->structure())
//        DAL_THROW(internal_error, 
//  		"Incompatibility between fem and integration method");
    return dal::singleton<dal::FONC_TABLE<intfem, intfem> >
      ::instance().add(intfem(ppf, ppi ? ppi : im_none));
  }
  
  dal::bit_vector mesh_fem::dof_on_set(size_type b) const {
    if (!dof_enumeration_made) this->enumerate_dof();
    dal::bit_vector res;
    if (linked_mesh().set_exists(b)) {
      for (dal::bv_visitor cv(linked_mesh().convexes_in_set(b));
	   !cv.finished(); ++cv) {
	if (linked_mesh().set_is_boundary(b)) {
	  mesh_cvf_set::face_bitset fb
	    = linked_mesh().faces_of_convex_in_set(cv, b);
	  for (unsigned f = 0;
	       f < linked_mesh().structure_of_convex(cv)->nb_faces(); ++f)
	    if (fb[f]) {
	    size_type nbb =
	      dof_structure.structure_of_convex(cv)->nb_points_of_face(f);
	    for (size_type i = 0; i < nbb; ++i) {
	      size_type n = Qdim / fem_of_element(cv)->target_dim();
	      for (size_type ll = 0; ll < n; ++ll)
	       res.add(dof_structure.ind_points_of_face_of_convex(cv,f)[i]+ll);
	    }
	  }
        }
	else {
	  size_type nbb =
	    dof_structure.structure_of_convex(cv)->nb_points();
	  for (size_type i = 0; i < nbb; ++i) {
	    size_type n = Qdim / fem_of_element(cv)->target_dim();
	    for (size_type ll = 0; ll < n; ++ll)
	      res.add(dof_structure.ind_points_of_convex(cv)[i]+ll);
	  }
	}
      }
    }
    return res;
  }
  
  void mesh_fem::receipt(const MESH_CLEAR &) { clear(); }
  void mesh_fem::receipt(const MESH_DELETE &) {
    clear(); is_valid = false;
    sup_sender(linked_mesh_->lmsg_sender());
  }
  void mesh_fem::receipt(const MESH_SUP_CONVEX &m) { 
    if (fe_convex[m.icv])
      { fe_convex[m.icv] = false; dof_enumeration_made = false; }
  }
  void mesh_fem::receipt(const MESH_SWAP_CONVEX &m) { 
    fe_convex.swap(m.icv1, m.icv2);
    f_elems.swap(m.icv1, m.icv2);
  }
   
  void mesh_fem::set_finite_element(size_type cv, pintfem pif) {
    if (pif == NULL || pif->pf == NULL ) {
      if (fe_convex.is_in(cv)) {
	fe_convex.sup(cv);
	dof_enumeration_made = false;
	touch();
      }
    }
    else {
      if (linked_mesh_->structure_of_convex(cv)->basic_structure() 
	  != pif->pf->basic_structure(cv) || 
	  (pif->pf->target_dim() != Qdim && pif->pf->target_dim() != 1))
	DAL_THROW(std::logic_error,
		  "Incompatibility between fem " << name_of_fem(pif->pf) << 
		  " and mesh element " << name_of_geometric_trans(linked_mesh_->trans_of_convex(cv)));
      if (!fe_convex.is_in(cv) || f_elems[cv] != pif) {
	fe_convex.add(cv);
	f_elems[cv] = pif;
	dof_enumeration_made = false;  
	touch();
      }
    }
  }

  void mesh_fem::set_finite_element(const dal::bit_vector &cvs, pfem ppf,
				    pintegration_method ppi) { 
    pintfem pif =  give_intfem(ppf, ppi);
    for (dal::bv_visitor cv(cvs); !cv.finished(); ++cv)
      set_finite_element(cv, pif);
  }

  void mesh_fem::set_finite_element(pfem ppf, pintegration_method ppi) { 
    set_finite_element(linked_mesh().convex_index(), ppf, ppi);
  }
  
  void mesh_fem::set_classical_finite_element(const dal::bit_vector &cvs, 
					      dim_type fem_degree, dim_type im_degree) {
    for (dal::bv_visitor cv(cvs); !cv.finished(); ++cv) {
      pfem pf = getfem::classical_fem(linked_mesh().trans_of_convex(cv), fem_degree);
      pintegration_method pim = im_degree != dim_type(-1) ? 
	getfem::classical_approx_im(linked_mesh().trans_of_convex(cv), im_degree) : 0;
      set_finite_element(cv, pf, pim);
    }
  }

  void mesh_fem::set_classical_finite_element(dim_type fem_degree, dim_type im_degree) { 
    set_classical_finite_element(linked_mesh().convex_index(), fem_degree, im_degree);
  }

  void mesh_fem::set_classical_discontinuous_finite_element(const dal::bit_vector &cvs, 
							    dim_type fem_degree, dim_type im_degree) {
    for (dal::bv_visitor cv(cvs); !cv.finished(); ++cv) {
      pfem pf = getfem::classical_discontinuous_fem(linked_mesh().trans_of_convex(cv), fem_degree);
      pintegration_method pim = im_degree != dim_type(-1) ? 
	getfem::classical_approx_im(linked_mesh().trans_of_convex(cv), im_degree) : 0;
      set_finite_element(cv, pf, pim);
    }
  }

  void mesh_fem::set_classical_discontinuous_finite_element(dim_type fem_degree, dim_type im_degree) { 
    set_classical_discontinuous_finite_element(linked_mesh().convex_index(), fem_degree, im_degree);
  }

  base_node mesh_fem::point_of_dof(size_type cv, size_type i) const {
    pfem pf = f_elems[cv]->pf;
    return linked_mesh().trans_of_convex(cv)->transform
      (pf->node_of_dof(cv, i * pf->target_dim() / Qdim),
       linked_mesh().points_of_convex(cv));
  }

  base_node mesh_fem::point_of_dof(size_type d) const { 
    if (!dof_enumeration_made) enumerate_dof();
    return point_of_dof(first_convex_of_dof(d),
			ind_in_first_convex_of_dof(d));
  }

  dim_type mesh_fem::dof_qdim(size_type d) const {
    if (!dof_enumeration_made) enumerate_dof();
    size_type tdim = f_elems[first_convex_of_dof(d)]->pf->target_dim();
    return ind_in_first_convex_of_dof(d) % (Qdim / tdim);
  }

  struct dof_comp_ { 
    dal::approx_less<scalar_type> comp;
    int operator()(const fem_dof& m, const fem_dof& n) const { 
      int d = dal::lexicographical_compare(m.P.begin(), m.P.end(),
					   n.P.begin(), n.P.end(), comp);
      if (d != 0) return d;
      return dof_description_compare(m.pnd, n.pnd);
    }
    dof_comp_(double e = 1.0E-10) : comp(e) { }
  };
  

  size_type mesh_fem::first_convex_of_dof(size_type d) const {
    for (size_type i = d; i != d - Qdim && i != size_type(-1); --i) {
      size_type j = dof_structure.first_convex_of_point(i);
      if (j != size_type(-1)) return j;
    }
    return size_type(-1);
  }

  size_type mesh_fem::ind_in_first_convex_of_dof(size_type d) const {
    for (size_type i = d; i != d - Qdim && i != size_type(-1); --i) {
      size_type j = dof_structure.first_convex_of_point(i);
      if (j != size_type(-1))
	return (dof_structure.ind_in_first_convex_of_point(i) * Qdim / f_elems[j]->pf->target_dim());
    }
    return size_type(-1);
  }

  bgeot::mesh_convex_ind_ct mesh_fem::convex_to_dof(size_type d) const {
    for (size_type i = d; i != d - Qdim && i != size_type(-1); --i) {
      size_type j = dof_structure.first_convex_of_point(i);
      if (j != size_type(-1)) return dof_structure.convex_to_point(i);
    }
    return bgeot::mesh_convex_ind_ct();
  }

  void mesh_fem::enumerate_dof(void) const {
    dal::bit_vector nn = fe_convex;
    std::queue<int> pile;
    dal::dynamic_tree_sorted<fem_dof, dof_comp_, 10> dof_sort;
    dal::bit_vector encountered_global_dof;
    dal::dynamic_array<size_type> ind_global_dof;
    std::vector<size_type> tab;
    size_type cv;
    fem_dof fd;

    cv = nn.take_first();
    dof_structure.clear();
    encountered_global_dof.clear();


    while (cv != ST_NIL) {
      /* ajout des voisins dans la pile.                                  */

      size_type nbp = linked_mesh_->nb_points_of_convex(cv);

      for (size_type i = 0; i < nbp; i++) {
	size_type ip = linked_mesh_->ind_points_of_convex(cv)[i];
	bgeot::mesh_convex_ind_ct::const_iterator 
	  it = linked_mesh_->convex_to_point(ip).begin(),
	  ite =  linked_mesh_->convex_to_point(ip).end();
	for ( ; it != ite; ++it)
	  if (nn.is_in(*it)) { nn.sup(*it); pile.push(*it); }
      }

      pfem pf = fem_of_element(cv);
      size_type nbd = pf->nb_dof(cv); 
      pdof_description andof = global_dof(pf->dim());
      tab.resize(nbd);
      for (size_type i = 0; i < nbd; i++) {
	fd.P = linked_mesh().trans_of_convex(cv)->transform
	  (pf->node_of_dof(cv, i), linked_mesh().points_of_convex(cv));
	//point_of_dof(cv,i); 
	fd.pnd = pf->dof_types()[i];
	size_type j = 0, j_old = 0;
	if (fd.pnd == andof) {
	  // cout << "detecting a specialdof\n";
	  size_type num = pf->index_of_global_dof(cv, i);
	  if (!(encountered_global_dof[num])) {
	    ind_global_dof[num] = dof_sort.add(fd);
	    encountered_global_dof[num] = true;
	  }
	  j = ind_global_dof[num];
	  tab[i] = j;
	} else if (pf->target_dim() == 1 && Qdim != 1) {
	  pdof_description paux = fd.pnd;
	  
	  for (size_type k = 0; k < Qdim; ++k) {
	    fd.pnd = to_coord_dof(paux, k);
	    
	    if (dof_linkable(fd.pnd))
	      j = dof_sort.add_norepeat(fd);
	      else
		j = dof_sort.add(fd);
	    if (k == 0)
	      tab[i] = j;
	    else if (j != j_old + 1) {
	      dof_sort.swap(j, j_old+1);
	    }
	    j_old = j;
	  }
	} else {
	  if (dof_linkable(fd.pnd))
	    j = dof_sort.add_norepeat(fd);
	  else
	    j = dof_sort.add(fd);
	  tab[i] = j;
	}
      }
      
      dof_structure.add_convex_noverif(pf->structure(cv), tab.begin(), cv);
      
      if (pile.empty()) cv = nn.take_first();
                 else { cv = pile.front(); pile.pop(); }
    }
    
    dof_enumeration_made = true;
    nb_total_dof = (dof_sort.card() == 0) ? 0 : dof_sort.index().last_true()+1;
  }

/*     void mesh_fem::enumerate_dof(void) A finir ...  */
/*   { */
     /* L'algorithme de parcours est de type avancement par front          */
     /* Le tri des ddl prend pas mal de memoire ... faire autrement ?      */
     /* on peut deja ne pas stocker les noeuds interieurs.                 */
/*     dal::int_set nn = fe_convex; */
/*     std::queue<int> pile; */
/*     dal::dynamic_tree_sorted<dof_, dof_comp_, 10 > dof_sort; */
/*     dal::dynamic_array<int> front1, front2; */

/*     int *tab = new int[10000]; */
/*     int *degree = new int[nn.last_true() + 1]; */
/*     int cv, nbp, i, ip, ncv, nbd, fd, degree_min, first_cv; */
/*     dof_ P; */

     /* 1-) On cherche le convexe de degree minimal (moins de voisins).    */

/*     degree_min = -1; */
/*     dal::int_set nm = nn; */
/*     for (cv << nm; cv >= 0; cv << nm)  il faut un iterateur sur nn.     */
/*     { */
/*       degree[cv] = 0; */
/*       nbp = linked_mesh_->nb_points_of_convex(cv); */
/*       for (i = 0; i < nbp; i++) */
/*       { */
/* 	ip = linked_mesh_->ind_point_of_convex(cv, i); */
/* 	nbd = linked_mesh_->convex_to_point(ip, tab); */
/* 	ssert(nbd < 10000); */
/* 	for (i = 0; i < nbd; i++) */
/* 	{ */
/* 	  ncv = tab[i]; */
/* 	  if (nn.is_in(ncv)) degree[cv]++;  cette strategie compte les  */
 	  /* voisins avec un poids correspondant au nombre de points      */
 	  /* communs.                                                     */
/* 	} */
/*       } */
/*       if (degree_min == -1 && degree_min > degree[cv]) */
/* 	{ degree_min = degree[cv]; first_cv = cv; } */
/*     } */

     /* 2-) construction du premier front.                                 */
/*     if (degree_min != -1) */
/*     { front1[0] = first_cv; front1[1] = -2; nn[first_cv] = false; } */
/*     else */
/*       front1[0] = -2; */

     /* 3-) iterations sur les fronts.                                    */
    
/*     dof_list.clear(); */
/*     while (front1[0] != -2) */
/*     { */
/*       i = 0;  + boucle sur les fronts.        */
/*       j = 0; */
/*       while (front1[i] >= 0) */
/*       { */
/* 	cv = front1[i++]; */
/* 	nbp = linked_mesh_->nb_points_of_convex(cv); */
/* 	nbd = linked_mesh_->convex_to_point(ip, tab); */
/* 	ssert(nbd < 10000); extraire une fonction qui ajoute les ddl d'un cv*/
/* 	for (i = 0; i < nbd; i++) */
/* 	{ */
/* 	  ncv = tab[i]; */
/* 	  if (nn.is_in(ncv)) { nn.sup(ncv); front2[j++] = ncv;  + tri  } */
/* 	} */
/*       } */
/*       if (j > 0 && front2[j-1] >= 0) front2[j++] = -1; */
/*     } */
/*   } */

  void mesh_fem::clear(void) {
    fe_convex.clear();
    dof_enumeration_made = false;
    touch();
    dof_structure.clear();
  }

  mesh_fem::mesh_fem(getfem_mesh &me, dim_type Q)
    : dof_enumeration_made(false), Qdim(Q) {
    linked_mesh_ = &me;
    this->add_dependency(me);
    add_sender(me.lmsg_sender(), *this,
	   lmsg::mask(MESH_CLEAR()) | lmsg::mask(MESH_SUP_CONVEX()) |
	   lmsg::mask(MESH_SWAP_CONVEX()) | lmsg::mask(MESH_DELETE()));
    is_valid = true;
  }

  mesh_fem::~mesh_fem() {}


  void mesh_fem::read_from_file(std::istream &ist) {
    dal::bit_vector npt;
    dal::dynamic_array<double> tmpv;
    char tmp[1024];
    bool dof_read = false;
    ist.precision(16);
    clear();
    ist.seekg(0);ist.clear();
    ftool::read_until(ist, "BEGIN MESH_FEM");

    while (true)
    {
      ist >> std::ws; ftool::get_token(ist, tmp, 1023);
      if (strcmp(tmp, "END")==0) {
	break;
      } else if (strcmp(tmp, "CONVEX")==0) {
	ftool::get_token(ist, tmp, 1023);
	size_type ic = atoi(tmp);
	if (!linked_mesh().convex_index().is_in(ic)) {
	  DAL_THROW(failure_error, "Convex " << ic <<
		    " does not exist, are you sure "
		    "that the mesh attached to this object is right one ?");
	}
	
	ftool::get_token(ist, tmp, 1023);
	getfem::pfem fem = getfem::fem_descriptor(tmp);
	if (!fem) DAL_THROW(failure_error, "could not create the FEM '" 
			    << tmp << "'");

	ftool::get_token(ist, tmp, 1023);
	getfem::pintegration_method pfi = getfem::int_method_descriptor(tmp);
	if (!pfi) DAL_THROW(failure_error,
	  "could not create the integration method '" << tmp << "'");
	
	dal::bit_vector bv; bv.add(ic);
	set_finite_element(bv, fem, pfi);
      } else if (strcmp(tmp, "BEGIN")==0) {
	ftool::get_token(ist, tmp, 1023);
	if (!strcmp(tmp,"DOF_ENUMERATION")) {
	  //cerr << "begin dof enumeration" << '\n';
	  dal::bit_vector doflst;
	  dof_structure.clear(); dof_enumeration_made = false; touch();
	  while (true) {
	    ftool::get_token(ist, tmp, 1023);
	    if (strcmp(tmp, "END")==0) { 
	      //cerr << "end of dof enumeration\n"; 
	      break;
	    }
	    size_type ic = atoi(tmp);
	    std::vector<size_type> tab;
	    if (convex_index().is_in(ic) && strlen(tmp) &&
		tmp[strlen(tmp)-1] == ':') {
	      tab.resize(nb_dof_of_element(ic));
	      //cerr << "convex " << ic << ", tab=";
	      for (size_type i=0; i < fem_of_element(ic)->nb_dof(ic); i++) {
		ist >> tab[i];
		for (size_type q=0; q < size_type(get_qdim())
		       / fem_of_element(ic)->target_dim(); ++q)
		  doflst.add(tab[i]+q);
		//cerr << tab[i] << ",";
	      }
	      //cerr << '\n';
	      dof_structure.add_convex_noverif
		(fem_of_element(ic)->structure(ic), tab.begin(), ic);
	    } else DAL_THROW(failure_error, "Missing convex or wrong number "
			     << "in dof enumeration: '" 
			     << tmp << "' [pos="
			     << std::streamoff(ist.tellg())<<"]");
	  } 
	  dof_read = true;
	  this->dof_enumeration_made = true;
	  touch();
	  this->nb_total_dof = doflst.card();
	  ist >> ftool::skip("DOF_ENUMERATION");
	} else if (strlen(tmp))
	  DAL_THROW(failure_error, "Syntax error in file at token '"
		    << tmp << "' [pos=" << std::streamoff(ist.tellg())
							  << "]");
      } else if (strcmp(tmp, "QDIM")==0) {
	if (dof_read)
	  DAL_THROW(failure_error, "Can't change QDIM after dof enumeration");
	ftool::get_token(ist, tmp, 1023);
	int q = atoi(tmp);
	if (q <= 0 || q > 250) DAL_THROW(failure_error, "invalid qdim: "<<q);
	set_qdim(q);
      } else if (strlen(tmp)) {
	DAL_THROW(failure_error, "Unexpected token '" << tmp <<
		  "' [pos=" << std::streamoff(ist.tellg()) << "]");
      } else if (ist.eof()) {
	DAL_THROW(failure_error, "Unexpected end of stream "
		  << "(missing BEGIN MESH_FEM/END MESH_FEM ?)");	
      }
    }
  }

  void mesh_fem::read_from_file(const std::string &name)
  { 
    std::ifstream o(name.c_str());
    if (!o) DAL_THROW(file_not_found_error,
		      "Mesh_fem file '" << name << "' does not exist");
    read_from_file(o);
    o.close();
  }

  void mesh_fem::write_to_file(std::ostream &ost) const
  {
    ost << '\n' << "BEGIN MESH_FEM" << '\n' << '\n';
    ost << "QDIM " << size_type(get_qdim()) << '\n';
    for (dal::bv_visitor cv(convex_index()); !cv.finished(); ++cv) {
      ost << " CONVEX " << cv;
      ost << " " << name_of_fem(fem_of_element(cv));
      ost << " " << name_of_int_method(int_method_of_element(cv));
      ost << '\n';
    }

    ost << " BEGIN DOF_ENUMERATION " << '\n';
    for (dal::bv_visitor cv(convex_index()); !cv.finished(); ++cv) {
      ost << "  " << cv << ": ";
      ref_mesh_dof_ind_ct::const_iterator it = ind_dof_of_element(cv).begin();
      while (it != ind_dof_of_element(cv).end()) {
	ost << " " << *it;
	// skip repeated dofs for "pseudo" vector elements
	for (size_type i=0;
	     i < size_type(get_qdim())/fem_of_element(cv)->target_dim(); ++i)
	  ++it;
      }
      ost << '\n';
    }
    ost << " END DOF_ENUMERATION " << '\n';
    ost << "END MESH_FEM" << '\n';
  }

  void mesh_fem::write_to_file(const std::string &name, bool with_mesh) const
  {
    std::ofstream o(name.c_str());
    if (!o)
      DAL_THROW(failure_error, "impossible to open file '" << name << "'");
    o << "% GETFEM MESH_FEM FILE " << '\n';
    o << "% GETFEM VERSION " << GETFEM_VERSION << '\n' << '\n' << '\n';
    if (with_mesh) linked_mesh().write_to_file(o);
    write_to_file(o);
    o.close();
  }
}  /* end of namespace getfem.                                             */


