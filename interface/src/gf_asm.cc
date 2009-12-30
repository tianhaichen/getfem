// -*- c++ -*- (enables emacs c++ mode)
//===========================================================================
//
// Copyright (C) 2006-2008 Yves Renard, Julien Pommier.
//
// This file is a part of GETFEM++
//
// Getfem++  is  free software;  you  can  redistribute  it  and/or modify it
// under  the  terms  of the  GNU  Lesser General Public License as published
// by  the  Free Software Foundation;  either version 2.1 of the License,  or
// (at your option) any later version.
// This program  is  distributed  in  the  hope  that it will be useful,  but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or  FITNESS  FOR  A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// You  should  have received a copy of the GNU Lesser General Public License
// along  with  this program;  if not, write to the Free Software Foundation,
// Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
//
//===========================================================================


#include <getfem/getfem_assembling.h>
#include <getfemint_misc.h>
#include <getfemint_gsparse.h>
#include <getfem/getfem_interpolation.h>
#include <getfem/getfem_nonlinear_elasticity.h>
#include <getfem/getfem_fourth_order.h>

using namespace getfemint;
namespace getfemint {
  struct darray_with_gfi_array : public darray {
    gfi_array *mx;
    darray_with_gfi_array(const bgeot::tensor_ranges& r) {
      size_type siz = 1; for (size_type i=0; i < r.size(); ++i) siz *= r[i];
      if (siz == 0) ASM_THROW_TENSOR_ERROR("can't create a vector of size " << r);
      std::vector<int> tab(r.size());
      std::copy(r.begin(), r.end(), tab.begin());
      mx = checked_gfi_array_create(int(r.size()), &(tab.begin()[0]),GFI_DOUBLE);
      assign(mx);
    }
  };
}
namespace gmm {
  template<> struct linalg_traits<getfemint::darray_with_gfi_array> {
    typedef getfemint::darray_with_gfi_array this_type;
    typedef linalg_false is_reference;
    typedef abstract_vector linalg_type;
    typedef double value_type;
    typedef value_type origin_type;
    typedef double& reference;
    typedef this_type::iterator iterator;
    typedef this_type::const_iterator const_iterator;
    typedef abstract_dense storage_type;
    typedef linalg_true index_sorted;
    static size_type size(const this_type &v) { return v.size(); }
    static iterator begin(this_type &v) { return v.begin(); }
    static const_iterator begin(const this_type &v) { return v.begin(); }
    static iterator end(this_type &v) { return v.end(); }
    static const_iterator end(const this_type &v) { return v.end(); }
    static const origin_type* origin(const this_type &v) { return &v[0]; }
    static origin_type* origin(this_type &v) { return &v[0]; }
    static void clear(origin_type* , const iterator &it, const iterator &ite)
    { std::fill(it, ite, value_type(0)); }
    static void do_clear(this_type &v) { std::fill(v.begin(), v.end(), 0.); }
    static value_type access(const origin_type *, const const_iterator &it,
			     const const_iterator &, size_type i)
    { return it[i]; }
    static reference access(origin_type *, const iterator &it,
			    const iterator &, size_type i)
    { return it[i]; }
  };
}

namespace getfem {
  template<> class vec_factory<darray_with_gfi_array> :
    public base_vec_factory, private std::deque<asm_vec<getfemint::darray_with_gfi_array> > {
  public:
    base_asm_vec* create_vec(const tensor_ranges& r) {
      asm_vec<getfemint::darray_with_gfi_array> v(new getfemint::darray_with_gfi_array(r));
      push_back(v); return &this->back();
    }
    ~vec_factory() {
      for (size_type i=0; i < this->size(); ++i) {
	delete (*this)[i].vec(); // but it does not deallocate the gfi_array !! that's fine
      }
    }
  };
}

static void
do_generic_assembly(mexargs_in& in, mexargs_out& out, bool on_boundary)
{
  getfem::mesh_region rg = getfem::mesh_region::all_convexes();
  if (!on_boundary) {
    if (in.remaining() && !in.front().is_string()) {
      rg = in.pop().to_mesh_region();
    }
  } else rg = getfem::mesh_region(in.pop().to_integer());

  std::string s = in.pop().to_string();
  getfem::generic_assembly assem(s);
  /* stores the mesh_fem identifiers */
  while (in.remaining() && in.front().is_mesh_im()) {
    assem.push_mi(*in.pop().to_mesh_im());
  }
  if (assem.im().size() == 0)
    THROW_BADARG("generic assembly without any mesh_im has no sense!");
  /* stores the mesh_fem identifiers */
  while (in.remaining() && in.front().is_mesh_fem()) {
    assem.push_mf(*in.pop().to_const_mesh_fem());
  }

  /* store the data vectors */
  std::deque<darray> vtab;
  while (in.remaining()) {
    vtab.push_back(in.pop().to_darray());
  }
  /* DON't do that in the loop above, since push_back may invalidate every pointer on vtab[i] */
  for (size_type i=0; i < vtab.size(); ++i) assem.push_data(vtab[i]);

  /* set the kind of matrix/vector to build */
  getfem::mat_factory<gf_real_sparse_by_col> mat_fact;
  getfem::vec_factory<darray_with_gfi_array> vec_fact;
  assem.set_mat_factory(&mat_fact);
  assem.set_vec_factory(&vec_fact);

  assem.assembly(rg);
  // get the matrix back
  for (size_type i=0; out.remaining() && i < assem.mat().size(); ++i) {
    if (assem.mat()[i] != 0) {
      getfem::base_asm_mat *BM = assem.mat()[i];
      getfem::asm_mat<gf_real_sparse_by_col> * M =
	static_cast<getfem::asm_mat<gf_real_sparse_by_col>*>(BM);
      out.pop().from_sparse(*M->mat());
    }
  }

  if (out.remaining()) {
    for (size_type i=0; out.remaining() && i < assem.vec().size(); ++i) {
      if (assem.vec()[i] != 0) {
	getfem::base_asm_vec *BV = assem.vec()[i];
	getfem::asm_vec<darray_with_gfi_array> *V =
	  static_cast<getfem::asm_vec<darray_with_gfi_array> *>(BV);
	mexarg_out mo = out.pop(); mo.arg = V->vec()->mx;
      }
    }
  }
}


template<typename T> static void
gf_dirichlet(getfemint::mexargs_out& out,
	     const getfem::mesh_im &mim,
	     const getfem::mesh_fem &mf_u,
	     const getfem::mesh_fem &mf_d,
	     mexarg_in in_h, mexarg_in in_r, int boundary_num, T)
{
  unsigned q_dim = mf_u.get_qdim();

  garray<T> h = in_h.to_garray(T());
  if (h.ndim() == 2) in_h.check_dimensions(h, q_dim* q_dim,int(mf_d.nb_dof()));
  else               in_h.check_dimensions(h, q_dim, q_dim,int(mf_d.nb_dof()));
  garray<T> r = in_r.to_garray(q_dim, int(mf_d.nb_dof()), T());
  gmm::col_matrix<gmm::wsvector<T> > H(mf_u.nb_dof(), mf_u.nb_dof());
  mexarg_out out_H    = out.pop();
  garray<T> R         = out.pop().create_array_v(unsigned(mf_u.nb_dof()), T());
  getfem::asm_generalized_dirichlet_constraints(H, R, mim, mf_u, mf_d, mf_d, h, r, boundary_num);
  out_H.from_sparse(H/*,threshold*/);
}

void interpolate_or_extrapolate(mexargs_in &in, mexargs_out &out, int extrapolate) {
  const getfem::mesh_fem *mf1 = in.pop().to_const_mesh_fem();
  const getfem::mesh_fem *mf2 = in.pop().to_const_mesh_fem();
  gf_real_sparse_by_col M(mf2->nb_dof(), mf1->nb_dof());
  getfem::interpolation(*mf1, *mf2, M, extrapolate);
  out.pop().from_sparse(M);
}

static const getfem::mesh_im *get_mim(mexargs_in &in) {
  if (!in.front().is_mesh_im()) {
    THROW_BADARG("Since release 2.0 of getfem, all assembly functions"
		 " expect a mesh_im as their second argument");
  }
  return in.pop().to_const_mesh_im();
}

void assemble_source(size_type boundary_num,
		     mexargs_in &in, mexargs_out &out) {
  const getfem::mesh_im *mim = get_mim(in);
  const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
  const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
  unsigned q_dim = mf_u->get_qdim();
  if (!in.front().is_complex()) {
    darray g               = in.pop().to_darray(q_dim, int(mf_d->nb_dof()));
    darray F               = out.pop().create_darray_v(unsigned(mf_u->nb_dof()));
    getfem::asm_source_term(F, *mim, *mf_u, *mf_d, g, boundary_num);
  } else {
    carray g               = in.pop().to_carray(q_dim, int(mf_d->nb_dof()));
    carray F               = out.pop().create_carray_v(unsigned(mf_u->nb_dof()));
    getfem::asm_source_term(F, *mim, *mf_u, *mf_d, g, boundary_num);
  }
}

/*MLABCOM

  FUNCTION gf_asm(operation[, arg])

  General assembly function.

  Many of the functions below use more than one mesh_fem: the main
  mesh_fem (mf_u) used for the main unknow, and data mesh_fem (mf_d)
  used for the data. It is always assumed that the Qdim of mf_d is
  equal to 1: if mf_d is used to describe vector or tensor data, you
  just have to "stack" (in fortran ordering) as many scalar fields as
  necessary.

  @FUNC ::ASM('volumic source')
  @FUNC ::ASM('boundary source')
  @FUNC ::ASM('mass matrix')
  @FUNC ::ASM('laplacian')
  @FUNC ::ASM('linear elasticity')
  @FUNC ::ASM('nonlinear elasticity')
  @FUNC ::ASM('stokes')
  @FUNC ::ASM('helmholtz')
  @FUNC ::ASM('bilaplacian')
  @FUNC ::ASM('dirichlet')
  @FUNC ::ASM('boundary qu term')
  @FUNC ::ASM('pdetool boundary conditions')
  @FUNC ::ASM('volumic')
  @FUNC ::ASM('boundary')
  @FUNC ::ASM('interpolation matrix')
  @FUNC ::ASM('extrapolation matrix')
MLABCOM*/

/*MLABEXT
  if (nargin>=1 & strcmpi(varargin{1},'pdetool boundary conditions')),
    [varargout{1:nargout}]=gf_asm_pdetoolbc(varargin{[1 3:nargin]}); return;
  end;
  MLABEXT*/
void gf_asm(getfemint::mexargs_in& in, getfemint::mexargs_out& out)
{
  if (in.narg() < 1) {
    THROW_BADARG( "Wrong number of input arguments");
  }
  std::string cmd = in.pop().to_string();

  if (check_cmd(cmd, "mass matrix", in, out, 2, 3, 0, 1)) {
    /*@FUNC M = ::ASM('mass matrix',@tmim mim, @tmf mf1[, @tmf mf2])
    Assembly of a mass matrix.

    Return a @tsp object.
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u1 = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_u2 = in.remaining() ? in.pop().to_const_mesh_fem() : mf_u1;

    gf_real_sparse_by_col M(mf_u1->nb_dof(), mf_u2->nb_dof());
    getfem::asm_mass_matrix(M, *mim, *mf_u1, *mf_u2);
    out.pop().from_sparse(M);
  } else if (check_cmd(cmd, "laplacian", in, out, 4, 4,0, 1)) {
    /*@FUNC L = ::ASM('laplacian',@tmim mim, @tmf mf_u, @tmf mf_d, @dvec a)
    Assembly of the matrix for the Laplacian problem.

    @MATLAB{div(a(x)*grad(u))}@PYTHON{:math:`\\nabla\\cdot(a(x)\\nabla u)`}
    with `a` scalar.<Par>

    Return a @tsp object.
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    darray A               = in.pop().to_darray(int(mf_d->nb_dof()));
    gf_real_sparse_by_col M(mf_u->nb_dof(), mf_u->nb_dof());
    getfem::asm_stiffness_matrix_for_laplacian(M, *mim, *mf_u, *mf_d, A);
    out.pop().from_sparse(M);
  } else if (check_cmd(cmd, "linear elasticity", in, out, 5, 5, 0, 1)) {
    /*@FUNC Le = ::ASM('linear elasticity',@tmim mim, @tmf mf_u, @tmf mf_d, @dvec lambda_d, @dvec mu_d)
    Assembles of the matrix for the linear (isotropic) elasticity problem.

    @MATLAB{div(C(x):grad(u))}@PYTHON{:math:`\\nabla\\cdot(C(x):\\nabla u)`}
    with @MATLAB{C}@PYTHON{:math:`C`} defined via `lambda_d` and `mu_d`.<Par>

    Return a @tsp object.
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    darray lambda          = in.pop().to_darray(int(mf_d->nb_dof()));
    darray mu              = in.pop().to_darray(int(mf_d->nb_dof()));
    gf_real_sparse_by_col M(mf_u->nb_dof(), mf_u->nb_dof());
    getfem::asm_stiffness_matrix_for_linear_elasticity(M, *mim, *mf_u, *mf_d, lambda, mu);
    out.pop().from_sparse(M);
  } else if (check_cmd(cmd, "nonlinear elasticity", in, out, 3,-1,0,-1)) {
    /*@FUNC TRHS = ::ASM('nonlinear elasticity',@tmim mim, @tmf mf_u, @dvec U, @str law, @tmf mf_d, @dmat params, {'tangent matrix'|'rhs'|'incompressible tangent matrix', @tmf mf_p, @dvec P|'incompressible rhs', @tmf mf_p, @dvec P})
    Assembles terms (tangent matrix and right hand side) for nonlinear elasticity.

    The solution `U` is required at the current time-step. The `law`
    may be choosen among:<Par>
    - 'SaintVenant Kirchhoff':<par>
       Linearized law, should be avoided). This law has the two usual<par>
       Lame coefficients as parameters, called lambda and mu.<par>
    - 'Mooney Rivlin':<par>
       Only for incompressibility. This law has two parameters,<par>
       called C1 and C2.<par>
    - 'Ciarlet Geymonat':<par>
       This law has 3 parameters, called lambda, mu and gamma, with<par>
       gamma chosen such that gamma is in ]-lambda/2-mu, -mu[.<Par>

    The parameters of the material law are described on the @tmf `mf_d`.
    The matrix `params` should have `nbdof(mf_d)` columns, each row
    correspounds to a parameter.<Par>

    The last argument selects what is to be built: either the tangent
    matrix, or the right hand side. If the incompressibility is
    considered, it should be followed by a @tmf `mf_p`, for the
    pression.<Par>

    Return a @tsp object (tangent matrix), @dcvec object (right hand
    side), tuple of @tsp objects (incompressible tangent matrix), or
    tuple of @dcvec objects (incompressible right hand side).
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    darray U = in.pop().to_darray(int(mf_u->nb_dof()));
    std::string lawname = in.pop().to_string();
    /* a refaire , pas bon, le terme incompressible se passe de loi */
    const getfem::abstract_hyperelastic_law &law
      = abstract_hyperelastic_law_from_name(lawname);
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    darray param = in.pop().to_darray(int(law.nb_params()),
				      int(mf_d->nb_dof()));
    while (in.remaining()) {
      std::string what = in.pop().to_string();
      if (cmd_strmatch(what, "tangent matrix")) {
	gf_real_sparse_by_col  K(mf_u->nb_dof(), mf_u->nb_dof());
	getfem::asm_nonlinear_elasticity_tangent_matrix(K, *mim, *mf_u, U,
							mf_d, param, law);
	out.pop().from_sparse(K);
      } else if (cmd_strmatch(what, "rhs")) {
	darray B = out.pop().create_darray_v(unsigned(mf_u->nb_dof()));
	getfem::asm_nonlinear_elasticity_rhs(B, *mim, *mf_u, U, mf_d,
					     param, law);
      } else if (cmd_strmatch(what, "incompressible tangent matrix")) {
	const getfem::mesh_fem *mf_p = in.pop().to_const_mesh_fem();
	darray P = in.pop().to_darray(int(mf_p->nb_dof()));
	gf_real_sparse_by_col  K(mf_u->nb_dof(), mf_u->nb_dof()),
	  B(mf_u->nb_dof(), mf_p->nb_dof());
	getfem::asm_nonlinear_incomp_tangent_matrix(K, B, *mim, *mf_u,
						    *mf_p, U, P);
	out.pop().from_sparse(K);
	out.pop().from_sparse(B);
      } else if (cmd_strmatch(what, "incompressible rhs")) {
	const getfem::mesh_fem *mf_p = in.pop().to_const_mesh_fem();
	darray P = in.pop().to_darray(int(mf_p->nb_dof()));
	darray RU = out.pop().create_darray_v(unsigned(mf_u->nb_dof()));
	darray RB = out.pop().create_darray_v(unsigned(mf_p->nb_dof()));
	getfem::asm_nonlinear_incomp_rhs(RU, RB, *mim, *mf_u, *mf_p, U, P);
      } else {
	THROW_BADARG("expecting 'tangent matrix' or 'rhs', or "
		     "'incomp tangent matrix' or 'incomp rhs', got '"
		     << what << "'");
      }
    }
    if (in.remaining())
      THROW_BADARG("too much arguments for asm(nonlinear_elasticity)");
  } else if (check_cmd(cmd, "stokes", in, out, 5, 5, 0, 2)) {
    /*@FUNC @CELL{K, B} = ::ASM('stokes',@tmim mim, @tmf mf_u, @tmf mf_p, @tmf mf_d, @dvec nu)
    Assembly of matrices for the Stokes problem.

    @MATLAB{`-nu(x).Delta(u) + grad(p) = 0`}@PYTHON{:math:`-\\nu(x)\\Delta u + \\nabla p = 0`}<Par>
    @MATLAB{`div(u) = 0`}@PYTHON{:math:`\\nabla\cdot u  = 0`}<Par>
    with @MATLAB{`nu`}@PYTHON{:math:`\\nu` (`nu`)}, the fluid's dynamic
    viscosity.<Par>

    On output, `K` is the usual linear elasticity stiffness matrix with
    @MATLAB{lambda = 0}@PYTHON{:math:`\\lambda = 0`} and
    @MATLAB{2mu = nu}@PYTHON{:math:`2\\mu = \\nu`}. `B` is a matrix
    corresponding to @MATLAB{$\int p.div v$}@PYTHON{:math:`\\int p\\nabla\cdot\\phi`}.<Par>

    `K` and `B` are @tsp object's.
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_p = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    darray           vec_d = in.pop().to_darray(int(mf_d->nb_dof()));
    gf_real_sparse_by_col  K(mf_u->nb_dof(), mf_u->nb_dof());
    gf_real_sparse_by_col  B(mf_u->nb_dof(), mf_p->nb_dof());
    getfem::asm_stokes(K, B, *mim, *mf_u, *mf_p, *mf_d, vec_d);
    out.pop().from_sparse(K);
    out.pop().from_sparse(B);
  } else if (check_cmd(cmd, "helmholtz", in, out, 4, 4, 0, 1)) {
    /*@FUNC A = ::ASM('helmholtz',@tmim mim, @tmf mf_u, @tmf mf_d, @cvec k)
    Assembly of the matrix for the Helmholtz problem.

    @MATLAB{`Laplacian(u) + k^2 u` = 0}@PYTHON{:math:`\\Delta u + k^2 u` = 0}
    with `k` complex scalar.<Par>

    Return a @tsp object.
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    carray           wn = in.pop().to_carray(int(mf_d->nb_dof()));
    std::vector<complex_type> WN(wn.size());
    for (size_type i=0; i < wn.size(); ++i) WN[i] = gmm::sqr(wn[i]);
    gf_cplx_sparse_by_col  A(mf_u->nb_dof(), mf_u->nb_dof());
    getfem::asm_Helmholtz(A, *mim, *mf_u, *mf_d, WN);
    out.pop().from_sparse(A);

  } else if (check_cmd(cmd, "bilaplacian", in, out, 4, 4, 0, 1)) {
    /*@FUNC A = ::ASM('bilaplacian',@tmim mim, @tmf mf_u, @tmf mf_d, @dvec a)
    Assembly of the matrix for the Bilaplacian problem.

    @MATLAB{`Laplacian(a(x)*Laplacian(u)) = 0`}@PYTHON{:math:`\\Delta(a(x)\\Delta u) = 0`}
    with `a` scalar.<Par>

    Return a @tsp object.
    @*/
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    darray           a = in.pop().to_darray(int(mf_d->nb_dof()));
    gf_real_sparse_by_col  A(mf_u->nb_dof(), mf_u->nb_dof());
    getfem::asm_stiffness_matrix_for_bilaplacian(A, *mim, *mf_u, *mf_d, a);
    out.pop().from_sparse(A);
  } else if (check_cmd(cmd, "volumic source", in, out, 4, 4, 1, 1)) {
    /*@FUNC V = ::ASM('volumic source',@tmim mim, @tmf mf_u, @tmf mf_d, @dcvec fd)
    Assembly of a volumic source term.

    Output a vector `V`, assembled on the @tmf `mf_u`, using the data
    vector `fd` defined on the data @tmf `mf_d`. `fd` may be real or
    complex-valued.<Par>

    Return a @dcvec object.
    @*/
    assemble_source(size_type(-1), in, out);
  } else if (check_cmd(cmd, "boundary source", in, out, 5, 5, 0, 1)) {
    /*@FUNC B = ::ASM('boundary source',@int bnum, @tmim mim, @tmf mf_u, @tmf mf_d, @dvec G)
    Assembly of a boundary source term.

    `G` should be a [Qdim x N] matrix, where N is the number of dof
    of `mf_d`, and Qdim is the dimension of the unkown u (that is set
    when creating the @tmf).<Par>

    Return a @dcvec object.
    @*/
    int boundary_num = in.pop().to_integer();
    assemble_source(boundary_num, in, out);
  } else if (check_cmd(cmd, "dirichlet", in, out, 6, 7, 2, 2)) {
    /*@FUNC @CELL{HH, RR} = ::ASM('dirichlet',@int bnum, @tmim mim, @tmf mf_u, @tmf mf_d, @dmat H, @dvec R [, threshold])
    Assembly of Dirichlet conditions of type `h.u = r`.

    Handle `h.u = r` where h is a square matrix (of any rank) whose
    size is equal to the dimension of the unkown u. This matrix is
    stored in `H`, one column per dof in `mf_d`, each column containing
    the values of the matrix h stored in fortran order:<Par>

    `H(:,j) = [h11(x_j) h21(x_j) h12(x_j) h22(x_j)]`<Par>

    if u is a 2D vector field.<Par>

    Of course, if the unknown is a scalar field, you just have to set
    `H = ones(1, N)`, where N is the number of dof of `mf_d`.<Par>

    This is basically the same than calling ::ASM('boundary qu term')
    for `H` and calling ::ASM('neumann') for `R`, except that this
    function tries to produce a 'better' (more diagonal) constraints
    matrix (when possible).<Par>

    See also SPMAT:GET('Dirichlet_nullspace').@*/
    int boundary_num       = in.pop().to_integer();
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();
    mexarg_in in_h = in.pop();
    mexarg_in in_r = in.pop();
    double threshold = 1e-8;
    if (in.remaining()) {
      threshold = in.pop().to_scalar();
      if (threshold < 0 || threshold > 1e10) THROW_BADARG("wrong threshold\n");
    }

    if (in_h.is_complex() || in_r.is_complex())
      gf_dirichlet(out, *mim, *mf_u, *mf_d, in_h, in_r, boundary_num, complex_type());
    else gf_dirichlet(out, *mim, *mf_u, *mf_d, in_h, in_r, boundary_num, scalar_type());
  } else if (check_cmd(cmd, "boundary qu term", in, out, 5, 5, 0, 1)) {
    /*@FUNC Q = ::ASM('boundary qu term',@int boundary_num, @tmim mim, @tmf mf_u, @tmf mf_d, @dmat q)
    Assembly of a boundary qu term.

    `q` should be be a [Qdim x Qdim x N] array, where N is the number
    of dof of `mf_d`, and Qdim is the dimension of the unkown u (that
    is set when creating the @tmf).<Par>

    Return a @tsp object.
    @*/
    int boundary_num       = in.pop().to_integer();
    const getfem::mesh_im *mim = get_mim(in);
    const getfem::mesh_fem *mf_u = in.pop().to_const_mesh_fem();
    const getfem::mesh_fem *mf_d = in.pop().to_const_mesh_fem();

    unsigned q_dim = mf_u->get_qdim();
    if (!in.front().is_complex()) {
      darray q            = in.pop().to_darray();
      if (q.ndim() == 2) in.last_popped().check_dimensions(q, q_dim* q_dim, int(mf_d->nb_dof()));
      else               in.last_popped().check_dimensions(q, q_dim, q_dim, int(mf_d->nb_dof()));
      gf_real_sparse_by_col Q(mf_u->nb_dof(), mf_u->nb_dof());
      getfem::asm_qu_term(Q, *mim, *mf_u, *mf_d, q, boundary_num);
      out.pop().from_sparse(Q);
    } else {
      carray q            = in.pop().to_carray();
      if (q.ndim() == 2) in.last_popped().check_dimensions(q, q_dim* q_dim, int(mf_d->nb_dof()));
      else               in.last_popped().check_dimensions(q, q_dim, q_dim, int(mf_d->nb_dof()));
      gf_cplx_sparse_by_col Q(mf_u->nb_dof(), mf_u->nb_dof());
      getfem::asm_qu_term(Q, *mim, *mf_u, *mf_d, q, boundary_num);
      out.pop().from_sparse(Q);
    }
  } else if (check_cmd(cmd, "volumic", in, out, 2, -1, 0, -1)) {
    /*@FUNC @CELL{...} = ::ASM('volumic' [,CVLST], expr [, mesh_ims, mesh_fems, data...])
    Generic assembly procedure for volumic assembly.

    The expression `expr` is evaluated over the @tmf's listed in the
    arguments (with optional data) and assigned to the output arguments.
    For details about the syntax of assembly expressions, please refer
    to the getfem user manual (or look at the file getfem_assembling.h
    in the getfem++ sources).<Par>

    For example, the L2 norm of a field can be computed with<Par>

    ::COMPUTE('L2 norm') or with:<Par>

    ::ASM('volumic','u=data(#1); V()+=u(i).u(j).comp(Base(#1).Base(#1))(i,j)',mim,mf,U)<Par>

    The Laplacian stiffness matrix can be evaluated with<Par>

    ::ASM('laplacian',mim, mf, A) or equivalently with:<Par>

    ::ASM('volumic','a=data(#2);M(#1,#1)+=sym(comp(Grad(#1).Grad(#1).Base(#2))(:,i,:,i,j).a(j))', mim,mf, A);@*/
    do_generic_assembly(in, out, false);
  } else if (check_cmd(cmd, "boundary", in, out, 3, -1, 0, -1)) {
    /*@FUNC @CELL{...} = ::ASM('boundary',@int bnum, @str expr [, @tmim mim, @tmf mf, data...])
    Generic boundary assembly.

    See the help for ::ASM('volumic').@*/
    do_generic_assembly(in, out, true);
  } else if (check_cmd(cmd, "interpolation matrix", in, out, 2, 2, 0, 1)) {
    /*@FUNC Mi = ::ASM('interpolation matrix',@tmf mf, @tmf mfi)
    Build the interpolation matrix from a @tmf onto another @tmf.

    Return a matrix `Mi`, such that `V = Mi.U` is equal to
    ::COMPUTE('interpolate_on',mfi). Useful for repeated interpolations.
    Note that this is just interpolation, no elementary integrations
    are involved here, and `mfi` has to be lagrangian. In the more
    general case, you would have to do a L2 projection via the mass
    matrix.<Par>

    `Mi` is a @tsp object.
    @*/
    interpolate_or_extrapolate(in, out, 0);
  } else if (check_cmd(cmd, "extrapolation matrix", in, out, 2, 2, 0, 1)) {
    /*@FUNC Me = ::ASM('extrapolation matrix',@tmf mf, @tmf mfe)
    Build the extrapolation matrix from a @tmf onto another @tmf.

    Return a matrix `Me`, such that `V = Me.U` is equal to
    ::COMPUTE('extrapolate_on',mfe). Useful for repeated
    extrapolations.

    `Me` is a @tsp object.
    @*/
    interpolate_or_extrapolate(in, out, 2);
  } else bad_cmd(cmd);
}

    /*@FUNC @CELL{Q, G, H, R, F} = ::ASM('pdetool boundary conditions',mf_u, mf_d, b, e[, f_expr])
    Assembly of pdetool boundary conditions.

    `B` is the boundary matrix exported by pdetool, and `E` is the
    edges array. `f_expr` is an optionnal expression (or vector) for
    the volumic term. On return `Q, G, H, R, F` contain the assembled
    boundary conditions (`Q` and `H` are matrices), similar to the
    ones returned by the function ASSEMB from PDETOOL.@*/
