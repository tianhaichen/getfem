/*===========================================================================

 Copyright (C) 2002-2020 Yves Renard

 This file is a part of GetFEM++

 GetFEM++  is  free software;  you  can  redistribute  it  and/or modify it
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

===========================================================================*/

#include "getfem/dal_singleton.h"
#include "getfem/bgeot_comma_init.h"
#include "getfem/bgeot_poly_composite.h"

namespace bgeot {

  inline scalar_type sfloor(scalar_type x)
    { return (x >= 0) ? floor(x) : -floor(-x); }


  int imbricated_box_less::operator()(const base_node &x,
                                      const base_node &y) const {
    size_type s = x.size();
    scalar_type c1 = c_max, c2 = c_max * scalar_type(base);
    GMM_ASSERT2(y.size() == s, "dimension error");

    base_node::const_iterator itx=x.begin(), itex=x.end(), ity=y.begin();
    int ret = 0;
    for (; itx != itex; ++itx, ++ity) {
      long a = long(sfloor((*itx) * c1)), b = long(sfloor((*ity) * c1));
      if ((scalar_type(gmm::abs(a)) > scalar_type(base))
          || (scalar_type(gmm::abs(b)) > scalar_type(base))) {
        exp_max++; c_max /= scalar_type(base);
        return (*this)(x,y);
      }
      if (ret == 0) { if (a < b) ret = -1; else if (a > b) ret = 1; }
    }
    if (ret) return ret;

    for (int e = exp_max; e >= exp_min; --e, c1 *= scalar_type(base),
           c2 *= scalar_type(base)) {
      itx = x.begin(), itex = x.end(), ity = y.begin();
      for (; itx != itex; ++itx, ++ity) {
        int a = int(sfloor(((*itx) * c2) - sfloor((*itx) * c1)
                           * scalar_type(base)));
        int b = int(sfloor(((*ity) * c2) - sfloor((*ity) * c1)
                           * scalar_type(base)));
        if (a < b) return -1; else if (a > b) return 1;
      }
    }
    return 0;
  }

  mesh_precomposite::mesh_precomposite(const basic_mesh &m) : box_tree{1e-13} {
    initialise(m);
  }

  void mesh_precomposite::initialise(const basic_mesh &m) {
    msh = &m;
    det.resize(m.nb_convex()); orgs.resize(m.nb_convex());
    gtrans.resize(m.nb_convex()); gtransinv.resize(m.nb_convex());
    for (size_type i = 0; i <= m.points().index().last_true(); ++i)
      vertices.add(m.points()[i]);

    base_node min, max;
    for (dal::bv_visitor cv(m.convex_index()); !cv.finished(); ++cv) {

      pgeometric_trans pgt = m.trans_of_convex(cv);
      size_type N = pgt->structure()->dim();
      size_type P = m.dim();
      GMM_ASSERT1(pgt->is_linear(), "Bad geometric transformation");

      base_matrix G(P, pgt->nb_points());
      base_node X(N);
      
      m.points_of_convex(cv, G);
      bgeot::geotrans_interpolation_context ctx(pgt, X, G);
      gtrans[cv] = ctx.K();
      gtransinv[cv] = ctx.B();
      det[cv] = ctx.J();
      
      auto points_of_convex = m.points_of_convex(cv);
      orgs[cv] = pgt->transform(X, G);
      bounding_box(min, max, points_of_convex);
      box_to_convexes_map[box_tree.add_box(min, max)].push_back(cv);
    }

    box_tree.build_tree();
  }

  DAL_TRIPLE_KEY(base_poly_key, short_type, short_type,
                 std::vector<opt_long_scalar_type>);

  polynomial_composite::polynomial_composite(const mesh_precomposite &m,
                                             bool lc, bool ff)
    : mp(&m), local_coordinate(lc), faces_first(ff),
      default_polys(mp->dim()+1) {
    for (dim_type i = 0; i <= mp->dim(); ++i)
      default_polys[i] = base_poly(i, 0.);
  }

  static void mult_diff_transposed(const base_matrix &M, const base_node &p,
                                   const base_node &p1, base_node &p2) {
    for (dim_type d = 0; d < p2.size(); ++d) {
      p2[d] = 0;
      auto col = mat_col(M, d);
      for (dim_type i = 0; i < p1.size(); ++i)
        p2[d] += col[i] * (p[i] - p1[i]);
    }
  }

  scalar_type polynomial_composite::eval(const base_node &p,
                                         size_type l) const {
    if (l != size_type(-1)) {
      if (!local_coordinate) return poly_of_subelt(l).eval(p.begin());
      base_node p1(gmm::mat_ncols(mp->gtransinv[l]));
      mult_diff_transposed(mp->gtransinv[l], p, mp->orgs[l], p1);
      return poly_of_subelt(l).eval(p1.begin());
    }

    auto dim = mp->dim();
    base_node p1_stored, p1, p2(dim);
    size_type cv_stored(-1);

    auto &box_tree = mp->box_tree;
    rtree::pbox_set box_list;
    box_tree.find_boxes_at_point(p, box_list);
    
    while (box_list.size()) {
      auto pmax_box = *box_list.begin();
      
      if (box_list.size() > 1) {
        auto max_rate = -1.0;
        for (auto &&pbox : box_list) {
          auto box_rate = 1.0;
          for (size_type i = 0; i < dim; ++i) {
            auto h = pbox->max->at(i) - pbox->min->at(i);
            if (h > 0) {
              auto rate = std::min(pbox->max->at(i) - p[i],
                                   p[i] - pbox->min->at(i)) / h;
              box_rate = std::min(rate, box_rate);
            }
          }
          if (box_rate > max_rate) { pmax_box = pbox; max_rate = box_rate; }
        }
      }
      
      for (auto cv : mp->box_to_convexes_map.at(pmax_box->id)) {
        dim_type elt_dim = dim_type(gmm::mat_ncols(mp->gtransinv[cv]));
        p1.resize(elt_dim);
        mult_diff_transposed(mp->gtransinv[cv], p, mp->orgs[cv], p1);
        scalar_type ddist(0);

        if (elt_dim < dim) {
          p2 = mp->orgs[cv]; gmm::mult_add(mp->gtrans[cv], p1, p2);
          ddist = gmm::vect_dist2(p2, p);
        }
        if (mp->trans_of_convex(cv)->convex_ref()->is_in(p1) < 1E-9
            && ddist < 1E-9) {
          if (!faces_first || elt_dim < dim) {
            scalar_type res = to_scalar(poly_of_subelt(cv).eval(local_coordinate
                                                     ? p1.begin() : p.begin()));
            return res;
          }
          p1_stored = p1; cv_stored = cv;
        }
      }
        
      if (box_list.size() == 1) break;
      box_list.erase(pmax_box);
    }
    if (cv_stored != size_type(-1)) {
      scalar_type res =
        to_scalar(poly_of_subelt(cv_stored).eval(local_coordinate
                                              ? p1_stored.begin(): p.begin()));
      return res;
    }
    GMM_ASSERT1(false, "Element not found in composite polynomial: " << p);
  }
  

  void polynomial_composite::derivative(short_type k) {
    if (local_coordinate) {
      dim_type P = mp->dim();
      base_vector e(P), f(P); e[k] = 1.0;
      for (size_type ic = 0; ic < mp->nb_convex(); ++ic) {
        dim_type N = dim_type(gmm::mat_ncols(mp->gtransinv[ic]));
        f.resize(N);
        gmm::mult(gmm::transposed(mp->gtransinv[ic]), e, f);
        base_poly Der(N, 0), Q;
        if (polytab.find(ic) != polytab.end()) {
          auto &poly = poly_of_subelt(ic);
          for (dim_type n = 0; n < N; ++n) {
            Q = poly;
            Q.derivative(n);
            Der += Q * f[n];
          }
          if (Der.is_zero()) polytab.erase(ic); else set_poly_of_subelt(ic,Der);
        }
      }
    }
    else
    for (size_type ic = 0; ic < mp->nb_convex(); ++ic) {
      auto poly = poly_of_subelt(ic);
      poly.derivative(k);
      if (polytab.find(ic) != polytab.end()) set_poly_of_subelt(ic, poly);
    }
  }
  
  void polynomial_composite::set_poly_of_subelt(size_type l,
                                                const base_poly &poly) {
    auto poly_key =
      std::make_shared<base_poly_key>(poly.degree(), poly.dim(), poly);
    pstored_base_poly o(std::dynamic_pointer_cast<const stored_base_poly>
                        (dal::search_stored_object(poly_key)));
    if (!o) {
      o = std::make_shared<stored_base_poly>(poly);
      dal::add_stored_object(poly_key, o);
    }
    polytab[l] = o;
  }

  const base_poly &polynomial_composite::poly_of_subelt(size_type l) const {
    auto it = polytab.find(l);
    if (it == polytab.end()) {
      if (local_coordinate)
        return default_polys[gmm::mat_ncols(mp->gtransinv[l])];
      else
        return default_polys[mp->dim()];
    }
    return *(it->second);
  }


  /* build a regularly refined mesh for a simplex of dimension <= 3.
  All simplexes have the same orientation as the original simplex.
  */
  static void
  structured_mesh_for_simplex_(pconvex_structure cvs,
                               pgeometric_trans opt_gt,
                               const std::vector<base_node> *opt_gt_pts,
                               short_type k, pbasic_mesh pm) {
      scalar_type h = 1./k;
      switch (cvs->dim()) {
      case 1 :
        {
          base_node a(1), b(1);
          for (short_type i = 0; i < k; ++i) {
            a[0] = i * h; b[0] = (i+1) * h;
            if (opt_gt) a = opt_gt->transform(a, *opt_gt_pts);
            if (opt_gt) b = opt_gt->transform(b, *opt_gt_pts);
            size_type na = pm->add_point(a);
            size_type nb = pm->add_point(b);
            pm->add_segment(na, nb);
          }
        }
        break;
      case 2 :
        {
          base_node A(2),B(2),C(2),D(2);
          for (short_type i = 0; i < k; ++i) {
            scalar_type a = i * h, b = (i+1) * h;
            for (short_type l = 0; l+i < k; ++l) {
              scalar_type c = l * h, d = (l+1) * h;
              A[0] = a; A[1] = c;
              B[0] = b; B[1] = c;
              C[0] = a; C[1] = d;
              D[0] = b; D[1] = d;
              if (opt_gt) {
                A = opt_gt->transform(A, *opt_gt_pts);
                B = opt_gt->transform(B, *opt_gt_pts);
                C = opt_gt->transform(C, *opt_gt_pts);
                D = opt_gt->transform(D, *opt_gt_pts);
              }
              /* add triangle with correct orientation (det [B-A;C-A] > 0) */
              size_type nA = pm->add_point(A);
              size_type nB = pm->add_point(B);
              size_type nC = pm->add_point(C);
              size_type nD = pm->add_point(D);
              pm->add_triangle(nA,nB,nC);
              if (l+i+1 < k)
                pm->add_triangle(nC,nB,nD);
            }
          }
        }
        break;
      case 3 :
        {
          /* based on decompositions of small cubes
          the number of tetrahedrons is k^3
          */
          base_node A,B,C,D,E,F,G,H;
          for (short_type ci = 0; ci < k; ci++) {
            scalar_type x = ci*h;
            for (short_type cj = 0; cj < k-ci; cj++) {
              scalar_type y=cj*h;
              for (short_type ck = 0; ck < k-ci-cj; ck++) {
                scalar_type z=ck*h;

                A = {x, y, z};
                B = {x+h, y, z};
                C = {x, y+h, z};
                D = {x+h, y+h, z};
                E = {x, y, z+h};
                F = {x+h, y, z+h};
                G = {x, y+h, z+h};
                H = {x+h, y+h, z+h};
                if (opt_gt) {
                  A = opt_gt->transform(A, *opt_gt_pts);
                  B = opt_gt->transform(B, *opt_gt_pts);
                  C = opt_gt->transform(C, *opt_gt_pts);
                  D = opt_gt->transform(D, *opt_gt_pts);
                  E = opt_gt->transform(E, *opt_gt_pts);
                  F = opt_gt->transform(F, *opt_gt_pts);
                  G = opt_gt->transform(G, *opt_gt_pts);
                  H = opt_gt->transform(H, *opt_gt_pts);
                }
                size_type t[8];
                t[0] = pm->add_point(A);
                t[1] = pm->add_point(B);
                t[2] = pm->add_point(C);
                t[4] = pm->add_point(E);
                t[3] = t[5] = t[6] = t[7] = size_type(-1);
                if (k > 1 && ci+cj+ck < k-1) {
                  t[3] = pm->add_point(D);
                  t[5] = pm->add_point(F);
                  t[6] = pm->add_point(G);
                }
                if (k > 2 && ci+cj+ck < k-2) {
                  t[7] = pm->add_point(H);
                }
                /**
                Note that the orientation of each tetrahedron is the same
                (det > 0)
                */
                pm->add_tetrahedron(t[0], t[1], t[2], t[4]);
                if (k > 1 && ci+cj+ck < k-1) {
                  pm->add_tetrahedron(t[1], t[2], t[4], t[5]);
                  pm->add_tetrahedron(t[6], t[4], t[2], t[5]);
                  pm->add_tetrahedron(t[2], t[3], t[5], t[1]);
                  pm->add_tetrahedron(t[2], t[5], t[3], t[6]);
                }
                if (k > 2 && ci+cj+ck < k-2) {
                  pm->add_tetrahedron(t[3], t[5], t[7], t[6]);
                }
              }
            }
          }
        }
        break;
      default :
        GMM_ASSERT1(false, "Sorry, not implemented for simplices of "
          "dimension " << int(cvs->dim()));
      }
  }

  static void structured_mesh_for_parallelepiped_
    (pconvex_structure cvs, pgeometric_trans opt_gt,
    const std::vector<base_node> *opt_gt_pts, short_type k, pbasic_mesh pm) {
      scalar_type h = 1./k;
      size_type n = cvs->dim();
      size_type pow2n = (size_type(1) << n);
      size_type powkn = 1; for (size_type i=0; i < n; ++i) powkn *= k;
      std::vector<size_type> strides(n);
      size_type nbpts = 1;
      for (size_type i=0; i < n; ++i) { strides[i] = nbpts; nbpts *= (k+1); }
      std::vector<short_type> kcnt; kcnt.resize(n+1,0);
      std::vector<size_type> pids; pids.reserve(nbpts);
      base_node pt(n);
      size_type kk;
      /* insert nodes and fill pids with their numbers */
      while (kcnt[n] == 0) {
        for (size_type z = 0; z < n; ++z)
          pt[z] = h*kcnt[z];
        if (opt_gt) pt = opt_gt->transform(pt, *opt_gt_pts);
        pids.push_back(pm->add_point(pt));
        kk=0;
        while (kk <= n)
        { kcnt[kk]++; if (kcnt[kk] == k+1) { kcnt[kk] = 0; kk++; } else break; }
      }

      /*
      insert convexes using node ids stored in 'pids'
      */
      std::vector<size_type> ppts(pow2n);
      kcnt[n] = 0;
      while (kcnt[n] == 0) {
        for (kk = 0; kk < pow2n; ++kk) {
          size_type pos = 0;
          for (size_type z = 0; z < n; ++z) {
            pos += kcnt[z]*strides[z];
            if ((kk & (size_type(1) << z))) pos += strides[z];
          }
          ppts[kk] = pids.at(pos);
        }
        pm->add_convex(parallelepiped_linear_geotrans(n), ppts.begin());
        kcnt[(kk = 0)]++;
        while (kk < n && kcnt[kk] == k) { kcnt[kk] = 0; kcnt[++kk]++; }
      }
  }

  static void
  structured_mesh_for_convex_(pconvex_structure cvs,
                              pgeometric_trans opt_gt,
                              const std::vector<base_node> *opt_gt_pts,
                              short_type k, pbasic_mesh pm) {
      size_type nbp = basic_structure(cvs)->nb_points();
      dim_type n = cvs->dim();
      /* Identifying simplexes.                                           */
      if (nbp == size_type(n+1) &&
          *key_of_stored_object(basic_structure(cvs))
          ==*key_of_stored_object(simplex_structure(n))) {
          // smc.pm->write_to_file(cout);
          structured_mesh_for_simplex_(cvs,opt_gt,opt_gt_pts,k,pm);
          /* Identifying parallelepipeds.                                     */
      } else if (nbp == (size_type(1) << n) &&
          *key_of_stored_object(basic_structure(cvs))
          == *key_of_stored_object(parallelepiped_structure(n))) {
          structured_mesh_for_parallelepiped_(cvs,opt_gt,opt_gt_pts,k,pm);
      } else if (nbp == size_type(2 * n) &&
          *key_of_stored_object(basic_structure(cvs))
          == *key_of_stored_object(prism_P1_structure(n))) {
          GMM_ASSERT1(false, "Sorry, structured_mesh not implemented for prisms.");
      } else {
        GMM_ASSERT1(false, "This element is not taken into account. Contact us");
      }
  }

  /* extract the mesh_structure on faces */
  static void structured_mesh_of_faces_(pconvex_ref cvr, dim_type f,
    const basic_mesh &m,
    mesh_structure &facem) {
      facem.clear();
      dal::bit_vector on_face;
      for (size_type i = 0; i < m.nb_max_points(); ++i) {
        if (m.is_point_valid(i)) {
          if (gmm::abs(cvr->is_in_face(f, m.points()[i])) < 1e-12)
            on_face.add(i);
        }
      }
      //cerr << "on_face=" << on_face << endl;
      for (dal::bv_visitor cv(m.convex_index()); !cv.finished(); ++cv) {
        for (short_type ff=0; ff < m.structure_of_convex(cv)->nb_faces(); ++ff) {
          mesh_structure::ind_pt_face_ct
            ipts=m.ind_points_of_face_of_convex(cv,ff);
          bool allin = true;
          for (size_type i=0; i < ipts.size(); ++i) if (!on_face[ipts[i]])
          { allin = false; break; }
          if (allin) {
            /* cerr << "ajout de la face " << ff << " du convexe " << cv << ":";
            for (size_type i=0; i < ipts.size(); ++i)
            cerr << on_face[ipts[i]] << "/" << ipts[i] << " ";
            cerr << endl;
            */
            facem.add_convex(m.structure_of_convex(cv)->faces_structure()[ff],
              ipts.begin());
          }
        }
      }
  }

  DAL_TRIPLE_KEY(str_mesh_key, pconvex_structure, short_type, bool);

  struct str_mesh_cv__  : virtual public dal::static_stored_object {
    pconvex_structure cvs;
    short_type n;
    bool simplex_mesh; /* true if the convex has been splited into simplexes,
                       which were refined */
    std::unique_ptr<basic_mesh> pm;
    std::vector<std::unique_ptr<mesh_structure>> pfacem; /* array of mesh_structures for faces */
    dal::bit_vector nodes_on_edges;
    std::unique_ptr<mesh_precomposite> pmp;
    str_mesh_cv__(pconvex_structure c, short_type k, bool smesh_) :
      cvs(c), n(k), simplex_mesh(smesh_)
    { DAL_STORED_OBJECT_DEBUG_CREATED(this, "cv mesh"); }
    ~str_mesh_cv__() { DAL_STORED_OBJECT_DEBUG_DESTROYED(this,"cv mesh"); }
  };

  typedef std::shared_ptr<const str_mesh_cv__> pstr_mesh_cv__;

  /**
  * This function returns a mesh in pm which contains a refinement of the convex
  * cvr if force_simplexification is false, refined convexes have the same
  * basic_structure than cvr, if it is set to true, the cvr is decomposed
  * into simplexes which are then refined.
  * TODO: move it into another file and separate the pmesh_precomposite part ?
  **/
  void structured_mesh_for_convex(pconvex_ref cvr, short_type k,
                                  pbasic_mesh &pm, pmesh_precomposite &pmp,
                                  bool force_simplexification) {
    size_type n = cvr->structure()->dim();
    size_type nbp = basic_structure(cvr->structure())->nb_points();

    force_simplexification = force_simplexification || (nbp == n+1);
    dal::pstatic_stored_object_key
      pk = std::make_shared<str_mesh_key>(basic_structure(cvr->structure()),
                                          k, force_simplexification);

    dal::pstatic_stored_object o = dal::search_stored_object_on_all_threads(pk);
    pstr_mesh_cv__ psmc;
    if (o)
      psmc = std::dynamic_pointer_cast<const str_mesh_cv__>(o);
    else {

      auto ppsmc = std::make_shared<str_mesh_cv__>
        (basic_structure(cvr->structure()), k, force_simplexification);
      str_mesh_cv__ &smc(*ppsmc);
      psmc = ppsmc;

      smc.pm = std::make_unique<basic_mesh>();

      if (force_simplexification) {
        // cout << "cvr = " << int(cvr->structure()->dim()) << " : "
        //      << cvr->structure()->nb_points() << endl;
        const mesh_structure* splx_mesh
          = basic_convex_ref(cvr)->simplexified_convex();
        /* splx_mesh is correctly oriented */
        for (size_type ic=0; ic < splx_mesh->nb_convex(); ++ic) {
          std::vector<base_node> cvpts(splx_mesh->nb_points_of_convex(ic));
          pgeometric_trans sgt
            = simplex_geotrans(cvr->structure()->dim(), 1);
          for (size_type j=0; j < cvpts.size(); ++j) {
            size_type ip = splx_mesh->ind_points_of_convex(ic)[j];
            cvpts[j] = basic_convex_ref(cvr)->points()[ip];
            //cerr << "cvpts[" << j << "]=" << cvpts[j] << endl;
          }
          structured_mesh_for_convex_(splx_mesh->structure_of_convex(ic),
                                      sgt, &cvpts, k, smc.pm.get());
        }
      } else {
        structured_mesh_for_convex_(cvr->structure(), 0, 0, k, smc.pm.get());
      }
      smc.pfacem.resize(cvr->structure()->nb_faces());
      for (dim_type f=0; f < cvr->structure()->nb_faces(); ++f) {
        smc.pfacem[f] = std::make_unique<mesh_structure>();
        structured_mesh_of_faces_(cvr, f, *(smc.pm), *(smc.pfacem[f]));
      }

      smc.pmp = std::make_unique<mesh_precomposite>(*(smc.pm));
      dal::add_stored_object(pk, psmc, basic_structure(cvr->structure()));
    }
    pm  = psmc->pm.get();
    pmp = psmc->pmp.get();
  }

  const basic_mesh *
    refined_simplex_mesh_for_convex(pconvex_ref cvr, short_type k) {
      pbasic_mesh pm; pmesh_precomposite pmp;
      structured_mesh_for_convex(cvr, k, pm, pmp, true);
      return pm;
  }

  const std::vector<std::unique_ptr<mesh_structure>>&
    refined_simplex_mesh_for_convex_faces(pconvex_ref cvr, short_type k) {
    dal::pstatic_stored_object_key
      pk = std::make_shared<str_mesh_key>(basic_structure(cvr->structure()),
                                          k, true);
    dal::pstatic_stored_object o = dal::search_stored_object(pk);
    if (o) {
      pstr_mesh_cv__ psmc = std::dynamic_pointer_cast<const str_mesh_cv__>(o);
      return psmc->pfacem;
    }
    else GMM_ASSERT1(false,
                     "call refined_simplex_mesh_for_convex first (or fix me)");
  }

}  /* end of namespace getfem.                                            */
