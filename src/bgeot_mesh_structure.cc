// -*- c++ -*- (enables emacs c++ mode)
//========================================================================
//
// Library : Basic GEOmetric Tool  (bgeot)
// File    : bgeot_mesh_structure.cc : mesh structures.
//           
// Date    : November 5, 1999.
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



#include <bgeot_mesh_structure.h>

namespace bgeot
{
  mc_const_iterator::mc_const_iterator(const mesh_structure &ms,
				       size_type ip) { 
    p = &ms; const mesh_point *q = &(p->point_structures()[ip]);
    ind_cv = q->first; ind_in_cv = q->ind_in_first;
    pc = &(p->convex()[ind_cv]); 
  }
	
  mc_const_iterator &mc_const_iterator::operator ++() { 
    const mesh_point_link *q = &(p->links()[pc->pts+ind_in_cv]);
    ind_cv = q->next; ind_in_cv = q->ind_in_next;
    pc = &(p->convex()[ind_cv]); return *this;
  }
  
  dal::bit_vector mesh_structure::convex_index(dim_type n) const {
    dal::bit_vector res = convex_tab.index();
    for (dal::bv_visitor cv(convex_tab.index()); !cv.finished(); ++cv)
      if (structure_of_convex(cv)->dim() != n) res.sup(cv);
    return res;
  }

  size_type mesh_structure::local_ind_of_convex_point(size_type ic,
						      size_type ip) const {
    ref_mesh_point_ind_ct ct = ind_points_of_convex(ic);

    /* we can't use it.index() here */
    size_type ind = 0;
    ref_mesh_point_ind_ct::const_iterator it = ct.begin();
    for (; it != ct.end() && (*it) != ip; ++it) ind++;
    if (it == ct.end())
      DAL_THROW(internal_error, "This point does not exist on this convex.");
    return ind;
  }
    

  ind_ref_mesh_point_ind_ct
     mesh_structure::ind_points_of_face_of_convex(size_type ic,
					      short_type iff) const {
    mesh_point_ind_ct::const_iterator r = point_lists.begin();
    const mesh_convex_structure *q = &(convex_tab[ic]); r += q->pts;
    const convex_ind_ct *p = &(q->cstruct->ind_points_of_face(iff));
    return ind_ref_mesh_point_ind_ct(r, p->begin(), p->end());  
  }

  ref_mesh_point_ind_ct
  mesh_structure::ind_points_of_convex(size_type ic) const {
    const mesh_convex_structure *q = &(convex_tab[ic]);
    mesh_point_ind_ct::const_iterator r = point_lists.begin();
    r += q->pts;
    return ref_mesh_point_ind_ct(r, r + q->cstruct->nb_points());
  }

  size_type mesh_structure::memsize(void) const {
    return sizeof(mesh_structure) + points_tab.memsize()
      + convex_tab.memsize() + point_lists.memsize()
      + point_links.memsize();
  }

  void mesh_structure::swap_points(size_type i, size_type j) {
    if (i != j) {
      mesh_convex_ind_ct ct = convex_to_point(i);
      for ( ; !ct.empty(); ct.pop_front())
	point_lists[ct.begin().pc->pts + ct.begin().ind_in_cv] = j;
      ct = convex_to_point(j);
      for ( ; !ct.empty(); ct.pop_front())
	point_lists[ct.begin().pc->pts + ct.begin().ind_in_cv] = i;
      points_tab.swap(i,j);
    }			 
  }

  void mesh_structure::sup_convex(size_type ic) {
    // cout << "sup convex" << ic << " !! " << endl;
    mesh_convex_structure *p = &(convex_tab[ic]);
    short_type nb = p->cstruct->nb_points();
    mesh_link_ct::iterator ipl = point_links.begin(); ipl += p->pts;
    mesh_point_ind_ct::const_iterator ipt = point_lists.begin(); ipt += p->pts;
    for (short_type i = 0; i < nb; ++i, ++ipl, ++ipt)
    {
      mesh_point *os = &(points_tab[*ipt]);
      mesh_convex_ind_ct::iterator b = convex_to_point(*ipt).begin(), c = b;
      if (*b == ic)
      { os->first = (*ipl).next; os->ind_in_first = (*ipl).ind_in_next; }
      else
      {
	++b; while (*b != ic) { c = b; ++b; }
	mesh_point_link *ssp = &(point_links[c.pc->pts + c.ind_in_cv]);
	ssp->next = (*ipl).next; ssp->ind_in_next = (*ipl).ind_in_next;
      }
    }
    point_links.free(p->pts, nb); convex_tab.sup(ic);
  }

  mesh_point_search_ind_ct
    mesh_structure::ind_points_to_point(size_type ip) const
  { // pas tres efficace.
    mesh_point_search_ind_ct r;
    dal::bit_vector nn;
    mesh_convex_ind_ct ct = convex_to_point(ip);
    for ( ; !ct.empty(); ct.pop_front())
    {
      ref_mesh_point_ind_ct pt = ind_points_of_convex(ct.front());
      for ( ; !pt.empty(); pt.pop_front())
      { if (ip != pt.front()) nn.add(pt.front()); }
    }
    r.clear(); r.reserve(nn.card());
    for (dal::bv_visitor j(nn); !j.finished(); ++j) r.push_back(j);
    return r;
  }

  size_type mesh_structure::add_face_of_convex(size_type ic, short_type f)
  {
    return add_convex( (structure_of_convex(ic)->faces_structure())[f],
		  ind_points_of_face_of_convex(ic, f).begin());
  }

  void mesh_structure::add_faces_of_convex(size_type ic)
  { // a refaire
    // cout << "debut de add_faces(" << ic << ")" << endl;
    // write_to_file(cout); 
    pconvex_structure ps = structure_of_convex(ic);
    for (short_type iff = 0; iff < ps->nb_faces(); ++iff)
      add_convex( (ps->faces_structure())[iff],
		  ind_points_of_face_of_convex(ic, iff).begin());
    // cout << "fin de add_faces " << endl;
    // write_to_file(cout); getchar();
  }
    
  void mesh_structure::to_faces(dim_type n)
  { // a refaire
    // cout << "debut de to_faces(" << int(n) << ")" << endl;
    mesh_convex_ct::tas_iterator b = convex_tab.tas_begin(),
                                 e = convex_tab.tas_end();
    for ( ; b != e; ++b)
      if ((*b).cstruct->dim() == n)
	{ add_faces_of_convex(b.index()); sup_convex(b.index()); }
    // cout << "fin de to_faces " << endl;
    // write_to_file(cout); getchar();
  }

  void mesh_structure::to_edges(void)
  { // a refaire
    dim_type dmax = 0;
    mesh_convex_ct::tas_iterator b = convex_tab.tas_begin(),
                                 e = convex_tab.tas_end();
    for ( ; b != e; ++b) dmax = std::max(dmax, (*b).cstruct->dim());
    for ( ; dmax > 1; --dmax) to_faces(dmax);
  }

  size_type mesh_structure::add_segment(size_type a, size_type b) {
    static pconvex_structure cs = NULL;
    if (!cs) cs = simplex_structure(1);
    size_type t[2]; t[0] = a; t[1] = b;
    return add_convex(cs, &t[0]);
  }

  void mesh_structure::swap_convex(size_type i, size_type j)
  {
    size_type ip, ic, *p;
    dal::bit_vector nn;

    if (i != j) {
      if (convex_is_valid(i)) {
	nn.clear();
	ref_mesh_point_ind_ct pt = ind_points_of_convex(i);
	for( ; !pt.empty(); pt.pop_front()) {
	  ip = pt.front(); p = &(points_tab[ip].first);
	  if (!(nn[ip])) {
	    nn[ip] = true;
	    mesh_convex_ind_ct ct = convex_to_point(ip);
	    
	    for( ; !ct.empty(); ct.pop_front()) {
	      ic = ct.front();
	      if (*p == i) *p = j; else if (*p == j) *p = i;
	      p = &(point_links[convex_tab[ic].pts+ct.begin().ind_in_cv].next);
	    }
	  }
	}
      }
      if (convex_is_valid(j)) {
	nn.clear();
	ref_mesh_point_ind_ct pt = ind_points_of_convex(j);
	for( ; !pt.empty(); pt.pop_front()) {
	  ip = pt.front();
	  if (!(nn[ip])) {
	    nn[ip] = true;
	    if (!convex_is_valid(i) || !is_convex_has_points( i, 1, &ip)) {
	      p = &(points_tab[ip].first);
	      mesh_convex_ind_ct ct = convex_to_point(ip);
	      
	      for( ; !ct.empty(); ct.pop_front()) {
		ic = ct.front();
		if (*p == i) *p = j; else if (*p == j) *p = i;
		p=&(point_links[convex_tab[ic].pts+ct.begin().ind_in_cv].next);
	      }
	    }
	  }
	}
      }
      convex_tab.swap(i,j);
    }
  }

  void mesh_structure::clear(void)
  { 
    points_tab.clear(); convex_tab.clear();
    point_links.clear(); point_lists.clear();
  }

  void mesh_structure::optimize_structure()
  {
    size_type i, j;
    j = nb_convex();
    for (i = 0; i < j; i++)
      if (!convex_tab.index_valid(i))
	swap_convex(i, convex_tab.ind_last());
    if (points_tab.size())
      for (i = 0, j = (points_tab.end()-points_tab.begin())-1; i < j; ++i, --j)
	{
	  while (i < j && points_tab[i].first != ST_NIL) ++i;
	  while (i < j && points_tab[j].first == ST_NIL) --j;
	  if (i < j) swap_points(i, j);
	}
  }

  void mesh_structure::stat(void)
  {
    cout << "mesh structure with " << nb_convex() << " valid convex, "
	 << "for a total memory size of " << memsize() << " bytes.\n";
  }

  mesh_over_convex_ind_ct over_convex(const mesh_structure &ms, size_type ic)
  {
    ref_mesh_point_ind_ct pt = ms.ind_points_of_convex(ic);
    mesh_convex_ind_ct ct = ms.convex_to_point(pt[0]);
    return mesh_over_convex_ind_ct(ct.begin(), ct.end(),
       mesh_convex_has_points<ref_mesh_point_ind_ct::const_iterator>
       (ms.structure_of_convex(ic)->nb_points() - 1, pt.begin() + 1,
	ms.structure_of_convex(ic)->dim()+1));
  }

  mesh_face_convex_ind_ct face_of_convex(const mesh_structure &ms, 
					 size_type ic, short_type iff)
  {
    pconvex_structure q = ms.structure_of_convex(ic);
    short_type n = q->nb_points_of_face(iff);
    ind_ref_mesh_point_ind_ct pt = ms.ind_points_of_face_of_convex(ic, iff);
    mesh_convex_ind_ct ct = ms.convex_to_point(pt[0]);
    return mesh_face_convex_ind_ct(ct.begin(), ct.end(), mesh_convex_has_points<ind_ref_mesh_point_ind_ct::const_iterator>(n-1, ++(pt.begin()), q->dim()-1, true));
  }

  mesh_face_convex_ind_ct neighbour_of_convex(const mesh_structure &ms, 
					  size_type ic, short_type iff)
  {
    pconvex_structure q = ms.structure_of_convex(ic);
    short_type n = q->nb_points_of_face(iff);
    ind_ref_mesh_point_ind_ct pt = ms.ind_points_of_face_of_convex(ic, iff);
    mesh_convex_ind_ct ct = ms.convex_to_point(pt[0]);
    return mesh_face_convex_ind_ct(ct.begin(), ct.end(), mesh_convex_has_points<ind_ref_mesh_point_ind_ct::const_iterator>(n-1, ++(pt.begin()), q->dim(), false, ic));
  }



  /* ********************************************************************* */
  /*                                                                       */
  /*  Gives the list of edges of a convex face.                            */
  /*                                                                       */
  /* ********************************************************************* */

  void mesh_edge_list_convex(pconvex_structure cvs, std::vector<size_type> points_of_convex, 
                             size_type cv_id, edge_list &el, bool merge_convex)
  { // a tester ... optimisable.
    size_type n = cvs->dim();
    size_type nbp = cvs->nb_points();
    size_type ncv = merge_convex ? 0 : cv_id;

    if (nbp == n+1 && cvs == simplex_structure(n)) {
      for (dim_type k = 0; k < n; ++k)
	for (dim_type l = k+1; l <= n; ++l)
	  el.add(edge_list_elt(points_of_convex[k],
			       points_of_convex[l], ncv));
    }
    else if (nbp == (size_type(1) << n) 
	     && cvs == parallelepiped_structure(n)) {
      for (size_type k = 0; k < (size_type(1) << n); ++k)
	for (dim_type j = 0; j < n; ++j)
	  if ((k & (1 << j)) == 0)
	    el.add(edge_list_elt(points_of_convex[k],
			      points_of_convex[k | (1 << j)], ncv));
    }
    else if (nbp == 2 * n && cvs == prism_structure(n)) {
      for (dim_type k = 0; k < n - 1; ++k)
	for (dim_type l = k+1; l < n; ++l) {
	  el.add(edge_list_elt(points_of_convex[k],
			       points_of_convex[l], ncv));
	  el.add(edge_list_elt(points_of_convex[k+n],
			       points_of_convex[l+n], ncv));
	}
      for (dim_type k = 0; k < n; ++k)
	el.add(edge_list_elt(points_of_convex[k],
			     points_of_convex[k+n], ncv));
    }
    else {
      dal::dynamic_array<pconvex_structure> cvstab;
      dal::dynamic_array< std::vector<size_type> > indpttab;
      size_type ncs = 1;
      cvstab[0] = cvs;
      indpttab[0].resize(cvstab[0]->nb_points());
      std::copy(points_of_convex.begin(),
		points_of_convex.end(), indpttab[0].begin());

      /* pseudo recursive decomposition of the initial convex into
	 face of face of ... of face of convex , cvstab and indpttab
	 being the "stack" of convexes to treat
      */
      while (ncs != 0) {
	ncs--;
	cvs = cvstab[ncs];
	std::vector< size_type > pts = indpttab[ncs];
	if (cvs->dim() == 1) { // il faudrait �tendre aux autres cas classiques.
	  
	  for (size_type j = 1; j < cvs->nb_points(); ++j) {
	    //cerr << "ncs=" << ncs << "j=" << j << ", ajout de " << (indpttab[ncs])[j-1] << "," << (indpttab[ncs])[j] << endl;
	    el.add(edge_list_elt((indpttab[ncs])[j-1],(indpttab[ncs])[j],ncv));
	  }
	}
	else {
	  size_type nf = cvs->nb_faces();
	  //cerr << "ncs = " << ncs << ",cvs->dim=" << int(cvs->dim()) << ", nb_faces=" << nf << endl;
	  for (size_type f = 0; f < nf; ++f) {
	    cvstab[ncs+f] = (cvs->faces_structure())[f];
	    indpttab[ncs+f].resize(cvs->nb_points_of_face(f));
	    /*
	    cerr << "   -> f=" << f << ", cvs->nb_points_of_face(f)=" << 
	      cvs->nb_points_of_face(f) << "=[";
	    for (size_type k = 0; k < cvs->nb_points_of_face(f); ++k) 
	      cerr << (std::string(k==0?"":",")) 
		   << int(cvs->ind_points_of_face(f)[k]) << "{" << pts[cvs->ind_points_of_face(f)[k]] << "}";
	    cerr << "]" << endl;
	    */
	    for (size_type k = 0; k < cvs->nb_points_of_face(f); ++k)
	      (indpttab[ncs+f])[k] = pts[cvs->ind_points_of_face(f)[k]];
	  }
	  //	  cvstab[ncs] = cvstab[ncs + nf - 1];
	  //indpttab[ncs] = indpttab[ncs + nf - 1];
	  ncs += nf;
	  //cerr << "on empile les " << nf << " faces, -> ncs=" << ncs << endl;
	}
      }
    }
  }
    

  void mesh_edge_list(const mesh_structure &m, edge_list &el, 
		       bool merge_convex)
  {
    std::vector<size_type> p;
    for (dal::bv_visitor cv(m.convex_index()); !cv.finished(); ++cv) {
      p.resize(m.nb_points_of_convex(cv));
      std::copy(m.ind_points_of_convex(cv).begin(), m.ind_points_of_convex(cv).end(), p.begin());
      mesh_edge_list_convex(m.structure_of_convex(cv), p, cv, el, merge_convex);
    }
  }



}  /* end of namespace bgeot.                                              */
